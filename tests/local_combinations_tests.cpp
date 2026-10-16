#include <QtTest>
#include <bit>
#include <cmath>
#include <cstring>
#include "render/local_renderer.h"
#include "render/local_reference.h"
#include "render/local_preview_cache.h"
#include "render/vignette_operator.h"
#include "render/grain_operator.h"
#include "color/color_pipeline.h"
using namespace LocalEdit;
namespace {
SourceFrame fixture(bool raw){
    SourceFrame s;s.path="synthetic-c6";s.sha256=QString(64,'a');s.originalSize={37,29};s.raw=raw;
    s.cameraToWork={1,0,0,0,1,0,0,0,1};s.cameraWB={1,1,1,1};
    s.linear=QImage(s.originalSize,QImage::Format_RGBA32FPx4);
    for(int y=0;y<29;++y)for(int x=0;x<37;++x){
        auto *p=reinterpret_cast<float *>(s.linear.scanLine(y))+4*x;const int i=y*37+x;
        p[0]=float(i%17)*.09f-.15f;p[1]=float(i%13)*.07f;p[2]=float(i%11)*.12f-.1f;
        p[3]=i%31==0?0:i%19==0?.4f:1;
    }
    return s;
}
PhotoDocument document(const SourceFrame &s){
    PhotoDocument d;d.asset.id="asset";d.asset.sourcePath=s.path;d.asset.sourceHash=s.sha256;
    d.asset.normalizedSize=s.originalSize;d.asset.raw=s.raw;
    AdjustmentLayer a;a.id="radial";a.opacity=.65;a.parameters={.4,.15,-.2,.2,.1,-.1,.1,-.1,.8};
    MaskComponent radial;radial.id="radial-add";radial.geometry=RadialGeometry{{18.5,14.5},12,9,15};
    MaskComponent brush;brush.id="brush-sub";brush.operation=MaskOperation::Subtract;brush.strength=.7;
    brush.geometry=BrushGeometry{{{6.5,6.5},{12.5,10.5},{20.5,18.5},{30.5,22.5}},7,.5};
    a.mask.components={radial,brush};a.mask.feather=2.3;
    AdjustmentLayer b;b.id="linear";b.parameters.exposure=-.2;b.parameters.contrast=-.1;b.parameters.saturation=1.2;
    MaskComponent linear;linear.id="linear-add";linear.geometry=LinearGeometry{{4,4},{33,25}};
    b.mask.components={linear};b.mask.invert=true;b.mask.feather=1.2;d.layers={a,b};return d;
}
void colors(Recipe &r){
    for(int i=0;i<4;++i)r.curves[i]={{0,0},{.25,.2+.01*i},{.7,.77-.01*i},{1,1}};
    r.luminanceCurve={{0,0},{.3,.25},{.7,.8},{1,1}};
    r.parametricCurve.regions={.1,-.1,.15,-.15};r.hsl[1]={.15,.2,-.1};r.hsl[5]={-.1,-.15,.1};
    r.primaries[0]={.1,.2};r.primaries[2]={-.15,.1};
    r.grading[0]={220,.1,-.05};r.grading[2]={40,.15,.05};r.gradingBlend=.6;r.gradingBalance=.1;
    PointColorAdjustment p;p.hue=30;p.hueWidth=90;p.saturationShift=.15;r.pointColors={p};
}
void geometry(Recipe &r){
    r.crop={.08,.06,.84,.87};r.angle=7;r.flipHorizontal=r.flipVertical=true;
    r.perspective={.08,-.06};r.distortion=.07;r.chromaticAberration={.3,-.2};r.lensShading=-.3;
}
void spatial(Recipe &r){
    r.texture=.2;r.clarity=.15;r.dehaze=.1;r.sharpenAmount=.35;r.sharpenRadius=1.2;
    r.sharpenThreshold=.015;r.sharpenMask=.2;r.noiseLuminance=.12;r.noiseChroma=.08;
    r.defringe.purpleAmount=.4;r.defringe.greenAmount=.3;r.defringe.radius=3;
}
void post(Recipe &r){
    r.vignetteAmount=-.3;r.vignetteMidpoint=.55;r.vignetteFeather=.45;
    r.grainAmount=.15;r.grainSize=2.5;r.grainRoughness=.55;
}
QImage serial(const SourceFrame &s,const PhotoDocument &d,QString &error,int &fractional){
    auto prefix=d.global;prefix.vignetteAmount=prefix.grainAmount=0;
    prefix.vignetteMidpoint=prefix.vignetteFeather=.5;prefix.grainSize=3;prefix.grainRoughness=.5;
    auto image=renderLinear(s,prefix,0,error);if(image.isNull())return {};
    for(const auto &layer:d.layers){
        const BrushMaskReference mask(layer.mask);QVector<float> coverage;
        if(!mask.rasterize({s.originalSize,image.size(),d.global},image,QRect(QPoint(),image.size()),coverage,error))return {};
        const LocalPixelTransform transform(layer.parameters);
        for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x){
            const float w=coverage[y*image.width()+x];fractional+=w>0&&w<1;
            auto *pixel=reinterpret_cast<float *>(image.scanLine(y))+4*x;
            if(!transform.apply(pixel,w,float(layer.opacity))){error="reference local rejected";return {};}
        }
    }
    const PostCropVignette vignette(d.global,image.size());
    const ProceduralGrain grain(d.global,image.size(),s.pixelScaleX()*s.linear.width()*d.global.crop.width()/image.width(),
        s.pixelScaleY()*s.linear.height()*d.global.crop.height()/image.height());
    if(!grain.valid()){error="reference grain invalid";return {};}
    for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x){
        auto *p=reinterpret_cast<float *>(image.scanLine(y))+4*x;
        if(p[3]>1e-8f){vignette.apply(p,x,y);grain.apply(p,x,y);}
    }
    return image;
}
bool agrees(const QImage &a,const QImage &b){
    if(a.isNull()||b.isNull()||a.size()!=b.size()||a.format()!=b.format())return false;
    for(int y=0;y<a.height();++y)for(int x=0;x<a.width()*4;++x){
        const float p=reinterpret_cast<const float *>(a.constScanLine(y))[x];
        const float q=reinterpret_cast<const float *>(b.constScanLine(y))[x];
        if(!std::isfinite(p)||!std::isfinite(q)||std::abs(p-q)>2e-6f+2e-6f*std::abs(q))return false;
        if(x%4==3&&std::bit_cast<quint32>(p)!=std::bit_cast<quint32>(q))return false;
    }
    return true;
}
}
class LocalCombinationsTests:public QObject {
    Q_OBJECT
private slots:
    void orderedReferenceAndCache_data(){
        QTest::addColumn<int>("combination");QTest::addColumn<bool>("raw");
        for(int i=0;i<8;++i)for(bool raw:{false,true})
            QTest::newRow(qPrintable(QString("combo-%1-%2").arg(i).arg(raw?"raw":"jpeg")))<<i<<raw;
    }
    void orderedReferenceAndCache(){
        QFETCH(int,combination);QFETCH(bool,raw);const auto s=fixture(raw);auto d=document(s);
        switch(combination){
        case 0:colors(d.global);break;
        case 1:colors(d.global);d.global.monochrome=true;d.global.monochromeMix[1]=.2;break;
        case 2:colors(d.global);d.global.monochrome=true;d.global.monochromeStrength=.4;break;
        case 3:geometry(d.global);break;
        case 4:spatial(d.global);break;
        case 5:geometry(d.global);post(d.global);break;
        case 6:colors(d.global);spatial(d.global);break;
        case 7:colors(d.global);geometry(d.global);spatial(d.global);post(d.global);
            d.global.monochrome=true;d.global.monochromeStrength=.25;break;
        }
        QString error;QVERIFY(validRecipe(d.global));QVERIFY2(validDocument(d,error,true),qPrintable(error));
        int fractional=0;const auto expected=serial(s,d,error,fractional);QVERIFY2(!expected.isNull(),qPrintable(error));QVERIFY(fractional>0);
        const auto full=renderDocumentFullLinear(s,d,error);QVERIFY2(!full.isNull(),qPrintable(error));QVERIFY(agrees(full,expected));
        LocalPreviewCache cache;
        const auto cached=renderDocumentPreviewLinear(s,d,d.global,3840,error,{},&cache);
        QVERIFY2(!cached.isNull(),qPrintable(error));QCOMPARE(cached,full);
        QCOMPARE(renderDocumentPreviewLinear(s,d,d.global,3840,error),full);
        const auto builds=cache.stats().builds;QCOMPARE(renderDocumentPreviewLinear(s,d,d.global,3840,error,{},&cache),full);
        QCOMPARE(cache.stats().builds,builds);
        const auto srgb=workToSrgb(full,error);QVERIFY2(!srgb.isNull(),qPrintable(error));
        const auto displayed=renderDocumentPreview(s,d,d.global,3840,nullptr,{}, {},&cache);
        QVERIFY2(displayed.error.isEmpty(),qPrintable(displayed.error));QCOMPARE(displayed.image,srgb);
        const auto globalOnly=renderLinear(s,d.global,0,error);QVERIFY2(!globalOnly.isNull(),qPrintable(error));QVERIFY(full!=globalOnly);
        const auto frozen=d;d.layers[0].parameters.exposure=-.6;
        const auto changed=renderDocumentPreviewLinear(s,d,d.global,3840,error,{},&cache);
        QVERIFY(changed!=full);QVERIFY(agrees(changed,serial(s,d,error,fractional)));
        QCOMPARE(cache.stats().builds,builds); // parameter edits reuse prefix and coverage
        QCOMPARE(renderDocumentFullLinear(s,frozen,error),full);
        std::swap(d.layers[0],d.layers[1]);const auto reordered=renderDocumentPreviewLinear(s,d,d.global,3840,error,{},&cache);
        QVERIFY(reordered!=changed);QVERIFY(agrees(reordered,serial(s,d,error,fractional)));
        QCOMPARE(cache.stats().builds,builds); // order changes composition, not cached dependencies
        QCOMPARE(s.linear,fixture(raw).linear);
    }
};
QTEST_GUILESS_MAIN(LocalCombinationsTests)
#include "local_combinations_tests.moc"
