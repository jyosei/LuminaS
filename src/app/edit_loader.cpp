#include "edit_loader.h"
#include "core/neutral_balance.h"
#include "core/auto_balance.h"
#include "render/geometry_transform.h"
#include "core/point_color_sampling.h"
#include "io/source_reader.h"
#include "render/cpu_renderer.h"
#include "render/local_renderer.h"
#include "render/local_preview_cache.h"
#include <QtConcurrentRun>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QFile>
#include <QCryptographicHash>
#include <algorithm>
#include <cmath>
#include <QThread>
static QMutex fullSourceMutex;
static FrameResult rawWorkerRead(const QString &path,const QString &hash,double warmth,double tint,
    const CancellationToken &cancel,const std::optional<Recipe> &geometry={}){
    return geometry?prepareSource(path,*geometry,warmth,tint,cancel,hash):readSource(path,warmth,tint,cancel,hash);
}
// A new request must not invalidate a still-valid same-generation interactive round trip:
// those keep feeding the current slider drag. Everything else is superseded.
bool EditLoader::compatible(const Request &active,const Request &next){
    return active.path==next.path&&active.tag.interactive&&next.tag.interactive&&active.view==next.view&&sameEditGeneration(active.tag,next.tag);
}
static bool draftRawBalance(const SourceFrame &source,double warmth,double tint,SourceFrame &proxy){
    if(source.rawWarmth==warmth&&source.rawTint==tint){proxy.draftBalance={1,0,0,0,1,0,0,0,1};return true;}
    const auto &a=source.cameraToWork;
    const double determinant=a[0]*(a[4]*a[8]-a[5]*a[7])-a[1]*(a[3]*a[8]-a[5]*a[6])+a[2]*(a[3]*a[7]-a[4]*a[6]);
    if(!std::isfinite(determinant)||std::abs(determinant)<1e-10)return false;
    const double inverse[]{(a[4]*a[8]-a[5]*a[7])/determinant,(a[2]*a[7]-a[1]*a[8])/determinant,(a[1]*a[5]-a[2]*a[4])/determinant,
        (a[5]*a[6]-a[3]*a[8])/determinant,(a[0]*a[8]-a[2]*a[6])/determinant,(a[2]*a[3]-a[0]*a[5])/determinant,
        (a[3]*a[7]-a[4]*a[6])/determinant,(a[1]*a[6]-a[0]*a[7])/determinant,(a[0]*a[4]-a[1]*a[3])/determinant};
    double gains[]{std::exp2(warmth-source.rawWarmth),std::exp2(tint-source.rawTint),std::exp2(source.rawWarmth-warmth),std::exp2(tint-source.rawTint)};
    auto desired=source.cameraWB;for(int i=0;i<4;++i)desired[i]*=gains[i];
    const double normalization=*std::min_element(source.cameraWB.begin(),source.cameraWB.end())/ *std::min_element(desired.begin(),desired.end());
    proxy.draftBalance.fill(0);for(int c=0;c<3;++c)for(int j=0;j<3;++j)for(int k=0;k<3;++k)proxy.draftBalance[c*3+j]+=float(a[c*3+k]*gains[k]*normalization*inverse[k*3+j]);
    return true;
}
bool EditLoader::acceptsResult(const EditResult &result) const{
    return !closing&&hasCurrentRequest&&result.path==latestPath&&result.view==latestView&&acceptsEditTag(result.tag,latest);
}
EditLoader::EditLoader(QObject *parent):QObject(parent),localPreviewCache(std::make_unique<LocalEdit::LocalPreviewCache>()){
    pool.setMaxThreadCount(1);pointSamplePool.setMaxThreadCount(1);livePool.setMaxThreadCount(1);
    prefetchPool.setMaxThreadCount(1);prefetchPool.setThreadPriority(QThread::LowestPriority);
    connect(&pointSampleWatcher,&QFutureWatcher<PointSampleResult>::finished,this,[this]{
        pointSampleBusy=false;const auto result=pointSampleWatcher.result();
        if(!closing)emit pointColorSampleStateChanged();
        if(!closing&&result.generation==pointSampleGeneration&&result.recipe==latestRecipe&&canPickNeutral(result.path,result.recipe,result.tag))emit pointColorSampled(result.path,result.tag,result.point,result.error);
    });
    connect(&liveWatcher,&QFutureWatcher<EditResult>::finished,this,[this]{
        const bool valid=liveRequest&&!isCanceled(liveRequest->cancel);liveRequest.reset();liveBusy=false;
        const auto result=liveWatcher.result();
        if(valid&&acceptsResult(result)){if(result.error.isEmpty())remember(result);emit ready(result);}
        dispatchLive();
    });
    connect(&prefetchWatcher,&QFutureWatcher<EditResult>::finished,this,[this]{
        const bool valid=prefetchRequest&&!isCanceled(prefetchRequest->cancel);prefetchRequest.reset();prefetchBusy=false;
        const auto result=prefetchWatcher.result();
        if(valid&&!closing){
            ++completed;if(!result.error.isEmpty())++failures;
            if(result.error.isEmpty()&&(!displayCache.contains(result.path)||(displayCache[result.path].recipe==Recipe{}&&displayCache[result.path].tag.localIdentity==0)))remember(result);
            emit prefetched(result);emit prefetchProgress(completed,int(imported.size()),failures);
        }
        dispatch();
    });
    connect(&watcher,&QFutureWatcher<EditResult>::finished,this,[this]{
        const bool valid=currentRequest&&!isCanceled(currentRequest->cancel);currentRequest.reset();busy=false;
        const auto result=watcher.result();
        if(valid&&acceptsResult(result)){
            if(result.error.isEmpty()){
                if(result.workCrop==latestRecipe.crop&&!result.workFrame.linear.isNull()){
                    lastBase=result.workFrame;lastCrop=result.workCrop;lastAngle=result.workAngle;
                    lastPerspective=result.workPerspective;lastDistortion=result.workDistortion;
                    lastChromaticAberration=result.workChromaticAberration;lastLensShading=result.workLensShading;
                    lastFullGeometry=result.fullGeometry;lastBytes=result.sourceBytes;lastMtime=result.sourceMtime;
                }
                remember(result);
            }
            emit ready(result);
        }
        dispatch();
    });
}
void EditLoader::invalidate(){
    // Close the identity gate first. Tokens only request cooperative termination.
    hasCurrentRequest=false;latestPath.clear();pending.reset();livePending.reset();
    currentCancel.cancel();liveCancel.cancel();cancelPointColorSample();
    localPreviewCache->clear();
    lastBase={};lastFullGeometry.reset();
}
void EditLoader::resetImports(){
    invalidate();prefetchCancel.cancel();prefetchQueue.clear();imported.clear();
    displayCache.clear();displayOrder.clear();displayBytes=0;completed=failures=0;
    emit prefetchProgress(0,0,0);
}
EditLoader::~EditLoader(){
    closing=true;invalidate();prefetchQueue.clear();prefetchCancel.cancel();
    disconnect(&pointSampleWatcher,nullptr,this,nullptr);disconnect(&watcher,nullptr,this,nullptr);
    disconnect(&prefetchWatcher,nullptr,this,nullptr);disconnect(&liveWatcher,nullptr,this,nullptr);
    pointSamplePool.waitForDone();livePool.waitForDone();pool.waitForDone();prefetchPool.waitForDone();
}
bool EditLoader::canPickNeutral(const QString &path,const Recipe &recipe,EditTag tag) const{
    const QFileInfo info(path);
    return !closing&&hasCurrentRequest&&recipe==latestRecipe&&latestView.pointRange<0&&!latestView.sharpenMask&&!latestView.localMaskVisible&&tag==latest&&!tag.interactive&&!busy&&!pending&&path==latestPath&&lastBase.path==path&&!lastBase.linear.isNull()&&lastCrop==recipe.crop&&lastAngle==recipe.angle&&lastPerspective==recipe.perspective&&lastDistortion==recipe.distortion&&lastChromaticAberration==recipe.chromaticAberration&&lastLensShading==recipe.lensShading&&info.size()==lastBytes&&info.lastModified()==lastMtime&&(!lastBase.raw||(lastBase.rawWarmth==recipe.warmth&&lastBase.rawTint==recipe.tint));
}
bool EditLoader::pickNeutral(const QString &path,const Recipe &recipe,EditTag tag,QPointF uv,Recipe &picked,QString &error) const{
    if(!canPickNeutral(path,recipe,tag)){error=QStringLiteral("请等待当前预览解析完成后取样");return false;}
    picked=recipe;return neutralBalance(lastBase,uv,picked,error);
}
bool EditLoader::autoBalance(const QString &path,const Recipe &recipe,EditTag tag,Recipe &estimated,QString &error) const{
    if(!canPickNeutral(path,recipe,tag)){error=QStringLiteral("请等待当前精确浮点预览完成后估计白平衡");return false;}
    auto candidate=recipe;if(!automaticBalance(lastBase,candidate,error))return false;estimated=candidate;return true;
}
bool EditLoader::requestPointColorSample(const QString &path,const Recipe &recipe,EditTag tag,QPointF uv,QString &error){
    error.clear();if(!canSamplePointColor(path,recipe,tag)){error=QStringLiteral("请等待当前精确预览或取色完成");return false;}
    if(!std::isfinite(uv.x())||!std::isfinite(uv.y())||uv.x()<0||uv.y()<0||uv.x()>=1||uv.y()>=1){error=QStringLiteral("取样位置不在有效预览内");return false;}
    const auto base=lastBase;pointSampleBusy=true;const auto generation=++pointSampleGeneration;
    pointSampleWatcher.setFuture(QtConcurrent::run(&pointSamplePool,[base,recipe,tag,path,uv,generation]{PointSampleResult result;result.generation=generation;result.path=path;result.recipe=recipe;result.tag=tag;samplePointColor(base,recipe,uv,result.point,result.error);return result;}));return true;
}
bool EditLoader::canSamplePointColor(const QString &path,const Recipe &recipe,EditTag tag) const{return !pointSampleBusy&&recipe==latestRecipe&&canPickNeutral(path,recipe,tag);}
void EditLoader::request(const QString &path,const Recipe &recipe,EditTag tag,const RenderView &view){
    queueRequest(path,recipe,tag,view,{});
}
bool EditLoader::requestDocument(const QString &path,LocalEdit::DocumentSnapshot document,EditTag tag,
    QString &error,const RenderView &view){
    error.clear();
    if(!document||!LocalEdit::validDocument(*document,error,true)){
        if(error.isEmpty())error="Missing local document";return false;
    }
    if(document->asset.sourcePath!=path){error="Document source path mismatch";return false;}
    if(closing){error="Loader closing";return false;}
    // Do not trust a const shared_ptr to exclude aliases retained by its caller.
    auto frozen=std::make_shared<const LocalEdit::PhotoDocument>(*document);
    const auto recipe=frozen->global;
    queueRequest(path,recipe,tag,view,std::move(frozen));return true;
}
void EditLoader::queueRequest(const QString &path,const Recipe &recipe,EditTag tag,const RenderView &view,
    LocalEdit::DocumentSnapshot document){
    if(closing)return;
    // Invalidate old delivery before requesting cancellation; never revoke its buffers.
    latest=tag;latestPath=path;latestRecipe=recipe;latestView=view;hasCurrentRequest=true;
    emit requestQueued(path,tag);
    // A synchronous diagnostic observer may invalidate or replace the request.
    // Do not let this outer enqueue overwrite that newer state.
    if(closing||!hasCurrentRequest||latest!=tag||latestPath!=path)return;
    pending.reset();livePending.reset();Request next{path,recipe,tag};next.view=view;next.document=std::move(document);
    if(currentRequest&&!compatible(*currentRequest,next))currentCancel.cancel();
    if(liveRequest&&!compatible(*liveRequest,next))liveCancel.cancel();
    const QFileInfo liveInfo(path);
    const bool fullUsable=lastFullGeometry&&lastFullGeometry->path==path&&liveInfo.size()==lastBytes&&liveInfo.lastModified()==lastMtime&&(!lastFullGeometry->raw||(lastFullGeometry->rawWarmth==recipe.warmth&&lastFullGeometry->rawTint==recipe.tint));
    if(tag.interactive&&lastBase.path==path&&!lastBase.linear.isNull()&&((lastCrop==recipe.crop&&lastPerspective==recipe.perspective&&lastDistortion==recipe.distortion&&lastChromaticAberration==recipe.chromaticAberration&&lastLensShading==recipe.lensShading)||fullUsable)){
        next.base=lastBase;next.baseAngle=lastAngle;if(fullUsable)next.fullGeometry=lastFullGeometry;
        next.baseBytes=lastBytes;next.baseMtime=lastMtime;livePending=std::move(next);dispatchLive();return;
    }
    pending=std::move(next);dispatch();
}
void EditLoader::dispatchLive(){
    if(closing||liveBusy||!livePending)return;
    auto request=*livePending;livePending.reset();
    if(!hasCurrentRequest||request.path!=latestPath||request.view!=latestView||!acceptsEditTag(request.tag,latest))return;
    liveCancel=CancellationSource();request.cancel=liveCancel.token();liveRequest=request;liveBusy=true;
    liveWatcher.setFuture(QtConcurrent::run(&livePool,[this,request]{return processLive(request);}));
}
void EditLoader::prefetch(const QStringList &paths){if(closing)return;for(const auto &path:paths)if(!imported.contains(path)){imported.insert(path);prefetchQueue.append(path);}emit prefetchProgress(completed,int(imported.size()),failures);dispatch();}
void EditLoader::remember(const EditResult &result){
    if(result.view.pointRange>=0||result.view.sharpenMask||result.view.localMaskVisible)return; // transient display modes must never enter the display/prefetch cache
    if(displayCache.contains(result.path))displayBytes-=displayCache[result.path].image.sizeInBytes();displayOrder.removeAll(result.path);
    auto entry=result;entry.workFrame={};entry.fullGeometry.reset();displayCache[result.path]=std::move(entry);displayOrder.append(result.path);displayBytes+=result.image.sizeInBytes();
    while(displayBytes>512LL*1024*1024&&!displayOrder.isEmpty()){const auto path=displayOrder.takeFirst();displayBytes-=displayCache.take(path).image.sizeInBytes();}
}
std::optional<EditResult> EditLoader::preview(const QString &path,const Recipe &recipe){
    if(closing)return {};
    if(!displayCache.contains(path))return {};const auto result=displayCache.value(path);const QFileInfo info(path);
    if((hasCurrentRequest&&path==latestPath&&result.tag.localIdentity!=latest.localIdentity)||result.recipe!=recipe||info.size()!=result.sourceBytes||info.lastModified()!=result.sourceMtime)return {};
    displayOrder.removeAll(path);displayOrder.append(path);return result;
}
void EditLoader::dispatch(){
    if(closing)return;
    if(!busy&&pending){
        auto request=*pending;pending.reset();
        if(hasCurrentRequest&&request.path==latestPath&&request.view==latestView&&acceptsEditTag(request.tag,latest)){
            currentCancel=CancellationSource();request.cancel=currentCancel.token();currentRequest=request;busy=true;
            watcher.setFuture(QtConcurrent::run(&pool,[this,request]{return process(request);}));
        }
    }
    if(!prefetchBusy&&!pending&&!prefetchQueue.isEmpty()){
        Request request;request.path=prefetchQueue.takeFirst();request.prefetch=true;
        prefetchCancel=CancellationSource();request.cancel=prefetchCancel.token();prefetchRequest=request;prefetchBusy=true;
        prefetchWatcher.setFuture(QtConcurrent::run(&prefetchPool,[this,request]{return process(request);}));
    }
}
EditResult EditLoader::process(const Request &request){
        QElapsedTimer timer;timer.start();EditResult result;result.path=request.path;result.tag=request.tag;result.angle=request.recipe.angle;result.recipe=request.recipe;result.view=request.view;result.prefetch=request.prefetch;
        if(closing||isCanceled(request.cancel)){result.error="Canceled";return result;}
        const bool raw=QFileInfo(request.path).suffix().compare("cr3",Qt::CaseInsensitive)==0;
        const double warmth=raw?request.recipe.warmth:0,tint=raw?request.recipe.tint:0;
        QString digest;
        if(request.tag.interactive&&proxy.path==request.path&&!proxy.linear.isNull())digest=proxy.sha256;
        else{QFile file(request.path);QCryptographicHash hash(QCryptographicHash::Sha256);
            if(!file.open(QIODevice::ReadOnly)){result.error=QStringLiteral("源文件不可读");return result;}
            while(!file.atEnd()){if(isCanceled(request.cancel)){result.error="Canceled";return result;}const auto block=file.read(1024*1024);if(block.isEmpty()&&file.error()!=QFile::NoError){result.error=QStringLiteral("源文件不可读");return result;}hash.addData(block);}
            digest=QString::fromLatin1(hash.result().toHex());}
        if(request.prefetch){
            auto stored=workCache.get(request.path,digest);SourceFrame base;
            if(stored)base=std::move(*stored);else{
                QMutexLocker sourceGuard(&fullSourceMutex);
                if(closing||isCanceled(request.cancel)){result.error="Canceled";return result;}
                auto appeared=workCache.get(request.path,digest);
                if(appeared)base=std::move(*appeared);else{
                    const auto read=raw?rawWorkerRead(request.path,digest,0,0,request.cancel,Recipe{}):readSource(request.path,0,0,request.cancel);if(!read.error.isEmpty()){result.error=read.error;return result;}result.sourceRebuilt=true;
                    base=*read.source;if(!raw)base.linear=renderLinear(*read.source,{},3840,result.error,{},request.cancel);
                    if(isCanceled(request.cancel)){result.image={};result.error="Canceled";return result;}
                    if(!result.error.isEmpty())return result;
                    if(!raw){base.sourcePixelsPerPixelX=double(read.source->linear.width())/base.linear.width();base.sourcePixelsPerPixelY=double(read.source->linear.height())/base.linear.height();
                        base.sourcePixelsPerPixel=std::max(base.sourcePixelsPerPixelX,base.sourcePixelsPerPixelY);}
                    if(!workCache.put(base))result.colorNote=QStringLiteral("临时缓存写入失败，内存预览仍可用");
                }
            }
            const auto rendered=renderFrame(base,{},0,nullptr,{},request.cancel);result.error=rendered.error;result.image=rendered.image;result.originalSize=base.originalSize;result.fullSize=base.originalSize;result.sourceHash=base.sha256;
            result.colorNote=base.colorNote+" · "+result.colorNote;
        }else{
        if(proxy.path!=request.path||proxy.sha256!=digest){
            proxy={};cached.reset();auto stored=workCache.get(request.path,digest);if(stored){proxy=std::move(*stored);proxyCrop={0,0,1,1};proxyAngle=0;proxyPerspective={0,0};proxyDistortion=0;proxyChromaticAberration={0,0};proxyLensShading=0;}
        }
        const bool balanceValid=request.tag.interactive||(proxy.rawWarmth==warmth&&proxy.rawTint==tint);
        if(proxy.linear.isNull()||!balanceValid||proxyCrop!=request.recipe.crop||proxyAngle!=request.recipe.angle||proxyPerspective!=request.recipe.perspective||proxyDistortion!=request.recipe.distortion||proxyChromaticAberration!=request.recipe.chromaticAberration||proxyLensShading!=request.recipe.lensShading){
            QMutexLocker sourceGuard(&fullSourceMutex);
            if(closing||isCanceled(request.cancel)){result.error="Canceled";return result;}
            if(proxy.linear.isNull()&&warmth==0&&tint==0&&request.recipe.crop==QRectF(0,0,1,1)&&request.recipe.angle==0&&!hasPreparedAdjustment(request.recipe)){
                auto appeared=workCache.get(request.path,digest);if(appeared){proxy=std::move(*appeared);proxyCrop={0,0,1,1};proxyAngle=0;proxyPerspective={0,0};proxyDistortion=0;proxyChromaticAberration={0,0};proxyLensShading=0;}
            }
            if(proxy.linear.isNull()||(!request.tag.interactive&&(proxy.rawWarmth!=warmth||proxy.rawTint!=tint))||proxyCrop!=request.recipe.crop||proxyAngle!=request.recipe.angle||proxyPerspective!=request.recipe.perspective||proxyDistortion!=request.recipe.distortion||proxyChromaticAberration!=request.recipe.chromaticAberration||proxyLensShading!=request.recipe.lensShading){
            Recipe geometry=preparedGeometry(request.recipe);
            if(raw&&!hasPreparedAdjustment(request.recipe)&&!request.tag.interactive){
                // Ordinary selected/preloaded RAW needs only the exact 4K frame.
                // Full geometry support is decoded separately when requested.
                proxy={};cached.reset();const auto read=rawWorkerRead(request.path,digest,warmth,tint,request.cancel,geometry);
                if(!read.error.isEmpty()){result.error=read.error;return result;}proxy=*read.source;result.sourceRebuilt=true;
            }else{
                if(!cached||cached->path!=request.path||cached->sha256!=digest||cached->rawWarmth!=warmth||cached->rawTint!=tint){
                    proxy={};cached.reset();const auto read=raw?rawWorkerRead(request.path,digest,warmth,tint,request.cancel):readSource(request.path,warmth,tint,request.cancel);
                    if(!read.error.isEmpty()){result.error=read.error;return result;}cached=read.source;result.sourceRebuilt=true;
                }
                proxy=*cached;proxy.linear=renderLinear(*cached,geometry,3840,result.error,{},request.cancel);
                if(!proxy.linear.isNull()){
                    proxy.sourcePixelsPerPixelX=cached->pixelScaleX()*cached->linear.width()*geometry.crop.width()/proxy.linear.width();proxy.sourcePixelsPerPixelY=cached->pixelScaleY()*cached->linear.height()*geometry.crop.height()/proxy.linear.height();
                    proxy.sourcePixelsPerPixel=std::max(cached->linear.width()*geometry.crop.width()/proxy.linear.width(),cached->linear.height()*geometry.crop.height()/proxy.linear.height());
                }
            }
            if(isCanceled(request.cancel)){result.image={};result.error="Canceled";return result;}
            if(!result.error.isEmpty())return result;
            proxyCrop=geometry.crop;proxyAngle=geometry.angle;proxyPerspective=geometry.perspective;proxyDistortion=geometry.distortion;proxyChromaticAberration=geometry.chromaticAberration;proxyLensShading=geometry.lensShading;
            if(warmth==0&&tint==0&&proxyCrop==QRectF(0,0,1,1)&&proxyAngle==0&&proxyPerspective.isNull()&&proxyDistortion==0&&proxyChromaticAberration.isNull()&&proxyLensShading==0)workCache.put(proxy);
            if(hasPreparedAdjustment(request.recipe)||request.tag.interactive)result.fullGeometry=cached;else cached.reset();
            }
        }
        auto previewSource=proxy;auto applied=request.recipe;clearPreparedGeometry(applied);
        if(raw&&(warmth!=proxy.rawWarmth||tint!=proxy.rawTint)&&!draftRawBalance(proxy,warmth,tint,previewSource)){result.error="RAW draft matrix";return result;}
        const auto rendered=request.document?
            LocalEdit::renderDocumentPreview(previewSource,*request.document,applied,3840,&spatialCache,request.view,request.cancel,localPreviewCache.get()):
            renderFrame(previewSource,applied,0,&spatialCache,request.view,request.cancel);
        result.document=request.document;result.error=rendered.error;result.image=rendered.image;
        result.localMask=rendered.localMask;result.effectElapsedMs=request.document?rendered.effectElapsedMs:rendered.elapsedMs;result.maskElapsedMs=rendered.maskElapsedMs;
        if(isCanceled(request.cancel)){result.image={};result.error="Canceled";return result;}
        result.originalSize=proxy.originalSize;result.fullSize=renderedSize(proxy.originalSize,request.recipe);result.colorNote=proxy.colorNote;result.sourceHash=proxy.sha256;
        if(raw&&request.tag.interactive&&(warmth!=proxy.rawWarmth||tint!=proxy.rawTint))result.colorNote+=QStringLiteral(" · WB拖动缓存反馈，松手源精算");
        if(!hasPreparedAdjustment(request.recipe)&&!request.tag.interactive)cached.reset();
        result.workFrame=proxy;result.workCrop=proxyCrop;result.workAngle=proxyAngle;result.workPerspective=proxyPerspective;result.workDistortion=proxyDistortion;result.workChromaticAberration=proxyChromaticAberration;result.workLensShading=proxyLensShading;if(hasPreparedAdjustment(request.recipe)&&cached)result.fullGeometry=cached;
        }
        if(!request.tag.interactive){QFile after(request.path);QCryptographicHash afterHash(QCryptographicHash::Sha256);
            if(isCanceled(request.cancel)){result.image={};result.error="Canceled";return result;}
            if(!after.open(QIODevice::ReadOnly)){result.error=QStringLiteral("源文件处理期间变化");result.image={};}
            else{while(!after.atEnd()){if(isCanceled(request.cancel)){result.image={};result.error="Canceled";return result;}const auto block=after.read(1024*1024);if(block.isEmpty()&&after.error()!=QFile::NoError){result.error=QStringLiteral("源文件处理期间变化");result.image={};break;}afterHash.addData(block);}
                if(result.error.isEmpty()&&QString::fromLatin1(afterHash.result().toHex())!=digest){result.error=QStringLiteral("源文件处理期间变化");result.image={};}}}
        const QFileInfo info(request.path);result.sourceBytes=info.size();result.sourceMtime=info.lastModified();result.elapsedMs=double(timer.elapsed());return result;
}
EditResult EditLoader::processLive(const Request &request){
    QElapsedTimer timer;timer.start();EditResult result;result.path=request.path;result.tag=request.tag;result.recipe=request.recipe;result.view=request.view;result.angle=request.baseAngle;
    if(isCanceled(request.cancel)){result.error="Canceled";return result;}
    auto base=request.base;
    if(request.fullGeometry){
        const auto geometry=preparedGeometry(request.recipe);base=*request.fullGeometry;base.linear=renderLinear(*request.fullGeometry,geometry,3840,result.error,{},request.cancel);
        if(isCanceled(request.cancel)){result.image={};result.error="Canceled";return result;}
        if(base.linear.isNull())return result;
        base.sourcePixelsPerPixelX=request.fullGeometry->pixelScaleX()*request.fullGeometry->linear.width()*geometry.crop.width()/base.linear.width();base.sourcePixelsPerPixelY=request.fullGeometry->pixelScaleY()*request.fullGeometry->linear.height()*geometry.crop.height()/base.linear.height();base.sourcePixelsPerPixel=std::max(base.pixelScaleX(),base.pixelScaleY());result.angle=request.recipe.angle;
    }
    auto applied=request.recipe;clearPreparedGeometry(applied);
    if(base.raw&&!draftRawBalance(base,request.recipe.warmth,request.recipe.tint,base)){result.error="RAW live matrix";return result;}
    const auto rendered=request.document?
        LocalEdit::renderDocumentPreview(base,*request.document,applied,3840,&spatialCache,request.view,request.cancel,localPreviewCache.get()):
        renderFrame(base,applied,0,&spatialCache,request.view,request.cancel);
    result.document=request.document;result.image=rendered.image;result.error=rendered.error;
    result.localMask=rendered.localMask;result.effectElapsedMs=request.document?rendered.effectElapsedMs:rendered.elapsedMs;result.maskElapsedMs=rendered.maskElapsedMs;
    if(isCanceled(request.cancel)){result.image={};result.error="Canceled";return result;}
    result.sourceHash=base.sha256;result.originalSize=base.originalSize;
    result.fullSize=renderedSize(base.originalSize,request.recipe);result.colorNote=base.colorNote+QStringLiteral(" · 4K缓存实时反馈");result.sourceBytes=request.baseBytes;result.sourceMtime=request.baseMtime;result.elapsedMs=double(timer.elapsed());return result;
}







