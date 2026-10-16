#include <QtTest>
#include <QImage>
#include <QRect>
#include <QPointF>
#include <QSize>
#include <QVector>
#include <QString>
#include <QRandomGenerator>
#include <QtGlobal>
#include <QtTypes>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <limits>
#include <algorithm>
#include <utility>
#include "core/document.h"
#include "core/photo_document.h"
#include "core/cancellation.h"
#include "render/local_reference.h"
#include "render/local_mask.h"
#include "render/geometry_transform.h"
#include "render/cpu_renderer.h"

using namespace LocalEdit;

namespace {

constexpr int kTile=256;        // production traversal tile
constexpr int kMaxTile=512;     // hard interface ceiling

// --- fixed 32-bit replica of qHash(QPointF), QRandomGenerator-safe -----------
inline quint32 mix32(quint32 a,quint32 b){
    a^=b+0x9e3779b9u+(a<<6)+(a>>2);
    return a;
}
inline quint32 pointHash(const QPointF &p){
    const double c[2]={p.x(),p.y()};
    quint32 h=0;
    for(double v:c){
        if(std::isnan(v))v=0.0;
        if(v==0.0)v=0.0;                       // collapse -0.0
        h=mix32(h,static_cast<quint32>(static_cast<qint32>(
            static_cast<qint64>(std::llround(v*1e6)))));
    }
    return h;
}
inline double unit(QRandomGenerator &rng){return rng.generateDouble();} // [0,1)
inline bool coin(QRandomGenerator &rng){return unit(rng)<0.5;}

inline quint32 bits(float v){return std::bit_cast<quint32>(v);}
inline bool sameBits(float a,float b){return bits(a)==bits(b);}
inline QString describe(const QPointF &p){
    return QStringLiteral("(%1,%2)").arg(p.x(),0,'g',17).arg(p.y(),0,'g',17);
}
// First differing output coordinate plus both bit patterns; never a whole image.
inline QString firstDifference(const QVector<float> &expected,const QVector<float> &actual,
                               int width,const char *label){
    if(expected.size()!=actual.size())
        return QStringLiteral("%1 size %2 vs %3").arg(QLatin1String(label))
            .arg(expected.size()).arg(actual.size());
    for(qsizetype i=0;i<expected.size();++i)
        if(bits(expected[i])!=bits(actual[i]))
            return QStringLiteral("%1 mismatch at (%2,%3) ref=%4 prepared=%5")
                .arg(QLatin1String(label)).arg(i%width).arg(i/width)
                .arg(bits(expected[i]),8,16,QLatin1Char('0'))
                .arg(bits(actual[i]),8,16,QLatin1Char('0'));
    return QString();
}
// RGBA32FPx4 frame written through scanLine; alpha is the only field under test.
QImage alphaFrame(QSize size,float alpha){
    QImage image(size,QImage::Format_RGBA32FPx4);
    for(int y=0;y<size.height();++y){
        float *line=reinterpret_cast<float*>(image.scanLine(y));
        for(int x=0;x<size.width();++x){
            line[x*4+0]=0.0f;line[x*4+1]=0.0f;line[x*4+2]=0.0f;line[x*4+3]=alpha;
        }
    }
    return image;
}
QImage alphaFrameWith(QSize size,float alpha,const QVector<QPair<QPoint,float>> &overrides){
    QImage image=alphaFrame(size,alpha);
    for(const auto &entry:overrides){
        float *line=reinterpret_cast<float*>(image.scanLine(entry.first.y()));
        line[entry.first.x()*4+3]=entry.second;
    }
    return image;
}

// Every component carries a non-empty unique id, non-empty geometry, radius>0,
// hardness in [0,1]. Nothing is left at a default that could hide a bug.
MaskComponent brush(const QVector<QPointF> &points,double radius,double hardness,
                    MaskOperation op,double strength){
    MaskComponent c;
    static quint64 sequence=0;
    c.id=QStringLiteral("mm-%1").arg(++sequence);
    c.operation=op;
    c.strength=strength;
    BrushGeometry g;
    g.points=points;
    g.radius=radius;
    g.hardness=hardness;
    c.geometry=g;
    Q_ASSERT(!c.id.isEmpty());
    Q_ASSERT(!g.points.isEmpty());
    Q_ASSERT(g.radius>0.0);
    Q_ASSERT(g.hardness>=0.0&&g.hardness<=1.0);
    return c;
}
// Well-formed gradient fixtures for mixed-component composition.
MaskComponent linearComponent(const QPointF &start,const QPointF &end,double strength,MaskOperation op){
    MaskComponent c;
    static quint64 sequence=0;
    c.id=QStringLiteral("lin-%1").arg(++sequence);
    c.operation=op;
    c.strength=strength;
    LinearGeometry g;
    g.start=start;
    g.end=end;
    c.geometry=g;
    return c;
}
MaskComponent radialComponent(const QPointF &center,double radiusX,double radiusY,
                              double angleDegrees,double strength,MaskOperation op){
    MaskComponent c;
    static quint64 sequence=0;
    c.id=QStringLiteral("rad-%1").arg(++sequence);
    c.operation=op;
    c.strength=strength;
    RadialGeometry g;
    g.center=center;
    g.radiusX=radiusX;
    g.radiusY=radiusY;
    g.angleDegrees=angleDegrees;
    c.geometry=g;
    return c;
}

MaskMapping mappingFor(QSize source,QSize output,const Recipe &geometry){
    MaskMapping mapping;
    mapping.sourceSize=source;
    mapping.outputSize=output;
    mapping.geometry=geometry;
    return mapping;
}
QVector<float> sentinel(qsizetype count){
    return QVector<float>(count,std::numeric_limits<float>::quiet_NaN());
}
bool allSentinel(const QVector<float> &values){
    for(float v:values)if(!std::isnan(v))return false;
    return true;
}

} // namespace

class LocalMaskTests:public QObject {
    Q_OBJECT
private slots:
    void sampleMatchesReferenceBitForBit();
    void hardBrushCenterInOneByOneTile();
    void preparedSnapshotIsIndependentOfSourcePoints();
    void edgeCasesAndUnsupportedGeometry();
    void tileReplayMatchesFullFrame();
    void rasterizeRejectsOverlargeTileAndBadAlpha();
    void rasterizeCancellationKeepsSentinel();
    void outputSourceMappingMatchesPublicFunction();
};

// Deterministic stroke: dense polyline, exact duplicates and dwell points.
QVector<QPointF> denseStroke(QRandomGenerator &rng,const QPointF &origin,double span,int count){
    QVector<QPointF> points;
    points.reserve(count);
    QPointF current=origin;
    points.append(current);
    while(points.size()<count){
        const int mode=static_cast<int>(rng.bounded(4u));
        if(mode==0)points.append(current);                                   // exact duplicate
        else if(mode==1)points.append(points.at(rng.bounded(static_cast<quint32>(points.size()))));
        else{
            current+=QPointF((unit(rng)-0.5)*span,(unit(rng)-0.5)*span);
            points.append(current);
        }
    }
    return points;
}
// 2000 random queries over three strokes with hardness 0/.5/1 and strengths
// .7/.4/1 in Add/Subtract/Add order. Prepared must equal the resolved reference
// bit-for-bit at the same coordinates.
void LocalMaskTests::sampleMatchesReferenceBitForBit(){
    QRandomGenerator rng(0x5eed2026u);
    const QSize source(1200,800);
    const QSize output(600,400);

    Mask mask;
    const double hardness[3]={0.0,0.5,1.0};
    const double strength[3]={0.7,0.4,1.0};
    const MaskOperation operations[3]={MaskOperation::Add,MaskOperation::Subtract,MaskOperation::Add};
    for(int s=0;s<3;++s){
        const QPointF origin(120.0+s*210.0,150.0+s*170.0);
        mask.components.append(brush(denseStroke(rng,origin,90.0,200),
            190.0+s*15.0,hardness[s],operations[s],strength[s]));
    }
    QCOMPARE(mask.components.size(),3);
    QVERIFY(!mask.invert);
    QCOMPARE(mask.feather,0.0);

    BrushMaskReference reference(mask);
    QVERIFY2(reference.valid(),qPrintable(reference.error()));
    PreparedBrushMask prepared(mask);
    QVERIFY2(prepared.valid(),qPrintable(prepared.error()));
    QVERIFY(prepared.indexBytes()>0);

    // A single resolved whole-frame result gives a second, independent scalar
    // oracle besides the 2000 direct point queries.
    const Recipe geometry;
    const MaskMapping mapping=mappingFor(source,output,geometry);
    const QImage alpha=alphaFrame(output,1.0f);
    QVector<float> full;
    QString error;
    QVERIFY2(reference.rasterize(mapping,alpha,QRect(QPoint(0,0),output),full,error),qPrintable(error));
    QCOMPARE(full.size(),qsizetype(output.width())*output.height());

    int inside=0;
    for(int i=0;i<2000;++i){
        const double c[2]={unit(rng),unit(rng)};
        const int pixelX=int((coin(rng)?c[0]:1.0-c[0])*output.width())%output.width();
        const int pixelY=int((coin(rng)?c[1]:1.0-c[1])*output.height())%output.height();
        const QPointF outputPoint(pixelX+.5,pixelY+.5);
        const QPointF sourcePoint=OutputSourceMapping(source,geometry).map(
            {outputPoint.x()/output.width(),outputPoint.y()/output.height()});
        const float expected=reference.sample(sourcePoint,1.0f);
        const float actual=prepared.sample(sourcePoint,1.0f);
        QVERIFY2(sameBits(expected,actual),qPrintable(QStringLiteral(
            "sample %1 source=%2 hash=%3 ref=%4 prepared=%5")
            .arg(i).arg(describe(sourcePoint)).arg(pointHash(sourcePoint))
            .arg(bits(expected),8,16,QLatin1Char('0'))
            .arg(bits(actual),8,16,QLatin1Char('0'))));
        if(expected>0.0f){++inside;
            const qsizetype index=qsizetype(outputPoint.y())*output.width()+qsizetype(outputPoint.x());
            if(index>=0&&index<full.size())
                QVERIFY2(sameBits(full[index],actual),qPrintable(QStringLiteral(
                    "sample %1 differs from full-frame replay at (%2,%3)")
                    .arg(i).arg(outputPoint.x()).arg(outputPoint.y())));
        }
        if(expected<=0.0f||expected>1.0f)
            QVERIFY2(expected>=0.0f&&expected<=1.0f,qPrintable(QStringLiteral("range %1").arg(expected)));
    }
    QVERIFY(inside>0); // the probe must actually enter the mask, not just return zeros
}
// Known hard disc: center (1,1), radius .1, hardness 1. With a 2x2 source and a
// prepared 1x1 tile the single output pixel is exactly 1. This catches a search
// seeded at the radius that never confirms an interior point, and residual tiles.
void LocalMaskTests::hardBrushCenterInOneByOneTile(){
    Mask mask;
    mask.components.append(brush({QPointF(1,1)},0.1,1.0,MaskOperation::Add,1.0));
    BrushMaskReference reference(mask);
    PreparedBrushMask prepared(mask);
    QVERIFY2(reference.valid(),qPrintable(reference.error()));
    QVERIFY2(prepared.valid(),qPrintable(prepared.error()));

    const QSize source(2,2);
    const QSize output(1,1);
    const Recipe geometry;
    const MaskMapping mapping=mappingFor(source,output,geometry);
    const QImage alpha=alphaFrame(output,1.0f);
    const QRect tile(0,0,1,1);

    QVector<float> preparedTile;
    QString error;
    QVERIFY2(prepared.rasterize(mapping,alpha,tile,preparedTile,error),qPrintable(error));
    QCOMPARE(preparedTile.size(),qsizetype(1));
    QCOMPARE(bits(preparedTile[0]),bits(1.0f));

    QVector<float> referenceTile;
    QVERIFY2(reference.rasterize(mapping,alpha,tile,referenceTile,error),qPrintable(error));
    QCOMPARE(bits(preparedTile[0]),bits(referenceTile[0]));

    // Point queries at the disc center and just outside share the same frame.
    const QPointF center=OutputSourceMapping(source,geometry).map(QPointF(0.5,0.5));
    QCOMPARE(bits(prepared.sample(center,1.0f)),bits(reference.sample(center,1.0f)));
    QCOMPARE(bits(prepared.sample(center,1.0f)),bits(1.0f));
    QCOMPARE(bits(prepared.sample(QPointF(1.0+0.1,1.0),1.0f)),bits(0.0f));
}

// The prepared index owns its point records: mutating the originals afterwards
// must not change any already published result.
void LocalMaskTests::preparedSnapshotIsIndependentOfSourcePoints(){
    QRandomGenerator rng(0x51a7c0deu);
    const QSize source(400,300);
    const QSize output(200,150);

    Mask mask;
    mask.components.append(brush(denseStroke(rng,QPointF(200,150),120.0,200),
        90.0,0.5,MaskOperation::Add,0.8));
    PreparedBrushMask prepared(mask);
    QVERIFY2(prepared.valid(),qPrintable(prepared.error()));
    const qsizetype bytes=prepared.indexBytes();
    QVERIFY(bytes>0);

    const Recipe geometry;
    const MaskMapping mapping=mappingFor(source,output,geometry);
    const QImage alpha=alphaFrame(output,1.0f);
    QVector<QPointF> probes;
    for(int i=0;i<32;++i)
        probes.append(QPointF(20.0+unit(rng)*360.0,20.0+unit(rng)*260.0));
    const QPointF center=OutputSourceMapping(source,geometry).map(QPointF(.5,.5));
    probes.append(center);

    QVector<float> before;
    for(const QPointF &probe:probes)before.append(prepared.sample(probe,1.0f));
    QVector<float> tileBefore;
    QString error;
    QVERIFY2(prepared.rasterize(mapping,alpha,QRect(0,0,128,128),tileBefore,error),qPrintable(error));

    // Mutate the original geometry in place; the prepared snapshot must not move.
    BrushGeometry &live=std::get<BrushGeometry>(mask.components[0].geometry);
    for(QPointF &point:live.points)point=QPointF(-1000.0,-1000.0);
    live.radius=7.0;
    live.hardness=0.0;
    mask.components.append(brush({QPointF(60,60)},12.0,0.9,MaskOperation::Subtract,0.5));
    mask.feather=0.25;
    mask.invert=true;

    for(int i=0;i<probes.size();++i)
        QVERIFY2(sameBits(before[i],prepared.sample(probes[i],1.0f)),
            qPrintable(QStringLiteral("snapshot changed at %1").arg(describe(probes[i]))));
    QCOMPARE(prepared.indexBytes(),bytes);

    QVector<float> tileAfter;
    QVERIFY2(prepared.rasterize(mapping,alpha,QRect(0,0,128,128),tileAfter,error),qPrintable(error));
    QCOMPARE(firstDifference(tileBefore,tileAfter,128,"snapshot tile"),QString());

    // A freshly constructed record over the mutated source must now differ, which
    // proves the probe coordinates really do observe the stroke.
    QVERIFY(!PreparedBrushMask(mask).valid());
    mask.feather=0;mask.invert=false;
    PreparedBrushMask rebuilt(mask);
    QVERIFY(rebuilt.valid());
    QVERIFY(rebuilt.indexBytes()!=bytes);
    bool differs=false;
    for(const QPointF &probe:probes)
        if(!sameBits(rebuilt.sample(probe,1.0f),prepared.sample(probe,1.0f)))differs=true;
    QVERIFY(differs);
}
// Alpha boundaries, empty mask, and the unsupported nodes B1/B2 must reject
// instead of silently ignoring.
void LocalMaskTests::edgeCasesAndUnsupportedGeometry(){
    QRandomGenerator rng(0xa17a0000u);
    const QSize source(64,48);
    const QSize output(32,24);
    const Recipe geometry;
    const MaskMapping mapping=mappingFor(source,output,geometry);
    const QRect frame(0,0,output.width(),output.height());

    const float nan=std::numeric_limits<float>::quiet_NaN();
    const float inf=std::numeric_limits<float>::infinity();

    // Alpha 0 is fully transparent, .25 is a partial pixel and never extra-weighted.
    QVector<MaskComponent> parts;
    parts.append(brush({QPointF(32,24),QPointF(33,24),QPointF(32,25)},18.0,0.5,MaskOperation::Add,0.9));
    Mask transparent;
    transparent.components=parts;
    BrushMaskReference reference(transparent);
    PreparedBrushMask prepared(transparent);
    QVERIFY2(reference.valid(),qPrintable(reference.error()));
    QVERIFY2(prepared.valid(),qPrintable(prepared.error()));

    {
        const QImage alpha=alphaFrame(output,0.0f);
        QVector<float> expected,actual;
        QString error;
        QVERIFY2(reference.rasterize(mapping,alpha,frame,expected,error),qPrintable(error));
        QVERIFY2(prepared.rasterize(mapping,alpha,frame,actual,error),qPrintable(error));
        QCOMPARE(firstDifference(expected,actual,output.width(),"alpha 0"),QString());
        for(float v:actual)QCOMPARE(bits(v),bits(0.0f));
    }
    {
        // One opaque pixel inside the disc must stay exactly at the reference value.
        const QPointF center=OutputSourceMapping(source,geometry).map(QPointF(.5,.5));
        const QImage alpha=alphaFrame(output,0.0f);
        QVector<float> expected,actual;
        QString error;
        QVERIFY2(reference.rasterize(mapping,alpha,frame,expected,error),qPrintable(error));
        QVERIFY2(prepared.rasterize(mapping,alpha,frame,actual,error),qPrintable(error));
        QCOMPARE(firstDifference(expected,actual,output.width(),"alpha 0 uniform"),QString());
        QCOMPARE(bits(prepared.sample(center,1.0f)),bits(prepared.sample(center,0.25f)));
        QVERIFY(bits(prepared.sample(center,1.0f))!=bits(0.0f));
    }
    {
        // Full alpha compared against a partial alpha multiplied by nothing: the
        // contract forbids multiplying coverage by alpha a second time.
        const QImage opaque=alphaFrame(output,1.0f);
        const QImage partial=alphaFrame(output,0.25f);
        QVector<float> expected,actual;
        QString error;
        QVERIFY2(reference.rasterize(mapping,partial,frame,expected,error),qPrintable(error));
        QVERIFY2(prepared.rasterize(mapping,opaque,frame,actual,error),qPrintable(error));
        QCOMPARE(firstDifference(expected,actual,output.width(),"alpha .25"),QString());
    }
    {
        // NaN and Inf alpha are errors and publish no partial tile.
        const float poisons[2]={nan,inf};
        for(float poison:poisons){
            const QImage alpha=alphaFrameWith(output,1.0f,{{QPoint(5,5),poison}});
            QVector<float> expected=sentinel(qsizetype(frame.width())*frame.height());
            QString error;
            QVERIFY2(!reference.rasterize(mapping,alpha,frame,expected,error),qPrintable("reference accepted poison alpha"));
            QVERIFY(!error.isEmpty());
            QVERIFY(allSentinel(expected));
            QVector<float> actual=sentinel(qsizetype(frame.width())*frame.height());
            error.clear();
            QVERIFY2(!prepared.rasterize(mapping,alpha,frame,actual,error),qPrintable("prepared accepted poison alpha"));
            QVERIFY(!error.isEmpty());
            QVERIFY(allSentinel(actual));
        }
    }
    {
        // Empty mask: zero coverage, identical bit patterns, no failure.
        Mask empty;
        QVERIFY(empty.components.isEmpty());
        QVERIFY(!empty.invert);
        QCOMPARE(empty.feather,0.0);
        BrushMaskReference referenceEmpty(empty);
        PreparedBrushMask preparedEmpty(empty);
        QVERIFY2(referenceEmpty.valid(),qPrintable(referenceEmpty.error()));
        QVERIFY2(preparedEmpty.valid(),qPrintable(preparedEmpty.error()));
        QCOMPARE(bits(preparedEmpty.sample(QPointF(32,24),1.0f)),bits(0.0f));
        QCOMPARE(bits(preparedEmpty.sample(QPointF(32,24),1.0f)),
                 bits(referenceEmpty.sample(QPointF(32,24),1.0f)));
        const QImage alpha=alphaFrame(output,1.0f);
        QVector<float> expected,actual;
        QString error;
        QVERIFY2(referenceEmpty.rasterize(mapping,alpha,frame,expected,error),qPrintable(error));
        QVERIFY2(preparedEmpty.rasterize(mapping,alpha,frame,actual,error),qPrintable(error));
        QCOMPARE(firstDifference(expected,actual,output.width(),"empty mask"),QString());
        for(float v:actual)QCOMPARE(bits(v),bits(0.0f));
    }
    {
        // C3 gradients are supported; whole-mask feather/invert remain C4.
        Mask unsupported;
        unsupported.components.append(linearComponent(QPointF(10,10),QPointF(50,40),0.5,MaskOperation::Add));
        unsupported.components.append(radialComponent(QPointF(30,20),12.0,9.0,15.0,0.7,MaskOperation::Subtract));
        QCOMPARE(unsupported.components.size(),2);
        QVERIFY(!unsupported.components[0].id.isEmpty());
        QVERIFY(unsupported.components[0].id!=unsupported.components[1].id);
        BrushMaskReference referenceUnsupported(unsupported);
        PreparedBrushMask preparedUnsupported(unsupported);
        QVERIFY(referenceUnsupported.valid());
        QVERIFY(referenceUnsupported.error().isEmpty());
        QVERIFY(preparedUnsupported.valid());
        QVERIFY(preparedUnsupported.error().isEmpty());
        QCOMPARE(preparedUnsupported.sample(QPointF(30,20),1.0f),referenceUnsupported.sample(QPointF(30,20),1.0f));

        Mask feathered=transparent;
        feathered.feather=0.2;
        QVERIFY(!feathered.invert);
        BrushMaskReference referenceFeather(feathered);
        PreparedBrushMask preparedFeather(feathered);
        QVERIFY(!referenceFeather.valid());
        QVERIFY(!preparedFeather.valid());
        QVERIFY(!referenceFeather.error().isEmpty());
        QVERIFY(!preparedFeather.error().isEmpty());
        QVERIFY(std::isnan(preparedFeather.sample(QPointF(32,24),1.0f)));

        Mask inverted=transparent;
        inverted.invert=true;
        QCOMPARE(inverted.feather,0.0);
        BrushMaskReference referenceInvert(inverted);
        PreparedBrushMask preparedInvert(inverted);
        QVERIFY(!referenceInvert.valid());
        QVERIFY(!preparedInvert.valid());
        QVERIFY(!preparedInvert.error().isEmpty());
        QVERIFY(std::isnan(preparedInvert.sample(QPointF(32,24),1.0f)));

        // An invalid prepared record must also refuse to rasterize.
        QVector<float> guarded=sentinel(qsizetype(frame.width())*frame.height());
        QString error;
        const auto before=guarded;
        QVERIFY(!preparedFeather.rasterize(mapping,alphaFrame(output,1.0f),frame,guarded,error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(guarded.size(),before.size());
        QVERIFY(std::memcmp(guarded.constData(),before.constData(),size_t(before.size())*sizeof(float))==0);
    }
}
// Two strokes of 50 points near the centre, source 1026x514, output 513x257,
// full geometry, chroma aberration and distortion. The reference resolves the
// whole frame; the prepared index replays 256 and 512 tiles. Bit patterns must
// agree everywhere; on failure only the smallest coordinate is reported.
void LocalMaskTests::tileReplayMatchesFullFrame(){
    QRandomGenerator rng(0x713e2026u);
    const QSize source(1026,514);
    const QSize output(513,257);

    Mask mask;
    mask.components.append(brush(denseStroke(rng,QPointF(512,258),12.0,50),
        60.0,0.35,MaskOperation::Add,0.85));
    mask.components.append(brush(denseStroke(rng,QPointF(513,257),12.0,50),
        45.0,0.75,MaskOperation::Subtract,0.6));
    QCOMPARE(mask.components.size(),2);
    QVERIFY(mask.components[0].id!=mask.components[1].id);
    QVERIFY(!mask.invert);
    QCOMPARE(mask.feather,0.0);

    Recipe geometry;
    geometry.crop=QRectF(.1,.1,.8,.8);
    geometry.angle=13.0;
    geometry.flipHorizontal=true;
    geometry.flipVertical=true;
    geometry.perspective=QPointF(.1,-.1);
    geometry.distortion=.05;
    geometry.chromaticAberration=QPointF(2,-2);
    QVERIFY(validRecipe(geometry));

    const MaskMapping mapping=mappingFor(source,output,geometry);
    const QImage alpha=alphaFrame(output,1.0f);

    BrushMaskReference reference(mask);
    QVERIFY2(reference.valid(),qPrintable(reference.error()));
    PreparedBrushMask prepared(mask);
    QVERIFY2(prepared.valid(),qPrintable(prepared.error()));

    QVector<float> full;
    QString error;
    QVERIFY2(reference.rasterize(mapping,alpha,QRect(QPoint(0,0),output),full,error),qPrintable(error));
    QCOMPARE(full.size(),qsizetype(output.width())*output.height());

    bool sawCoverage=false;
    for(int tile:{kTile,kMaxTile}){
        QVector<float> assembled(qsizetype(output.width())*output.height(),
            std::numeric_limits<float>::quiet_NaN());
        for(int ty=0;ty<output.height();ty+=tile){
            for(int tx=0;tx<output.width();tx+=tile){
                const QRect rect(tx,ty,std::min(tile,output.width()-tx),
                                 std::min(tile,output.height()-ty));
                QVector<float> piece;
                QVERIFY2(prepared.rasterize(mapping,alpha,rect,piece,error),qPrintable(error));
                QCOMPARE(piece.size(),qsizetype(rect.width())*rect.height());
                for(int y=0;y<rect.height();++y)for(int x=0;x<rect.width();++x)
                    assembled[qsizetype(rect.y()+y)*output.width()+(rect.x()+x)]=
                        piece[qsizetype(y)*rect.width()+x];
            }
        }
        for(qsizetype i=0;i<assembled.size();++i){
            if(bits(assembled[i])!=bits(full[i]))
                QFAIL(qPrintable(QStringLiteral("tile %1 mismatch at (%2,%3)")
                    .arg(tile).arg(i%output.width()).arg(i/output.width())));
            if(full[i]>0.0f)sawCoverage=true;
        }
    }
    QVERIFY(sawCoverage);

    // A 512 tile next to the reference on the same coordinates, and the shared
    // public map agreeing with the cached map on ordinary values.
    const QRect wide(0,0,std::min(kMaxTile,output.width()),std::min(kMaxTile,output.height()));
    QVector<float> wideTile;
    QVERIFY2(prepared.rasterize(mapping,alpha,wide,wideTile,error),qPrintable(error));
    for(int y=0;y<wide.height();++y)for(int x=0;x<wide.width();++x)
        QCOMPARE(bits(wideTile[qsizetype(y)*wide.width()+x]),
                 bits(full[qsizetype(y)*output.width()+x]));
}
// A tile larger than 512 must be refused without publishing partial output, and
// the same must hold for a cancelled token.
void LocalMaskTests::rasterizeRejectsOverlargeTileAndBadAlpha(){
    const QSize source(1200,1040);
    const QSize output(600,520);
    Mask mask;
    mask.components.append(brush({QPointF(600,450),QPointF(650,470)},120.0,0.4,MaskOperation::Add,0.9));
    QVERIFY(mask.feather==0.0&&!mask.invert);

    BrushMaskReference reference(mask);
    PreparedBrushMask prepared(mask);
    QVERIFY2(reference.valid(),qPrintable(reference.error()));
    QVERIFY2(prepared.valid(),qPrintable(prepared.error()));

    const Recipe geometry;
    const MaskMapping mapping=mappingFor(source,output,geometry);
    const QImage alpha=alphaFrame(output,1.0f);
    const qsizetype count=qsizetype(kMaxTile+1)*qsizetype(kMaxTile+1);

    {
        const QRect tooLarge(0,0,kMaxTile+1,kMaxTile+1);
        QVector<float> values=sentinel(count);
        QString error;
        QVERIFY(!prepared.rasterize(mapping,alpha,tooLarge,values,error));
        QVERIFY(!error.isEmpty());
        QVERIFY(allSentinel(values));
    }
    {
        const QRect tooWide(0,0,kMaxTile+1,4);
        QVector<float> values=sentinel(qsizetype(tooWide.width())*tooWide.height());
        QString error;
        QVERIFY(!prepared.rasterize(mapping,alpha,tooWide,values,error));
        QVERIFY(!error.isEmpty());
        QVERIFY(allSentinel(values));
    }
    {
        // Exactly 512 is still accepted, so the boundary is the size and not one less.
        const QRect limit(0,0,kMaxTile,kMaxTile);
        QVector<float> values=sentinel(qsizetype(limit.width())*limit.height());
        QString error;
        QVERIFY2(prepared.rasterize(mapping,alpha,limit,values,error),qPrintable(error));
        bool any=false;
        for(float v:values)if(v>0.0f)any=true;
        QVERIFY(any);
    }
    {
        // The reference implementation has no tile ceiling, so the refusal above
        // is a prepared-layer guard rather than a shared geometry failure.
        const QRect over(0,0,600,450);
        QVector<float> values;
        QString error;
        QVERIFY2(reference.rasterize(mapping,alpha,over,values,error),qPrintable(error));
        QCOMPARE(values.size(),qsizetype(600)*450);
    }
    {
        const float nan=std::numeric_limits<float>::quiet_NaN();
        const QImage poisoned=alphaFrameWith(output,1.0f,{{QPoint(7,9),nan}});
        QVector<float> values=sentinel(qsizetype(kMaxTile)*kMaxTile);
        QString error;
        QVERIFY(!prepared.rasterize(mapping,poisoned,QRect(0,0,kMaxTile,kMaxTile),values,error));
        QVERIFY(!error.isEmpty());
        QVERIFY(allSentinel(values));
    }
}

// A pre-cancelled token must abort before publishing: the caller's sentinel
// buffer stays untouched rather than receiving a partial frame.
void LocalMaskTests::rasterizeCancellationKeepsSentinel(){
    const QSize source(800,600);
    const QSize output(400,300);
    Mask mask;
    mask.components.append(brush({QPointF(400,300),QPointF(430,320)},90.0,0.5,MaskOperation::Add,1.0));
    PreparedBrushMask prepared(mask);
    QVERIFY2(prepared.valid(),qPrintable(prepared.error()));

    const Recipe geometry;
    const MaskMapping mapping=mappingFor(source,output,geometry);
    const QImage alpha=alphaFrame(output,1.0f);
    const QRect rect(0,0,256,256);

    QVector<float> values=sentinel(qsizetype(rect.width())*rect.height());
    CancellationSource cancelled;
    cancelled.cancel();
    QVERIFY(isCanceled(cancelled.token()));
    QString error;
    QVERIFY2(!prepared.rasterize(mapping,alpha,rect,values,error,cancelled.token()),
        qPrintable("cancelled rasterize reported success"));
    QVERIFY(!error.isEmpty());
    QVERIFY(allSentinel(values));

    // The live token still yields real coverage, so the refusal above is cancellation.
    CancellationSource live;
    QVERIFY(!isCanceled(live.token()));
    QVector<float> live4=sentinel(qsizetype(rect.width())*rect.height());
    error.clear();
    QVERIFY2(prepared.rasterize(mapping,alpha,rect,live4,error,live.token()),qPrintable(error));
    bool any=false;
    for(float v:live4)if(v>0.0f)any=true;
    QVERIFY(any);
}
// Also retain the previous free-function expression as an independent anchor:
// comparing only the two new public entry points would be tautological.
void LocalMaskTests::outputSourceMappingMatchesPublicFunction(){
    const QSize source(1026,514);
    Recipe geometry;
    geometry.crop=QRectF(.1,.1,.8,.8);
    geometry.angle=13.0;
    geometry.flipHorizontal=true;
    geometry.flipVertical=true;
    geometry.perspective=QPointF(.1,-.1);
    geometry.distortion=.05;
    geometry.chromaticAberration=QPointF(2,-2);
    QVERIFY(validRecipe(geometry));

    const OutputSourceMapping cached(source,geometry);
    const double tolerance=1e-9;
    for(int i=0;i<=20;++i)for(int j=0;j<=20;++j){
        const QPointF uv(i/20.0,j/20.0);
        const QPointF mapped=cached.map(uv);
        const QPointF direct=outputToSource(uv,source,geometry);
        QVERIFY2(std::abs(mapped.x()-direct.x())<=tolerance&&
                 std::abs(mapped.y()-direct.y())<=tolerance,
            qPrintable(QStringLiteral("map %1 disagrees: %2 vs %3")
                .arg(describe(uv),describe(mapped),describe(direct))));
        auto legacyUv=uv;
        if(geometry.flipHorizontal)legacyUv.setX(1-legacyUv.x());
        if(geometry.flipVertical)legacyUv.setY(1-legacyUv.y());
        const double cx=(geometry.crop.x()+geometry.crop.width()/2)*source.width();
        const double cy=(geometry.crop.y()+geometry.crop.height()/2)*source.height();
        const double dx=(legacyUv.x()-.5)*geometry.crop.width()*source.width();
        const double dy=(legacyUv.y()-.5)*geometry.crop.height()*source.height();
        const double radians=geometry.angle*3.14159265358979323846/180;
        const auto legacy=geometrySource({cx+dx*std::cos(radians)+dy*std::sin(radians),
            cy-dx*std::sin(radians)+dy*std::cos(radians)},source,geometry);
        QCOMPARE(mapped,legacy);
    }

    // Corners beyond the crop are still finite for a valid recipe.
    const QPointF corner=cached.map(QPointF(0.0,1.0));
    QVERIFY(std::isfinite(corner.x())&&std::isfinite(corner.y()));
}

QTEST_APPLESS_MAIN(LocalMaskTests)
#include "local_mask_tests.moc"
