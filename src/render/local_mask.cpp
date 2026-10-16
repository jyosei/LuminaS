#include "local_mask.h"
#include "local_mask_math.h"
#include "local_gradient_math.h"
#include "local_mask_feather.h"
#include "geometry_transform.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>
#include <QElapsedTimer>
#include <QThreadPool>
#include <QtConcurrentRun>
#include <array>
#include <atomic>
#include <cstring>
#include <optional>

namespace LocalEdit {
namespace {
struct Bounds {
    double left=0,right=0,top=0,bottom=0;bool empty=true;
    void add(QPointF p){
        if(empty){left=right=p.x();top=bottom=p.y();empty=false;}
        else{left=std::min(left,p.x());right=std::max(right,p.x());top=std::min(top,p.y());bottom=std::max(bottom,p.y());}
    }
    bool contains(QPointF p,double radius) const{return !empty&&p.x()>=left-radius&&p.x()<=right+radius&&
        p.y()>=top-radius&&p.y()<=bottom+radius;}
    bool intersects(const Bounds &b,double radius) const{return !empty&&!b.empty&&
        right+radius>=b.left&&left-radius<=b.right&&bottom+radius>=b.top&&top-radius<=b.bottom;}
};
struct Stroke {
    QVector<QPointF> points;
    Bounds bounds;
    double radius=0,hardness=0;
    std::optional<LinearGeometry> linear;
    std::optional<RadialGeometry> radial;
    double co=1,si=0;
    float strength;
    MaskOperation operation;
    void build(int first,int end,int axis){
        if(first>=end)return;
        const int middle=first+(end-first)/2;
        auto *data=points.data();
        std::nth_element(data+first,data+middle,data+end,[axis](QPointF a,QPointF b){
            return axis?a.y()<b.y():a.x()<b.x();
        });
        build(first,middle,1-axis);build(middle+1,end,1-axis);
    }
    void nearest(QPointF p,int first,int end,int axis,double &best) const {
        if(first>=end||(best<radius&&best<=radius*hardness))return;
        const int middle=first+(end-first)/2;
        const auto center=points[middle];
        const double dx=p.x()-center.x(),dy=p.y()-center.y();
        if(std::abs(dx)<best&&std::abs(dy)<best)best=std::min(best,std::hypot(dx,dy));
        const double difference=axis?p.y()-center.y():p.x()-center.x();
        if(difference<0){
            nearest(p,first,middle,1-axis,best);
            if(std::abs(difference)<=std::nextafter(best,std::numeric_limits<double>::infinity()))
                nearest(p,middle+1,end,1-axis,best);
        }else{
            nearest(p,middle+1,end,1-axis,best);
            if(std::abs(difference)<=std::nextafter(best,std::numeric_limits<double>::infinity()))
                nearest(p,first,middle,1-axis,best);
        }
    }
    float amount(QPointF p) const {
        if(strength==0)return 0;
        if(linear)return Detail::linearGradientCoverage(*linear,p)*strength;
        if(radial)return Detail::radialGradientCoverage(*radial,p,co,si)*strength;
        if(strength==0||!bounds.contains(p,radius))return 0;
        double distance=radius;
        nearest(p,0,int(points.size()),0,distance);
        return Detail::brushHeadCoverage(distance,radius,hardness)*strength;
    }
};
}
struct PreparedBrushMask::Impl {std::vector<Stroke> strokes;qsizetype bytes=0;double feather=0;bool invert=false,zeroMask=true;};
PreparedBrushMask::PreparedBrushMask(const Mask &mask){
    const BrushMaskReference check(mask);
    if(!check.valid()){problem=check.error();return;}
    auto prepared=std::make_shared<Impl>();prepared->strokes.reserve(mask.components.size());
    prepared->feather=mask.feather;prepared->invert=mask.invert;
    for(const auto &component:mask.components){
        if(component.operation==MaskOperation::Add&&component.strength>0)prepared->zeroMask=false;
        Stroke stroke;
        stroke.strength=float(component.strength);stroke.operation=component.operation;
        if(const auto *geometry=std::get_if<BrushGeometry>(&component.geometry)){
            stroke.points=geometry->points;stroke.radius=geometry->radius;stroke.hardness=geometry->hardness;
            for(const auto &point:geometry->points)stroke.bounds.add(point);
            stroke.build(0,int(stroke.points.size()),0);
        }else if(const auto *linear=std::get_if<LinearGeometry>(&component.geometry))stroke.linear=*linear;
        else {
            const auto radial=std::get<RadialGeometry>(component.geometry);stroke.radial=radial;
            const double angle=radial.angleDegrees*3.14159265358979323846/180;
            stroke.co=std::cos(angle);stroke.si=std::sin(angle);
            const double ex=std::hypot(radial.radiusX*stroke.co,radial.radiusY*stroke.si);
            const double ey=std::hypot(radial.radiusX*stroke.si,radial.radiusY*stroke.co);
            const double pad=8*std::numeric_limits<double>::epsilon()*(1+std::abs(radial.center.x())+std::abs(radial.center.y())+radial.radiusX+radial.radiusY);
            stroke.bounds.add(radial.center-QPointF(ex+pad,ey+pad));
            stroke.bounds.add(radial.center+QPointF(ex+pad,ey+pad));
        }
        prepared->bytes+=stroke.points.capacity()*qsizetype(sizeof(QPointF))+qsizetype(sizeof(Stroke));
        prepared->strokes.push_back(std::move(stroke));
    }
    impl=std::move(prepared);
}
qsizetype PreparedBrushMask::indexBytes() const{return impl?impl->bytes:0;}
float PreparedBrushMask::sample(QPointF point,float alpha) const {
    if(!impl||!std::isfinite(alpha))return std::numeric_limits<float>::quiet_NaN();
    if(alpha<=1e-8f||!std::isfinite(point.x())||!std::isfinite(point.y()))return 0;
    const float value=impl->zeroMask?0:impl->feather>0?
        Detail::featherSample(point,impl->feather,[this](QPointF p){return rawSample(p);}):rawSample(point);
    return impl->invert?1-value:value;
}
float PreparedBrushMask::rawSample(QPointF point) const {
    float coverage=0;
    for(const auto &stroke:impl->strokes){const float a=stroke.amount(point);
        coverage=stroke.operation==MaskOperation::Add?coverage+(1-coverage)*a:coverage*(1-a);}
    return std::clamp(coverage,0.f,1.f);
}
bool PreparedBrushMask::rasterize(const MaskMapping &mapping,const QImage &alphaFrame,QRect rect,
    QVector<float> &output,QString &error,CancellationToken cancel,MaskRasterScratch *scratch) const {
    error.clear();
    if(!impl){error=problem;return false;}
    if(isCanceled(cancel)){error="Canceled";return false;}
    if(mapping.sourceSize.isEmpty()||mapping.outputSize.isEmpty()||!validRecipe(mapping.geometry)||
       qint64(mapping.sourceSize.width())*mapping.sourceSize.height()>45000000||
       qint64(mapping.outputSize.width())*mapping.outputSize.height()>45000000||rect.isEmpty()||
       rect.width()>512||rect.height()>512||!QRect(QPoint(),mapping.outputSize).contains(rect)||
       alphaFrame.size()!=mapping.outputSize||alphaFrame.format()!=QImage::Format_RGBA32FPx4){
        error="Invalid mask tile/mapping/alpha frame";return false;
    }
    QElapsedTimer timer;timer.start();
    const qsizetype count=qsizetype(rect.width())*rect.height();
    MaskRasterScratch local;
    auto &memory=scratch?*scratch:local;
    constexpr qsizetype maximumTilePoints=512*512;
    if(memory.points.capacity()>maximumTilePoints)memory.points={};
    if(memory.active.capacity()>maximumTilePoints)memory.active={};
    if(memory.values.capacity()>maximumTilePoints)memory.values={};
    memory.mappingNs=memory.samplingNs=0;memory.featherScratchBytes=0;
    memory.points.resize(count);memory.active.fill(0,count);memory.values.fill(0,count);
    auto *points=memory.points.data();auto *active=memory.active.data();auto *result=memory.values.data();
    const OutputSourceMapping transform(mapping.sourceSize,mapping.geometry);
    Bounds tileBounds;
    const double width=mapping.outputSize.width(),height=mapping.outputSize.height();
    for(int y=rect.top();y<=rect.bottom();++y){
        if(isCanceled(cancel)){error="Canceled";return false;}
        const auto *row=reinterpret_cast<const float *>(alphaFrame.constScanLine(y));
        for(int x=rect.left();x<=rect.right();++x){
            const float alpha=row[x*4+3];
            if(!std::isfinite(alpha)){error="Invalid alpha";return false;}
            if(alpha<=1e-8f)continue;
            const auto p=transform.map({(x+.5)/width,(y+.5)/height});
            if(!std::isfinite(p.x())||!std::isfinite(p.y()))continue;
            const qsizetype index=qsizetype(y-rect.top())*rect.width()+x-rect.left();
            points[index]=p;active[index]=1;tileBounds.add(p);
        }
    }
    memory.mappingNs=timer.nsecsElapsed();
    if(impl->feather>0&&!impl->zeroMask){
        QVector<float> feathered;
        if(!Detail::featherPoints(memory.points,memory.active,impl->feather,[this](QPointF p){return rawSample(p);},
            feathered,error,cancel,&memory.featherScratchBytes))return false;
        memory.values=std::move(feathered);result=memory.values.data();
    }else for(const auto &stroke:impl->strokes){
        if(isCanceled(cancel)){error="Canceled";return false;}
        if(stroke.strength==0||(!stroke.linear&&!stroke.bounds.intersects(tileBounds,stroke.radius)))continue;
        for(qsizetype i=0;i<count;++i)if(active[i]){
            const float a=stroke.amount(points[i]);
            result[i]=stroke.operation==MaskOperation::Add?result[i]+(1-result[i])*a:result[i]*(1-a);
        }
    }
    if(isCanceled(cancel)){error="Canceled";return false;}
    for(qsizetype i=0;i<count;++i){
        result[i]=std::clamp(result[i],0.f,1.f);
        if(impl->invert&&active[i])result[i]=1-result[i];
    }
    memory.samplingNs=timer.nsecsElapsed()-memory.mappingNs;
    output.swap(memory.values);
    if(memory.values.capacity()>maximumTilePoints)memory.values={};
    return true;
}

bool rasterizeMaskPreview(const PreparedBrushMask &mask,const MaskMapping &mapping,
    const QImage &alpha,QVector<float> &output,QString &error,CancellationToken cancel,
    int tileSize,MaskPreviewStats *stats){
    error.clear();if(stats)*stats={};
    if(isCanceled(cancel)){error="Canceled";return false;}
    if(!mask.valid()){error=mask.error();return false;}
    if((tileSize!=256&&tileSize!=512)||mapping.outputSize.isEmpty()||
       std::max(mapping.outputSize.width(),mapping.outputSize.height())>3840){
        error="Invalid mask preview size/tile";return false;
    }
    QVector<float> result(qsizetype(mapping.outputSize.width())*mapping.outputSize.height());
    auto *destination=result.data();
    const int columns=(mapping.outputSize.width()+tileSize-1)/tileSize;
    const int rows=(mapping.outputSize.height()+tileSize-1)/tileSize;
    const int tileCount=columns*rows,workers=std::min(4,tileCount);
    static QThreadPool pool;
    // Configure once: concurrent preview callers must not mutate pool settings.
    static const bool configured=[] {pool.setMaxThreadCount(4);return true;}();
    Q_UNUSED(configured);
    std::array<QFuture<void>,4> futures;
    std::array<QString,4> errors;
    std::array<MaskPreviewStats,4> perWorker;
    std::atomic<bool> failed{false};
    for(int worker=0;worker<workers;++worker)futures[worker]=QtConcurrent::run(&pool,[&,worker]{
        MaskRasterScratch scratch;QVector<float> tile;
        for(int index=worker;index<tileCount;index+=workers){
            if(failed.load(std::memory_order_relaxed)||isCanceled(cancel))break;
            const int x=(index%columns)*tileSize,y=(index/columns)*tileSize;
            const QRect rect(x,y,std::min(tileSize,mapping.outputSize.width()-x),
                std::min(tileSize,mapping.outputSize.height()-y));
            if(!mask.rasterize(mapping,alpha,rect,tile,errors[worker],cancel,&scratch)){
                failed.store(true,std::memory_order_relaxed);break;
            }
            const auto *source=tile.constData();
            for(int row=0;row<rect.height();++row)
                std::memcpy(destination+qsizetype(y+row)*mapping.outputSize.width()+x,
                    source+qsizetype(row)*rect.width(),size_t(rect.width())*sizeof(float));
            auto &part=perWorker[worker];++part.tiles;
            part.maximumScratchBytes=std::max(part.maximumScratchBytes,
                scratch.storageBytes()+tile.capacity()*qsizetype(sizeof(float)));
            part.mappingAccumulatedMs+=double(scratch.mappingNs)/1e6;
            part.samplingAccumulatedMs+=double(scratch.samplingNs)/1e6;
        }
    });
    for(int worker=0;worker<workers;++worker)futures[worker].waitForFinished();
    if(isCanceled(cancel)){error="Canceled";return false;}
    if(failed.load()){for(const auto &message:errors)if(!message.isEmpty()){error=message;break;}return false;}
    if(stats){
        stats->workers=workers;
        for(const auto &part:perWorker){stats->tiles+=part.tiles;
            stats->maximumScratchBytes=std::max(stats->maximumScratchBytes,part.maximumScratchBytes);
            stats->mappingAccumulatedMs+=part.mappingAccumulatedMs;stats->samplingAccumulatedMs+=part.samplingAccumulatedMs;}
    }
    output=std::move(result);return true;
}
}
