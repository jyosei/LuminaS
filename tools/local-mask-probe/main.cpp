// Local mask replay probe. Generates its own 4K float32 base frame in memory and
// never reads a file, an argument image or a photo. It measures mask replay, the
// prepared index footprint and scalar agreement only; this is not import, native
// event or RGB effect latency, and no 30/60 FPS target is asserted.
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointF>
#include <QRandomGenerator>
#include <QSize>
#include <QString>
#include <QTextStream>
#include <QVector>
#include <QtGlobal>
#include <QtTypes>
#include <cstdint>
#include <bit>
#include <algorithm>
#include <utility>
#include "core/document.h"
#include "color/color_pipeline.h"
#include "core/photo_document.h"
#include "core/cancellation.h"
#include "render/local_reference.h"
#include "render/local_mask.h"
#include "render/local_renderer.h"
#include "render/local_preview_cache.h"
#include "render/geometry_transform.h"

using namespace LocalEdit;

namespace {

constexpr int kTile=256;
constexpr int kWarmups=1;
constexpr int kRounds=5;
constexpr int kPointsPerStroke=500;
constexpr int kQueries=200;

quint32 bits(float v){return std::bit_cast<quint32>(v);}

QImage baseFrame(int width,int height){
    QImage image(width,height,QImage::Format_RGBA32FPx4);
    for(int y=0;y<height;++y){
        float *line=reinterpret_cast<float*>(image.scanLine(y));
        for(int x=0;x<width;++x){
            line[x*4+0]=0.18f;line[x*4+1]=0.18f;line[x*4+2]=0.18f;line[x*4+3]=1.0f;
        }
    }
    return image;
}

// Dense walk with repeats so the index sees duplicates and clustered points.
QVector<QPointF> stroke(QRandomGenerator &rng,QPointF current,double span,int count){
    QVector<QPointF> points;
    points.reserve(count);
    points.append(current);
    while(points.size()<count){
        const int mode=static_cast<int>(rng.bounded(4u));
        if(mode==0||mode==1)points.append(current);
        else if(mode==2)points.append(points.at(rng.bounded(static_cast<quint32>(points.size()))));
        else{
            current+=QPointF((rng.generateDouble()-0.5)*span,(rng.generateDouble()-0.5)*span);
            points.append(current);
        }
    }
    return points;
}

} // namespace
int main(int argc,char **argv){
    QCoreApplication app(argc,argv);
    QTextStream out(stdout);

    // Synthetic 3840x2560 straight RGBA float32 frame: RGB .18, alpha 1.
    const int width=3840,height=2560;
    const QImage base=baseFrame(width,height);
    if(base.isNull()||base.width()!=width||base.height()!=height){
        qCritical("failed to build the 3840x2560 RGBA32F base frame");
        return 1;
    }
    const QSize sourceSize(6000,4000);
    const QSize outputSize(width,height);

    // Two strokes, 500 points each, unique ids, radius 120, hardness .6,
    // strength .8; the second subtracts at a different location.
    QRandomGenerator rng(0x10ca1a5u);
    Mask mask;
    {
        MaskComponent add;
        add.id=QStringLiteral("probe-add");
        add.operation=MaskOperation::Add;
        add.strength=0.8;
        BrushGeometry geometry;
        geometry.points=stroke(rng,QPointF(1500.0,1200.0),260.0,kPointsPerStroke);
        geometry.radius=120.0;
        geometry.hardness=0.6;
        add.geometry=geometry;
        if(add.id.isEmpty()||geometry.points.isEmpty()||geometry.radius<=0.0||
           geometry.hardness<0.0||geometry.hardness>1.0){
            qCritical("malformed add component");
            return 1;
        }
        mask.components.append(add);
    }
    {
        MaskComponent subtract;
        subtract.id=QStringLiteral("probe-subtract");
        subtract.operation=MaskOperation::Subtract;
        subtract.strength=0.8;
        BrushGeometry geometry;
        geometry.points=stroke(rng,QPointF(2600.0,1500.0),260.0,kPointsPerStroke);
        geometry.radius=120.0;
        geometry.hardness=0.6;
        subtract.geometry=geometry;
        if(subtract.id==mask.components[0].id||geometry.points.isEmpty()||
           geometry.radius<=0.0||geometry.hardness<0.0||geometry.hardness>1.0){
            qCritical("malformed subtract component");
            return 1;
        }
        mask.components.append(subtract);
    }
    if(mask.invert||mask.feather!=0.0){
        qCritical("probe mask must be plain: feather 0 and no invert");
        return 1;
    }
    const int strokeCount=mask.components.size();
    int pointCount=0;
    for(const MaskComponent &component:mask.components)
        pointCount+=std::get<BrushGeometry>(component.geometry).points.size();
    if(pointCount!=strokeCount*kPointsPerStroke){
        qCritical("unexpected point count");
        return 1;
    }
    const Recipe geometry; // prepared working frame: identity mask geometry
    MaskMapping mapping;
    mapping.sourceSize=sourceSize;
    mapping.outputSize=outputSize;
    mapping.geometry=geometry;

    QElapsedTimer timer;
    timer.start();
    PreparedBrushMask prepared(mask);
    const qint64 prepareMs=timer.elapsed();
    if(!prepared.valid()){
        qCritical().noquote()<<QStringLiteral("prepare failed: %1").arg(prepared.error());
        return 1;
    }
    const qint64 indexBytes=static_cast<qint64>(prepared.indexBytes());
    if(indexBytes<=0){
        qCritical("prepared index reported no bytes");
        return 1;
    }
    // Preview-only full mask; full-source rendering keeps tile traversal.
    QVector<qint64> warmupMs,measuredMs;
    QVector<float> fullMask;
    qint64 nonzeroPixels=0;
    double sumCoverage=0;
    bool sawCoverage=false;
    QString error;
    QJsonArray mappingTimes,samplingTimes;
    MaskPreviewStats stats;
    for(int round=0;round<kWarmups+kRounds;++round){
        timer.restart();
        if(!rasterizeMaskPreview(prepared,mapping,base,fullMask,error,{},kTile,&stats)){
            qCritical().noquote()<<error;return 1;
        }
        qint64 nonzeroRound=0;double sumRound=0;
        for(float value:std::as_const(fullMask)){
            if(!(value>=0&&value<=1)){qCritical("invalid coverage");return 1;}
            if(value>0){++nonzeroRound;sumRound+=double(value);}
        }
        sawCoverage|=nonzeroRound>0;
        if(round<kWarmups)warmupMs.append(timer.elapsed());
        else{measuredMs.append(timer.elapsed());nonzeroPixels=nonzeroRound;sumCoverage=sumRound;
            mappingTimes.append(stats.mappingAccumulatedMs);samplingTimes.append(stats.samplingAccumulatedMs);}
    }
    if(!sawCoverage){
        qCritical("the probe strokes covered no output pixel; the traversal is not representative");
        return 1;
    }
    // Scalar agreement at 200 single points: reference against prepared, bitwise.
    BrushMaskReference reference(mask);
    if(!reference.valid()){
        qCritical().noquote()<<QStringLiteral("reference rejected the probe mask: %1").arg(reference.error());
        return 1;
    }
    QRandomGenerator queryRng(0xc0ffeeu);
    int referenceMismatches=0;
    int referenceNonzero=0;
    QString firstMismatch;
    for(int i=0;i<kQueries;++i){
        const double u=queryRng.generateDouble(),v=queryRng.generateDouble();
        const auto &head=std::get<BrushGeometry>(mask.components[0].geometry);
        const QPointF sourcePoint=i<20?head.points[i]+QPointF(head.radius*.75,head.radius*.2):
            OutputSourceMapping(sourceSize,geometry).map({u,v});
        const float expected=reference.sample(sourcePoint,1.0f);
        const float actual=prepared.sample(sourcePoint,1.0f);
        if(expected>0)++referenceNonzero;
        if(bits(expected)!=bits(actual)){
            ++referenceMismatches;
            if(firstMismatch.isEmpty())
                firstMismatch=QStringLiteral("(%1,%2) ref=%3 prepared=%4")
                    .arg(sourcePoint.x(),0,'g',17).arg(sourcePoint.y(),0,'g',17)
                    .arg(bits(expected),8,16,QLatin1Char('0'))
                    .arg(bits(actual),8,16,QLatin1Char('0'));
        }
    }

    // Actual synthetic RGB-effect path, separately timed from mask replay.
    // No preview cache yet: these numbers include global linear rendering,
    // local composition and working-to-sRGB conversion, not native painting.
    SourceFrame source;source.path="synthetic";source.sha256=QString(64,'a');
    source.linear=base;source.originalSize=sourceSize;
    source.sourcePixelsPerPixelX=double(sourceSize.width())/width;
    source.sourcePixelsPerPixelY=double(sourceSize.height())/height;
    PhotoDocument document;document.asset.id="probe-asset";document.asset.sourcePath=source.path;
    document.asset.sourceHash=source.sha256;document.asset.normalizedSize=sourceSize;
    AdjustmentLayer layer;layer.id="probe-layer";layer.mask=mask;
    layer.parameters.exposure=.5;layer.parameters.saturation=1.1;document.layers={layer};
    QJsonArray effectMs;QString effectHash;
    for(int round=0;round<4;++round){
        timer.restart();const auto rendered=renderDocumentPreview(source,document,document.global);
        const double elapsed=timer.nsecsElapsed()/1e6;
        if(rendered.image.isNull()){qCritical().noquote()<<rendered.error;return 1;}
        if(round>0)effectMs.append(elapsed);
        const auto hash=QString::fromLatin1(QCryptographicHash::hash(
            QByteArrayView(reinterpret_cast<const char *>(rendered.image.constBits()),rendered.image.sizeInBytes()),
            QCryptographicHash::Sha256).toHex());
        if(!effectHash.isEmpty()&&effectHash!=hash){qCritical("unstable effect output");return 1;}
        effectHash=hash;
    }
    LocalPreviewCache cache;
    RenderView cachedView;cachedView.localMaskVisible=app.arguments().contains("--overlay");cachedView.localMaskLayer=layer.id;
    QJsonArray cachedEffectMs,cachedMaskMs;double cachedColdMs=0;QString indexedHash;
    const bool stageProfile=app.arguments().contains("--stages");
    QJsonArray cachedLinearMs,floatCopyMs,standaloneSrgbMs;
    for(int round=0;round<4;++round){
        document.layers[0].parameters.exposure=.5+.1*round;
        document.layers[0].version++;document.revision++;document.localIdentity++;
        timer.restart();const auto rendered=renderDocumentPreview(source,document,document.global,
            3840,nullptr,cachedView, {},&cache);
        const double elapsed=timer.nsecsElapsed()/1e6;
        if(rendered.image.isNull()){qCritical().noquote()<<rendered.error;return 1;}
        if(round==0)cachedColdMs=elapsed;else {cachedEffectMs.append(elapsed);cachedMaskMs.append(rendered.maskElapsedMs);}
        if(cachedView.localMaskVisible){
            const auto hash=QString::fromLatin1(QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char *>(rendered.localMask.constBits()),rendered.localMask.sizeInBytes()),QCryptographicHash::Sha256).toHex());
            if(!indexedHash.isEmpty()&&hash!=indexedHash){qCritical("mask changed during parameter drag");return 1;}indexedHash=hash;
        }
        const auto referenceEffect=renderDocumentPreview(source,document,document.global);
        if(referenceEffect.image.isNull()||rendered.image!=referenceEffect.image){
            qCritical("cached effect disagrees with uncached effect");return 1;
        }
        // Profile only after the normal measurement and correctness check. In
        // particular, do not prime the cold cache with an extra linear render.
        if(stageProfile&&round>0){
            QString stageError;
            timer.restart();
            const auto linear=renderDocumentPreviewLinear(source,document,document.global,
                3840,stageError,{},&cache);
            const double linearElapsed=timer.nsecsElapsed()/1e6;
            if(linear.isNull()||!stageError.isEmpty()){qCritical().noquote()<<stageError;return 1;}
            timer.restart();
            const auto copied=linear.copy();
            const double copyElapsed=timer.nsecsElapsed()/1e6;
            if(copied.isNull()||copied.size()!=linear.size()){qCritical("float copy failed");return 1;}
            timer.restart();
            const auto converted=workToSrgb(linear,stageError);
            const double srgbElapsed=timer.nsecsElapsed()/1e6;
            if(converted.isNull()||!stageError.isEmpty()||converted!=rendered.image){
                qCritical("standalone sRGB conversion disagrees with preview");return 1;
            }
            cachedLinearMs.append(linearElapsed);floatCopyMs.append(copyElapsed);
            standaloneSrgbMs.append(srgbElapsed);
        }
    }
    const auto cacheStats=cache.stats();
    QJsonArray warmupArrayJson;
    QJsonArray measuredArray;
    for(qint64 value:warmupMs)warmupArrayJson.append(static_cast<double>(value));
    for(qint64 value:measuredMs)measuredArray.append(static_cast<double>(value));
    QJsonObject report{
        {QStringLiteral("width"),width},
        {QStringLiteral("height"),height},
        {QStringLiteral("sourceWidth"),sourceSize.width()},
        {QStringLiteral("sourceHeight"),sourceSize.height()},
        {QStringLiteral("tile"),kTile},
        {QStringLiteral("strokeCount"),strokeCount},
        {QStringLiteral("pointCount"),pointCount},
        {QStringLiteral("indexBytes"),static_cast<double>(indexBytes)},
        {QStringLiteral("prepareMs"),static_cast<double>(prepareMs)},
        {QStringLiteral("warmupMs"),warmupArrayJson},
        {QStringLiteral("measuredMs"),measuredArray},
        {QStringLiteral("syntheticRgbEffectMs"),effectMs},
        {QStringLiteral("cachedColdRgbEffectMs"),cachedColdMs},
        {QStringLiteral("cachedParameterDragRgbEffectMs"),cachedEffectMs},
        {QStringLiteral("cachedMaskViewMs"),cachedMaskMs},
        {QStringLiteral("stageProfile"),stageProfile},
        {QStringLiteral("cachedLinearMs"),cachedLinearMs},
        {QStringLiteral("floatCopyMs"),floatCopyMs},
        {QStringLiteral("standaloneSrgbMs"),standaloneSrgbMs},
        {QStringLiteral("indexedMaskSha256"),indexedHash},
        {QStringLiteral("overlay"),cachedView.localMaskVisible},
        {QStringLiteral("cacheBuilds"),double(cacheStats.builds)},
        {QStringLiteral("cacheHits"),double(cacheStats.hits)},
        {QStringLiteral("cacheRetainedBytes"),double(cacheStats.retainedBytes)},
        {QStringLiteral("syntheticRgbEffectSha256"),effectHash},
        {QStringLiteral("effectMeaning"),QStringLiteral("global+local+sRGB; no native painting, no cache")},
        {QStringLiteral("mappingAccumulatedMs"),mappingTimes},
        {QStringLiteral("samplingAccumulatedMs"),samplingTimes},
        {QStringLiteral("accumulatedTimeMeaning"),QStringLiteral("summed worker time, not wall time")},
        {QStringLiteral("workers"),stats.workers},
        {QStringLiteral("tiles"),stats.tiles},
        {QStringLiteral("maximumWorkerScratchBytes"),double(stats.maximumScratchBytes)},
        {QStringLiteral("maskBytes"),double(fullMask.size()*sizeof(float))},
        {QStringLiteral("nonzeroPixels"),static_cast<double>(nonzeroPixels)},
        {QStringLiteral("sumCoverage"),sumCoverage},
        {QStringLiteral("referenceMismatches"),referenceMismatches},
        {QStringLiteral("referenceNonzero"),referenceNonzero}};
    if(!firstMismatch.isEmpty())
        report.insert(QStringLiteral("firstMismatch"),firstMismatch);

    out<<QString::fromUtf8(QJsonDocument(report).toJson(QJsonDocument::Compact))<<Qt::endl;
    out.flush();
    if(referenceMismatches!=0||referenceNonzero==0){
        qCritical("reference and prepared scalars disagree");
        return 1;
    }
    return 0;
}
