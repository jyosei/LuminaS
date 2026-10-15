#include <QtTest>
#include <QJsonObject>
#include <QJsonValue>
#include <algorithm>
#include <cmath>
#include <limits>
#include "core/document.h"
#include "core/perspective_json.h"
#include "render/geometry_transform.h"
#include "render/cpu_renderer.h"
#include "color/color_pipeline.h"
#include "core/presets.h"
#include "render/spatial_operator.h"
// Mapping/serialization properties on a synthetic linear plane only. The
// geometry algorithm is owned by GPT; expectations are computed independently
// (outputToSource + analytic plane), never by copying renderer sampling.
// Scope: no claim about camera/photographic quality or real-image resampling.
class PerspectiveTests:public QObject{
    Q_OBJECT
private slots:
    void mixedSpatialAndCache();
    void jsonStrictAndAtomic();void recipeLegacyAndRoundtrip();void helperAndInverse();void neutralIdentityAndOrder();
    void warpedPlaneAndCorners();void cropAngleFlipCombined();void presetRoundtripAndNeutral();void fusedAndAlpha();
};
static SourceFrame planeFrame(int w=257,int h=129){
    SourceFrame s;s.linear=QImage(w,h,QImage::Format_RGBA32FPx4);
    for(int y=0;y<h;++y){auto *row=reinterpret_cast<float *>(s.linear.scanLine(y));for(int x=0;x<w;++x){row[x*4]=float((x+.5)/w);row[x*4+1]=float((y+.5)/h);row[x*4+2]=.25f;row[x*4+3]=1.f;}}
    s.originalSize=s.linear.size();return s;
}
static bool planeExpected(QPointF p,QSize size,float rgb[3]){
    if(!(p.x()>=0&&p.y()>=0&&p.x()<=size.width()&&p.y()<=size.height()))return false;
    const double x=std::clamp(p.x()-.5,0.0,double(size.width()-1)),y=std::clamp(p.y()-.5,0.0,double(size.height()-1));
    rgb[0]=float((x+.5)/size.width());rgb[1]=float((y+.5)/size.height());rgb[2]=.25f;return true;
}
static const float *pixelAt(const QImage &i,int x,int y){return reinterpret_cast<const float *>(i.constScanLine(y))+x*4;}
static const double notANumber=std::numeric_limits<double>::quiet_NaN(),inf=std::numeric_limits<double>::infinity();
void PerspectiveTests::jsonStrictAndAtomic(){
    QCOMPARE(perspectiveJson(QPointF(0,0)),(QJsonObject{{"horizontal",0.0},{"vertical",0.0}}));
    QCOMPARE(perspectiveJson(QPointF(.2,-.1)),(QJsonObject{{"horizontal",.2},{"vertical",-.1}}));
    for(const auto p:{QPointF(0,0),QPointF(-.3,.3),QPointF(.125,-.075)}){QPointF o(-9,-9);QVERIFY(perspectiveFromJson(perspectiveJson(p),o));QCOMPARE(o,p);}
    const QPointF sentinel(-7,7);QPointF out=sentinel;const QJsonValue bad[]{QJsonValue(QStringLiteral("x")),QJsonValue(3.0),QJsonValue::Null,
        QJsonObject{{"horizontal",.1}},QJsonObject{{"vertical",.1}},QJsonObject{{"horizontal",.1},{"vertical",.1},{"extra",0.0}},
        QJsonObject{{"horizontal",.1},{"vertical",.1},{"horizontal2",0.0}},
        QJsonObject{{"horizontal","a"},{"vertical",.1}},QJsonObject{{"horizontal",true},{"vertical",.1}},
        QJsonObject{{"horizontal",notANumber},{"vertical",0.0}},QJsonObject{{"horizontal",0.0},{"vertical",inf}},QJsonObject{{"horizontal",.3000001},{"vertical",0.0}},
        QJsonObject{{"horizontal",0.0},{"vertical",-.3000001}}};
    for(const QJsonValue &v:bad){QVERIFY(!perspectiveFromJson(v,out));QCOMPARE(out,sentinel);}
    QPointF edge(-1,-1);QVERIFY(perspectiveFromJson(QJsonObject{{"horizontal",.3},{"vertical",-.3}},edge));QCOMPARE(edge,QPointF(.3,-.3));QVERIFY(validPerspective(QPointF(-.3,.3)));QVERIFY(!validPerspective(QPointF(.3,.3000001)));QVERIFY(!validPerspective(QPointF(notANumber,0)));
}
void PerspectiveTests::recipeLegacyAndRoundtrip(){
    const Recipe legacy=recipeFromJson(recipeJson(Recipe{}));QCOMPARE(legacy.perspective,QPointF(0,0));QVERIFY(!hasPerspective(legacy));
    Recipe r;r.perspective={.2,-.1};QCOMPARE(recipeFromJson(recipeJson(r)),r);
    Recipe bad=r;bad.perspective={.4,0};QVERIFY(!validRecipe(bad));bad.perspective={notANumber,0};QVERIFY(!validRecipe(bad));
}
void PerspectiveTests::helperAndInverse(){
    const QSize size(100,100);QCOMPARE(perspectiveSource(QPointF(50,50),size,Recipe{}),QPointF(50,50));Recipe r;r.perspective={.2,0};const QPointF mapped=perspectiveSource(QPointF(75,50),size,r);
    QVERIFY(std::abs(mapped.x()-(.5/1.1+1)*50)<1e-12);QCOMPARE(mapped.y(),50.0);QCOMPARE(perspectiveSource(QPointF(50,50),size,r),QPointF(50,50));
    Recipe none;QVERIFY(!hasPerspective(none));none.perspective={0,1e-300};QVERIFY(hasPerspective(none));
    const QSize big(257,129);for(const auto p:{QPointF(.2,-.1),QPointF(-.3,.3),QPointF(.05,.25)})for(const auto pt:{QPointF(40,30),QPointF(130,70),QPointF(210,100)}){Recipe g;g.perspective=p;const double X=2*pt.x()/big.width()-1,Y=2*pt.y()/big.height()-1,d=1+p.x()*X+p.y()*Y;
        const QPointF got=perspectiveSource(pt,big,g);QVERIFY(std::abs(got.x()-(X/d+1)*big.width()/2)<1e-10);QVERIFY(std::abs(got.y()-(Y/d+1)*big.height()/2)<1e-10);
    }
    Recipe extreme;extreme.perspective={.3,.3};const QPointF bad=perspectiveSource(QPointF(-big.width(),-big.height()),big,extreme);QVERIFY(!std::isfinite(bad.x())||!std::isfinite(bad.y()));
    Recipe zero;QCOMPARE(perspectiveSource(QPointF(12.5,7.25),big,zero),QPointF(12.5,7.25));
}
void PerspectiveTests::neutralIdentityAndOrder(){
    const SourceFrame source=planeFrame();const QSize size=source.linear.size();Recipe neutral;
    for(const auto p:{QPointF(0,0),QPointF(.5,.5),QPointF(1,1),QPointF(.25,.75)})
        QCOMPARE(outputToSource(p,size,neutral),QPointF(p.x()*size.width(),p.y()*size.height()));
    QVERIFY(!hasPerspective(Recipe{}));
    Recipe geometry;geometry.crop={.1,.1,.8,.8};geometry.angle=13;geometry.flipHorizontal=true;geometry.perspective={.2,-.1};
    Recipe older=geometry;older.perspective={0,0};
    for(const auto uv:{QPointF(.2,.3),QPointF(.7,.6)}){const QPointF sourcePoint=outputToSource(uv,size,older); // older order: crop/angle/flip only
        QCOMPARE(outputToSource(uv,size,geometry),perspectiveSource(sourcePoint,size,geometry));
        QVERIFY(!(perspectiveSource(sourcePoint,size,geometry)==sourcePoint)); // perspective applied after
    }
}
void PerspectiveTests::warpedPlaneAndCorners(){
    const SourceFrame source=planeFrame();const QSize size=source.linear.size();Recipe r;r.perspective={.2,-.1};QString error;
    const QImage full=renderLinear(source,r,0,error);QVERIFY2(error.isEmpty(),qPrintable(error));QCOMPARE(full.size(),size);const QImage proxy=renderLinear(source,r,64,error);QVERIFY(error.isEmpty());QCOMPARE(std::max(proxy.width(),proxy.height()),64);
    int transparent=0;
    for(const QImage &image:{full,proxy}){for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x){
            const float *p=pixelAt(image,x,y);float expected[3];for(int c=0;c<4;++c)QVERIFY(std::isfinite(p[c]));
            const QPointF target((x+.5)*size.width()/image.width(),(y+.5)*size.height()/image.height());
            const auto mapped=perspectiveSource(target,size,r);if(mapped.x()< -.5||mapped.y()< -.5||mapped.x()>size.width()+.5||mapped.y()>size.height()+.5){++transparent;QVERIFY(p[3]<1e-6f);continue;}if(mapped.x()<.5||mapped.y()<.5||mapped.x()>size.width()-.5||mapped.y()>size.height()-.5)continue;QVERIFY(planeExpected(mapped,size,expected));
            QVERIFY(p[3]>.999f);for(int c=0;c<3;++c)QVERIFY(std::abs(p[c]-expected[c])<2e-6f);
        }
    }
    QVERIFY(transparent>0);QCOMPARE(source.linear.size(),size);const float *corner=pixelAt(source.linear,0,0);QCOMPARE(corner[0],float(.5/size.width()));QCOMPARE(corner[3],1.f);
}
void PerspectiveTests::cropAngleFlipCombined(){
    const SourceFrame source=planeFrame();const QSize size=source.linear.size();Recipe r;r.crop={.1,.1,.8,.8};r.angle=13;r.flipHorizontal=true;r.perspective={.2,-.1};
    QVERIFY(validRecipe(r));QString error;const QImage image=renderLinear(source,r,0,error);QVERIFY2(error.isEmpty(),qPrintable(error));QCOMPARE(image.size(),renderedSize(size,r));
    int checked=0;for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x){
        const float *p=pixelAt(image,x,y);if(p[3]<1e-6f)continue;float expected[3];const QPointF target=outputToSource(QPointF((x+.5)/image.width(),(y+.5)/image.height()),size,r);
        if(target.x()<.5||target.y()<.5||target.x()>size.width()-.5||target.y()>size.height()-.5)continue;if(!planeExpected(target,size,expected))continue;
        for(int c=0;c<3;++c)QVERIFY(std::abs(p[c]-expected[c])<1e-5f);++checked;
    }QVERIFY(checked>100);
}
void PerspectiveTests::presetRoundtripAndNeutral(){
    const SourceFrame source=planeFrame();QString error;Recipe neutral;
    const QImage baseline=renderLinear(source,neutral,0,error);QVERIFY(error.isEmpty());
    const QImage repeated=renderLinear(source,recipeFromJson(recipeJson(neutral)),0,error);QVERIFY(error.isEmpty());
    QCOMPARE(repeated.size(),baseline.size());for(qsizetype i=0;i<baseline.sizeInBytes();++i)QCOMPARE(repeated.constBits()[i],baseline.constBits()[i]);Recipe current;current.perspective={.2,-.1};current.crop={.05,.02,.9,.94};current.angle=7;
    for(const auto &preset:builtinPresets()){const Recipe applied=applyPreset(current,preset);
        QCOMPARE(applied.crop,current.crop);QCOMPARE(applied.angle,current.angle);QCOMPARE(applied.perspective,current.perspective);
        const QJsonObject object=recipeJson(applied);QPointF out;
        QVERIFY(perspectiveFromJson(object.value("perspective"),out));QCOMPARE(out,QPointF(.2,-.1));
        QCOMPARE(recipeFromJson(object).perspective,QPointF(.2,-.1));
    }
    Recipe bad;bad.perspective={.5,0};QVERIFY(!validRecipe(bad));Recipe nanSource=neutral;nanSource.perspective={.2,-.1};SourceFrame broken=source;broken.linear=source.linear.copy();reinterpret_cast<float *>(broken.linear.scanLine(source.linear.height()/2))[source.linear.width()/2*4]=float(notANumber);
    QString nanError;QVERIFY(renderLinear(broken,nanSource,0,nanError).isNull());QVERIFY(!nanError.isEmpty());
}
void PerspectiveTests::fusedAndAlpha(){
    const SourceFrame source=planeFrame();Recipe r;r.perspective={.2,-.1};QString error;const QImage linear=renderLinear(source,r,0,error);QVERIFY(error.isEmpty());const QImage reference=workToSrgbReference(linear,error);QVERIFY(error.isEmpty());
    const QImage fused=renderFrame(source,r,0).image;QVERIFY(!fused.isNull());QCOMPARE(fused.size(),reference.size());
    int difference=0;for(qsizetype i=0;i<fused.sizeInBytes();++i)difference=std::max(difference,std::abs(int(fused.constBits()[i])-int(reference.constBits()[i])));QVERIFY(difference<=1);
    SourceFrame quad;quad.linear=QImage(64,64,QImage::Format_RGBA32FPx4);quad.linear.fill(Qt::transparent);for(int y=0;y<64;++y){auto *row=reinterpret_cast<float *>(quad.linear.scanLine(y));for(int x=0;x<64;++x)row[x*4+1]=10000.f;}for(int y=24;y<40;++y){auto *row=reinterpret_cast<float *>(quad.linear.scanLine(y));for(int x=24;x<40;++x){row[x*4]=1.f;row[x*4+1]=row[x*4+2]=0.f;row[x*4+3]=1.f;}}
    quad.originalSize=quad.linear.size();Recipe warp;warp.perspective={.2,-.1};
    const QImage image=renderLinear(quad,warp,0,error);QVERIFY2(error.isEmpty(),qPrintable(error));int clear=0,opaque=0;
    for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x){const float *p=pixelAt(image,x,y);for(int c=0;c<4;++c)QVERIFY(std::isfinite(p[c]));QVERIFY(p[3]>=0.f&&p[3]<=1.f);
        if(p[3]<=1e-6f){++clear;for(int c=0;c<3;++c)QVERIFY(std::abs(p[c])<1e-6f);} // no color leak when transparent
        else{++opaque;for(int c=0;c<3;++c)QVERIFY(std::abs(p[c]-(c==0?1.f:0.f))<1e-3f);} // bright edge stays consistent
    }
    QVERIFY(clear>1000);QVERIFY(opaque>100);
}
void PerspectiveTests::mixedSpatialAndCache(){
    auto source=planeFrame(81,57);Recipe r;r.perspective={.17,-.08};r.crop={.08,.1,.84,.8};r.angle=11;r.flipVertical=true;r.texture=.12;r.sharpenAmount=.2;r.grainAmount=.3;r.exposure=.1;QString error;const auto linear=renderLinear(source,r,0,error);QVERIFY2(error.isEmpty(),qPrintable(error));const auto reference=workToSrgbReference(linear,error);SpatialCache cache;const auto rendered=renderFrame(source,r,0,&cache);QVERIFY2(rendered.error.isEmpty(),qPrintable(rendered.error));QCOMPARE(rendered.image.size(),reference.size());int difference=0;for(qsizetype i=0;i<reference.sizeInBytes();++i)difference=std::max(difference,std::abs(int(reference.constBits()[i])-int(rendered.image.constBits()[i])));QVERIFY(difference<=1);auto changed=r;changed.perspective={-.15,.07};const auto cached=renderFrame(source,changed,0,&cache),fresh=renderFrame(source,changed,0);QVERIFY(cached.error.isEmpty());QCOMPARE(cached.image,fresh.image);
}

QTEST_MAIN(PerspectiveTests)
#include "perspective_tests.moc"



