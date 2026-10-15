#include <QtTest>
#include <limits>
#include "render/defringe_operator.h"
#include "render/cpu_renderer.h"
#include "render/spatial_operator.h"
#include "render/geometry_transform.h"
#include "color/color_pipeline.h"
#include "core/presets.h"
#include <QSemaphore>
#include <QtConcurrentRun>
class DefringeTests:public QObject {
    Q_OBJECT
private slots:
    void boundsAndZero();void fringeAndLuminance();void objectAndAlphaProtection();void scaleAndHeadroom();void fourKFrame();
    void pipelineOrder();void cacheIdentityAndStaleCommit();void presetCircularHue();
};
static const float *pixel(const QImage &i,int x,int y){return reinterpret_cast<const float *>(i.constScanLine(y))+4*x;}
static QImage edge(bool green=false,bool colouredObject=false){
    QImage image(65,33,QImage::Format_RGBA32FPx4);
    for(int y=0;y<33;++y){auto *p=reinterpret_cast<float *>(image.scanLine(y));for(int x=0;x<65;++x){
        const float gray=x<32?.05f:.9f;float rgb[]{gray,gray,gray};
        if(x==32||(colouredObject&&x>32)){rgb[0]=green?.05f:.7f;rgb[1]=green?.8f:.1f;rgb[2]=green?.05f:.7f;}
        for(int c=0;c<3;++c)p[4*x+c]=rgb[c];p[4*x+3]=.4f;
    }}return image;
}
static double yValue(const float *p){return .2627*double(p[0])+.6780*double(p[1])+.0593*double(p[2]);}
void DefringeTests::boundsAndZero(){
    DefringeSettings s;QVERIFY(validDefringe(s));QString error;const auto before=edge();QCOMPARE(applyDefringe(before,s,1,1,error),before);QVERIFY(error.isEmpty());
    for(double amount:{-1.,1.01,std::numeric_limits<double>::quiet_NaN()}){s.purpleAmount=amount;QVERIFY(!validDefringe(s));QVERIFY(applyDefringe(before,s,1,1,error).isNull());}
    s={};s.purpleWidth=4;QVERIFY(!validDefringe(s));s={};s.radius=21;QVERIFY(!validDefringe(s));
    s={};s.greenHue=361;QVERIFY(!validDefringe(s));s={};s.purpleAmount=1;
    QVERIFY(applyDefringe(before,s,0,1,error).isNull());QVERIFY(applyDefringe(before,s,1,std::numeric_limits<double>::infinity(),error).isNull());
}
void DefringeTests::fringeAndLuminance(){
    for(bool green:{false,true}){const auto before=edge(green);const auto copy=before;DefringeSettings s;s.purpleAmount=green?0:.75;s.greenAmount=green?.75:0;
        QString error;const auto after=applyDefringe(before,s,1,1,error);QVERIFY(error.isEmpty());const auto *a=pixel(before,32,16),*b=pixel(after,32,16);
        for(int c=0;c<3;++c)QVERIFY(std::abs(b[c]-(yValue(a)+(a[c]-yValue(a))*.25))<1e-7);
        QVERIFY(std::abs(yValue(a)-yValue(b))<1e-7);QCOMPARE(b[3],a[3]);QCOMPARE(before,copy);
        QCOMPARE(pixel(after,20,16)[0],pixel(before,20,16)[0]);QCOMPARE(pixel(after,45,16)[1],pixel(before,45,16)[1]);
        DefringeSettings off=s;if(green)off.greenHue=300;else off.purpleHue=120;
        QCOMPARE(applyDefringe(before,off,1,1,error),before);
    }
}
void DefringeTests::objectAndAlphaProtection(){
    DefringeSettings s;s.purpleAmount=s.greenAmount=1;QString error;
    for(bool green:{false,true}){const auto object=edge(green,true);QCOMPARE(applyDefringe(object,s,1,1,error),object);QVERIFY(error.isEmpty());}
    auto transparent=edge();for(int y=0;y<33;++y){auto *p=reinterpret_cast<float *>(transparent.scanLine(y));p[4*29+3]=0;p[4*35+3]=0;
        // Block both vertical/diagonal evidence endpoints too.
        for(int x=0;x<65;++x)if(x!=32)p[4*x+3]=0;
    }
    QCOMPARE(applyDefringe(transparent,s,1,1,error),transparent);QVERIFY(error.isEmpty());
    auto bad=edge();reinterpret_cast<float *>(bad.scanLine(16))[4*32]=std::numeric_limits<float>::quiet_NaN();
    QVERIFY(applyDefringe(bad,s,1,1,error).isNull());QVERIFY(!error.isEmpty());
}
void DefringeTests::scaleAndHeadroom(){
    auto before=edge();for(int y=0;y<33;++y){auto *p=reinterpret_cast<float *>(before.scanLine(y));p[4*32]=2;p[4*32+1]=-.1f;p[4*32+2]=2;}
    DefringeSettings s;s.purpleAmount=.25;QString error;const auto after=applyDefringe(before,s,1,1,error);QVERIFY(error.isEmpty());
    QVERIFY(pixel(after,32,16)[0]>1);QVERIFY(std::abs(yValue(pixel(after,32,16))-yValue(pixel(before,32,16)))<1e-7);
    const auto scaled=applyDefringe(before,s,2,3,error);QVERIFY(error.isEmpty());QCOMPARE(scaled,after);
    s.purpleHue=360;s.purpleWidth=90;const auto redBand=applyDefringe(before,s,1,1,error);QVERIFY(error.isEmpty());
    s.purpleHue=0;QCOMPARE(applyDefringe(before,s,1,1,error),redBand);
}
void DefringeTests::fourKFrame(){
    QImage before(3840,2560,QImage::Format_RGBA32FPx4);
    for(int y=0;y<before.height();++y){auto *p=reinterpret_cast<float *>(before.scanLine(y));for(int x=0;x<before.width();++x){
        const float gray=x<1920?.05f:.9f;p[4*x]=p[4*x+1]=p[4*x+2]=gray;p[4*x+3]=1;
        if(x==1920){p[4*x]=p[4*x+2]=.7f;p[4*x+1]=.1f;}
    }}
    const auto original=before;DefringeSettings s;s.purpleAmount=.75;QString error;QElapsedTimer timer;timer.start();
    const auto after=applyDefringe(before,s,1.8125,1.8125,error);const auto elapsed=timer.elapsed();
    QVERIFY2(error.isEmpty(),qPrintable(error));QCOMPARE(after.size(),before.size());QCOMPARE(before,original);
    QVERIFY(std::abs(yValue(pixel(after,1920,1280))-yValue(pixel(before,1920,1280)))<1e-7);
    QVERIFY(pixel(after,1920,1280)[0]<pixel(before,1920,1280)[0]);
    qInfo()<<"defringe4K"<<after.size()<<"processingMs"<<elapsed<<"immutableSource"<<(before==original);
}
void DefringeTests::pipelineOrder(){
    for(bool raw:{false,true}){
        SourceFrame source;source.linear=edge();source.originalSize={130,66};source.sourcePixelsPerPixel=2;source.raw=raw;
        if(raw)source.draftBalance={1.2f,0,0,0,.9f,0,0,0,1.1f};const auto unchanged=source.linear;
        Recipe r;r.defringe.purpleAmount=.75;r.warmth=.18;r.tint=-.12;r.crop={.1,.1,.8,.8};r.angle=3;r.lensShading=.2;r.distortion=.03;
        r.exposure=.12;r.contrast=.08;r.texture=.2;r.vignetteAmount=-.1;r.pointColors.append(PointColorAdjustment{});
        auto prefix=preparedGeometry(r);prefix.warmth=r.warmth;prefix.tint=r.tint;QString error;
        auto before=renderLinear(source,prefix,32,error);QVERIFY2(error.isEmpty(),qPrintable(error));
        auto prepared=source;prepared.sourcePixelsPerPixelX=2*source.linear.width()*r.crop.width()/before.width();
        prepared.sourcePixelsPerPixelY=2*source.linear.height()*r.crop.height()/before.height();prepared.sourcePixelsPerPixel=std::max(prepared.pixelScaleX(),prepared.pixelScaleY());
        prepared.linear=applyDefringe(before,r.defringe,prepared.pixelScaleX(),prepared.pixelScaleY(),error);prepared.draftBalance={1,0,0,0,1,0,0,0,1};
        auto after=r;clearPreparedGeometry(after);after.warmth=after.tint=0;after.defringe={};
        const auto expected=renderLinear(prepared,after,0,error);QVERIFY(!expected.isNull());
        const auto actual=renderLinear(source,r,32,error);QVERIFY2(error.isEmpty(),qPrintable(error));QCOMPARE(actual,expected);
        SpatialCache cache;const auto shown=renderFrame(source,r,32,&cache);QVERIFY2(shown.error.isEmpty(),qPrintable(shown.error));
        QCOMPARE(shown.image,workToSrgb(expected,error));QCOMPARE(shown.fullSize,renderedSize(source.linear.size(),r));
        const auto rangeExpected=renderLinear(prepared,after,0,error,{0});const auto range=renderFrame(source,r,32,&cache,{0});
        QCOMPARE(range.image,workToSrgb(rangeExpected,error));QCOMPARE(source.linear,unchanged);
        r.defringe={};auto plain=r;plain.defringe.purpleHue=123;QCOMPARE(renderLinear(source,r,32,error),renderLinear(source,plain,32,error));
    }
}
void DefringeTests::cacheIdentityAndStaleCommit(){
    SourceFrame source;source.linear=edge();source.originalSize=source.linear.size();Recipe prefix;DefringeSettings s;s.purpleAmount=.7;
    DefringeCache cache;QString error;int builds=0;auto build=[&]{++builds;return source.linear;};
    auto first=cache.get(source,prefix,s,3840,1,1,build,error);QVERIFY(!first.isNull());
    QCOMPARE(cache.get(source,prefix,s,3840,1,1,build,error),first);QCOMPARE(builds,1);
    s.purpleAmount=.3;QVERIFY(!cache.get(source,prefix,s,3840,1,1,build,error).isNull());QCOMPARE(builds,1);
    prefix.warmth=.1;cache.get(source,prefix,s,3840,1,1,build,error);QCOMPARE(builds,2);
    source.draftBalance[0]=1.1f;cache.get(source,prefix,s,3840,1,1,build,error);QCOMPARE(builds,3);
    cache.get(source,prefix,s,3840,2,1,build,error);QCOMPARE(builds,4);
    cache.clear();QSemaphore entered,released;
    auto old=QtConcurrent::run([&]{QString oldError;return cache.get(source,prefix,s,3840,1,1,[&]{entered.release();released.acquire();return source.linear;},oldError);});
    QVERIFY(entered.tryAcquire(1,3000));auto newer=prefix;newer.tint=.2;
    const auto newOutput=cache.get(source,newer,s,3840,1,1,build,error);released.release();old.waitForFinished();
    QCOMPARE(cache.get(source,newer,s,3840,1,1,build,error),newOutput);QCOMPARE(builds,5);
    cache.clear();cache.get(source,newer,s,3840,1,1,build,error);QCOMPARE(builds,6);
}
void DefringeTests::presetCircularHue(){
    Recipe base,target;QString error;base.defringe.purpleHue=350;target.defringe.purpleHue=10;
    base.defringe.greenHue=10;target.defringe.greenHue=350;target.defringe.purpleAmount=.8;
    base.crop={.1,.1,.8,.8};base.chromaticAberration={2,-1};Recipe blended;
    QVERIFY(blendPreset(base,{"Fringe",target},.5,blended,error));QCOMPARE(blended.defringe.purpleHue,0.);QCOMPARE(blended.defringe.greenHue,0.);
    QCOMPARE(blended.defringe.purpleAmount,.4);QCOMPARE(blended.crop,base.crop);QCOMPARE(blended.chromaticAberration,base.chromaticAberration);
    QVERIFY(blendPreset(base,{"Fringe",target},0,blended,error));QCOMPARE(blended,base);
    QVERIFY(blendPreset(base,{"Fringe",target},1,blended,error));QCOMPARE(blended.defringe,target.defringe);
}
QTEST_MAIN(DefringeTests)
#include "defringe_tests.moc"
