#include "spatial_operator.h"
#include <QtConcurrentRun>
#include <QThreadPool>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <array>
#include <optional>
namespace {
QThreadPool &spatialPool(){static QThreadPool pool;pool.setMaxThreadCount(4);return pool;}
template<class F> void parallel(F fn){QFuture<void> jobs[4];for(int i=0;i<4;++i)jobs[i]=QtConcurrent::run(&spatialPool(),[fn,i]{fn(i);});for(auto &job:jobs)job.waitForFinished();}
void box(std::vector<float> &values,std::vector<float> &scratch,int width,int height,int radius,double fraction){
    if(radius<1&&fraction==0)return;const double inner=(1-fraction)/(2*radius+1)+fraction/(2*radius+3),outer=fraction/(2*radius+3);
    parallel([&](int part){for(int y=height*part/4;y<height*(part+1)/4;++y){const auto *row=values.data()+size_t(y)*width;auto *out=scratch.data()+size_t(y)*width;double sum=radius*double(row[0]);const int end=std::min(radius,width-1);for(int x=0;x<=end;++x)sum+=row[x];if(radius>end)sum+=(radius-end)*double(row[width-1]);for(int x=0;x<width;++x){out[x]=float(sum*inner+outer*(double(row[std::clamp(x-radius-1,0,width-1)])+row[std::clamp(x+radius+1,0,width-1)]));sum-=row[std::clamp(x-radius,0,width-1)];sum+=row[std::clamp(x+radius+1,0,width-1)];}}});
    parallel([&](int part){const int start=width*part/4,end=width*(part+1)/4;std::vector<double> sums(size_t(end-start));for(int x=start;x<end;++x)sums[size_t(x-start)]=radius*double(scratch[size_t(x)]);const int last=std::min(radius,height-1);for(int y=0;y<=last;++y)for(int x=start;x<end;++x)sums[size_t(x-start)]+=scratch[size_t(y)*width+x];if(radius>last)for(int x=start;x<end;++x)sums[size_t(x-start)]+=(radius-last)*double(scratch[size_t(height-1)*width+x]);
        for(int y=0;y<height;++y){const auto *remove=scratch.data()+size_t(std::clamp(y-radius,0,height-1))*width,*add=scratch.data()+size_t(std::clamp(y+radius+1,0,height-1))*width,*left=scratch.data()+size_t(std::clamp(y-radius-1,0,height-1))*width;auto *out=values.data()+size_t(y)*width;for(int x=start;x<end;++x){auto &sum=sums[size_t(x-start)];out[x]=float(sum*inner+outer*(double(left[x])+add[x]));sum+=double(add[x])-remove[x];}}});
}
std::vector<float> smooth(const std::vector<float> &input,const std::vector<float> &alpha,int w,int h,double sigma){
    sigma=std::min(sigma,double(std::max(w,h)-1));const int radius=std::max(0,int(std::floor((std::sqrt(1+4*sigma*sigma)-1)/2)));const double fraction=std::clamp((sigma*sigma-double(radius)*(radius+1))/(2*(radius+1)),0.,1.);
    auto values=input;std::vector<float> scratch(values.size());for(int pass=0;pass<3;++pass)box(values,scratch,w,h,radius,fraction);
    if(!alpha.empty()){auto weights=alpha;for(int pass=0;pass<3;++pass)box(weights,scratch,w,h,radius,fraction);parallel([&](int part){for(size_t i=values.size()*part/4;i<values.size()*(part+1)/4;++i)values[i]=weights[i]>1e-8f?values[i]/weights[i]:0;});}return values;
}
}
std::vector<float> spatialMean(std::vector<float> input,int w,int h,int radius){std::vector<float> scratch(input.size());box(input,scratch,w,h,radius,0);return input;}
std::vector<float> spatialGaussian(std::vector<float> values,int w,int h,double sigma,std::vector<float> &scratch){
    sigma=std::min(sigma,double(std::max(w,h)-1));const int radius=std::max(0,int(std::floor((std::sqrt(1+4*sigma*sigma)-1)/2)));const double fraction=std::clamp((sigma*sigma-double(radius)*(radius+1))/(2*(radius+1)),0.,1.);scratch.resize(values.size());for(int pass=0;pass<3;++pass)box(values,scratch,w,h,radius,fraction);return values;
}
std::vector<float> spatialGaussianMean(const std::vector<float> &input,const std::vector<float> &alpha,int w,int h,double sigma){return smooth(input,alpha,w,h,sigma);}
std::shared_ptr<SpatialBasis> prepareSpatial(QImage before,double scale,QSize original,QString &error,bool haze,bool detail,double sharpenRadius,bool noiseLuminance,bool noiseChroma,bool edgeMask){
    if(before.isNull()||before.format()!=QImage::Format_RGBA32FPx4||!std::isfinite(scale)||scale<=0){error="Spatial input/scale";return {};}
    auto result=std::make_shared<SpatialBasis>();result->before=std::move(before);const int w=result->before.width(),h=result->before.height();const size_t count=size_t(w)*h;
    result->sourceScale=scale;result->noiseLuminancePrepared=noiseLuminance;result->noiseChromaPrepared=noiseChroma;
    if((noiseLuminance||noiseChroma)&&qMax(w,h)<=3840){result->noise=prepareNoise(result->before,scale,noiseLuminance,noiseChroma,error,512);if(!result->noise)return {};}
    if(haze){result->haze=prepareHaze(result->before,scale,original,error);if(!result->haze)return {};}
    if(sharpenRadius!=0){
        if(!std::isfinite(sharpenRadius)||sharpenRadius<.5||sharpenRadius>4){error="Sharpen radius";return {};}
        std::vector<float> luminance(count),weights(count);std::atomic<bool> opaque{true},invalid{false};
        parallel([&](int part){for(int y=h*part/4;y<h*(part+1)/4;++y){const auto *p=reinterpret_cast<const float *>(result->before.constScanLine(y));for(int x=0;x<w;++x){const auto i=size_t(y)*w+x;for(int c=0;c<4;++c)if(!std::isfinite(p[x*4+c])){invalid=true;return;}const float a=std::clamp(p[x*4+3],0.f,1.f);weights[i]=a;if(a<1)opaque=false;luminance[i]=(.2627f*p[x*4]+.6780f*p[x*4+1]+.0593f*p[x*4+2])*a;}}});
        if(invalid){error="Sharpen NaN/Inf";return {};}if(opaque)std::vector<float>().swap(weights);auto blurred=smooth(luminance,weights,w,h,sharpenRadius/scale);result->sharpen.resize(count);result->sharpenRadius=sharpenRadius;
        parallel([&](int part){for(size_t i=count*part/4;i<count*(part+1)/4;++i){const float a=weights.empty()?1:weights[i];result->sharpen[i]=a>1e-8f?luminance[i]/a-blurred[i]:0;}});
        if(edgeMask){
            // An independent, alpha-normalized low-pass gradient in source pixels.
            // Release the USM blur before allocating the edge low-pass workspace.
            std::vector<float>().swap(blurred);auto low=smooth(luminance,weights,w,h,2/scale);result->sharpenEdges.resize(count);
            parallel([&](int part){for(int y=h*part/4;y<h*(part+1)/4;++y)for(int x=0;x<w;++x){const auto i=size_t(y)*w+x;const double center=low[i];auto sample=[&](int px,int py){const auto k=size_t(std::clamp(py,0,h-1))*w+std::clamp(px,0,w-1);return !weights.empty()&&weights[k]<=1e-8f?center:double(low[k]);};const double gradient=std::hypot(sample(x+1,y)-sample(x-1,y),sample(x,y+1)-sample(x,y-1))/(2*scale);if(!std::isfinite(center)||!std::isfinite(gradient)){invalid=true;continue;}result->sharpenEdges[i]=weights.empty()||weights[i]>1e-8f?std::uint16_t(std::lround(65535*std::clamp(gradient/(.08*(std::abs(center)+.18)),0.,1.))):0;}});
        }
        if(invalid){error="Sharpen edge NaN/Inf";return {};}
    }
    if(!detail)return result;
    std::vector<float> logY(count),alpha(count);std::atomic<bool> opaque{true},invalid{false};
    parallel([&](int part){for(int y=h*part/4;y<h*(part+1)/4;++y){const auto *p=reinterpret_cast<const float *>(result->before.constScanLine(y));for(int x=0;x<w;++x){const auto i=size_t(y)*w+x;const float luminance=.2627f*p[x*4]+.6780f*p[x*4+1]+.0593f*p[x*4+2],a=p[x*4+3];if(!std::isfinite(luminance)||!std::isfinite(a)){invalid=true;return;}alpha[i]=std::clamp(a,0.f,1.f);if(alpha[i]<1)opaque=false;logY[i]=std::log2(std::max(0.f,luminance)+.018f)*alpha[i];}}});
    if(invalid){error="Spatial NaN/Inf";return {};}if(opaque)std::vector<float>().swap(alpha);
    const double edge=original.isValid()?qMax(original.width(),original.height()):qMax(w,h);const double smallSigma=1.5*edge/3840/scale,largeSigma=20*edge/3840/scale;
    auto small=smooth(logY,alpha,w,h,smallSigma),large=smooth(logY,alpha,w,h,largeSigma);result->texture.resize(count);result->clarity.resize(count);
    parallel([&](int part){for(int y=h*part/4;y<h*(part+1)/4;++y){const auto *p=reinterpret_cast<const float *>(result->before.constScanLine(y));for(int x=0;x<w;++x){const auto i=size_t(y)*w+x;const float a=p[x*4+3];if(a<=1e-8f){result->texture[i]=result->clarity[i]=0;continue;}const float originalLog=alpha.empty()?logY[i]:logY[i]/a;const float luminance=std::max(0.f,.2627f*p[x*4]+.6780f*p[x*4+1]+.0593f*p[x*4+2]),t=luminance/(luminance+.18f);
                result->texture[i]=std::clamp(.7f*(originalLog-small[i]),-2.f,2.f);result->clarity[i]=std::clamp(1.1f*(small[i]-large[i])*4*t*(1-t),-2.f,2.f);}}});return result;
}
float sharpenMaskWeight(std::uint16_t edge,double amount){
    const float strength=float(amount),t=std::clamp((float(edge)/65535-.8f*strength)/.2f,0.f,1.f);
    return 1-strength+strength*t*t*(3-2*t);
}
QImage renderSharpenMask(const SpatialBasis &basis,double strength,QString &error){
    error.clear();const auto size=basis.before.size();
    if(basis.before.isNull()||basis.before.format()!=QImage::Format_RGBA32FPx4||!std::isfinite(strength)||strength<0||strength>1||
       (strength!=0&&basis.sharpenEdges.size()!=size_t(size.width())*size.height())){error="Invalid sharpen mask basis/strength";return {};}
    QImage result(size,QImage::Format_RGBA32FPx4);if(result.isNull()){error="Sharpen mask allocation";return {};}
    auto *bytes=result.bits();const auto stride=result.bytesPerLine();std::atomic<bool> invalid{false};
    parallel([&](int part){for(int y=size.height()*part/4;y<size.height()*(part+1)/4;++y){
        const auto *input=reinterpret_cast<const float *>(basis.before.constScanLine(y));auto *out=reinterpret_cast<float *>(bytes+y*stride);
        for(int x=0;x<size.width();++x){const float alpha=input[4*x+3];
            if(!std::isfinite(alpha)||alpha<0||alpha>1){invalid=true;return;}
            const float weight=alpha<=1e-8f?0:strength==0?1:sharpenMaskWeight(basis.sharpenEdges[size_t(y)*size.width()+x],strength);
            out[4*x]=out[4*x+1]=out[4*x+2]=weight;out[4*x+3]=alpha;}
    }});if(invalid){error="Invalid sharpen mask alpha";return {};}return result;
}
static QImage applySpatialImage(QImage result,const SpatialBasis &basis,const Recipe &r,QString &error){
    if((r.noiseLuminance!=0||r.noiseChroma!=0)&&!basis.noise){if(!applyNoiseInPlace(result,basis.sourceScale,r,error))return {};}
    auto *bytes=result.bits();const auto stride=result.bytesPerLine();const int w=result.width(),h=result.height();std::array<float,4097> gains{};for(int i=0;i<=4096;++i)gains[size_t(i)]=float(std::exp2(i/512.-4));std::atomic<bool> invalid{false};
    if(r.dehaze!=0&&!basis.haze){error="Missing dehaze basis";return {};}
    if(r.sharpenAmount!=0&&(basis.sharpen.size()!=size_t(w)*h||basis.sharpenRadius!=r.sharpenRadius)){error="Missing sharpen basis";return {};}
    if(r.sharpenAmount!=0&&r.sharpenMask!=0&&basis.sharpenEdges.size()!=size_t(w)*h){error="Missing sharpen edge basis";return {};}
    if(basis.noise&&((r.noiseLuminance!=0&&basis.noise->luminance.size()!=size_t(w)*h)||(r.noiseChroma!=0&&basis.noise->redChroma.size()!=size_t(w)*h))){error="Missing denoise basis";return {};}
    std::optional<HazeSampler> haze;if(r.dehaze!=0)haze.emplace(*basis.haze,QSize(w,h),r.dehaze);
    parallel([&](int part){for(int y=h*part/4;y<h*(part+1)/4;++y){auto *p=reinterpret_cast<float *>(bytes+y*stride);for(int x=0;x<w;++x){if(p[x*4+3]<=1e-8f){p[x*4]=p[x*4+1]=p[x*4+2]=0;continue;}if(basis.noise&&(r.noiseLuminance!=0||r.noiseChroma!=0)){const auto k=size_t(y)*w+x;const float dy=r.noiseLuminance!=0?float(r.noiseLuminance)*basis.noise->luminance[k]:0,dr=r.noiseChroma!=0?float(r.noiseChroma)*basis.noise->redChroma[k]:0,db=r.noiseChroma!=0?float(r.noiseChroma)*basis.noise->blueChroma[k]:0;p[x*4]+=dy+dr;p[x*4+1]+=dy-(.2627f*dr+.0593f*db)/.6780f;p[x*4+2]+=dy+db;}if(haze)haze->apply(x,y,p+x*4);float gain=1;if(r.texture!=0||r.clarity!=0){const auto k=size_t(y)*w+x;const float index=std::clamp(float(r.texture)*basis.texture[k]+float(r.clarity)*basis.clarity[k]+4,0.f,8.f)*512;const auto i=size_t(index),j=std::min(i+1,size_t(4096));gain=gains[i]+(gains[j]-gains[i])*(index-float(i));}float sharpen=0;if(r.sharpenAmount!=0){const float d=basis.sharpen[size_t(y)*w+x];sharpen=float(r.sharpenAmount)*std::copysign(std::max(0.f,std::abs(d)-float(r.sharpenThreshold)),d);if(r.sharpenMask!=0){sharpen*=sharpenMaskWeight(basis.sharpenEdges[size_t(y)*w+x],r.sharpenMask);}}for(int c=0;c<3;++c){p[x*4+c]=p[x*4+c]*gain+sharpen;if(!std::isfinite(p[x*4+c]))invalid=true;}}}});if(invalid){error="Spatial NaN/Inf result";return {};}return result;
}
QImage applySpatial(const SpatialBasis &basis,const Recipe &r,QString &error){return applySpatialImage(basis.before,basis,r,error);}
QImage applySpatialOwned(std::shared_ptr<SpatialBasis> basis,const Recipe &r,QString &error){
    if(basis.use_count()==1){auto before=std::move(basis->before);return applySpatialImage(std::move(before),*basis,r,error);}return applySpatial(*basis,r,error);
}
std::shared_ptr<const SpatialBasis> SpatialCache::get(const SourceFrame &source,const Recipe &recipe,int longEdge,double scale,const std::function<QImage()> &build,QString &error,bool haze,bool detail,double sharpenRadius,bool noiseLuminance,bool noiseChroma,bool edgeMask){
    QMutexLocker guard(&mutex);Recipe neutral=recipe;neutral.texture=neutral.clarity=neutral.dehaze=neutral.sharpenAmount=neutral.sharpenThreshold=neutral.noiseLuminance=neutral.noiseChroma=neutral.sharpenMask=0;neutral.sharpenRadius=1;
    const bool sameGeometry=basis&&imageKey==source.linear.cacheKey()&&prefix==neutral&&balance==source.draftBalance&&sourceScale==scale&&targetEdge==longEdge&&originalSize==source.originalSize&&raw==source.raw;
    if(sameGeometry&&(!haze||basis->haze)&&(!detail||!basis->texture.empty())&&(sharpenRadius==0||(!basis->sharpen.empty()&&basis->sharpenRadius==sharpenRadius))&&(!edgeMask||!basis->sharpenEdges.empty())&&(!noiseLuminance||basis->noiseLuminancePrepared)&&(!noiseChroma||basis->noiseChromaPrepared))return basis;
    auto prepared=sameGeometry?basis->before:build();basis.reset();if(prepared.isNull())return {};auto next=prepareSpatial(std::move(prepared),scale,source.originalSize.isValid()?source.originalSize:source.linear.size(),error,haze,detail,sharpenRadius,noiseLuminance,noiseChroma,edgeMask);if(!next)return {};
    imageKey=source.linear.cacheKey();prefix=neutral;balance=source.draftBalance;sourceScale=scale;targetEdge=longEdge;originalSize=source.originalSize;raw=source.raw;basis=next;return next;
}
void SpatialCache::clearSpatial(){QMutexLocker guard(&mutex);basis.reset();}
void SpatialCache::clear(){clearSpatial();defringe.clear();}

