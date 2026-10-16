#include "window.h"
#include "local_adjustment_panel.h"
#include "local_gradient_controls.h"
#include "local_mask_options.h"
#include "core/local_layer_operations.h"
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
#include <QVBoxLayout>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <limits>

LocalEdit::DocumentSnapshot Window::currentPhotoDocument() const{
    if(current<0)return {};
    if(brushTransaction.active())return brushTransaction.preview();
    if(gradientTransaction.active())return gradientTransaction.preview();
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
    const auto doc=currentPhotoDocument();
    if(!doc||doc->asset.id!=localAssetId){selectedLocalComponent.clear();localAssetId=doc?doc->asset.id:QString{};}
    const auto oldLayer=selectedLocalLayer;
    localPanel->setDocument(doc,selectedLocalLayer);
    selectedLocalLayer=localPanel->selectedLayerId();
    if(oldLayer!=selectedLocalLayer)selectedLocalComponent.clear();
    if(!gradientTransaction.active()){
        bool found=false;
        if(doc)for(const auto &layer:doc->layers)if(layer.id==selectedLocalLayer)
            for(const auto &component:layer.mask.components)if(component.id==selectedLocalComponent)found=true;
        if(!found)selectedLocalComponent.clear();
    }
    if(selectedLocalLayer.isEmpty())localPanel->setBrushActive(false);
    if(localGradients){
        localGradients->setDocument(doc,selectedLocalLayer,selectedLocalComponent);
        localGradients->setAvailable(localEditingAvailable());localGradients->setActiveKind(localGradientKind);
    }
    if(localMaskOptions){
        const LocalEdit::Mask *mask=nullptr;
        if(doc)for(const auto &layer:doc->layers)if(layer.id==selectedLocalLayer){mask=&layer.mask;break;}
        const auto owner=doc?QString::number(doc->asset.id.size())+":"+doc->asset.id+selectedLocalLayer:QString{};
        localMaskOptions->setMask(mask,owner);localMaskOptions->setAvailable(localEditingAvailable());
        localMaskOptions->setView(localMaskColor,localMaskBlackWhite);
    }
    localPanel->setGradientMode(localGradientKind!=0);
    localPanel->setAvailable(localEditingAvailable());syncLocalTool();
}
void Window::syncLocalTool(){
    if(!localPanel||localToolSync)return;
    const QScopedValueRollback<bool> guard(localToolSync,true);
    const bool armed=adjustmentScope&&adjustmentScope->currentIndex()==1&&!selectedLocalLayer.isEmpty()&&(localPanel->brushActive()||localGradientKind!=0)&&localEditingAvailable();
    canvas->setGradientMode(armed&&localGradientKind!=0);canvas->setBrushMode(armed);
    syncLocalGradientHandles();
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
    localPanel=new LocalAdjustmentPanel;scroll->setWidget(localPanel);
    localGradients=new LocalGradientControls(localPanel);
    localMaskOptions=new LocalMaskOptions(localPanel);
    if(auto *layout=qobject_cast<QVBoxLayout *>(localPanel->layout())){
        auto *settings=localPanel->findChild<QWidget *>("local-brush-settings");
        layout->insertWidget(layout->indexOf(settings),localGradients);
        layout->insertWidget(layout->indexOf(settings),localMaskOptions);
    }
    new Ui::ParameterWheelGuard(localPanel);
    adjustmentScope->addTab(scroll,QStringLiteral("局部调整"));dock->setWidget(adjustmentScope);
    connect(localPanel,&LocalAdjustmentPanel::createBrushLayerRequested,this,&Window::createLocalBrushLayer);
    connect(localPanel,&LocalAdjustmentPanel::selectedLayerChanged,this,[this](const QString &id){
        const QString copy=id;finishLocalEdits();selectedLocalLayer=copy;selectedLocalComponent.clear();refreshLocalPanel();requestEdit();
    });
    connect(localPanel,&LocalAdjustmentPanel::layerVisibilityChanged,this,[this](const QString &id,bool visible){
        LocalEdit::LayerChange change;change.kind=LocalEdit::LayerChangeKind::Visibility;change.layerId=id;change.visible=visible;
        applyLocalLayerChange(std::move(change),QStringLiteral("局部层显隐"));
    });
    connect(localPanel,&LocalAdjustmentPanel::layerRenameRequested,this,[this](const QString &id,const QString &name){
        LocalEdit::LayerChange change;change.kind=LocalEdit::LayerChangeKind::Rename;change.layerId=id;change.name=name;
        applyLocalLayerChange(std::move(change),QStringLiteral("重命名局部层"));
    });
    connect(localPanel,&LocalAdjustmentPanel::layerDuplicateRequested,this,[this](const QString &id){
        LocalEdit::LayerChange change;change.kind=LocalEdit::LayerChangeKind::Duplicate;change.layerId=id;
        applyLocalLayerChange(std::move(change),QStringLiteral("复制局部层"));
    });
    connect(localPanel,&LocalAdjustmentPanel::layerDeleteRequested,this,[this](const QString &id){
        LocalEdit::LayerChange change;change.kind=LocalEdit::LayerChangeKind::Remove;change.layerId=id;
        applyLocalLayerChange(std::move(change),QStringLiteral("删除局部层"));
    });
    connect(localPanel,&LocalAdjustmentPanel::layerOrderRequested,this,[this](const QStringList &topToBottom){
        LocalEdit::LayerChange change;change.kind=LocalEdit::LayerChangeKind::Reorder;change.order=topToBottom;
        std::reverse(change.order.begin(),change.order.end());applyLocalLayerChange(std::move(change),QStringLiteral("局部层排序"));
    });
    connect(localPanel,&LocalAdjustmentPanel::layerOpacityEdited,this,&Window::editLocalOpacity);
    connect(localPanel,&LocalAdjustmentPanel::layerOpacityFinished,this,[this]{if(localParameterDraft&&localOpacityDraft)finishLocalEdits();});
    connect(localPanel,&LocalAdjustmentPanel::parametersEdited,this,&Window::editLocalParameters);
    connect(localPanel,&LocalAdjustmentPanel::parametersFinished,this,[this]{if(localParameterDraft&&!localOpacityDraft&&!localFeatherDraft)finishLocalEdits();});
    connect(localMaskOptions,&LocalMaskOptions::featherEdited,this,&Window::editLocalFeather);
    connect(localMaskOptions,&LocalMaskOptions::featherFinished,this,[this]{if(localParameterDraft&&localFeatherDraft)finishLocalEdits();});
    connect(localMaskOptions,&LocalMaskOptions::invertRequested,this,[this](bool value){
        LocalEdit::LayerChange change;change.kind=LocalEdit::LayerChangeKind::Invert;change.layerId=selectedLocalLayer;change.inverted=value;
        applyLocalLayerChange(std::move(change),QStringLiteral("蒙版反相"));
    });
    const auto showMask=[this]{if(!localPanel->overlayVisible()){localPanel->setOverlayVisible(true);requestEdit();}};
    connect(localMaskOptions,&LocalMaskOptions::colorSelected,this,[this,showMask](QColor color){
        localMaskColor=color;canvas->setLocalMaskStyle(localMaskColor,localMaskBlackWhite);showMask();
    });
    connect(localMaskOptions,&LocalMaskOptions::blackWhiteToggled,this,[this,showMask](bool value){
        localMaskBlackWhite=value;canvas->setLocalMaskStyle(localMaskColor,localMaskBlackWhite);if(value)showMask();
    });
    connect(localPanel,&LocalAdjustmentPanel::brushToolToggled,this,[this](bool active){
        finishLocalEdits();if(active){localGradientKind=0;selectedLocalComponent.clear();}
        refreshLocalPanel();syncCanvasTools();
    });
    connect(localGradients,&LocalGradientControls::toolRequested,this,&Window::setLocalGradientTool);
    connect(localGradients,&LocalGradientControls::componentSelected,this,&Window::selectLocalComponent);
    connect(localGradients,&LocalGradientControls::componentRemovalRequested,this,[this](QString id){
        LocalEdit::LayerChange change;change.kind=LocalEdit::LayerChangeKind::RemoveComponent;
        change.layerId=selectedLocalLayer;change.componentId=id;
        applyLocalLayerChange(std::move(change),QStringLiteral("移除蒙版组成项"));
    });
    connect(localPanel,&LocalAdjustmentPanel::brushSettingsChanged,this,[this](const LocalEdit::BrushSettings &){finishLocalEdits();syncLocalTool();});
    connect(localPanel,&LocalAdjustmentPanel::overlayToggled,this,[this](bool){requestEdit();});
    connect(canvas,&Canvas::brushPressed,this,&Window::beginLocalBrush);
    connect(canvas,&Canvas::brushMoved,this,&Window::moveLocalBrush);
    connect(canvas,&Canvas::brushFinished,this,[this](bool canceled){finishLocalEdits(canceled);});
    connect(canvas,&Canvas::brushRadiusStepRequested,this,[this](int direction){
        if(!localPanel||!adjustmentScope||adjustmentScope->currentIndex()!=1||localGradientKind!=0)return;
        finishLocalEdits();localPanel->setBrushRadius(localPanel->brushSettings().radius*std::pow(1.1,direction));
    });
    connect(canvas,&Canvas::overlayToggleRequested,this,[this]{
        if(!localPanel||!adjustmentScope||adjustmentScope->currentIndex()!=1||selectedLocalLayer.isEmpty())return;
        localPanel->setOverlayVisible(!localPanel->overlayVisible());requestEdit();
    });
    connect(adjustmentScope,&QTabWidget::currentChanged,this,[this](int index){
        endControlEdit();if(index!=1){localPanel->setBrushActive(false);localGradientKind=0;}
        refreshLocalPanel();requestEdit();
    });
    if(auto *hand=findChild<QToolButton *>("tool-hand"))connect(hand,&QToolButton::clicked,this,[this]{
        finishLocalEdits();localPanel->setBrushActive(false);localGradientKind=0;refreshLocalPanel();syncCanvasTools();
    });
    if(auto *browse=findChild<QAction *>("workspace-browse"))connect(browse,&QAction::triggered,this,[this]{adjustmentScope->setCurrentIndex(0);});
    refreshLocalPanel();
}
void Window::createLocalBrushLayer(){
    endControlEdit();const auto base=currentPhotoDocument();if(!base||!localEditingAvailable())return;
    auto next=*base;LocalEdit::AdjustmentLayer layer;
    layer.id=QUuid::createUuid().toString(QUuid::WithoutBraces);layer.name=QStringLiteral("局部 %1").arg(next.layers.size()+1);
    next.layers.append(layer);QString error;
    const auto path=next.asset.sourcePath;
    if(!history.commitSnapshot(path,std::make_shared<const LocalEdit::PhotoDocument>(std::move(next)),QStringLiteral("新建局部层"),&error)){
        statusBar()->showMessage(error,6000);return;
    }
    ++revision;++localIdentity;selectedLocalLayer=layer.id;selectedLocalComponent.clear();localGradientKind=0;
    localPanel->setOverlayVisible(true);localPanel->setBrushActive(true);refreshLocalPanel();requestEdit();
}
void Window::beginLocalBrush(QPointF uv,bool subtract){
    // Preserve the hit index before flushing the preceding control edit, which
    // can refresh controls. That flush must not own this newly opened pointer.
    const int handle=canvas->pressedGradientHandle();
    endControlEdit();const auto doc=currentPhotoDocument();QString error;
    const auto settings=localPanel->brushSettings();
    if(localGradientKind!=0){
        bool started=false;
        gradientPreviousComponent=selectedLocalComponent;
        if(doc&&localEditingAvailable()&&!selectedLocalLayer.isEmpty()){
            if(handle>=0&&!selectedLocalComponent.isEmpty())
                started=gradientTransaction.beginEdit(doc,selectedLocalLayer,selectedLocalComponent,handle,uv,error);
            else{
                const auto id=QUuid::createUuid().toString(QUuid::WithoutBraces);
                started=gradientTransaction.beginCreate(doc,selectedLocalLayer,id,localGradientKind==1?LocalEdit::GradientKind::Linear:LocalEdit::GradientKind::Radial,
                    uv,settings.strength,subtract||settings.subtract?LocalEdit::MaskOperation::Subtract:LocalEdit::MaskOperation::Add,error);
                if(started)selectedLocalComponent=id;
            }
        }
        if(!started){canvas->finishBrushGesture(true);if(!error.isEmpty())statusBar()->showMessage(error,6000);return;}
        ++localIdentity;syncLocalGradientHandles();requestEdit();return;
    }
    if(!doc||!localEditingAvailable()||selectedLocalLayer.isEmpty()||!brushTransaction.begin(doc,selectedLocalLayer,
        QUuid::createUuid().toString(QUuid::WithoutBraces),uv,settings.radius,settings.hardness,settings.strength,
        subtract||settings.subtract?LocalEdit::MaskOperation::Subtract:LocalEdit::MaskOperation::Add,error)){
        canvas->finishBrushGesture(true);if(!error.isEmpty())statusBar()->showMessage(error,6000);return;
    }
    ++localIdentity;requestEdit();
}
void Window::moveLocalBrush(QPointF uv){
    if(gradientTransaction.active()){
        const auto before=gradientTransaction.preview();QString error;
        if(!gradientTransaction.append(uv,error)){statusBar()->showMessage(error,6000);return;}
        if(gradientTransaction.preview()!=before){syncLocalGradientHandles();requestEdit();}return;
    }
    if(!brushTransaction.active())return;const auto before=brushTransaction.preview();QString error;
    if(!brushTransaction.append(uv,error)){statusBar()->showMessage(error,6000);return;}
    if(brushTransaction.preview()!=before)requestEdit();
}
void Window::editLocalParameters(const QString &id,LocalEdit::LocalParameters parameters){
    if(localFinishing)return;
    if(localParameterBase&&(localOpacityDraft||localFeatherDraft||localDraftLayer!=id))finishLocalEdits();
    if(!localParameterBase){
        endControlEdit();if(!localEditingAvailable())return;
        localParameterBase=currentPhotoDocument();if(!localParameterBase)return;
        localOpacityDraft=false;localFeatherDraft=false;localDraftLayer=id;
        ++localIdentity;
    }
    const auto abandonEmptyDraft=[&]{if(!localParameterDraft){localParameterBase.reset();localOpacityDraft=false;localDraftLayer.clear();}};
    auto next=*localParameterBase;bool found=false;
    for(auto &layer:next.layers)if(layer.id==id){
        if(layer.parameters!=parameters){
            if(layer.version==std::numeric_limits<quint64>::max()){
                abandonEmptyDraft();statusBar()->showMessage(QStringLiteral("局部层版本已达上限"),6000);refreshLocalPanel();return;
            }
            layer.parameters=parameters;++layer.version;
        }
        found=true;break;
    }
    if(!found){abandonEmptyDraft();refreshLocalPanel();return;}
    QString error;if(!LocalEdit::validDocument(next,error,true)){abandonEmptyDraft();statusBar()->showMessage(error,6000);refreshLocalPanel();return;}
    if(localParameterDraft&&next==*localParameterDraft)return;
    localParameterDraft=std::make_shared<const LocalEdit::PhotoDocument>(std::move(next));requestEdit();
}
void Window::finishLocalEdits(bool canceled){
    if(localFinishing||(!brushTransaction.active()&&!gradientTransaction.active()&&!localParameterDraft))return;
    const QScopedValueRollback<bool> guard(localFinishing,true);
    // The canvas emits brushPressed after opening its gesture. Flushing a
    // preceding parameter draft at that point must not close this new gesture;
    // it belongs to a brush transaction only once beginLocalBrush creates one.
    if((brushTransaction.active()||gradientTransaction.active())&&canvas->brushGestureActive())canvas->finishBrushGesture(canceled);
    const bool wasGradient=gradientTransaction.active();
    QString error;LocalEdit::DocumentSnapshot candidate;
    if(current>=0){
        const auto committed=history.snapshot(photos[current].path);
        if(brushTransaction.active()&&!canceled)candidate=brushTransaction.finish(committed,error);
        else brushTransaction.cancel();
        if(gradientTransaction.active()&&!canceled)candidate=gradientTransaction.finish(committed,error);
        else gradientTransaction.cancel();
        if(wasGradient&&!candidate)selectedLocalComponent=gradientPreviousComponent;
        if(localParameterDraft&&!canceled&&committed&&localParameterBase){
            if(committed->global!=localParameterBase->global||committed->layers!=localParameterBase->layers)error=QStringLiteral("局部调整基底已变化，未覆盖后续编辑");
            else {auto next=*committed;next.layers=localParameterDraft->layers;candidate=std::make_shared<const LocalEdit::PhotoDocument>(std::move(next));}
        }
        if(candidate)history.commitSnapshot(photos[current].path,candidate,localParameterDraft?(localFeatherDraft?QStringLiteral("蒙版羽化"):localOpacityDraft?QStringLiteral("局部层强度"):QStringLiteral("局部调整")):(wasGradient?QStringLiteral("渐变"):QStringLiteral("笔刷")),&error);
    }else {brushTransaction.cancel();gradientTransaction.cancel();}
    localParameterBase.reset();localParameterDraft.reset();++revision;++localIdentity;
    gradientPreviousComponent.clear();
    localOpacityDraft=false;localFeatherDraft=false;localDraftLayer.clear();
    if(!error.isEmpty())statusBar()->showMessage(error,6000);
    refreshLocalPanel();requestEdit();
}
bool Window::cancelLocalEdits(){
    if(!brushTransaction.active()&&!gradientTransaction.active()&&!localParameterDraft)return false;
    finishLocalEdits(true);return true;
}
void Window::changeEvent(QEvent *event){
    if(event->type()==QEvent::ActivationChange&&!isActiveWindow())finishLocalEdits();
    QMainWindow::changeEvent(event);
}
