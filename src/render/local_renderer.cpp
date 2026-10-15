#include "local_renderer.h"
#include "local_mask.h"
#include "vignette_operator.h"
#include "grain_operator.h"
#include "color/color_pipeline.h"
#include "geometry_transform.h"
#include <QElapsedTimer>
#include <QThreadPool>
#include <QtConcurrentRun>
#include <array>
#include <atomic>
#include <cmath>

namespace LocalEdit {
namespace {
bool active(const AdjustmentLayer &layer){
    return layer.enabled&&layer.opacity>0&&layer.parameters!=LocalParameters{}&&
        (!layer.mask.components.isEmpty()||layer.mask.invert);
}
bool hasActive(const PhotoDocument &document){
    for(const auto &layer:document.layers)if(active(layer))return true;
    return false;
}
bool check(const SourceFrame &source,const PhotoDocument &document,QString &error,
    const CancellationToken &cancel){
    error.clear();if(isCanceled(cancel)){error="Canceled";return false;}
    if(!validDocument(document,error,true))return false;
    if(source.linear.isNull()||source.linear.format()!=QImage::Format_RGBA32FPx4||
       source.originalSize!=document.asset.normalizedSize||source.raw!=document.asset.raw||
       source.sha256.compare(document.asset.sourceHash,Qt::CaseInsensitive)!=0){
        error="Document/source identity mismatch";return false;
    }
    return true;
}
// Takes the sole frame owner. No second full-source float frame or full-source
// coverage buffer is allocated; each worker owns at most one 256px tile.
QImage compose(QImage image,const SourceFrame &source,const PhotoDocument &document,
    const Recipe &processingRecipe,QString &error,const CancellationToken &cancel){
    if(image.isNull())return {};
    constexpr int tileSize=256;
    const auto size=image.size();const int columns=(size.width()+tileSize-1)/tileSize;
    const int rows=(size.height()+tileSize-1)/tileSize,tiles=columns*rows,workers=std::min(4,tiles);
    const MaskMapping mapping{document.asset.normalizedSize,size,document.global};
    auto *pixels=image.bits();const auto stride=image.bytesPerLine();
    static QThreadPool pool;
    [[maybe_unused]] static const bool configured=[] {pool.setMaxThreadCount(4);return true;}();
    for(const auto &layer:document.layers){
        if(isCanceled(cancel)){error="Canceled";return {};}
        if(!active(layer))continue;
        const PreparedBrushMask mask(layer.mask);const LocalPixelTransform transform(layer.parameters);
        if(!mask.valid()||!transform.valid()){
            error=mask.valid()?QStringLiteral("Invalid local parameters"):mask.error();return {};
        }
        std::atomic<bool> failed{false};std::array<QString,4> errors;
        std::array<QFuture<void>,4> jobs;
        for(int worker=0;worker<workers;++worker)jobs[worker]=QtConcurrent::run(&pool,[&,worker]{
            MaskRasterScratch scratch;QVector<float> coverage;
            for(int index=worker;index<tiles;index+=workers){
                if(failed.load(std::memory_order_relaxed)||isCanceled(cancel))break;
                const int x=index%columns*tileSize,y=index/columns*tileSize;
                const QRect rect(x,y,std::min(tileSize,size.width()-x),std::min(tileSize,size.height()-y));
                // alpha is immutable during all layers. Concurrent RGB writes
                // use the pre-detached pointer, never non-const QImage access.
                if(!mask.rasterize(mapping,image,rect,coverage,errors[worker],cancel,&scratch)){
                    failed.store(true,std::memory_order_relaxed);break;
                }
                for(int row=0;row<rect.height();++row){
                    if(isCanceled(cancel))break;
                    auto *destination=reinterpret_cast<float *>(pixels+(y+row)*stride)+4*x;
                    for(int col=0;col<rect.width();++col){
                        const float weight=coverage[qsizetype(row)*rect.width()+col];
                        if(weight>0&&!transform.apply(destination+4*col,weight,float(layer.opacity))){
                            errors[worker]="Nonfinite local render";failed.store(true,std::memory_order_relaxed);return;
                        }
                    }
                }
            }
        });
        for(int worker=0;worker<workers;++worker)jobs[worker].waitForFinished();
        if(isCanceled(cancel)){error="Canceled";return {};}
        if(failed.load()){for(const auto &message:errors)if(!message.isEmpty()){error=message;break;}return {};}
    }
    const PostCropVignette vignette(document.global,size);
    const ProceduralGrain grain(document.global,size,
        source.pixelScaleX()*source.linear.width()*processingRecipe.crop.width()/size.width(),
        source.pixelScaleY()*source.linear.height()*processingRecipe.crop.height()/size.height());
    if(!grain.valid()){error="Invalid grain scale/allocation";return {};}
    if(vignette.enabled()||grain.enabled())for(int y=0;y<size.height();++y){
        if(isCanceled(cancel)){error="Canceled";return {};}
        auto *row=reinterpret_cast<float *>(pixels+y*stride);
        for(int x=0;x<size.width();++x){
            auto *pixel=row+4*x;
            if(pixel[3]<=1e-8f)continue;
            vignette.apply(pixel,x,y);grain.apply(pixel,x,y);
            for(int c=0;c<3;++c)if(!std::isfinite(pixel[c])){error="Nonfinite post-local render";return {};}
        }
    }
    if(isCanceled(cancel)){error="Canceled";return {};}
    return image;
}
QImage linear(const SourceFrame &source,const PhotoDocument &document,const Recipe &recipe,
    int longEdge,QString &error,const CancellationToken &cancel){
    if(!hasActive(document))return renderLinear(source,recipe,longEdge,error,{},cancel);
    auto prefix=recipe;prefix.vignetteAmount=prefix.grainAmount=0;
    auto image=renderLinear(source,prefix,longEdge,error,{},cancel);
    return compose(std::move(image),source,document,recipe,error,cancel);
}
bool previewRecipe(const PhotoDocument &document,const Recipe &recipe,QString &error){
    auto prepared=document.global;clearPreparedGeometry(prepared);
    if(recipe!=document.global&&recipe!=prepared){
        error="Preview recipe must match document or its prepared geometry";return false;
    }
    return true;
}
}
QImage renderDocumentPreviewLinear(const SourceFrame &source,const PhotoDocument &document,
    const Recipe &recipe,int longEdge,QString &error,const CancellationToken &cancel){
    if(!check(source,document,error,cancel))return {};
    if(!previewRecipe(document,recipe,error))return {};
    if(longEdge<=0||longEdge>3840){error="Preview long edge must be 1..3840";return {};}
    return linear(source,document,recipe,longEdge,error,cancel);
}
RenderResult renderDocumentPreview(const SourceFrame &source,const PhotoDocument &document,
    const Recipe &recipe,int longEdge,SpatialCache *cache,const RenderView &view,const CancellationToken &cancel){
    RenderResult result;QElapsedTimer timer;timer.start();
    if(!check(source,document,result.error,cancel))return result;
    result.fullSize=renderedSize(document.asset.normalizedSize,document.global);
    if(!previewRecipe(document,recipe,result.error))return result;
    if((view.sharpenMask&&view.pointRange>=0)||view.pointRange< -1||view.pointRange>=recipe.pointColors.size()){
        result.error="Invalid diagnostic view";return result;
    }
    if(longEdge<=0||longEdge>3840){result.error="Preview long edge must be 1..3840";return result;}
    // Existing diagnostics display the global stage they diagnose.
    if(!hasActive(document)||view.sharpenMask||view.pointRange>=0){
        auto legacy=renderFrame(source,recipe,longEdge,cache,view,cancel);
        legacy.fullSize=result.fullSize;return legacy;
    }
    auto image=linear(source,document,recipe,longEdge,result.error,cancel);
    if(!image.isNull())result.image=workToSrgb(image,result.error,cancel);
    result.elapsedMs=timer.nsecsElapsed()/1e6;return result;
}
QImage renderDocumentFullLinear(const SourceFrame &source,const PhotoDocument &document,
    QString &error,const CancellationToken &cancel){
    if(!check(source,document,error,cancel))return {};
    if(source.linear.size()!=document.asset.normalizedSize||source.pixelScaleX()!=1||source.pixelScaleY()!=1||
       (source.raw&&(source.rawWarmth!=document.global.warmth||source.rawTint!=document.global.tint))){
        error="Full-source render requires original pixels and matching RAW balance";return {};
    }
    return linear(source,document,document.global,0,error,cancel);
}
}
