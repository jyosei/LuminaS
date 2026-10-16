#include "window.h"
#include "core/local_layer_operations.h"
#include <QStatusBar>
#include <QUuid>
#include <cmath>

void Window::applyLocalLayerChange(LocalEdit::LayerChange change,const QString &name){
    endControlEdit();const auto base=currentPhotoDocument();
    if(!base||!localEditingAvailable()){refreshLocalPanel();return;}
    qsizetype oldIndex=-1;
    for(qsizetype i=0;i<base->layers.size();++i)if(base->layers[i].id==change.layerId){oldIndex=i;break;}
    if(change.kind==LocalEdit::LayerChangeKind::Duplicate&&oldIndex>=0){
        change.duplicateId=QUuid::createUuid().toString(QUuid::WithoutBraces);
        for(const auto &component:base->layers[oldIndex].mask.components){
            Q_UNUSED(component);
            change.componentIds.append(QUuid::createUuid().toString(QUuid::WithoutBraces));
        }
        auto stem=base->layers[oldIndex].name.left(4090);
        if(!stem.isEmpty()&&stem.at(stem.size()-1).isHighSurrogate())stem.chop(1);
        change.name=(stem.isEmpty()?QStringLiteral("局部层"):stem)+QStringLiteral(" 副本");
    }
    QString error;const auto candidate=LocalEdit::changedLayers(*base,change,error);
    if(!candidate){if(!error.isEmpty())statusBar()->showMessage(error,6000);refreshLocalPanel();return;}
    if(history.commitSnapshot(base->asset.sourcePath,candidate,name,&error)){
        ++revision;++localIdentity;
        if(change.kind==LocalEdit::LayerChangeKind::Duplicate)selectedLocalLayer=change.duplicateId;
        else if(change.kind==LocalEdit::LayerChangeKind::Remove&&selectedLocalLayer==change.layerId){
            selectedLocalLayer=candidate->layers.isEmpty()?QString{}:candidate->layers[qMin(oldIndex,candidate->layers.size()-1)].id;
        }
    }else if(!error.isEmpty())statusBar()->showMessage(error,6000);
    refreshLocalPanel();requestEdit();
}

void Window::editLocalOpacity(const QString &id,double opacity){
    if(localFinishing||!std::isfinite(opacity)||opacity<0||opacity>1)return;
    if(localParameterBase&&(!localOpacityDraft||localDraftLayer!=id))finishLocalEdits();
    if(!localParameterBase){
        endControlEdit();if(!localEditingAvailable())return;
        localParameterBase=currentPhotoDocument();if(!localParameterBase)return;
        localOpacityDraft=true;localFeatherDraft=false;localDraftLayer=id;++localIdentity;
    }
    LocalEdit::LayerChange change;change.kind=LocalEdit::LayerChangeKind::Opacity;change.layerId=id;change.opacity=opacity;
    QString error;auto candidate=LocalEdit::changedLayers(*localParameterBase,change,error);
    if(!candidate){
        if(!error.isEmpty()){
            if(!localParameterDraft){localParameterBase.reset();localOpacityDraft=false;localDraftLayer.clear();}
            statusBar()->showMessage(error,6000);refreshLocalPanel();return;
        }
        candidate=localParameterBase; // dragged back to the original value
    }
    if(localParameterDraft&&*localParameterDraft==*candidate)return;
    localParameterDraft=std::move(candidate);requestEdit();
}
void Window::editLocalFeather(double feather){
    if(localFinishing||!std::isfinite(feather)||feather<0||feather>16384)return;
    if(localParameterBase&&(!localFeatherDraft||localDraftLayer!=selectedLocalLayer))finishLocalEdits();
    if(!localParameterBase){
        endControlEdit();if(!localEditingAvailable())return;
        localParameterBase=currentPhotoDocument();if(!localParameterBase)return;
        localFeatherDraft=true;localOpacityDraft=false;localDraftLayer=selectedLocalLayer;++localIdentity;
    }
    LocalEdit::LayerChange change;change.kind=LocalEdit::LayerChangeKind::Feather;change.layerId=localDraftLayer;change.feather=feather;
    QString error;auto candidate=LocalEdit::changedLayers(*localParameterBase,change,error);
    if(!candidate){
        if(!error.isEmpty()){
            if(!localParameterDraft){localParameterBase.reset();localFeatherDraft=false;localDraftLayer.clear();}
            statusBar()->showMessage(error,6000);refreshLocalPanel();return;
        }
        candidate=localParameterBase;
    }
    if(localParameterDraft&&*localParameterDraft==*candidate)return;
    localParameterDraft=std::move(candidate);requestEdit();
}
