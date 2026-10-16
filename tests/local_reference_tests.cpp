#include <QtTest>
#include <QImage>
#include <QRect>
#include <QPointF>
#include <QSize>
#include <QVector>
#include <QString>
#include <cmath>
#include <limits>
#include <algorithm>
#include "core/document.h"
#include "core/photo_document.h"
#include "core/cancellation.h"
#include "render/local_reference.h"
#include "render/cpu_renderer.h"

// B1 reference-algorithm test draft. Expectations are hardcoded independent numbers
// from docs/engineering/S3-B-NUMERICAL-CONTRACT.md plus the existing public CPU
// renderer as the only oracle. No algorithm is reimplemented here.
namespace {
constexpr double kTol=1e-8;          // source-coordinate sampler tolerance
constexpr double kMaskTol=1e-6;      // resolved scalar mask tolerance
constexpr float  kRgbTol=1e-5f;      // absolute RGB floor
constexpr float  kRgbAbs=1e-4f;      // relative RGB coefficient

bool samePoint(const QPointF &a,const QPointF &b,double tol=kTol){
    return std::abs(a.x()-b.x())<=tol&&std::abs(a.y()-b.y())<=tol;
}

// The 101-point reference produced by a length-100 line at spacing 1 with the
// frozen endpoint appended once. Independent closed form, not the sampler.
QVector<QPointF> referenceLine101(){
    QVector<QPointF> expected;
    for(int i=0;i<=100;++i)expected.append(QPointF(10.5+i*1.0,20.5));
    return expected;
}

LocalEdit::MaskComponent brushComponent(const QVector<QPointF> &points,double radius,
                                        double hardness,LocalEdit::MaskOperation op,
                                        double strength){
    LocalEdit::MaskComponent c;
    static quint64 sequence=0;
    c.id=QStringLiteral("component-%1").arg(++sequence);
    c.operation=op;
    c.strength=strength;
    LocalEdit::BrushGeometry g;
    g.points=points;
    g.radius=radius;
    g.hardness=hardness;
    c.geometry=g;
    return c;
}

// RGBA32FPx4 frame filled with a constant straight-alpha colour.
QImage constantAlphaFrame(QSize size,float r,float g,float b,float a){
    QImage image(size,QImage::Format_RGBA32FPx4);
    for(int y=0;y<size.height();++y){
        float *line=reinterpret_cast<float*>(image.scanLine(y));
        for(int x=0;x<size.width();++x){
            line[x*4+0]=r;line[x*4+1]=g;line[x*4+2]=b;line[x*4+3]=a;
        }
    }
    return image;
}

QVector<float> sentinelOutput(int count){
    QVector<float> out(count,std::numeric_limits<float>::quiet_NaN());
    return out;
}
}

class LocalReferenceTests:public QObject {
    Q_OBJECT
private slots:
    void samplerSpacingAndEventDensity();
    void samplerRejectsInvalidInput();
    void maskScalarFalloffAndAccumulation();
    void maskAlphaCoverageAndUnsupported();
    void rasterizeAlphaAndHardLimit();
    void rasterizeTileMatchesFull();
    void rasterizeGeometryTileMatchesFull();
    void rasterizeFailureKeepsSentinel();
    void pixelTransformIdentityAndCoverage();
    void pixelTransformSaturationAndHdr();
    void pixelTransformRejectsInvalid();
    void nineFieldMatchesLinearRenderer();
};

// Radius 8 -> spacing 1. Start (10.5,20.5), end (110.5,20.5): one long line and
// 50 equal events with repeated dwell must yield the identical 101 points.
void LocalReferenceTests::samplerSpacingAndEventDensity(){
    const QVector<QPointF> expected=referenceLine101();

    LocalEdit::BrushSampler single(8.0,1.0);
    QVERIFY(single.valid());
    QString error;
    QVERIFY2(single.append(QPointF(10.5,20.5),error),qPrintable(error));
    QVERIFY2(single.append(QPointF(110.5,20.5),error),qPrintable(error));
    const LocalEdit::BrushGeometry a=single.snapshot();
    QCOMPARE(a.points.size(),expected.size());
    QCOMPARE(a.points.size(),101);
    for(int i=0;i<expected.size();++i)
        QVERIFY2(samePoint(a.points[i],expected[i]),qPrintable(QStringLiteral("point %1").arg(i)));

    LocalEdit::BrushSampler dense(8.0,1.0);
    QVERIFY(dense.valid());
    QVERIFY2(dense.append(QPointF(10.5,20.5),error),qPrintable(error));
    for(int i=1;i<=50;++i){
        const double x=10.5+(110.5-10.5)*i/50.0;
        QVERIFY2(dense.append(QPointF(x,20.5),error),qPrintable(error));
    }
    // Dwell events at the frozen endpoint add nothing.
    for(int i=0;i<10;++i)QVERIFY2(dense.append(QPointF(110.5,20.5),error),qPrintable(error));
    const LocalEdit::BrushGeometry b=dense.snapshot();
    QCOMPARE(b.points.size(),101);
    for(int i=0;i<expected.size();++i)
        QVERIFY2(samePoint(b.points[i],expected[i]),qPrintable(QStringLiteral("dense %1").arg(i)));

    // Endpoint is contributed exactly once: pressing at start then releasing adds no duplicate.
    LocalEdit::BrushSampler stationary(8.0,1.0);
    QVERIFY(stationary.append(QPointF(10.5,20.5),error));
    for(int i=0;i<5;++i)QVERIFY(stationary.append(QPointF(10.5,20.5),error));
    QCOMPARE(stationary.snapshot().points.size(),1);
}

void LocalReferenceTests::samplerRejectsInvalidInput(){
    QString error;
    LocalEdit::BrushSampler sampler(8.0,1.0);
    QVERIFY(sampler.append(QPointF(10.5,20.5),error));
    const int before=sampler.snapshot().points.size();

    error.clear();
    QVERIFY(!sampler.append(QPointF(std::numeric_limits<double>::quiet_NaN(),20.5),error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(sampler.snapshot().points.size(),before);

    error.clear();
    QVERIFY(!sampler.append(QPointF(10.5,std::numeric_limits<double>::infinity()),error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(sampler.snapshot().points.size(),before);

    // Zero radius is an invalid frozen setting, not a silent fallback.
    QString zeroError;
    LocalEdit::BrushSampler zero(0.0,1.0);
    QVERIFY(!zero.valid());
    QVERIFY(!zero.append(QPointF(10.5,20.5),zeroError));

    // Point budget: exceeding maximumPointsPerStroke must fail and keep prior state.
    LocalEdit::BrushSampler budget(8.0,1.0);
    QVERIFY(budget.append(QPointF(0.0,0.0),error));
    const int budgetBefore=budget.snapshot().points.size();
    const double far=double(LocalEdit::maximumPointsPerStroke)*8.0+16.0;
    error.clear();
    QVERIFY(!budget.append(QPointF(far,0.0),error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(budget.snapshot().points.size(),budgetBefore);
}

// One stroke: center (100,100), R=10, hardness .5, strength .5.
// d<=5 ->1, d>=10 ->0, geometric falloff between; single-stroke max then *strength.
void LocalReferenceTests::maskScalarFalloffAndAccumulation(){
    LocalEdit::Mask mask;
    mask.components.append(brushComponent({QPointF(100,100)},10.0,.5,
        LocalEdit::MaskOperation::Add,.5));
    LocalEdit::BrushMaskReference reference(mask);
    QVERIFY2(reference.valid(),qPrintable(reference.error()));

    QCOMPARE(double(reference.sample(QPointF(100,100))),.5);
    // d=7.5 -> t=(7.5-5)/5=.5 -> w=(1-.5)^2*(1+1)=.5 ; *strength .5 = .25
    QVERIFY(std::abs(double(reference.sample(QPointF(107.5,100)))-.25)<=kMaskTol);
    QCOMPARE(double(reference.sample(QPointF(110,100))),0.0);
    // Outside the disc stays zero.
    QCOMPARE(double(reference.sample(QPointF(100,120))),0.0);
    // Same-stroke repetition never accumulates.
    QCOMPARE(double(reference.sample(QPointF(100,100))),.5);

    auto repeated=mask;
    std::get<LocalEdit::BrushGeometry>(repeated.components[0].geometry).points.append({100,100});
    LocalEdit::BrushMaskReference duplicateCenters(repeated);
    QVERIFY(duplicateCenters.valid());
    QCOMPARE(duplicateCenters.sample({100,100}),.5f);

    // Add .5 then Add .5 -> .75 ; ordering matters.
    LocalEdit::Mask twoAdd;
    twoAdd.components.append(brushComponent({QPointF(100,100)},10.0,1.0,LocalEdit::MaskOperation::Add,.5));
    twoAdd.components.append(brushComponent({QPointF(100,100)},10.0,1.0,LocalEdit::MaskOperation::Add,.5));
    LocalEdit::BrushMaskReference addReference(twoAdd);
    QVERIFY(addReference.valid());
    QVERIFY(std::abs(double(addReference.sample(QPointF(100,100)))-.75)<=kMaskTol);

    // Add .5 then Subtract .5 -> .25 ; reversed order -> .5
    LocalEdit::Mask addSubtract=twoAdd;
    addSubtract.components[1].operation=LocalEdit::MaskOperation::Subtract;
    LocalEdit::BrushMaskReference subtractReference(addSubtract);
    QVERIFY(subtractReference.valid());
    QVERIFY(std::abs(double(subtractReference.sample(QPointF(100,100)))-.25)<=kMaskTol);

    LocalEdit::Mask reversed;
    reversed.components.append(addSubtract.components[1]);
    reversed.components.append(addSubtract.components[0]);
    LocalEdit::BrushMaskReference reversedReference(reversed);
    QVERIFY(reversedReference.valid());
    QVERIFY(std::abs(double(reversedReference.sample(QPointF(100,100)))-.5)<=kMaskTol);
}

void LocalReferenceTests::maskAlphaCoverageAndUnsupported(){
    LocalEdit::Mask mask;
    mask.components.append(brushComponent({QPointF(100,100)},10.0,1.0,LocalEdit::MaskOperation::Add,1.0));
    LocalEdit::BrushMaskReference reference(mask);
    QVERIFY(reference.valid());

    QCOMPARE(double(reference.sample(QPointF(100,100),0.0f)),0.0);       // alpha 0 removes coverage
    QCOMPARE(double(reference.sample(QPointF(100,100),.25f)),1.0);       // partial alpha is not re-multiplied
    QCOMPARE(double(reference.sample(QPointF(100,100),1.0f)),1.0);

    // NaN alpha is an invalid sample and reported as NaN.
    QVERIFY(std::isnan(double(reference.sample(QPointF(100,100),
        std::numeric_limits<float>::quiet_NaN()))));

    // C3 supports well-formed gradient nodes, while malformed/spatial nodes
    // still fail explicitly instead of silently producing an empty mask.
    LocalEdit::Mask linear;
    LocalEdit::MaskComponent linearComponent;
    linearComponent.id="linear";
    linearComponent.geometry=LocalEdit::LinearGeometry{QPointF(0,0),QPointF(1,0)};
    linear.components.append(linearComponent);
    LocalEdit::BrushMaskReference linearReference(linear);
    QVERIFY(linearReference.valid());
    QCOMPARE(linearReference.sample({.5,0}),.5f);
    auto degenerate=linear;degenerate.components[0].geometry=LocalEdit::LinearGeometry{{0,0},{0,0}};
    QVERIFY(!LocalEdit::BrushMaskReference(degenerate).valid());

    LocalEdit::Mask radial;
    LocalEdit::MaskComponent radialComponent;
    radialComponent.id="radial";
    radialComponent.geometry=LocalEdit::RadialGeometry{};
    radial.components.append(radialComponent);
    QVERIFY(LocalEdit::BrushMaskReference(radial).valid());
    QCOMPARE(LocalEdit::BrushMaskReference(radial).sample({0,0}),1.f);

    LocalEdit::Mask feathered=mask;
    feathered.feather=.5;
    QVERIFY(!LocalEdit::BrushMaskReference(feathered).valid());

    LocalEdit::Mask inverted=mask;
    inverted.invert=true;
    QVERIFY(!LocalEdit::BrushMaskReference(inverted).valid());
}

// rasterize consumes RGBA32FPx4 alpha; output rect uses global output coordinates.
void LocalReferenceTests::rasterizeAlphaAndHardLimit(){
    LocalEdit::Mask mask;
    mask.components.append(brushComponent({QPointF(1,1)},0.1,1.0,LocalEdit::MaskOperation::Add,1.0));
    LocalEdit::BrushMaskReference reference(mask);
    QVERIFY2(reference.valid(),qPrintable(reference.error()));
    QCOMPARE(mask.feather,0.0);
    QVERIFY(!mask.invert);

    LocalEdit::MaskMapping mapping;
    mapping.sourceSize=QSize(34,26);
    mapping.outputSize=QSize(17,13);
    mapping.geometry.crop=QRectF(0,0,1,1);

    const QImage alpha=constantAlphaFrame(mapping.outputSize,0,0,0,1.0f);
    QCOMPARE(alpha.format(),QImage::Format_RGBA32FPx4);

    QVector<float> output;
    QString error;
    QVERIFY2(reference.rasterize(mapping,alpha,QRect(QPoint(0,0),mapping.outputSize),output,error),
        qPrintable(error));
    QCOMPARE(output.size(),17*13);
    QCOMPARE(double(output[0]),1.0);
    for(int i=1;i<output.size();++i)QCOMPARE(double(output[i]),0.0);

    // flipH: known correspondence source (33,1) drives output (0,0).
    LocalEdit::MaskMapping flipped=mapping;
    flipped.geometry.flipHorizontal=true;
    auto flippedMask=mask;
    std::get<LocalEdit::BrushGeometry>(flippedMask.components[0].geometry).points={{33,1}};
    LocalEdit::BrushMaskReference flippedReference(flippedMask);
    QVector<float> flippedOutput;
    QVERIFY2(flippedReference.rasterize(flipped,alpha,QRect(QPoint(0,0),flipped.outputSize),
        flippedOutput,error),qPrintable(error));
    QCOMPARE(flippedOutput.size(),17*13);
    QCOMPARE(double(flippedOutput[0]),1.0);
    for(int i=1;i<flippedOutput.size();++i)QCOMPARE(double(flippedOutput[i]),0.0);
}

// Tile replay must equal the full-frame result bit-for-bit at the same output
// coordinates, via shared outputToSource. 513x257 output from a 1026x514 source.
void LocalReferenceTests::rasterizeTileMatchesFull(){
    const QSize source(1026,514);
    const QSize output(513,257);
    LocalEdit::Mask mask;
    mask.components.append(brushComponent({QPointF(512,258)},100.0,.5,
        LocalEdit::MaskOperation::Add,1.0));
    LocalEdit::BrushMaskReference reference(mask);
    QVERIFY2(reference.valid(),qPrintable(reference.error()));

    LocalEdit::MaskMapping mapping;
    mapping.sourceSize=source;
    mapping.outputSize=output;
    mapping.geometry.crop=QRectF(0,0,1,1);

    const QImage alpha=constantAlphaFrame(output,0,0,0,1.0f);
    QVector<float> full;
    QString error;
    QVERIFY2(reference.rasterize(mapping,alpha,QRect(QPoint(0,0),output),full,error),
        qPrintable(error));
    QCOMPARE(full.size(),output.width()*output.height());

    // The shared public mapping is the oracle for the untiled probe, so this test
    // never restates a geometry formula of its own.
    const QPointF center=outputToSource(QPointF(512./source.width(),258./source.height()),source,mapping.geometry);
    QCOMPARE(double(reference.sample(center)),1.0);

    const int tile=256;
    for(int ty=0;ty<output.height();ty+=tile){
        for(int tx=0;tx<output.width();tx+=tile){
            const QRect rect(tx,ty,std::min(tile,output.width()-tx),
                             std::min(tile,output.height()-ty));
            QVector<float> piece;
            QVERIFY2(reference.rasterize(mapping,alpha,rect,piece,error),qPrintable(error));
            QCOMPARE(piece.size(),rect.width()*rect.height());
            for(int y=0;y<rect.height();++y)for(int x=0;x<rect.width();++x){
                const int f=(rect.y()+y)*output.width()+(rect.x()+x);
                const float a=full[f],b=piece[y*rect.width()+x];
                // Bitwise equality is required for identical coordinates.
                QVERIFY2(a==b,qPrintable(QStringLiteral("mismatch %1,%2").arg(f).arg(a-b)));
            }
        }
    }

    // A spatial probe independent of the tiling loop.
    QCOMPARE(double(reference.sample(QPointF(512,258))),1.0);
    QCOMPARE(double(reference.sample(QPointF(512,258)+QPointF(100,0))),0.0);
}

// Same tiling contract once geometry is non-trivial; geometry itself is never re-derived.
void LocalReferenceTests::rasterizeGeometryTileMatchesFull(){
    const QSize source(1026,514);
    const QSize output(513,257);
    LocalEdit::Mask mask;
    mask.components.append(brushComponent({QPointF(512,258)},100.0,.5,
        LocalEdit::MaskOperation::Add,1.0));
    LocalEdit::BrushMaskReference reference(mask);
    QVERIFY(reference.valid());

    LocalEdit::MaskMapping mapping;
    mapping.sourceSize=source;
    mapping.outputSize=output;
    mapping.geometry.crop=QRectF(.1,.1,.8,.8);
    mapping.geometry.angle=10.0;
    mapping.geometry.flipHorizontal=true;
    mapping.geometry.flipVertical=true;
    mapping.geometry.perspective=QPointF(.1,-.1);
    mapping.geometry.distortion=.1;
    mapping.geometry.chromaticAberration=QPointF(2,-2);
    QVERIFY(validRecipe(mapping.geometry));

    const QImage alpha=constantAlphaFrame(output,0,0,0,1.0f);
    QVector<float> full;
    QString error;
    QVERIFY2(reference.rasterize(mapping,alpha,QRect(QPoint(0,0),output),full,error),
        qPrintable(error));

    const int tile=256;
    for(int ty=0;ty<output.height();ty+=tile){
        for(int tx=0;tx<output.width();tx+=tile){
            const QRect rect(tx,ty,std::min(tile,output.width()-tx),
                             std::min(tile,output.height()-ty));
            QVector<float> piece;
            QVERIFY2(reference.rasterize(mapping,alpha,rect,piece,error),qPrintable(error));
            for(int y=0;y<rect.height();++y)for(int x=0;x<rect.width();++x){
                const int f=(rect.y()+y)*output.width()+(rect.x()+x);
                QVERIFY(full[f]==piece[y*rect.width()+x]);
            }
        }
    }
}

void LocalReferenceTests::rasterizeFailureKeepsSentinel(){
    LocalEdit::Mask mask;
    mask.components.append(brushComponent({QPointF(1,1)},0.1,1.0,LocalEdit::MaskOperation::Add,1.0));
    LocalEdit::BrushMaskReference reference(mask);
    QVERIFY(reference.valid());

    LocalEdit::MaskMapping mapping;
    mapping.sourceSize=QSize(34,26);
    mapping.outputSize=QSize(17,13);
    mapping.geometry.crop=QRectF(0,0,1,1);
    const QRect rect(QPoint(0,0),mapping.outputSize);

    const float nan=std::numeric_limits<float>::quiet_NaN();

    // Cancelled before work starts: no partial publication.
    {
        const QImage alpha=constantAlphaFrame(mapping.outputSize,0,0,0,1.0f);
        CancellationSource source;
        source.cancel();
        QVector<float> output=sentinelOutput(rect.width()*rect.height());
        QString error;
        QVERIFY(!reference.rasterize(mapping,alpha,rect,output,error,source.token()));
        QVERIFY(!error.isEmpty());
        for(float value:output)QVERIFY(std::isnan(value));
    }

    // NaN alpha anywhere in the consumed frame is an error, not a silent zero.
    {
        QImage alpha=constantAlphaFrame(mapping.outputSize,0,0,0,1.0f);
        float *line=reinterpret_cast<float*>(alpha.scanLine(4));
        line[4*4+3]=nan;
        QVector<float> output=sentinelOutput(rect.width()*rect.height());
        QString error;
        QVERIFY(!reference.rasterize(mapping,alpha,rect,output,error));
        QVERIFY(!error.isEmpty());
        for(float value:output)QVERIFY(std::isnan(value));
    }
}

// Identity and the documented warm/tint/exposure composition, independent of impl.
void LocalReferenceTests::pixelTransformIdentityAndCoverage(){
    LocalEdit::LocalPixelTransform identity(LocalEdit::LocalParameters{});
    QVERIFY(identity.valid());
    QVERIFY(identity.identity());

    const float original[4]={-.2f,.1f,4.0f,.25f};
    float pixel[4]={original[0],original[1],original[2],original[3]};
    QVERIFY(identity.apply(pixel,1.0f,1.0f));
    for(int c=0;c<4;++c)QCOMPARE(pixel[c],original[c]);

    // EV1 with coverage .5 and opacity .4 -> w=.2 -> C=F+0.2*(A(F)-F); RGB scale 1.2.
    LocalEdit::LocalParameters parameters;
    parameters.exposure=1.0;
    LocalEdit::LocalPixelTransform transform(parameters);
    QVERIFY(transform.valid());
    QVERIFY(!transform.identity());

    float covered[4]={-.2f,.1f,4.0f,.25f};
    QVERIFY(transform.apply(covered,.5f,.4f));
    QVERIFY(std::abs(covered[0]-(-.24f))<=kRgbTol+kRgbAbs*.24f);
    QVERIFY(std::abs(covered[1]-.12f)<=kRgbTol+kRgbAbs*.12f);
    QVERIFY(std::abs(covered[2]-4.8f)<=kRgbTol+kRgbAbs*4.8f);
    QCOMPARE(covered[3],.25f);

    // Zero coverage, zero opacity and alpha 0 all leave RGB untouched.
    {
        float untouched[4]={-.2f,.1f,4.0f,.25f};
        QVERIFY(transform.apply(untouched,0.0f,1.0f));
        QCOMPARE(untouched[0],-.2f);
        QCOMPARE(untouched[2],4.0f);
    }
    {
        float untouched[4]={-.2f,.1f,4.0f,.25f};
        QVERIFY(transform.apply(untouched,1.0f,0.0f));
        QCOMPARE(untouched[0],-.2f);
        QCOMPARE(untouched[2],4.0f);
    }
    {
        float untouched[4]={-.2f,.1f,4.0f,.0f};
        QVERIFY(transform.apply(untouched,1.0f,1.0f));
        QCOMPARE(untouched[0],-.2f);
        QCOMPARE(untouched[2],4.0f);
        QCOMPARE(untouched[3],0.0f);
    }
}

// Rec.2020 luminance weights .2627/.6780/.0593 on primaries; HDR is not clipped.
void LocalReferenceTests::pixelTransformSaturationAndHdr(){
    LocalEdit::LocalParameters parameters;
    parameters.saturation=0.0;
    LocalEdit::LocalPixelTransform transform(parameters);
    QVERIFY(transform.valid());

    float red[4]={1,0,0,1};
    float green[4]={0,1,0,1};
    float blue[4]={0,0,1,1};
    QVERIFY(transform.apply(red,1.0f,1.0f));
    QVERIFY(transform.apply(green,1.0f,1.0f));
    QVERIFY(transform.apply(blue,1.0f,1.0f));
    QVERIFY(std::abs(red[0]-.2627f)<=kRgbTol+kRgbAbs*.2627f);
    QVERIFY(std::abs(green[0]-.6780f)<=kRgbTol+kRgbAbs*.6780f);
    QVERIFY(std::abs(blue[0]-.0593f)<=kRgbTol+kRgbAbs*.0593f);
    QCOMPARE(red[3],1.0f);
    QCOMPARE(green[3],1.0f);
    QCOMPARE(blue[3],1.0f);

    // Negative and super-white RGB survive exposure without clamping; alpha exact.
    LocalEdit::LocalParameters bright;
    bright.exposure=1.0;
    LocalEdit::LocalPixelTransform brightTransform(bright);
    float hdr[4]={-1.0f,4.0f,10.0f,.5f};
    QVERIFY(brightTransform.apply(hdr,1.0f,1.0f));
    QVERIFY(std::abs(hdr[0]-(-2.0f))<=kRgbAbs);
    QVERIFY(std::abs(hdr[1]-8.0f)<=kRgbAbs);
    QVERIFY(std::abs(hdr[2]-20.0f)<=kRgbAbs);
    QCOMPARE(hdr[3],.5f);
}

void LocalReferenceTests::pixelTransformRejectsInvalid(){
    LocalEdit::LocalParameters parameters;
    parameters.exposure=6.0; // outside the global [-5,5] bound
    LocalEdit::LocalPixelTransform transform(parameters);
    QVERIFY(!transform.valid());

    LocalEdit::LocalParameters nanParameters;
    nanParameters.contrast=std::numeric_limits<double>::quiet_NaN();
    LocalEdit::LocalPixelTransform nanTransform(nanParameters);
    QVERIFY(!nanTransform.valid());

    // A rejected pixel stays unchanged.
    const float before[4]={.1f,.2f,.3f,.4f};
    float pixel[4]={before[0],before[1],before[2],before[3]};
    QVERIFY(!transform.apply(pixel,1.0f,1.0f));
    for(int c=0;c<4;++c)QCOMPARE(pixel[c],before[c]);

    float pixel2[4]={before[0],before[1],before[2],before[3]};
    QVERIFY(!nanTransform.apply(pixel2,1.0f,1.0f));
    for(int c=0;c<4;++c)QCOMPARE(pixel2[c],before[c]);

    // Out-of-range coverage is invalid and must not mutate the pixel.
    LocalEdit::LocalParameters valid;
    LocalEdit::LocalPixelTransform good(valid);
    QVERIFY(good.valid());
    float pixel3[4]={before[0],before[1],before[2],before[3]};
    QVERIFY(!good.apply(pixel3,-0.5f,1.0f));
    for(int c=0;c<4;++c)QCOMPARE(pixel3[c],before[c]);

    // Non-finite pixel input cannot silently produce a finite result.
    float pixel4[4]={std::numeric_limits<float>::infinity(),.2f,.3f,.4f};
    QVERIFY(!good.apply(pixel4,1.0f,1.0f));
}

// Nine-field local recipe against the existing public linear renderer. Only identity
// bit-equality and strict alpha are required; RGB uses the contract tolerance.
void LocalReferenceTests::nineFieldMatchesLinearRenderer(){
    const int width=8,height=6;
    SourceFrame source;
    source.linear=QImage(width,height,QImage::Format_RGBA32FPx4);
    source.raw=false;
    source.colorNote=QStringLiteral("linear Rec2020");
    for(int y=0;y<height;++y){
        float *line=reinterpret_cast<float*>(source.linear.scanLine(y));
        for(int x=0;x<width;++x){
            line[x*4+0]=.2f+.05f*x;
            line[x*4+1]=.3f+.03f*y;
            line[x*4+2]=.4f;
            line[x*4+3]=1.0f;
        }
    }
    source.originalSize=source.linear.size();

    LocalEdit::LocalParameters parameters;
    parameters.exposure=.5;
    parameters.contrast=.1;
    parameters.warmth=.1;
    parameters.tint=-.05;
    parameters.saturation=1.2;
    parameters.highlights=.2;parameters.shadows=-.1;parameters.whites=.15;parameters.blacks=-.1;

    Recipe equivalent;
    equivalent.exposure=parameters.exposure;
    equivalent.contrast=parameters.contrast;
    equivalent.warmth=parameters.warmth;
    equivalent.tint=parameters.tint;
    equivalent.saturation=parameters.saturation;
    equivalent.highlights=parameters.highlights;equivalent.shadows=parameters.shadows;
    equivalent.whites=parameters.whites;equivalent.blacks=parameters.blacks;
    QVERIFY(validRecipe(equivalent));

    QString error;
    const QImage rendered=renderLinear(source,equivalent,0,error);
    QVERIFY2(!rendered.isNull(),qPrintable(error));
    QVERIFY2(error.isEmpty(),qPrintable(error));
    QCOMPARE(rendered.format(),QImage::Format_RGBA32FPx4);
    QCOMPARE(rendered.size(),source.linear.size());

    LocalEdit::LocalPixelTransform transform(parameters);
    QVERIFY(transform.valid());

    for(int y=0;y<height;++y){
        const float *reference=reinterpret_cast<const float*>(rendered.constScanLine(y));
        const float *linear=reinterpret_cast<const float*>(source.linear.constScanLine(y));
        for(int x=0;x<width;++x){
            float pixel[4]={linear[x*4+0],linear[x*4+1],linear[x*4+2],linear[x*4+3]};
            QVERIFY(transform.apply(pixel,1.0f,1.0f));
            for(int c=0;c<3;++c){
                const float tolerance=kRgbTol+kRgbAbs*std::abs(reference[x*4+c]);
                QVERIFY2(std::abs(pixel[c]-reference[x*4+c])<=tolerance,
                    qPrintable(QStringLiteral("rgb %1,%2 ch%3").arg(x).arg(y).arg(c)));
            }
            QCOMPARE(pixel[3],reference[x*4+3]);
        }
    }

    // Full coverage with alpha .25 is the documented partial-coverage comparison.
    for(int y=0;y<height;++y){
        const float *reference=reinterpret_cast<const float*>(rendered.constScanLine(y));
        const float *linear=reinterpret_cast<const float*>(source.linear.constScanLine(y));
        for(int x=0;x<width;++x){
            float pixel[4]={linear[x*4+0],linear[x*4+1],linear[x*4+2],.25f};
            QVERIFY(transform.apply(pixel,1.0f,1.0f));
            for(int c=0;c<3;++c){
                const float tolerance=kRgbTol+kRgbAbs*std::abs(reference[x*4+c]);
                QVERIFY(std::abs(pixel[c]-reference[x*4+c])<=tolerance);
            }
            QCOMPARE(pixel[3],.25f);
        }
    }
}

QTEST_APPLESS_MAIN(LocalReferenceTests)
#include "local_reference_tests.moc"
