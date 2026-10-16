#include "local_renderer.h"
#include "local_mask.h"
#include "local_preview_cache.h"
#include "vignette_operator.h"
#include "grain_operator.h"
#include "color/color_pipeline.h"
#include "geometry_transform.h"
#include <QElapsedTimer>
#include <QDataStream>
#include <QIODevice>
#include <QCryptographicHash>
#include <QJsonDocument>
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
QByteArray sourceKey(const SourceFrame &source){
    QByteArray data;QDataStream stream(&data,QIODevice::WriteOnly);
    stream<<quint32(1)<<quint32(localAlgorithmVersion)<<source.sha256<<source.linear.cacheKey()
        <<source.linear.size()<<source.originalSize;
    return data;
}
QByteArray baseKey(const SourceFrame &source,const Recipe &prefix,int longEdge){
    QByteArray data=sourceKey(source);QDataStream stream(&data,QIODevice::Append);
    stream<<QByteArray("global-linear")<<longEdge<<source.raw<<source.rawWarmth<<source.rawTint
        <<source.pixelScaleX()<<source.pixelScaleY();
    for(float value:source.draftBalance)stream<<value;
    for(float value:source.cameraToWork)stream<<value;
    for(double value:source.cameraWB)stream<<value;
    stream<<QJsonDocument(recipeJson(prefix)).toJson(QJsonDocument::Compact);
    return QCryptographicHash::hash(data,QCryptographicHash::Sha256);
}
QByteArray maskKey(const SourceFrame &source,const PhotoDocument &document,
    const AdjustmentLayer &layer,QSize size,QString &error){
    auto keyDocument=document;
    keyDocument.revision=1;keyDocument.localIdentity=0;
    keyDocument.global=preparedGeometry(document.global);
    keyDocument.global.flipHorizontal=document.global.flipHorizontal;
    keyDocument.global.flipVertical=document.global.flipVertical;
    auto keyLayer=layer;keyLayer.parameters={};keyLayer.name.clear();keyLayer.version=1;
    keyLayer.opacity=1;keyLayer.enabled=true;keyDocument.layers={keyLayer};
    const auto json=documentJson(keyDocument,error);if(!error.isEmpty())return {};
    QByteArray data=sourceKey(source);QDataStream stream(&data,QIODevice::Append);
    stream<<QByteArray("brush-mask")<<size<<QJsonDocument(json).toJson(QJsonDocument::Compact);
    return QCryptographicHash::hash(data,QCryptographicHash::Sha256);
}
LocalPreviewCache::Ptr cachedBuild(LocalPreviewCache &cache,const QByteArray &key,
    const LocalPreviewCache::Builder &build,QString &error,const CancellationToken &cancel){
    auto result=cache.getOrBuild(key,build,error,cancel);
    // A different request may own a joined build and cancel it. Our live
    // request can make one fresh attempt after that failed entry is removed.
    if(!result&&!isCanceled(cancel)&&error==QStringLiteral("Canceled"))
        result=cache.getOrBuild(key,build,error,cancel);
    return result;
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
    const Recipe &processingRecipe,QString &error,const CancellationToken &cancel,LocalPreviewCache *cache){
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
        LocalPreviewCache::Ptr maskPayload;
        if(cache){
            const auto key=maskKey(source,document,layer,size,error);if(key.isEmpty())return {};
            maskPayload=cachedBuild(*cache,key,[&](QString &problem)->LocalPreviewCache::Ptr {
                LocalPreviewPayload value;
                if(!rasterizeMaskPreview(mask,mapping,image,value.coverage,problem,cancel))return {};
                return std::make_shared<const LocalPreviewPayload>(std::move(value));
            },error,cancel);
            if(!maskPayload)return {};
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
                if(!maskPayload&&!mask.rasterize(mapping,image,rect,coverage,errors[worker],cancel,&scratch)){
                    failed.store(true,std::memory_order_relaxed);break;
                }
                for(int row=0;row<rect.height();++row){
                    if(isCanceled(cancel))break;
                    auto *destination=reinterpret_cast<float *>(pixels+(y+row)*stride)+4*x;
                    for(int col=0;col<rect.width();++col){
                        const float weight=maskPayload?
                            maskPayload->coverage[qsizetype(y+row)*size.width()+x+col]:
                            coverage[qsizetype(row)*rect.width()+col];
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
    int longEdge,QString &error,const CancellationToken &cancel,LocalPreviewCache *cache=nullptr){
    if(!hasActive(document))return renderLinear(source,recipe,longEdge,error,{},cancel);
    auto prefix=recipe;prefix.vignetteAmount=prefix.grainAmount=0;
    prefix.vignetteMidpoint=prefix.vignetteFeather=.5;prefix.grainSize=3;prefix.grainRoughness=.5;
    LocalPreviewCache::Ptr base;
    QImage image;
    if(cache){
        base=cachedBuild(*cache,baseKey(source,prefix,longEdge),[&](QString &problem)->LocalPreviewCache::Ptr {
            LocalPreviewPayload value;value.pixels=renderLinear(source,prefix,longEdge,problem,{},cancel);
            if(value.pixels.isNull())return {};
            return std::make_shared<const LocalPreviewPayload>(std::move(value));
        },error,cancel);
        if(!base)return {};image=base->pixels;
    }else image=renderLinear(source,prefix,longEdge,error,{},cancel);
    return compose(std::move(image),source,document,recipe,error,cancel,cache);
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
    const Recipe &recipe,int longEdge,QString &error,const CancellationToken &cancel,LocalPreviewCache *cache){
    if(!check(source,document,error,cancel))return {};
    if(!previewRecipe(document,recipe,error))return {};
    if(longEdge<=0||longEdge>3840){error="Preview long edge must be 1..3840";return {};}
    return linear(source,document,recipe,longEdge,error,cancel,cache);
}
RenderResult renderDocumentPreview(const SourceFrame &source,const PhotoDocument &document,
    const Recipe &recipe,int longEdge,SpatialCache *cache,const RenderView &view,const CancellationToken &cancel,
    LocalPreviewCache *localCache){
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
    auto image=linear(source,document,recipe,longEdge,result.error,cancel,localCache);
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
