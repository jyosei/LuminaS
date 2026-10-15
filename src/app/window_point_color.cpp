#include "window.h"
#include <QCheckBox>
#include <QFormLayout>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStatusBar>

// Window::pointColorPanel controller. This unit only wires the PointColorPanel
// widget to the host document: it never samples, renders or invents point-color
// values. Sampling stays in EditLoader::requestPointColorSample(), commits go
// through history/applyRecipe(), and range previews are ordinary requestEdit()
// passes once the host routes the transient RenderView.

void Window::setupPointColorPanel(QFormLayout *form){
    if(!form)return; // setup timing guard; the panel may already exist
    pointColorPanel=new PointColorPanel(this);
    pointColorPanel->setObjectName(QStringLiteral("point-color-panel"));
    form->addRow(pointColorPanel);
    connect(editor,&EditLoader::pointColorSampleStateChanged,this,&Window::updatePickerState);
    // Widget-driven edits: the panel owns the raw list, the host validates it
    // against the live recipe before anything reaches the document.
    connect(pointColorPanel,&PointColorPanel::pointsEdited,this,[this](const QVector<PointColorAdjustment> &points){
        if(restoring||current<0||comparing||cropSelecting)return;
        if(points.size()>maximumPointColors)return;
        Recipe candidate=live;
        candidate.pointColors=points;
        if(!validRecipe(candidate))return;
        if(candidate.pointColors==live.pointColors)return; // unchanged: no extra request
        if(pointPickerActive){ // an explicit edit supersedes a pending pick
            pointPickerActive=false;
            editor->cancelPointColorSample();
            pointColorPanel->setPickActive(false);
        }
        live=candidate; // live only; the shared history commits via endControlEdit()
        requestEdit();  // no applyRecipe() per drag
    });
    connect(pointColorPanel,&PointColorPanel::editFinished,this,&Window::endControlEdit);
    connect(pointColorPanel,&PointColorPanel::pickRequested,this,[this](bool checked){
        const bool leaveMask=checked&&sharpenMaskActive;
        // The white-balance and point pickers share canvas->pickerMode: only one
        // may stay checked, so arming one releases the other.
        if(checked)if(auto *neutral=findChild<QPushButton *>("wb-picker"))neutral->setChecked(false);
        editor->cancelPointColorSample();
        if(checked){
            endControlEdit(); // close any pending gesture before sampling
            // Point picking and the diagnostic mask are mutually exclusive transient views.
            sharpenMaskActive=false;
            if(sharpenMaskPreview){const QSignalBlocker block(sharpenMaskPreview);sharpenMaskPreview->setChecked(false);}
            pointPickerActive=true;
            pointRangeActive=false;
            pointColorPanel->setRangeActive(false);
        } else {
            pointPickerActive=false;
        }
        pointColorPanel->setPickActive(pointPickerActive); // canvas mode refresh below
        if(leaveMask)requestEdit();
        updatePickerState();
    });
    connect(pointColorPanel,&PointColorPanel::selectedChanged,this,[this](int){
        if(pointRangeActive)requestEdit();
    });
    connect(pointColorPanel,&PointColorPanel::rangePreviewChanged,this,[this](bool checked,int index){
        endControlEdit(); // the previous selection's gesture is over
        editor->cancelPointColorSample();
        if(pointPickerActive){pointPickerActive=false;pointColorPanel->setPickActive(false);}
        if(checked){sharpenMaskActive=false;if(sharpenMaskPreview){const QSignalBlocker block(sharpenMaskPreview);sharpenMaskPreview->setChecked(false);}}
        pointRangeActive=checked&&index>=0&&index<live.pointColors.size();
        pointColorPanel->setRangeActive(pointRangeActive);
        requestEdit(); // transient view only: no Recipe or history mutation
    });
    // Asynchronous sample results. The payload is only trusted for the photo,
    // tag and mode that requested it; the encoded preview is never resampled.
    connect(editor,&EditLoader::pointColorSampled,this,[this](const QString &path,EditTag tag,PointColorAdjustment point,const QString &error){
        const bool usable=current>=0&&path==photos[current].path&&tag==expected&&tag.job==presentedJob;
        const bool expecting=usable&&pointPickerActive&&!comparing&&!cropSelecting;
        if(!expecting)return;
        if(expecting){
            pointPickerActive=false;
            if(pointColorPanel->findChild<QPushButton *>("point-color-picker"))pointColorPanel->setPickActive(false);
        }
        if(!error.isEmpty()){
            statusBar()->showMessage(error,6000);
            updatePickerState();
            return;
        }
        if(expecting){
            if(live.pointColors.size()>=maximumPointColors){
                statusBar()->showMessage(QStringLiteral("最多 %1 个精确分色取样点").arg(maximumPointColors),6000);
            } else {
                Recipe candidate=live;
                candidate.pointColors.append(point);
                if(validRecipe(candidate)){
                    applyRecipe(candidate,QStringLiteral("精确分色取样"));pointColorPanel->setPoints(candidate.pointColors,candidate.pointColors.size()-1);
                }
            }
        }
        updatePickerState();
    });
}

void Window::updatePointPickerState(bool available){
    if(!pointColorPanel)return;
    auto *picker=pointColorPanel->findChild<QPushButton *>("point-color-picker");
    pointColorPanel->setEnabled(current>=0&&!comparing&&!cropSelecting);
    const bool normalMode=current>=0&&!comparing&&!cropSelecting&&!pointRangeActive&&!sharpenMaskActive&&
        this->live.pointColors.size()<maximumPointColors;
    const bool gate=available&&normalMode&&editor->canSamplePointColor(photos[current].path,this->live,expected);
    if(!gate&&pointPickerActive&&(current<0||comparing||cropSelecting)){
        // A mode change invalidates the pending pick; no result will be applied.
        pointPickerActive=false;
        editor->cancelPointColorSample();
    }
    pointColorPanel->setPickerAvailable(gate);
    pointColorPanel->setPickActive(pointPickerActive&&gate);
    if(picker)picker->setChecked(pointPickerActive&&gate);
    // The canvas owns one picker mode. The white-balance button keeps priority
    // and the point picker only takes the canvas when the gate allows it.
    auto *neutral=findChild<QPushButton *>("wb-picker");
    const bool neutralActive=neutral&&neutral->isChecked();
    canvas->setPickerMode(neutralActive||(pointPickerActive&&gate));
}

void Window::clearPointColorModes(){
    editor->cancelPointColorSample();
    pointPickerActive=false;
    pointRangeActive=false;
    // The diagnostic mask occupies the same single transient view slot: any mode clear
    // (switch, restore, browse, preset apply) must release it without re-entrant signals.
    sharpenMaskActive=false;
    if(sharpenMaskPreview){const QSignalBlocker block(sharpenMaskPreview);sharpenMaskPreview->setChecked(false);}
    if(pointColorPanel){
        pointColorPanel->setPickActive(false);
        pointColorPanel->setRangeActive(false);
    }
    // No requestEdit(): the caller restores controls and requests a fresh pass.
}

bool Window::pickPointColor(QPointF uv,QString &error){
    error.clear();
    if(!pointColorPanel){error=QStringLiteral("分色面板尚未就绪");return false;}
    if(current<0||comparing||cropSelecting){error=QStringLiteral("请在正常编辑模式取样");return false;}
    if(pointRangeActive){error=QStringLiteral("请先关闭范围预览再取色");return false;}
    if(sharpenMaskActive){error=QStringLiteral("请先关闭锐化蒙版显示再取色");return false;}
    if(this->live.pointColors.size()>=maximumPointColors){error=QStringLiteral("最多 %1 个精确分色取样点").arg(maximumPointColors);return false;}
    if(!neutralPickerAvailable()||!editor->canSamplePointColor(photos[current].path,this->live,expected)){
        error=previewError.isEmpty()?QStringLiteral("正在准备当前预览，完成后可取样"):QStringLiteral("当前预览失败，请检查图片");
        updatePickerState();
        return false;
    }
    pointPickerActive=true; // set before the request so an immediate result is accepted
    pointColorPanel->setPickActive(true);
    if(auto *picker=pointColorPanel->findChild<QPushButton *>("point-color-picker"))picker->setChecked(true);
    if(!editor->requestPointColorSample(photos[current].path,this->live,expected,uv,error)){
        pointPickerActive=false;
        if(auto *picker=pointColorPanel->findChild<QPushButton *>("point-color-picker"))picker->setChecked(false);
        updatePickerState();
        return false;
    }
    updatePickerState();
    return true;
}





