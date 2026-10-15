#include "denoise_operator.h"
#include "spatial_operator.h"
#include <QtConcurrentRun>
#include <QThreadPool>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <functional>
namespace {
QThreadPool &noisePool(){static QThreadPool pool;pool.setMaxThreadCount(4);return pool;}
template<class F> void parallel(size_t n,F fn){QFuture<void> jobs[4];for(int i=0;i<4;++i)jobs[i]=QtConcurrent::run(&noisePool(),[&,i]{for(size_t k=n*i/4;k<n*(i+1)/4;++k)fn(k);});for(auto &job:jobs)job.waitForFinished();}
NoiseBasis estimate(const QImage &image,double scale,bool luminance,bool chroma,QString &error){
    const int w=image.width(),h=image.height();const size_t n=size_t(w)*h;std::vector<float> alpha(n);std::atomic<bool> opaque{true},invalid{false};
    NoiseBasis result;if(luminance)result.luminance.resize(n);if(chroma){result.redChroma.resize(n);result.blueChroma.resize(n);}
    parallel(size_t(h),[&](size_t row){const auto *p=reinterpret_cast<const float *>(image.constScanLine(int(row)));for(int x=0;x<w;++x){const auto i=row*w+x;for(int c=0;c<4;++c)if(!std::isfinite(p[x*4+c]))invalid=true;const float y=.2627f*p[x*4]+.6780f*p[x*4+1]+.0593f*p[x*4+2];alpha[i]=std::clamp(p[x*4+3],0.f,1.f);if(alpha[i]<1)opaque=false;if(luminance)result.luminance[i]=y;if(chroma){result.redChroma[i]=p[x*4]-y;result.blueChroma[i]=p[x*4+2]-y;}}});
    if(invalid){error="Denoise NaN/Inf input";return {};}if(opaque)std::vector<float>().swap(alpha);
    std::vector<float> scratch;auto weights=[&](double sigma){return alpha.empty()?std::vector<float>{}:spatialGaussian(alpha,w,h,sigma,scratch);};
    const auto luminanceWeights=luminance?weights(2/scale):std::vector<float>{},chromaWeights=chroma?weights(4/scale):std::vector<float>{};
    auto channel=[&](std::vector<float> &input,double sigma,float epsilon,const std::vector<float> &denominator){
        auto mean=[&](std::vector<float> values){if(!alpha.empty())parallel(n,[&](size_t i){values[i]*=alpha[i];});values=spatialGaussian(std::move(values),w,h,sigma,scratch);if(!alpha.empty())parallel(n,[&](size_t i){values[i]=denominator[i]>1e-8f?values[i]/denominator[i]:0;});return values;};
        auto average=mean(input);std::vector<float> a(n),b(n);parallel(n,[&](size_t i){a[i]=input[i]*input[i];});a=mean(std::move(a));
        parallel(n,[&](size_t i){const float variance=std::max(0.f,a[i]-average[i]*average[i]);a[i]=variance/(variance+epsilon);b[i]=average[i]*(1-a[i]);});std::vector<float>().swap(average);a=mean(std::move(a));b=mean(std::move(b));
        parallel(n,[&](size_t i){const float delta=a[i]*input[i]+b[i]-input[i];input[i]=alpha.empty()||alpha[i]>1e-8f?delta:0;if(!std::isfinite(input[i]))invalid=true;});
    };
    if(luminance)channel(result.luminance,2/scale,.0016f,luminanceWeights);
    if(chroma){channel(result.redChroma,4/scale,.0025f,chromaWeights);channel(result.blueChroma,4/scale,.0025f,chromaWeights);}
    if(invalid){error="Denoise NaN/Inf result";return {};}return result;
}
bool process(const QImage &image,double scale,bool luminance,bool chroma,int stripRows,bool inplace,const std::function<void(int,int,const NoiseBasis &,int)> &consume,QString &error){
    if(image.isNull()||image.format()!=QImage::Format_RGBA32FPx4||!std::isfinite(scale)||scale<=0||stripRows<=0){error="Denoise input/scale";return false;}
    const int w=image.width(),h=image.height();const double sigma=std::min((chroma?4.:2.)/scale,double(std::max(w,h)-1));const double r=std::floor((std::sqrt(1+4*sigma*sigma)-1)/2);const int halo=int(std::min(double(h-1),6*(r+1)));
    QImage backup;int backupTop=0;
    for(int start=0;start<h;start+=stripRows){const int end=std::min(h,start+stripRows),top=std::max(0,start-halo),bottom=std::min(h,end+halo);QImage tile(w,bottom-top,QImage::Format_RGBA32FPx4);if(tile.isNull()){error="Denoise tile allocation";return false;}
        for(int y=top;y<bottom;++y){const auto *row=inplace&&y<start?backup.constScanLine(y-backupTop):image.constScanLine(y);std::memcpy(tile.scanLine(y-top),row,size_t(w)*16);}
        auto deltas=estimate(tile,scale,luminance,chroma,error);if(!error.isEmpty())return false;
        if(inplace){backupTop=std::max(0,end-halo);backup=tile.copy(0,backupTop-top,w,end-backupTop);if(backup.isNull()&&end<h&&halo>0){error="Denoise halo allocation";return false;}}
        consume(start,end,deltas,start-top);
    }return true;
}
}
std::shared_ptr<const NoiseBasis> prepareNoise(const QImage &image,double scale,bool luminance,bool chroma,QString &error,int stripRows){
    if(image.isNull()||image.format()!=QImage::Format_RGBA32FPx4||!std::isfinite(scale)||scale<=0||stripRows<=0){error="Denoise input/scale";return {};}
    error.clear();auto result=std::make_shared<NoiseBasis>();const size_t count=size_t(image.width())*image.height();if(luminance)result->luminance.resize(count);if(chroma){result->redChroma.resize(count);result->blueChroma.resize(count);}
    if(!process(image,scale,luminance,chroma,stripRows,false,[&](int start,int end,const NoiseBasis &tile,int offset){const auto from=size_t(offset)*image.width(),to=size_t(start)*image.width(),n=size_t(end-start)*image.width();if(luminance)std::copy_n(tile.luminance.data()+from,n,result->luminance.data()+to);if(chroma){std::copy_n(tile.redChroma.data()+from,n,result->redChroma.data()+to);std::copy_n(tile.blueChroma.data()+from,n,result->blueChroma.data()+to);}},error))return {};return result;
}
bool applyNoiseInPlace(QImage &image,double scale,const Recipe &r,QString &error,int stripRows){
    if(!validRecipe(r)){error="Denoise invalid recipe";return false;}if(r.noiseLuminance==0&&r.noiseChroma==0){error.clear();return true;}
    error.clear();image.bits();return process(image,scale,r.noiseLuminance!=0,r.noiseChroma!=0,stripRows,true,[&](int start,int end,const NoiseBasis &tile,int offset){for(int y=start;y<end;++y){auto *p=reinterpret_cast<float *>(image.scanLine(y));for(int x=0;x<image.width();++x){const auto k=size_t(y-start+offset)*image.width()+x;if(p[x*4+3]<=1e-8f){p[x*4]=p[x*4+1]=p[x*4+2]=0;continue;}const float dy=r.noiseLuminance!=0?float(r.noiseLuminance)*tile.luminance[k]:0,dr=r.noiseChroma!=0?float(r.noiseChroma)*tile.redChroma[k]:0,db=r.noiseChroma!=0?float(r.noiseChroma)*tile.blueChroma[k]:0;p[x*4]+=dy+dr;p[x*4+1]+=dy-(.2627f*dr+.0593f*db)/.6780f;p[x*4+2]+=dy+db;}}},error);
}
