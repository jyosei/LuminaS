#include "defringe_operator.h"
#include <QThreadPool>
#include <QtConcurrentRun>
#include <algorithm>
#include <atomic>
#include <QMutexLocker>

QImage DefringeCache::get(const SourceFrame &source,const Recipe &prefix,const DefringeSettings &settings,
                         int edge,double scaleX,double scaleY,const std::function<QImage()> &build,QString &error){
    error.clear();QImage input;quint64 ticket;
    {
        QMutexLocker guard(&mutex);
        const bool same=!before.isNull()&&sourceKey==source.linear.cacheKey()&&cachedPrefix==prefix&&
            originalSize==source.originalSize&&balance==source.draftBalance&&camera==source.cameraToWork&&
            pixelX==scaleX&&pixelY==scaleY&&targetEdge==edge&&raw==source.raw;
        ticket=++generation;
        if(same){input=before;if(!after.isNull()&&processedSettings==settings)return after;}
    }
    if(input.isNull())input=build();if(input.isNull())return {};
    auto output=applyDefringe(input,settings,scaleX,scaleY,error);if(output.isNull())return {};
    if(std::max(input.width(),input.height())<=3840){
        QMutexLocker guard(&mutex);
        if(ticket==generation){sourceKey=source.linear.cacheKey();cachedPrefix=prefix;originalSize=source.originalSize;
            balance=source.draftBalance;camera=source.cameraToWork;pixelX=scaleX;pixelY=scaleY;targetEdge=edge;raw=source.raw;
            before=std::move(input);after=output;processedSettings=settings;}
    }
    return output;
}
void DefringeCache::clear(){QMutexLocker guard(&mutex);++generation;before={};after={};}

namespace {
double smooth(double low,double high,double value){const double t=std::clamp((value-low)/(high-low),0.,1.);return t*t*(3-2*t);}
bool finitePixel(const float *p){for(int c=0;c<4;++c)if(!std::isfinite(p[c]))return false;return p[3]>=0&&p[3]<=1;}
struct Descriptor {double y=0,saturation=0,hue=0;};
Descriptor describe(const float *p){
    Descriptor d;d.y=.2627*double(p[0])+.6780*double(p[1])+.0593*double(p[2]);
    const double high=std::max({double(p[0]),double(p[1]),double(p[2])}),low=std::min({double(p[0]),double(p[1]),double(p[2])}),delta=high-low;
    if(delta<=1e-7)return d;
    d.saturation=std::clamp(delta/std::max(std::abs(high),.18),0.,1.);
    const double h=high==p[0]?(p[1]-double(p[2]))/delta:high==p[1]?2+(p[2]-double(p[0]))/delta:4+(p[0]-double(p[1]))/delta;
    d.hue=std::fmod(h*60+360,360.);return d;
}
double selection(double hue,double centre,double width){
    return 1-smooth(width*.8,width,std::abs(std::remainder(hue-centre,360.)));
}
}
QImage applyDefringe(const QImage &before,const DefringeSettings &s,double scaleX,double scaleY,QString &error){
    error.clear();
    if(before.isNull()||before.format()!=QImage::Format_RGBA32FPx4||!validDefringe(s)||!std::isfinite(scaleX)||!std::isfinite(scaleY)||scaleX<=0||scaleY<=0){error="Invalid defringe input/settings/scale";return {};}
    if(!hasDefringe(s))return before;
    const int width=before.width(),height=before.height();
    const int rx=std::max(1,int(std::lround(std::min(double(width),s.radius/scaleX)))),ry=std::max(1,int(std::lround(std::min(double(height),s.radius/scaleY))));
    QImage result(before.size(),before.format());if(result.isNull()){error="Defringe allocation";return {};}
    const auto *input=before.constBits();auto *output=result.bits();const auto inStride=before.bytesPerLine(),outStride=result.bytesPerLine();
    const int dx[]{rx,0,rx,rx},dy[]{0,ry,ry,-ry};std::atomic<bool> invalid{false};
    auto rows=[&](int start,int end){for(int y=start;y<end&&!invalid.load();++y){auto *out=reinterpret_cast<float *>(output+y*outStride);
        for(int x=0;x<width;++x){const auto *p=reinterpret_cast<const float *>(input+y*inStride)+4*x;
            if(!finitePixel(p)){invalid=true;return;}for(int c=0;c<4;++c)out[4*x+c]=p[c];
            if(p[3]<=1e-8f)continue;
            const auto d=describe(p);if(d.y<=0||d.saturation<=.01)continue;
            const double amount=std::max(s.purpleAmount*selection(d.hue,s.purpleHue,s.purpleWidth),s.greenAmount*selection(d.hue,s.greenHue,s.greenWidth));
            if(amount==0)continue;double evidence=0;
            for(int direction=0;direction<4;++direction){const int ax=x-dx[direction],ay=y-dy[direction],bx=x+dx[direction],by=y+dy[direction];
                if(ax<0||ay<0||bx<0||by<0||ax>=width||ay>=height||bx>=width||by>=height)continue;
                const auto *a=reinterpret_cast<const float *>(input+ay*inStride)+4*ax,*b=reinterpret_cast<const float *>(input+by*inStride)+4*bx;
                if(!finitePixel(a)||!finitePixel(b)){invalid=true;return;}if(a[3]<=1e-8f||b[3]<=1e-8f)continue;
                const auto da=describe(a),db=describe(b);
                const double neutral=(1-smooth(.08,.30,da.saturation))*(1-smooth(.08,.30,db.saturation));
                const double contrast=std::abs(da.y-db.y)/std::max({.18,std::abs(da.y),std::abs(db.y)});
                evidence=std::max(evidence,neutral*smooth(.05,.25,contrast));
            }
            const double strength=amount*evidence;
            if(strength>0)for(int c=0;c<3;++c)out[4*x+c]=float(d.y+(double(p[c])-d.y)*(1-strength));
        }
    }};
    static QThreadPool pool;pool.setMaxThreadCount(4);QFuture<void> jobs[4];
    for(int i=0;i<4;++i)jobs[i]=QtConcurrent::run(&pool,[&,i]{rows(height*i/4,height*(i+1)/4);});
    for(auto &job:jobs)job.waitForFinished();if(invalid){error="Nonfinite/invalid-alpha defringe source";return {};}
    return result;
}
