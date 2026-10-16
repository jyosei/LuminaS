#include "local_reference.h"
#include "cpu_renderer.h"
#include "tone_operator.h"
#include "local_mask_math.h"
#include "local_gradient_math.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <QSet>

namespace LocalEdit {
namespace {
bool validPoint(QPointF point){return std::isfinite(point.x())&&std::isfinite(point.y())&&
    std::abs(point.x())<=1e8&&std::abs(point.y())<=1e8;}
bool validHead(double radius,double hardness){return std::isfinite(radius)&&radius>0&&radius<=1e8&&
    std::isfinite(hardness)&&hardness>=0&&hardness<=1;}
Recipe localRecipe(const LocalParameters &p){
    Recipe r;r.exposure=p.exposure;r.contrast=p.contrast;r.highlights=p.highlights;
    r.shadows=p.shadows;r.whites=p.whites;r.blacks=p.blacks;r.saturation=p.saturation;
    r.warmth=p.warmth;r.tint=p.tint;return r;
}
float headCoverage(QPointF position,QPointF center,const BrushGeometry &brush){
    const double distance=std::hypot(position.x()-center.x(),position.y()-center.y());
    return Detail::brushHeadCoverage(distance,brush.radius,brush.hardness);
}
}

BrushSampler::BrushSampler(double radius,double hardness):radius(radius),hardness(hardness),
    step(radius/8),next(step),validSettings(validHead(radius,hardness)&&step>0&&std::isfinite(step)){}
bool BrushSampler::append(QPointF point,QString &error){
    error.clear();
    if(!validSettings||!validPoint(point)){error="Invalid brush settings/point";return false;}
    if(samples.isEmpty()){samples.append(point);last=point;return true;}
    const double length=std::hypot(point.x()-last.x(),point.y()-last.y());
    if(length==0)return true;
    const double count=length<next?0:std::floor((length-next)/step)+1;
    // Reserve a possible final endpoint. Check before mutating any sampler state.
    if(!std::isfinite(count)||count>maximumPointsPerStroke-samples.size()-1){
        error="Brush stroke exceeds point budget";return false;
    }
    const QPointF delta=point-last;
    for(int i=0;i<int(count);++i){
        const double distance=std::min(length,next+i*step);
        samples.append(last+delta*(distance/length));
    }
    next=next+count*step-length;
    // At an exact endpoint a roundoff-sized non-positive residual means the
    // endpoint was already emitted; the following stamp is one step farther.
    if(next<=0)next=step;
    last=point;return true;
}
BrushGeometry BrushSampler::snapshot() const {
    BrushGeometry result;result.radius=radius;result.hardness=hardness;result.points=samples;
    if(!samples.isEmpty()&&samples.last()!=last)result.points.append(last);
    return result;
}

BrushMaskReference::BrushMaskReference(Mask value):mask(std::move(value)){
    if(mask.version<1||mask.feather!=0||mask.invert||mask.components.size()>maximumComponentsPerLayer){
        problem="Brush reference requires feather=0 and invert=false";return;
    }
    QSet<QString> ids;qsizetype count=0;
    for(const auto &component:mask.components){
        const auto *brush=std::get_if<BrushGeometry>(&component.geometry);
        if(component.id.isEmpty()||component.id.size()>128||ids.contains(component.id)||
           !std::isfinite(component.strength)||component.strength<0||component.strength>1||
           (component.operation!=MaskOperation::Add&&component.operation!=MaskOperation::Subtract)||
           (brush&&(!validHead(brush->radius,brush->hardness)||brush->points.isEmpty()||
           brush->points.size()>maximumPointsPerStroke))){problem="Invalid mask component";return;}
        if(!brush&&!validateMaskGeometry(component.geometry,problem))return;
        ids.insert(component.id);if(!brush)continue;
        count+=brush->points.size();
        if(count>maximumPointsPerDocument){problem="Brush mask exceeds point budget";return;}
        for(const auto &point:brush->points)if(!validPoint(point)){problem="Invalid brush point";return;}
    }
}
float BrushMaskReference::sample(QPointF point,float alpha) const {
    if(!valid()||!std::isfinite(alpha))return std::numeric_limits<float>::quiet_NaN();
    if(alpha<=1e-8f||!std::isfinite(point.x())||!std::isfinite(point.y()))return 0;
    float coverage=0;
    for(const auto &component:mask.components){
        if(component.strength==0)continue;
        float maximum=0;
        if(const auto *brush=std::get_if<BrushGeometry>(&component.geometry)){
            for(const auto &center:brush->points){maximum=std::max(maximum,headCoverage(point,center,*brush));if(maximum==1)break;}
        }else if(const auto *linear=std::get_if<LinearGeometry>(&component.geometry))maximum=Detail::linearGradientCoverage(*linear,point);
        else maximum=Detail::radialGradientCoverage(std::get<RadialGeometry>(component.geometry),point);
        const float amount=maximum*float(component.strength);
        coverage=component.operation==MaskOperation::Add?coverage+(1-coverage)*amount:coverage*(1-amount);
    }
    return std::clamp(coverage,0.f,1.f);
}
bool BrushMaskReference::rasterize(const MaskMapping &mapping,const QImage &alphaFrame,QRect rect,
    QVector<float> &output,QString &error,CancellationToken cancel) const {
    error.clear();
    if(!valid()){error=problem;return false;}
    if(isCanceled(cancel)){error="Canceled";return false;}
    if(mapping.sourceSize.isEmpty()||mapping.outputSize.isEmpty()||!validRecipe(mapping.geometry)||
       qint64(mapping.sourceSize.width())*mapping.sourceSize.height()>45000000||
       qint64(mapping.outputSize.width())*mapping.outputSize.height()>45000000||rect.isEmpty()||
       !QRect(QPoint(),mapping.outputSize).contains(rect)||alphaFrame.size()!=mapping.outputSize||
       alphaFrame.format()!=QImage::Format_RGBA32FPx4){error="Invalid mask mapping/alpha frame/rectangle";return false;}
    QVector<float> result(qsizetype(rect.width())*rect.height());
    for(int y=rect.top();y<=rect.bottom();++y){
        if(isCanceled(cancel)){error="Canceled";return false;}
        const auto *alpha=reinterpret_cast<const float *>(alphaFrame.constScanLine(y));
        for(int x=rect.left();x<=rect.right();++x){
            const auto point=outputToSource({(x+.5)/mapping.outputSize.width(),(y+.5)/mapping.outputSize.height()},
                mapping.sourceSize,mapping.geometry);
            const float coverage=sample(point,alpha[x*4+3]);
            if(!std::isfinite(coverage)){error="Invalid alpha/mask sample";return false;}
            result[qsizetype(y-rect.top())*rect.width()+x-rect.left()]=coverage;
        }
    }
    output=std::move(result);return true;
}

struct LocalPixelTransform::Impl {
    LocalParameters parameters;
    ToneOperator tone;
    float red,green,blue,exposure,contrast;
    explicit Impl(const LocalParameters &p):parameters(p),tone(localRecipe(p)),
        red(float(std::exp2(p.warmth))),green(float(std::exp2(p.tint))),blue(float(std::exp2(-p.warmth))),
        exposure(float(std::exp2(p.exposure))),contrast(float(std::exp2(p.contrast))){}
};
LocalPixelTransform::LocalPixelTransform(const LocalParameters &p){
    if(validRecipe(localRecipe(p)))impl=std::make_shared<const Impl>(p);
}
bool LocalPixelTransform::identity() const{return impl&&impl->parameters==LocalParameters{};}
bool LocalPixelTransform::apply(float pixel[4],float coverage,float opacity) const {
    if(!impl||!pixel||!std::isfinite(coverage)||coverage<0||coverage>1||
       !std::isfinite(opacity)||opacity<0||opacity>1)return false;
    for(int c=0;c<4;++c)if(!std::isfinite(pixel[c]))return false;
    const float weight=coverage*opacity;
    if(weight==0||pixel[3]<=1e-8f||identity())return true;
    float adjusted[]{pixel[0]*impl->red,pixel[1]*impl->green,pixel[2]*impl->blue};
    for(int c=0;c<3;++c)adjusted[c]=(adjusted[c]*impl->exposure-.18f)*impl->contrast+.18f;
    impl->tone.apply(adjusted);
    float blended[3];
    for(int c=0;c<3;++c){
        if(!std::isfinite(adjusted[c]))return false;
        blended[c]=weight==1?adjusted[c]:float(double(pixel[c])+double(weight)*(double(adjusted[c])-pixel[c]));
        if(!std::isfinite(blended[c]))return false;
    }
    for(int c=0;c<3;++c)pixel[c]=blended[c];
    return true;
}
}
