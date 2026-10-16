#include "local_gradient_transaction.h"
#include "render/cpu_renderer.h"
#include <cmath>
#include <algorithm>
#include <limits>

namespace LocalEdit {
namespace {
constexpr double pi=3.14159265358979323846;
bool finitePoint(QPointF p){return std::isfinite(p.x())&&std::isfinite(p.y())&&std::abs(p.x())<=1e8&&std::abs(p.y())<=1e8;}
}
QVector<QPointF> gradientHandlePoints(const MaskGeometry &geometry){
    if(const auto *g=std::get_if<LinearGeometry>(&geometry))return {g->start,g->end,(g->start+g->end)/2};
    if(const auto *g=std::get_if<RadialGeometry>(&geometry)){
        const double angle=g->angleDegrees*pi/180;const QPointF x(std::cos(angle),std::sin(angle)),y(-x.y(),x.x());
        return {g->center,g->center+x*g->radiusX,g->center+y*g->radiusY,g->center+x*(1.25*g->radiusX)};
    }return {};
}
struct GradientTransaction::State {
    PhotoDocument base;DocumentSnapshot draft;
    qsizetype layerIndex=-1,componentIndex=-1;
    MaskComponent original;
    QPointF pressSource;
    int handle=-1;GradientKind kind=GradientKind::Linear;
    bool creating=false,modified=false;
    void resetToBase(){if(modified){draft=std::make_shared<const PhotoDocument>(base);modified=false;}}
    bool start(DocumentSnapshot doc,const QString &layerId,QPointF uv,QString &error){
        if(!doc||!validDocument(*doc,error,true)){if(error.isEmpty())error="Missing gradient document";return false;}
        base=*doc;
        for(qsizetype i=0;i<base.layers.size();++i)if(base.layers[i].id==layerId){layerIndex=i;break;}
        if(layerIndex<0){error="Gradient layer no longer exists";return false;}
        const auto &layer=base.layers[layerIndex];constexpr auto max=std::numeric_limits<quint64>::max();
        if(base.revision==max||base.localIdentity==max||layer.version==max||layer.mask.version==max){error="Gradient version overflow";return false;}
        if(!finitePoint(uv)){error="Invalid gradient position";return false;}
        pressSource=outputToSource(uv,base.asset.normalizedSize,base.global);
        if(!finitePoint(pressSource)){error="Invalid source mapping";return false;}
        draft=std::make_shared<const PhotoDocument>(base);return true;
    }
    bool update(QPointF uv,QString &error){
        if(!finitePoint(uv)){error="Invalid gradient position";return false;}
        const auto point=outputToSource(uv,base.asset.normalizedSize,base.global);
        if(!finitePoint(point)){error="Invalid source mapping";return false;}
        const auto delta=point-pressSource;auto component=original;
        if(!creating&&point==pressSource){resetToBase();return true;}
        if(creating){
            if(kind==GradientKind::Linear){
                if(QPointF::dotProduct(delta,delta)<=1e-12){resetToBase();return true;}
                component.geometry=LinearGeometry{pressSource,point};
            }else {
                const double radius=std::hypot(delta.x(),delta.y());
                if(radius==0){resetToBase();return true;}
                component.geometry=RadialGeometry{pressSource,radius,radius,0};
            }
        }else if(auto *g=std::get_if<LinearGeometry>(&component.geometry)){
            if(handle==0||handle==2)g->start+=delta;
            if(handle==1||handle==2)g->end+=delta;
        }else {
            auto &radial=std::get<RadialGeometry>(component.geometry);
            const auto handles=gradientHandlePoints(original.geometry);
            const double angle=radial.angleDegrees*pi/180;const QPointF x(std::cos(angle),std::sin(angle)),y(-x.y(),x.x());
            const auto vector=handles[handle]+delta-radial.center;
            if(handle==0)radial.center+=delta;
            else if(handle==1)radial.radiusX=std::max(1.,QPointF::dotProduct(vector,x));
            else if(handle==2)radial.radiusY=std::max(1.,QPointF::dotProduct(vector,y));
            else if(std::hypot(vector.x(),vector.y())>=1)radial.angleDegrees=std::atan2(vector.y(),vector.x())*180/pi;
        }
        if(!validateMaskGeometry(component.geometry,error))return false;
        if(!creating&&component.geometry==original.geometry){resetToBase();return true;}
        auto next=base;auto &layer=next.layers[layerIndex];
        if(creating)layer.mask.components.append(component);else layer.mask.components[componentIndex]=component;
        ++layer.version;++layer.mask.version;
        if(!validDocument(next,error,true))return false;
        if(draft&&next==*draft)return true;
        draft=std::make_shared<const PhotoDocument>(std::move(next));modified=true;return true;
    }
    bool meaningful() const {
        if(!modified)return false;
        if(!creating)return true;
        if(original.strength==0)return false;
        if(original.operation==MaskOperation::Add)return true;
        for(const auto &component:base.layers[layerIndex].mask.components)
            if(component.operation==MaskOperation::Add&&component.strength>0)return true;
        return false;
    }
};
GradientTransaction::GradientTransaction()=default;
GradientTransaction::~GradientTransaction()=default;
bool GradientTransaction::active() const{return bool(state);}
DocumentSnapshot GradientTransaction::preview() const{return state?state->draft:DocumentSnapshot{};}
void GradientTransaction::cancel(){state.reset();}
bool GradientTransaction::beginCreate(DocumentSnapshot doc,const QString &layerId,const QString &componentId,
    GradientKind kind,QPointF uv,double strength,MaskOperation operation,QString &error){
    error.clear();if(state){error="Gradient gesture already active";return false;}
    if((kind!=GradientKind::Linear&&kind!=GradientKind::Radial)||!std::isfinite(strength)||strength<0||strength>1||
       (operation!=MaskOperation::Add&&operation!=MaskOperation::Subtract)){error="Invalid gradient settings";return false;}
    auto next=std::make_unique<State>();if(!next->start(doc,layerId,uv,error))return false;
    next->creating=true;next->kind=kind;next->original.id=componentId;next->original.strength=strength;next->original.operation=operation;
    next->original.geometry=kind==GradientKind::Linear?MaskGeometry(LinearGeometry{}):MaskGeometry(RadialGeometry{});
    // Validate identity/capacity using a valid temporary shape. The zero-length
    // press itself is not yet a publishable gradient and remains a no-op.
    auto check=next->base;check.layers[next->layerIndex].mask.components.append(next->original);
    if(!validDocument(check,error,true))return false;
    state=std::move(next);return true;
}
bool GradientTransaction::beginEdit(DocumentSnapshot doc,const QString &layerId,const QString &componentId,
    int handle,QPointF uv,QString &error){
    error.clear();if(state){error="Gradient gesture already active";return false;}
    auto next=std::make_unique<State>();if(!next->start(doc,layerId,uv,error))return false;
    const auto &components=next->base.layers[next->layerIndex].mask.components;
    for(qsizetype i=0;i<components.size();++i)if(components[i].id==componentId){next->componentIndex=i;next->original=components[i];break;}
    if(next->componentIndex<0){error="Gradient component no longer exists";return false;}
    const auto handles=gradientHandlePoints(next->original.geometry);
    if(handle<0||handle>=handles.size()){error="Invalid gradient handle";return false;}
    next->handle=handle;state=std::move(next);return true;
}
bool GradientTransaction::append(QPointF uv,QString &error){
    error.clear();if(!state){error="No active gradient gesture";return false;}return state->update(uv,error);
}
DocumentSnapshot GradientTransaction::finish(DocumentSnapshot current,QString &error){
    error.clear();auto completed=std::move(state);if(!completed||!completed->meaningful())return {};
    if(!current||!validDocument(*current,error,true)){if(error.isEmpty())error="Missing current document";return {};}
    const auto &a=current->asset;const auto &b=completed->base.asset;
    if(a.id!=b.id||a.sourcePath!=b.sourcePath||a.sourceHash.compare(b.sourceHash,Qt::CaseInsensitive)!=0||a.normalizedSize!=b.normalizedSize||
       a.raw!=b.raw||current->global!=completed->base.global||current->layers!=completed->base.layers||current->algorithmVersion!=completed->base.algorithmVersion){error="Document changed during gradient gesture";return {};}
    constexpr auto max=std::numeric_limits<quint64>::max();
    if(current->revision==max||current->localIdentity==max){error="Gradient version overflow";return {};}
    auto result=*current;result.layers=completed->draft->layers;++result.revision;++result.localIdentity;
    if(!validDocument(result,error,true))return {};
    return std::make_shared<const PhotoDocument>(std::move(result));
}
}
