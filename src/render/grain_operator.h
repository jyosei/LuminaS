#pragma once
#include "core/document.h"
#include <QMutex>
#include <QMutexLocker>
#include <QThreadPool>
#include <QtConcurrentRun>
#include <vector>
#include <cmath>
#include <algorithm>

// Output-anchored artistic value noise, not a physical film/sensor model.
// Exact box integration of separable smoothstep interpolation suppresses aliasing.
class ProceduralGrain {
    struct Axis {int first=0;std::vector<double> weight;};
    struct PlaneSet {QSize size;double sx=1,sy=1,cell=3;QImage coarse,fine;};
    static uint32_t hash(int x,int y,uint32_t seed){uint32_t v=uint32_t(x)*0x9e3779b9u^uint32_t(y)*0x85ebca6bu^seed;v^=v>>16;v*=0x7feb352du;v^=v>>15;v*=0x846ca68bu;return v^(v>>16);}
    static double random(int x,int y,uint32_t seed){return double(hash(x,y,seed))/4294967295.*2-1;}
    static double primitive(double t){return t*t*t-.5*t*t*t*t;}
    static std::vector<Axis> axis(int count,double step,double cell){
        std::vector<Axis> result(size_t(count),Axis{});
        for(int i=0;i<count;++i){const double lo=i*step/cell,hi=(i+1.)*step/cell;const int first=int(std::floor(lo)),last=int(std::ceil(hi))-1;auto &a=result[size_t(i)];a.first=first;a.weight.resize(size_t(last-first+2),0);
            for(int k=first;k<=last;++k){const double left=std::max(lo,double(k))-k,right=std::min(hi,double(k+1))-k,blend=primitive(right)-primitive(left);a.weight[size_t(k-first)]+=(right-left-blend)/(hi-lo);a.weight[size_t(k-first+1)]+=blend/(hi-lo);}}
        return result;
    }
    static double noise(const Axis &x,const Axis &y,uint32_t seed){double n=0;for(size_t j=0;j<y.weight.size();++j)for(size_t i=0;i<x.weight.size();++i)n+=x.weight[i]*y.weight[j]*random(x.first+int(i),y.first+int(j),seed);return n;}
public:
    ProceduralGrain(const Recipe &r,QSize size,double sx,double sy):amount(float(r.grainAmount)),roughness(float(r.grainRoughness)),frameSize(size){
        if(amount==0)return;
        good=size.isValid()&&std::isfinite(sx)&&std::isfinite(sy)&&sx>=1e-6&&sy>=1e-6&&sx<=128&&sy<=128&&double(size.width())*size.height()*sx*sy<=50000000&&size.width()*sx/r.grainSize<1e9&&size.height()*sy/r.grainSize<1e9;
        if(!good)return;
        static QMutex mutex;static std::shared_ptr<const PlaneSet> cached;
        const bool cacheable=size.width()<=3840&&size.height()<=3840;
        if(cacheable){QMutexLocker lock(&mutex);if(cached&&cached->size==size&&cached->sx==sx&&cached->sy==sy&&cached->cell==r.grainSize){planes=cached;return;}}
        x=axis(size.width(),sx,r.grainSize);y=axis(size.height(),sy,r.grainSize);fx=axis(size.width(),sx,r.grainSize*.5);fy=axis(size.height(),sy,r.grainSize*.5);
        if(!cacheable)return;
        auto built=std::make_shared<PlaneSet>();built->size=size;built->sx=sx;built->sy=sy;built->cell=r.grainSize;built->coarse=QImage(size,QImage::Format_Grayscale16);built->fine=QImage(size,QImage::Format_Grayscale16);
        if(built->coarse.isNull()||built->fine.isNull()){good=false;return;}
        auto *c=built->coarse.bits(),*f=built->fine.bits();const auto cs=built->coarse.bytesPerLine(),fs=built->fine.bytesPerLine();static QThreadPool pool;[[maybe_unused]] static const bool configured=[](){pool.setMaxThreadCount(4);return true;}();QFuture<void> jobs[4];
        for(int part=0;part<4;++part)jobs[part]=QtConcurrent::run(&pool,[&,part]{for(int row=size.height()*part/4;row<size.height()*(part+1)/4;++row){auto *coarse=reinterpret_cast<quint16 *>(c+row*cs),*fine=reinterpret_cast<quint16 *>(f+row*fs);for(int col=0;col<size.width();++col){coarse[col]=quint16(std::lround((std::clamp(noise(x[size_t(col)],y[size_t(row)],0x4c534731u),-1.,1.)+1)*32767.5));fine[col]=quint16(std::lround((std::clamp(noise(fx[size_t(col)],fy[size_t(row)],0x4c534732u),-1.,1.)+1)*32767.5));}}});
        // Join before captured buffers/axes leave scope; publish only finished planes.
        for(auto &job:jobs)job.waitForFinished();planes=built;{QMutexLocker lock(&mutex);cached=built;}x.clear();y.clear();fx.clear();fy.clear();
    }
    bool valid() const{return good;}
    bool enabled() const{return amount!=0;}
    float gain(int col,int row) const{
        Q_ASSERT(col>=0&&row>=0&&col<frameSize.width()&&row<frameSize.height());
        if(!good||!enabled())return 1;double a,b;if(planes){a=reinterpret_cast<const quint16 *>(planes->coarse.constScanLine(row))[col]/32767.5-1;b=reinterpret_cast<const quint16 *>(planes->fine.constScanLine(row))[col]/32767.5-1;}else{a=noise(x[size_t(col)],y[size_t(row)],0x4c534731u);b=noise(fx[size_t(col)],fy[size_t(row)],0x4c534732u);}const double n=(a+.5*roughness*b)/(1+.5*roughness);return float(1+.35*amount*n);
    }
    void apply(float *rgb,int col,int row) const{if(!enabled())return;const float g=gain(col,row);for(int c=0;c<3;++c)rgb[c]*=g;}
private:
    float amount=0,roughness=.5f;bool good=true;
    QSize frameSize;
    std::shared_ptr<const PlaneSet> planes;
    std::vector<Axis> x,y,fx,fy;
};
