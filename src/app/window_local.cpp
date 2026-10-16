#include "window.h"
#include "local_adjustment_panel.h"
#include "parameter_wheel_guard.h"
#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QEvent>
#include <QFileInfo>
#include <QScopedValueRollback>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolButton>
#include <QUuid>
#include <cmath>
#include <limits>

LocalEdit::DocumentSnapshot Window::currentPhotoDocument() const{
    if(current<0)return {};
    if(brushTransaction.active())return brushTransaction.preview();
    if(localParameterDraft)return localParameterDraft;
    const auto stored=history.snapshot(photos[current].path);if(!stored)return {};
    auto copy=*stored;copy.global=live;return std::make_shared<const LocalEdit::PhotoDocument>(std::move(copy));
}
bool Window::localEditingAvailable() const{
    if(current<0||!localSourceReady||displayed!=photos[current].path||comparing||cropSelecting||pointRangeActive||sharpenMaskActive||pointPickerActive)return false;
    if(auto *picker=findChild<QPushButton *>("wb-picker");picker&&picker->isChecked())return false;
    return displayedTag.geometry==geometry&&displayedAngle==live.angle;
}
void Window::refreshLocalPanel(){
    if(!localPanel)return;
    localPanel->setDocument(currentPhotoDocument(),selectedLocalLayer);
    selectedLocalLayer=localPanel->selectedLayerId();
    if(selectedLocalLayer.isEmpty())localPanel->setBrushActive(false);
    localPanel->setAvailable(localEditingAvailable());syncLocalTool();
}
void Window::syncLocalTool(){
    if(!localPanel||localToolSync)return;
    const QScopedValueRollback<bool> guard(localToolSync,true);
    canvas->setBrushMode(adjustmentScope&&adjustmentScope->currentIndex()==1&&!selectedLocalLayer.isEmpty()&&localPanel->brushActive()&&localEditingAvailable());
}
void Window::bindLocalAsset(const EditResult &result){
    if(current<0||result.path!=photos[current].path)return;
    const auto old=history.snapshot(result.path);LocalEdit::AssetIdentity asset;
    asset.id=old?old->asset.id:QUuid::createUuid().toString(QUuid::WithoutBraces);
    asset.sourcePath=result.path;asset.sourceHash=result.sourceHash;asset.normalizedSize=result.originalSize;
    asset.raw=!result.workFrame.linear.isNull()?result.workFrame.raw:QFileInfo(result.path).suffix().compare("cr3",Qt::CaseInsensitive)==0;
    asset.sourceFacts=result.workFrame.facts;QString error;
    localSourceReady=history.bindAsset(result.path,asset,error);
    if(!localSourceReady)statusBar()->showMessage(error,6000);
}
void Window::setupLocalEditing(QDockWidget *dock){
    auto *global=dock->widget();global->setParent(nullptr);
    adjustmentScope=new QTabWidget(dock);adjustmentScope->setObjectName("adjustment-scope-tabs");
    adjustmentScope->addTab(global,QStringLiteral("全图基础"));
    auto *scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);
    localPanel=new LocalAdjustmentPanel;scroll->setWidget(localPanel);new Ui::ParameterWheelGuard(localPanel);
    adjustmentScope->addTab(scroll,QStringLiteral("局部调整"));dock->setWidget(adjustmentScope);
    connect(localPanel,&LocalAdjustmentPanel::createBrushLayerRequested,this,&Window::createLocalBrushLayer);
    connect(localPanel,&LocalAdjustmentPanel::selectedLayerChanged,this,[this](const QString &id){
        finishLocalEdits();selectedLocalLayer=id;refreshLocalPanel();requestEdit();
    });
    connect(localPanel,&LocalAdjustmentPanel::layerVisibilityChanged,this,[this](const QString &id,bool visible){
        finishLocalEdits();const auto base=currentPhotoDocument();if(!base)return;
        auto next=*base;for(auto &layer:next.layers)if(layer.id==id){
            if(layer.enabled==visible)return;
            if(layer.version==std::numeric_limits<quint64>::max())return;
            layer.enabled=visible;++layer.version;break;
        }
        const auto path=next.asset.sourcePath;
        QString error;if(history.commitSnapshot(path,std::make_shared<const LocalEdit::PhotoDocument>(std::move(next)),QStringLiteral("局部层显隐"),&error)){++revision;++localIdentity;}
        else if(!error.isEmpty())statusBar()->showMessage(error,6000);
        refreshLocalPanel();requestEdit();
    });
    connect(localPanel,&LocalAdjustmentPanel::parametersEdited,this,&Window::editLocalParameters);
    connect(localPanel,&LocalAdjustmentPanel::parametersFinished,this,[this]{finishLocalEdits();});
    connect(localPanel,&LocalAdjustmentPanel::brushToolToggled,this,[this](bool){finishLocalEdits();syncLocalTool();syncCanvasTools();});
    connect(localPanel,&LocalAdjustmentPanel::brushSettingsChanged,this,[this](const LocalEdit::BrushSettings &){finishLocalEdits();syncLocalTool();});
    connect(localPanel,&LocalAdjustmentPanel::overlayToggled,this,[this](bool){requestEdit();});
    connect(canvas,&Canvas::brushPressed,this,&Window::beginLocalBrush);
    connect(canvas,&Canvas::brushMoved,this,&Window::moveLocalBrush);
    connect(canvas,&Canvas::brushFinished,this,[this](bool canceled){finishLocalEdits(canceled);});
    connect(canvas,&Canvas::brushRadiusStepRequested,this,[this](int direction){
        if(!localPanel||!adjustmentScope||adjustmentScope->currentIndex()!=1)return;
        finishLocalEdits();localPanel->setBrushRadius(localPanel->brushSettings().radius*std::pow(1.1,direction));
    });
    connect(canvas,&Canvas::overlayToggleRequested,this,[this]{
        if(!localPanel||!adjustmentScope||adjustmentScope->currentIndex()!=1||selectedLocalLayer.isEmpty())return;
        localPanel->setOverlayVisible(!localPanel->overlayVisible());requestEdit();
    });
    connect(adjustmentScope,&QTabWidget::currentChanged,this,[this](int index){
        endControlEdit();if(index!=1)localPanel->setBrushActive(false);
        refreshLocalPanel();requestEdit();
    });
    if(auto *hand=findChild<QToolButton *>("tool-hand"))connect(hand,&QToolButton::clicked,this,[this]{
        finishLocalEdits();localPanel->setBrushActive(false);syncLocalTool();syncCanvasTools();
    });
    if(auto *browse=findChild<QAction *>("workspace-browse"))connect(browse,&QAction::triggered,this,[this]{adjustmentScope->setCurrentIndex(0);});
    refreshLocalPanel();
}
void Window::createLocalBrushLayer(){
    endControlEdit();const auto base=currentPhotoDocument();if(!base||!localEditingAvailable())return;
    auto next=*base;LocalEdit::AdjustmentLayer layer;
    layer.id=QUuid::createUuid().toString(QUuid::WithoutBraces);layer.name=QStringLiteral("笔刷 %1").arg(next.layers.size()+1);
    next.layers.append(layer);QString error;
    const auto path=next.asset.sourcePath;
    if(!history.commitSnapshot(path,std::make_shared<const LocalEdit::PhotoDocument>(std::move(next)),QStringLiteral("新建笔刷层"),&error)){
        statusBar()->showMessage(error,6000);return;
    }
    ++revision;++localIdentity;selectedLocalLayer=layer.id;
    localPanel->setOverlayVisible(true);localPanel->setBrushActive(true);refreshLocalPanel();requestEdit();
}
void Window::beginLocalBrush(QPointF uv,bool subtract){
    endControlEdit();const auto doc=currentPhotoDocument();QString error;
    const auto settings=localPanel->brushSettings();
    if(!doc||!localEditingAvailable()||selectedLocalLayer.isEmpty()||!brushTransaction.begin(doc,selectedLocalLayer,
        QUuid::createUuid().toString(QUuid::WithoutBraces),uv,settings.radius,settings.hardness,settings.strength,
        subtract||settings.subtract?LocalEdit::MaskOperation::Subtract:LocalEdit::MaskOperation::Add,error)){
        canvas->finishBrushGesture(true);if(!error.isEmpty())statusBar()->showMessage(error,6000);return;
    }
    ++localIdentity;requestEdit();
}
void Window::moveLocalBrush(QPointF uv){
    if(!brushTransaction.active())return;const auto before=brushTransaction.preview();QString error;
    if(!brushTransaction.append(uv,error)){statusBar()->showMessage(error,6000);return;}
    if(brushTransaction.preview()!=before)requestEdit();
}
void Window::editLocalParameters(const QString &id,LocalEdit::LocalParameters parameters){
    if(localFinishing)return;
    if(!localParameterBase){
        endControlEdit();if(!localEditingAvailable())return;
        localParameterBase=currentPhotoDocument();if(!localParameterBase)return;
        ++localIdentity;
    }
    auto next=*localParameterBase;bool found=false;
    for(auto &layer:next.layers)if(layer.id==id){
        if(layer.parameters!=parameters){
            if(layer.version==std::numeric_limits<quint64>::max())return;
            layer.parameters=parameters;++layer.version;
        }
        found=true;break;
    }
    if(!found){if(!localParameterDraft)localParameterBase.reset();return;}
    QString error;if(!LocalEdit::validDocument(next,error,true)){statusBar()->showMessage(error,6000);return;}
    if(localParameterDraft&&next==*localParameterDraft)return;
    localParameterDraft=std::make_shared<const LocalEdit::PhotoDocument>(std::move(next));requestEdit();
}
void Window::finishLocalEdits(bool canceled){
    if(localFinishing||(!brushTransaction.active()&&!localParameterDraft))return;
    const QScopedValueRollback<bool> guard(localFinishing,true);
    // The canvas emits brushPressed after opening its gesture. Flushing a
    // preceding parameter draft at that point must not close this new gesture;
    // it belongs to a brush transaction only once beginLocalBrush creates one.
    if(brushTransaction.active()&&canvas->brushGestureActive())canvas->finishBrushGesture(canceled);
    QString error;LocalEdit::DocumentSnapshot candidate;
    if(current>=0){
        const auto committed=history.snapshot(photos[current].path);
        if(brushTransaction.active()&&!canceled)candidate=brushTransaction.finish(committed,error);
        else brushTransaction.cancel();
        if(localParameterDraft&&!canceled&&committed&&localParameterBase){
            if(committed->global!=localParameterBase->global||committed->layers!=localParameterBase->layers)error=QStringLiteral("局部调整基底已变化，未覆盖后续编辑");
            else {auto next=*committed;next.layers=localParameterDraft->layers;candidate=std::make_shared<const LocalEdit::PhotoDocument>(std::move(next));}
        }
        if(candidate)history.commitSnapshot(photos[current].path,candidate,localParameterDraft?QStringLiteral("局部调整"):QStringLiteral("笔刷"),&error);
    }else brushTransaction.cancel();
    localParameterBase.reset();localParameterDraft.reset();++revision;++localIdentity;
    if(!error.isEmpty())statusBar()->showMessage(error,6000);
    refreshLocalPanel();requestEdit();
}
bool Window::cancelLocalEdits(){
    if(!brushTransaction.active()&&!localParameterDraft)return false;
    finishLocalEdits(true);return true;
}
void Window::changeEvent(QEvent *event){
    if(event->type()==QEvent::ActivationChange&&!isActiveWindow())finishLocalEdits();
    QMainWindow::changeEvent(event);
}
