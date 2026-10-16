#include "local_brush_transaction.h"
#include "render/local_reference.h"
#include "render/cpu_renderer.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace LocalEdit {
struct BrushTransaction::State {
    PhotoDocument base;
    DocumentSnapshot draft;
    qsizetype layerIndex=0;
    MaskComponent component;
    BrushSampler sampler;
    State(const PhotoDocument &document,qsizetype index,QString id,double radius,double hardness,
        double strength,MaskOperation operation):base(document),layerIndex(index),sampler(radius,hardness){
        component.id=std::move(id);component.strength=strength;component.operation=operation;
    }
    bool append(QPointF uv,QString &error){
        if(!std::isfinite(uv.x())||!std::isfinite(uv.y())){error="Invalid brush position";return false;}
        const auto point=outputToSource(uv,base.asset.normalizedSize,base.global);
        auto next=sampler;if(!next.append(point,error))return false;
        const auto geometry=next.snapshot();
        if(draft&&std::get<BrushGeometry>(draft->layers[layerIndex].mask.components.last().geometry)==geometry)return true;
        auto document=base;auto stroke=component;stroke.geometry=geometry;
        auto &layer=document.layers[layerIndex];layer.mask.components.append(stroke);
        ++layer.mask.version;++layer.version;++document.revision;++document.localIdentity;
        if(!validDocument(document,error,true))return false;
        sampler=std::move(next);draft=std::make_shared<const PhotoDocument>(std::move(document));return true;
    }
    bool meaningful() const {
        if(!draft||component.strength==0)return false;
        const auto &layer=base.layers[layerIndex];
        if(component.operation==MaskOperation::Subtract&&!layer.mask.invert){
            bool hasAddition=false;
            for(const auto &part:layer.mask.components)
                if(part.operation==MaskOperation::Add&&part.strength>0){hasAddition=true;break;}
            if(!hasAddition)return false;
        }
        const auto geometry=sampler.snapshot();
        const double width=base.asset.normalizedSize.width(),height=base.asset.normalizedSize.height();
        for(auto point:geometry.points){
            const double dx=std::max({0.,-point.x(),point.x()-width});
            const double dy=std::max({0.,-point.y(),point.y()-height});
            if(std::hypot(dx,dy)<geometry.radius)return true;
        }
        return false;
    }
};
BrushTransaction::BrushTransaction()=default;
BrushTransaction::~BrushTransaction()=default;
bool BrushTransaction::active() const{return bool(state);}
DocumentSnapshot BrushTransaction::preview() const{return state?state->draft:DocumentSnapshot{};}
void BrushTransaction::cancel(){state.reset();}
bool BrushTransaction::begin(DocumentSnapshot document,const QString &layerId,const QString &componentId,
    QPointF uv,double radius,double hardness,double strength,MaskOperation operation,QString &error){
    error.clear();
    if(state){error="Brush gesture already active";return false;}
    if(!document||!validDocument(*document,error,true)){
        if(error.isEmpty())error="Missing brush document";return false;
    }
    if(!std::isfinite(strength)||strength<0||strength>1||
       (operation!=MaskOperation::Add&&operation!=MaskOperation::Subtract)){
        error="Invalid brush strength/operation";return false;
    }
    qsizetype index=-1;
    for(qsizetype i=0;i<document->layers.size();++i)if(document->layers[i].id==layerId){index=i;break;}
    if(index<0){error="Brush layer no longer exists";return false;}
    constexpr auto maximum=std::numeric_limits<quint64>::max();
    const auto &layer=document->layers[index];
    if(document->revision==maximum||document->localIdentity==maximum||layer.version==maximum||layer.mask.version==maximum){
        error="Brush version overflow";return false;
    }
    auto next=std::make_unique<State>(*document,index,componentId,radius,hardness,strength,operation);
    if(!next->sampler.valid()){error="Invalid brush settings";return false;}
    if(!next->append(uv,error))return false;
    state=std::move(next);return true;
}
bool BrushTransaction::append(QPointF uv,QString &error){
    error.clear();if(!state){error="No active brush gesture";return false;}
    return state->append(uv,error);
}
DocumentSnapshot BrushTransaction::finish(DocumentSnapshot current,QString &error){
    error.clear();auto completed=std::move(state);if(!completed)return {};
    if(!completed->meaningful())return {};
    if(!current||!validDocument(*current,error,true)){
        if(error.isEmpty())error="Missing current document";return {};
    }
    const auto &a=current->asset;const auto &b=completed->base.asset;
    if(a.id!=b.id||a.sourcePath!=b.sourcePath||a.sourceHash.compare(b.sourceHash,Qt::CaseInsensitive)!=0||
       a.normalizedSize!=b.normalizedSize||a.raw!=b.raw||current->global!=completed->base.global||
       current->layers!=completed->base.layers||current->algorithmVersion!=completed->base.algorithmVersion){
        error="Document changed during brush gesture";return {};
    }
    constexpr auto maximum=std::numeric_limits<quint64>::max();
    if(current->revision==maximum||current->localIdentity==maximum){error="Brush version overflow";return {};}
    auto result=*current;result.layers=completed->draft->layers;++result.revision;++result.localIdentity;
    if(!validDocument(result,error,true))return {};
    return std::make_shared<const PhotoDocument>(std::move(result));
}
}
