#include "cpu_renderer.h"
#include "color/color_pipeline.h"
#include "io/preview_size.h"
#include <QElapsedTimer>
#include <cmath>
#include <algorithm>
#include <QThreadPool>
#include <QThread>
#include <QtConcurrentRun>
#include <atomic>
#include "tone_operator.h"
#include "spatial_operator.h"
#include "vignette_operator.h"
#include "grain_operator.h"
#include "geometry_transform.h"
#include "lens_shading_operator.h"
#include "chromatic_aberration_operator.h"
#include "defringe_operator.h"
namespace {
Recipe defringePrefix(const Recipe &r){
    auto prefix=preparedGeometry(r);prefix.flipHorizontal=r.flipHorizontal;prefix.flipVertical=r.flipVertical;
    prefix.warmth=r.warmth;prefix.tint=r.tint;return prefix;
}
Recipe afterDefringe(const Recipe &r){
    auto after=r;clearPreparedGeometry(after);after.flipHorizontal=after.flipVertical=false;
    after.warmth=after.tint=0;after.defringe={};return after;
}
SourceFrame defringedSource(const SourceFrame &source,const Recipe &r,QImage image){
    auto adjusted=source;
    adjusted.sourcePixelsPerPixelX=source.pixelScaleX()*source.linear.width()*r.crop.width()/image.width();
    adjusted.sourcePixelsPerPixelY=source.pixelScaleY()*source.linear.height()*r.crop.height()/image.height();
    adjusted.sourcePixelsPerPixel=std::max(adjusted.pixelScaleX(),adjusted.pixelScaleY());
    adjusted.linear=std::move(image);adjusted.draftBalance={1,0,0,0,1,0,0,0,1};return adjusted;
}
QImage sharpenMaskFrame(const SourceFrame &source,const Recipe &recipe,int longEdge,SpatialCache *cache,QString &error,const CancellationToken &cancel){
    auto prefix=preparedGeometry(recipe);prefix.flipHorizontal=recipe.flipHorizontal;prefix.flipVertical=recipe.flipVertical;
    auto neutral=source;neutral.draftBalance={1,0,0,0,1,0,0,0,1};
    const auto full=renderedSize(source.linear.size(),recipe),target=longEdge==0?full:previewSize(full,longEdge);
    const double scale=source.sourcePixelsPerPixel*std::max(source.linear.width()*recipe.crop.width()/target.width(),source.linear.height()*recipe.crop.height()/target.height());
    const double radius=recipe.sharpenMask==0?0:recipe.sharpenRadius;
    auto build=[&]{return renderLinear(neutral,prefix,longEdge,error,{},cancel);};
    std::shared_ptr<const SpatialBasis> basis;
    if(cache)basis=cache->get(neutral,prefix,longEdge,scale,build,error,false,false,radius,false,false,recipe.sharpenMask!=0);
    else{auto before=build();if(!before.isNull())basis=prepareSpatial(std::move(before),scale,source.originalSize,error,false,false,radius,false,false,recipe.sharpenMask!=0);}
    if(isCanceled(cancel)){error="Canceled";return {};}
    auto result=basis?renderSharpenMask(*basis,recipe.sharpenMask,error):QImage{};
    if(isCanceled(cancel)){error="Canceled";return {};}return result;
}
}
QSize renderedSize(QSize size,const Recipe &r) {
    return {std::max(1,int(std::lround(size.width()*r.crop.width()))),std::max(1,int(std::lround(size.height()*r.crop.height())))};
}
QPointF outputToSource(QPointF uv,QSize size,const Recipe &r) {
    return OutputSourceMapping(size,r).map(uv);
}
QImage renderLinear(const SourceFrame &source,const Recipe &r,int longEdge,QString &error,const RenderView &view,const CancellationToken &cancel) {
    error.clear();if(isCanceled(cancel)){error="Canceled";return {};}if((view.sharpenMask&&view.pointRange>=0)||view.pointRange< -1||view.pointRange>=r.pointColors.size()||!validRecipe(r)||source.linear.isNull()||source.linear.format()!=QImage::Format_RGBA32FPx4){error="Invalid frame/recipe";return {};}
    const auto full=renderedSize(source.linear.size(),r);const auto target=longEdge==0?full:previewSize(full,longEdge);
    if(hasDefringe(r.defringe)){
        auto before=renderLinear(source,defringePrefix(r),longEdge,error,{},cancel);if(before.isNull())return {};
        auto adjusted=defringedSource(source,r,std::move(before));
        adjusted.linear=applyDefringe(adjusted.linear,r.defringe,adjusted.pixelScaleX(),adjusted.pixelScaleY(),error);
        if(adjusted.linear.isNull())return {};
        return renderLinear(adjusted,afterDefringe(r),0,error,view,cancel);
    }
    if(view.sharpenMask)return sharpenMaskFrame(source,r,longEdge,nullptr,error,cancel);
    if(r.texture!=0||r.clarity!=0||r.dehaze!=0||r.sharpenAmount!=0||r.noiseLuminance!=0||r.noiseChroma!=0){SourceFrame neutral=source;neutral.draftBalance={1,0,0,0,1,0,0,0,1};Recipe geometry=preparedGeometry(r);geometry.flipHorizontal=r.flipHorizontal;geometry.flipVertical=r.flipVertical;auto before=renderLinear(neutral,geometry,longEdge,error,{},cancel);if(before.isNull())return {};const double scale=source.sourcePixelsPerPixel*std::max(source.linear.width()*r.crop.width()/target.width(),source.linear.height()*r.crop.height()/target.height());auto basis=prepareSpatial(std::move(before),scale,source.originalSize.isValid()?source.originalSize:source.linear.size(),error,r.dehaze!=0,r.texture!=0||r.clarity!=0,r.sharpenAmount!=0?r.sharpenRadius:0,r.noiseLuminance!=0,r.noiseChroma!=0,r.sharpenAmount!=0&&r.sharpenMask!=0);if(!basis)return {};auto adjusted=source;adjusted.linear=applySpatialOwned(std::move(basis),r,error);if(adjusted.linear.isNull())return {};adjusted.sourcePixelsPerPixel=scale;adjusted.sourcePixelsPerPixelX=source.pixelScaleX()*source.linear.width()*r.crop.width()/target.width();adjusted.sourcePixelsPerPixelY=source.pixelScaleY()*source.linear.height()*r.crop.height()/target.height();Recipe after=r;after.perspective={0,0};after.distortion=0;after.chromaticAberration={0,0};after.lensShading=0;after.crop={0,0,1,1};after.angle=after.texture=after.clarity=after.dehaze=after.sharpenAmount=after.noiseLuminance=after.noiseChroma=0;after.flipHorizontal=after.flipVertical=false;return renderLinear(adjusted,after,0,error,view,cancel);}
    QImage result(target,QImage::Format_RGBA32FPx4);if(result.isNull()){error="Render allocation";return {};}
    const float exposure=float(std::exp2(r.exposure)),contrast=float(std::exp2(r.contrast));
    const ToneOperator tone(r,view.pointRange);const PostCropVignette vignette(r,target);const ProceduralGrain grain(r,target,source.pixelScaleX()*source.linear.width()*r.crop.width()/target.width(),source.pixelScaleY()*source.linear.height()*r.crop.height()/target.height());if(!grain.valid()){error="Invalid grain scale/allocation";return {};}
    const float red=source.raw?1:float(std::exp2(r.warmth)),blue=source.raw?1:float(std::exp2(-r.warmth)),green=source.raw?1:float(std::exp2(r.tint));
    const auto size=source.linear.size();const LensShading lens(size,r.lensShading);const RadialDistortion distortion(size,r.distortion);const ChromaticSampler chromatic(source,r);if(!chromatic.valid()){error="CA channel matrix/scale invalid";return {};}
    const double radians=r.angle*3.14159265358979323846/180,co=std::cos(radians),si=std::sin(radians);
    const double cropW=r.crop.width()*size.width(),cropH=r.crop.height()*size.height();
    const double cx=(r.crop.x()+r.crop.width()/2)*size.width()-.5,cy=(r.crop.y()+r.crop.height()/2)*size.height()-.5;
    const double direction=r.flipHorizontal?-1:1;const double stepX=direction*cropW*co/target.width(),stepY=-direction*cropW*si/target.width();
    const bool unrotated=std::abs(r.angle)<1e-12&&!hasGeometricWarp(r)&&!chromatic.enabled();
    const bool direct=target==size&&r.crop==QRectF(0,0,1,1)&&unrotated;
    if(direct){
        static QThreadPool pool;pool.setMaxThreadCount(4);std::atomic<bool> invalid{false};QFuture<void> jobs[4];
        auto *destination=result.bits();const auto *input=source.linear.constBits();const auto inStride=source.linear.bytesPerLine(),outStride=result.bytesPerLine();
        for(int part=0;part<4;++part)jobs[part]=QtConcurrent::run(&pool,[&,part]{
            for(int y=size.height()*part/4;y<size.height()*(part+1)/4;++y){if(isCanceled(cancel))return;auto *out=reinterpret_cast<float *>(destination+y*outStride);const auto *in=reinterpret_cast<const float *>(input+(r.flipVertical?size.height()-1-y:y)*inStride);
                for(int x=0;x<size.width();++x){const auto *p=in+(r.flipHorizontal?size.width()-1-x:x)*4;for(int c=0;c<4;++c)if(!std::isfinite(p[c])){invalid=true;return;}
                    const float alpha=std::clamp(p[3],0.f,1.f);float v[3]{};
                    if(alpha>1e-8f){for(int c=0;c<3;++c)v[c]=source.draftBalance[c*3]*p[0]+source.draftBalance[c*3+1]*p[1]+source.draftBalance[c*3+2]*p[2];
                        if(lens.enabled()){const float gain=lens.gain(r.flipHorizontal?size.width()-1-x:x,r.flipVertical?size.height()-1-y:y);for(int c=0;c<3;++c)v[c]*=gain;}
                        v[0]*=red;v[1]*=green;v[2]*=blue;for(int c=0;c<3;++c)v[c]=(v[c]*exposure-.18f)*contrast+.18f;
                        tone.apply(v);vignette.apply(v,x,y);grain.apply(v,x,y);
                    }
                    for(int c=0;c<3;++c)out[x*4+c]=v[c];out[x*4+3]=alpha;
                    for(int c=0;c<3;++c)if(!std::isfinite(v[c])){invalid=true;return;}
                }
            }
        });
        for(auto &job:jobs)job.waitForFinished();if(isCanceled(cancel)){error="Canceled";return {};}if(invalid){error="NaN/Inf source";return {};}return result;
    }
    auto *destination=result.bits();const auto stride=result.bytesPerLine();std::atomic<int> invalid{0};
    auto rows=[&](int start,int end){
    for(int y=start;y<end;++y){if(invalid.load()||isCanceled(cancel))return;auto *row=reinterpret_cast<float *>(destination+y*stride);
        const double u=r.flipHorizontal?1-.5/target.width():.5/target.width(),v=r.flipVertical?1-(y+.5)/target.height():(y+.5)/target.height();const double dx=(u-.5)*cropW,dy=(v-.5)*cropH;
        const double firstX=cx+dx*co+dy*si,firstY=cy-dx*si+dy*co;
        for(int x=0;x<target.width();++x){
            double px=firstX+x*stepX,py=firstY+x*stepY;
            if(hasGeometricWarp(r)){const auto mapped=distortion.source(perspectiveSource({px+.5,py+.5},size,r));px=mapped.x()-.5;py=mapped.y()-.5;if(!std::isfinite(px)||!std::isfinite(py)||px< -1||py< -1||px>size.width()||py>size.height()){for(int c=0;c<4;++c)row[x*4+c]=0;continue;}}
            const int x0=int(std::floor(px)),y0=int(std::floor(py));const float fx=float(px-x0),fy=float(py-y0);float values[4]{};
            if(chromatic.enabled()){if(!chromatic.sample({px+.5,py+.5},values)){invalid=1;return;}}else if(direct){const auto *p=reinterpret_cast<const float *>(source.linear.constScanLine(y))+x*4;
                for(int c=0;c<4;++c){if(!std::isfinite(p[c])){invalid=1;return;}values[c]=p[c]*((c<3)?p[3]:1);}
            }else for(int j=0;j<2;++j)for(int i=0;i<2;++i){int sx=x0+i,sy=y0+j;
                if(unrotated){sx=std::clamp(sx,0,size.width()-1);sy=std::clamp(sy,0,size.height()-1);}
                if(sx<0||sy<0||sx>=size.width()||sy>=size.height())continue;
                const float weight=(i?fx:1-fx)*(j?fy:1-fy);const auto *pixel=reinterpret_cast<const float *>(source.linear.constScanLine(sy))+sx*4;
                if(!std::isfinite(pixel[0])||!std::isfinite(pixel[1])||!std::isfinite(pixel[2])||!std::isfinite(pixel[3])){invalid=1;return;}
                const float lensGain=lens.enabled()?lens.gain(sx,sy):1.f;for(int c=0;c<3;++c)values[c]+=pixel[c]*pixel[3]*weight*lensGain;values[3]+=pixel[3]*weight;
            }
            const float alpha=std::clamp(values[3],0.f,1.f);
            if(!chromatic.enabled()){if(alpha>1e-8f)for(int c=0;c<3;++c)values[c]/=alpha;else values[0]=values[1]=values[2]=0;}
            const float balanced[]{values[0],values[1],values[2]};
            for(int c=0;c<3;++c)values[c]=source.draftBalance[c*3]*balanced[0]+source.draftBalance[c*3+1]*balanced[1]+source.draftBalance[c*3+2]*balanced[2];
            values[0]*=red;values[1]*=green;values[2]*=blue;
            for(int c=0;c<3;++c)values[c]=(values[c]*exposure-.18f)*contrast+.18f;
            tone.apply(values);vignette.apply(values,x,y);grain.apply(values,x,y);
            for(int c=0;c<3;++c)if(!std::isfinite(values[c])){invalid=2;return;}
            for(int c=0;c<3;++c)row[x*4+c]=alpha>1e-8f?values[c]:0;row[x*4+3]=alpha;
        }
    }};
    if(hasPreparedAdjustment(r)||qint64(target.width())*target.height()>=1024*1024){
        static QThreadPool pool;const int workers=std::clamp(QThread::idealThreadCount(),1,6);pool.setMaxThreadCount(workers);
        QFuture<void> jobs[6];for(int i=0;i<workers;++i)jobs[i]=QtConcurrent::run(&pool,[&,i]{rows(target.height()*i/workers,target.height()*(i+1)/workers);});
        for(int i=0;i<workers;++i)jobs[i].waitForFinished();
    }else rows(0,target.height());
    if(isCanceled(cancel)){error="Canceled";return {};}if(invalid){error=invalid==1?"NaN/Inf source":"NaN/Inf render";return {};}return result;
}
RenderResult renderFrame(const SourceFrame &source,const Recipe &recipe,int longEdge,SpatialCache *spatialCache,const RenderView &view,const CancellationToken &cancel) {
    RenderResult result;if(isCanceled(cancel)){result.error="Canceled";return result;}QElapsedTimer timer;timer.start();result.fullSize=renderedSize(source.linear.size(),recipe);
    if((view.sharpenMask&&view.pointRange>=0)||view.pointRange< -1||view.pointRange>=recipe.pointColors.size()||!validRecipe(recipe)||source.linear.isNull()||source.linear.format()!=QImage::Format_RGBA32FPx4){result.error="Invalid frame/recipe";return result;}
    if(hasDefringe(recipe.defringe)){
        const auto target=longEdge==0?result.fullSize:previewSize(result.fullSize,longEdge);
        const double sx=source.pixelScaleX()*source.linear.width()*recipe.crop.width()/target.width();
        const double sy=source.pixelScaleY()*source.linear.height()*recipe.crop.height()/target.height();
        const auto prefix=defringePrefix(recipe);auto build=[&]{return renderLinear(source,prefix,longEdge,result.error,{},cancel);};QImage corrected;
        if(spatialCache&&std::max(target.width(),target.height())<=3840)
            corrected=spatialCache->defringe.get(source,prefix,recipe.defringe,longEdge,sx,sy,build,result.error);
        else {auto before=build();if(!before.isNull())corrected=applyDefringe(before,recipe.defringe,sx,sy,result.error);}
        if(!corrected.isNull()){
            const auto adjusted=defringedSource(source,recipe,std::move(corrected));
            auto next=renderFrame(adjusted,afterDefringe(recipe),0,spatialCache,view,cancel);result.image=std::move(next.image);result.error=next.error;
        }
        result.elapsedMs=double(timer.elapsed());return result;
    }
    if(view.sharpenMask){auto mask=sharpenMaskFrame(source,recipe,longEdge,spatialCache,result.error,cancel);if(!mask.isNull())result.image=workToSrgb(mask,result.error,cancel);result.elapsedMs=double(timer.elapsed());return result;}
    if(recipe.texture!=0||recipe.clarity!=0||recipe.dehaze!=0||recipe.sharpenAmount!=0||recipe.noiseLuminance!=0||recipe.noiseChroma!=0){Recipe prefix=preparedGeometry(recipe);prefix.flipHorizontal=recipe.flipHorizontal;prefix.flipVertical=recipe.flipVertical;SourceFrame neutral=source;neutral.draftBalance={1,0,0,0,1,0,0,0,1};const auto target=longEdge==0?result.fullSize:previewSize(result.fullSize,longEdge);const double scale=source.sourcePixelsPerPixel*std::max(source.linear.width()*recipe.crop.width()/target.width(),source.linear.height()*recipe.crop.height()/target.height());
        auto build=[&]{return renderLinear(neutral,prefix,longEdge,result.error,{},cancel);};QImage linear;
        if(spatialCache){auto basis=spatialCache->get(neutral,prefix,longEdge,scale,build,result.error,recipe.dehaze!=0,recipe.texture!=0||recipe.clarity!=0,recipe.sharpenAmount!=0?recipe.sharpenRadius:0,recipe.noiseLuminance!=0,recipe.noiseChroma!=0,recipe.sharpenAmount!=0&&recipe.sharpenMask!=0);if(basis)linear=applySpatial(*basis,recipe,result.error);}else{auto before=build();if(!before.isNull()){auto basis=prepareSpatial(std::move(before),scale,source.originalSize.isValid()?source.originalSize:source.linear.size(),result.error,recipe.dehaze!=0,recipe.texture!=0||recipe.clarity!=0,recipe.sharpenAmount!=0?recipe.sharpenRadius:0,recipe.noiseLuminance!=0,recipe.noiseChroma!=0,recipe.sharpenAmount!=0&&recipe.sharpenMask!=0);if(basis)linear=applySpatialOwned(std::move(basis),recipe,result.error);}}
        if(!linear.isNull()){SourceFrame adjusted=source;adjusted.linear=std::move(linear);adjusted.sourcePixelsPerPixel=scale;adjusted.sourcePixelsPerPixelX=source.pixelScaleX()*source.linear.width()*recipe.crop.width()/target.width();adjusted.sourcePixelsPerPixelY=source.pixelScaleY()*source.linear.height()*recipe.crop.height()/target.height();Recipe after=recipe;after.perspective={0,0};after.distortion=0;after.chromaticAberration={0,0};after.lensShading=0;after.crop={0,0,1,1};after.angle=after.texture=after.clarity=after.dehaze=after.sharpenAmount=after.noiseLuminance=after.noiseChroma=0;after.flipHorizontal=after.flipVertical=false;auto rendered=renderFrame(adjusted,after,0,nullptr,view,cancel);result.image=std::move(rendered.image);result.error=rendered.error;}result.elapsedMs=double(timer.elapsed());return result;
    }
    if(spatialCache)spatialCache->clearSpatial();
    if(!hasPreparedAdjustment(recipe)&&recipe.crop==QRectF(0,0,1,1)&&std::abs(recipe.angle)<1e-12&&(longEdge==0||previewSize(source.linear.size(),longEdge)==source.linear.size())){
        const float ev=float(std::exp2(recipe.exposure)),contrast=float(std::exp2(recipe.contrast)),sat=float(recipe.saturation);
        const float gains[]{source.raw?1:float(std::exp2(recipe.warmth)),source.raw?1:float(std::exp2(recipe.tint)),source.raw?1:float(std::exp2(-recipe.warmth))};
        const float weights[]{.2627f,.6780f,.0593f};std::array<float,9> matrix{};
        if(view.pointRange>=0||recipe.highlights!=0||recipe.shadows!=0||recipe.whites!=0||recipe.blacks!=0||recipe.vibrance!=0||hasColorEditing(recipe)||recipe.vignetteAmount!=0||recipe.grainAmount!=0){
            for(int c=0;c<3;++c)for(int j=0;j<3;++j)matrix[c*3+j]=gains[c]*ev*contrast*source.draftBalance[c*3+j];
            const float pivot=.18f*(1-contrast);const ToneOperator tone(recipe,view.pointRange);const PostCropVignette vignette(recipe,source.linear.size());const ProceduralGrain grain(recipe,source.linear.size(),source.pixelScaleX(),source.pixelScaleY());if(!grain.valid()){result.error="Invalid grain scale/allocation";return result;}
            result.image=workToSrgbAffine(source.linear,matrix,{pivot,pivot,pivot},result.error,&tone,vignette.enabled()?&vignette:nullptr,recipe.flipHorizontal,recipe.flipVertical,grain.enabled()?&grain:nullptr,cancel);result.elapsedMs=double(timer.elapsed());return result;
        }
        for(int c=0;c<3;++c)for(int j=0;j<3;++j)for(int k=0;k<3;++k)matrix[c*3+j]+=((c==k?sat:0)+(1-sat)*weights[k])*gains[k]*ev*contrast*source.draftBalance[k*3+j];
        const float pivot=.18f*(1-contrast);result.image=workToSrgbAffine(source.linear,matrix,{pivot,pivot,pivot},result.error,nullptr,nullptr,recipe.flipHorizontal,recipe.flipVertical,nullptr,cancel);result.elapsedMs=double(timer.elapsed());return result;
    }
    auto linear=renderLinear(source,recipe,longEdge,result.error,view,cancel);if(!linear.isNull())result.image=workToSrgb(linear,result.error,cancel);
    result.elapsedMs=double(timer.elapsed());return result;
}







