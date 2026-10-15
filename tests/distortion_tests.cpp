#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QFile>
#include <QLineF>
#include <algorithm>
#include <cmath>
#include <limits>
#include "core/document.h"
#include "core/presets.h"
#include "render/geometry_transform.h"
#include "render/cpu_renderer.h"
#include "render/spatial_operator.h"
#include "color/color_pipeline.h"

// Numeric expectations reviewed by GPT, independent of the mapping helper.
class DistortionTests:public QObject {
    Q_OBJECT
private slots:
    void parameterAndPreset();
    void radialInvariants();
    void analyticPlaneAndGeometry();
    void alphaHeadroomAndZero();
    void preparedSpatialAndCache();
};
static const float *pixel(const QImage &image,int x,int y){
    return reinterpret_cast<const float *>(image.constScanLine(y))+4*x;
}
static SourceFrame plane(){
    SourceFrame s;s.linear=QImage(257,129,QImage::Format_RGBA32FPx4);s.originalSize=s.linear.size();
    for(int y=0;y<129;++y){auto *p=reinterpret_cast<float *>(s.linear.scanLine(y));
        for(int x=0;x<257;++x){p[4*x]=-.2f+float((x+.5)/257);p[4*x+1]=1+float((y+.5)/129);p[4*x+2]=.4f;p[4*x+3]=1;}}
    return s;
}
static QPointF independentMap(QPointF uv,QSize size,const Recipe &r){
    const double w=size.width(),h=size.height();
    if(r.flipHorizontal)uv.rx()=1-uv.x();if(r.flipVertical)uv.ry()=1-uv.y();
    const double a=r.angle*3.14159265358979323846/180;
    const double dx=(uv.x()-.5)*r.crop.width()*w,dy=(uv.y()-.5)*r.crop.height()*h;
    const double px=(r.crop.x()+r.crop.width()/2)*w+dx*std::cos(a)+dy*std::sin(a);
    const double py=(r.crop.y()+r.crop.height()/2)*h-dx*std::sin(a)+dy*std::cos(a);
    const double den=1+r.perspective.x()*(2*px/w-1)+r.perspective.y()*(2*py/h-1);
    const double sx=((2*px/w-1)/den+1)*w/2,sy=((2*py/h-1)/den+1)*h/2;
    const double X=sx-w/2,Y=sy-h/2,t=4*(X*X+Y*Y)/(w*w+h*h),gain=1+r.distortion*t/(1+t);
    return {w/2+X*gain,h/2+Y*gain};
}
static int codeDifference(const QImage &a,const QImage &b){
    if(a.size()!=b.size())return 100000;int delta=0;
    for(int y=0;y<a.height();++y)for(int x=0;x<a.width();++x){
        const auto p=a.pixelColor(x,y),q=b.pixelColor(x,y);
        delta=std::max({delta,std::abs(p.red()-q.red()),std::abs(p.green()-q.green()),std::abs(p.blue()-q.blue()),std::abs(p.alpha()-q.alpha())});}
    return delta;
}
void DistortionTests::parameterAndPreset(){
    QCOMPARE(Recipe{}.distortion,0.);QCOMPARE(recipeFromJson({}).distortion,0.);
    for(double k:{-.4,0.,.4}){Recipe r;r.distortion=k;QVERIFY(validRecipe(r));QCOMPARE(recipeFromJson(recipeJson(r)),r);}
    for(QJsonValue bad:{QJsonValue(QJsonValue::Null),QJsonValue(true),QJsonValue("0.2"),QJsonValue(QJsonArray{.2}),QJsonValue(QJsonObject{{"k",.2}})}){
        auto j=recipeJson({});j["distortion"]=bad;QVERIFY(!validRecipe(recipeFromJson(j)));}
    for(double k:{-.400001,.400001,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){
        Recipe r;r.distortion=k;QVERIFY(!validRecipe(r));QVERIFY(!validRecipe(recipeFromJson(recipeJson(r))));}
    Recipe base;base.distortion=-.3;base.perspective={.1,-.2};base.crop={.1,.1,.8,.8};
    Recipe target;target.distortion=.4;target.exposure=.2;target.lensShading=.3;
    AdjustmentPreset preset{QStringLiteral("optical boundary"),target};
    for(double strength:{0.,.5,1.}){Recipe result;QString error;QVERIFY2(blendPreset(base,preset,strength,result,error),qPrintable(error));
        QCOMPARE(result.distortion,base.distortion);QCOMPARE(result.crop,base.crop);QCOMPARE(result.perspective,base.perspective);}
    QCOMPARE(applyPreset(base,preset).lensShading,.3);
    QTemporaryDir dir;QVERIFY(dir.isValid());QString error;const auto path=dir.filePath("preset.json");
    QVERIFY(savePresetFile(path,preset,error));QFile file(path);QVERIFY(file.open(QIODevice::ReadOnly));
    QVERIFY(!QJsonDocument::fromJson(file.readAll()).object()["adjustments"].toObject().contains("distortion"));
}
void DistortionTests::radialInvariants(){
    for(QSize size:{QSize(257,129),QSize(256,128)}){
        const QPointF c(size.width()/2.,size.height()/2.);
        for(double k:{-.4,.4}){RadialDistortion map(size,k);QCOMPARE(map.source(c),c);
            for(QPointF corner:{QPointF(0,0),QPointF(size.width(),size.height())})
                QVERIFY(QLineF(map.source(corner),c+(corner-c)*(1+k/2)).length()<1e-10);
            for(double radius:{1.,41.,200.,1000.}){
                const double x=map.source(c+QPointF(radius,0)).x()-c.x(),y=map.source(c+QPointF(0,radius)).y()-c.y();
                QVERIFY(std::abs(x-y)<1e-10);
                const double derivative=(map.source(c+QPointF(radius+.001,0)).x()-map.source(c+QPointF(radius,0)).x())/.001;
                QVERIFY(derivative>.5499);QVERIFY(x>0);
                QVERIFY(QLineF(map.source(c-QPointF(radius,0)),c-QPointF(x,0)).length()<1e-10);
            }
        }
        RadialDistortion zero(size,0);const QPointF p(-23.125,700.25);QCOMPARE(zero.source(p),p);
    }
}
void DistortionTests::analyticPlaneAndGeometry(){
    const auto source=plane();const auto original=source.linear;QString error;
    for(double k:{-.3,.3})for(bool combined:{false,true}){
        Recipe r;r.distortion=k;
        if(combined){r.crop={.1,.1,.8,.8};r.angle=13;r.flipHorizontal=true;r.perspective={.2,-.1};}
        const auto image=renderLinear(source,r,0,error);QVERIFY2(error.isEmpty(),qPrintable(error));QVERIFY(!image.isNull());
        int count=0;
        for(int y=2;y<image.height()-2;y+=3)for(int x=2;x<image.width()-2;x+=3){
            const QPointF uv((x+.5)/image.width(),(y+.5)/image.height());
            const auto p=independentMap(uv,source.linear.size(),r);
            QVERIFY(QLineF(p,outputToSource(uv,source.linear.size(),r)).length()<1e-10);
            if(p.x()<.5||p.y()<.5||p.x()>256.5||p.y()>128.5)continue;
            const float *v=pixel(image,x,y);
            QVERIFY(std::abs(v[0]-(-.2+p.x()/257))<2e-6);
            QVERIFY(std::abs(v[1]-(1+p.y()/129))<2e-6);
            QVERIFY(std::abs(v[2]-.4)<2e-6);QVERIFY(std::abs(v[3]-1)<1e-6);++count;
        }
        QVERIFY(count>500);
    }
    QCOMPARE(source.linear,original);
}
void DistortionTests::alphaHeadroomAndZero(){
    SourceFrame s;s.linear=QImage(65,33,QImage::Format_RGBA32FPx4);s.originalSize=s.linear.size();
    for(int y=0;y<33;++y){auto *p=reinterpret_cast<float *>(s.linear.scanLine(y));
        for(int x=0;x<65;++x){p[4*x]=-.2f;p[4*x+1]=1.2f;p[4*x+2]=2;p[4*x+3]=.4f;}}
    const auto original=s.linear;QString error;Recipe r;r.distortion=.4;
    auto result=renderLinear(s,r,0,error);QVERIFY(error.isEmpty());QCOMPARE(pixel(result,0,0)[3],0.f);
    const auto *centre=pixel(result,32,16);
    QVERIFY(std::abs(centre[0]+.2f)<1e-7);QVERIFY(std::abs(centre[1]-1.2f)<1e-7);QCOMPARE(centre[2],2.f);QCOMPARE(centre[3],.4f);
    Recipe explicitZero;explicitZero.distortion=0;QCOMPARE(renderLinear(s,explicitZero,0,error),renderLinear(s,{},0,error));
    QCOMPARE(s.linear,original);
}
void DistortionTests::preparedSpatialAndCache(){
    const auto source=plane();Recipe r;r.distortion=.2;r.lensShading=.3;r.texture=.2;r.sharpenAmount=.3;r.grainAmount=.1;
    auto prepared=preparedGeometry(r);QCOMPARE(prepared.distortion,r.distortion);QVERIFY(hasPreparedAdjustment(r));
    auto cleared=r;clearPreparedGeometry(cleared);QCOMPARE(cleared.distortion,0.);QCOMPARE(cleared.lensShading,0.);
    Recipe changed=r;changed.distortion=-.2;QVERIFY(!samePreparedGeometry(r,changed));
    SpatialCache cache;const auto first=renderFrame(source,r,0,&cache);QVERIFY(first.error.isEmpty());
    const auto rebuilt=renderFrame(source,changed,0,&cache),fresh=renderFrame(source,changed,0);
    QVERIFY(rebuilt.error.isEmpty()&&fresh.error.isEmpty());QCOMPARE(rebuilt.image,fresh.image);
    QVERIFY(codeDifference(first.image,fresh.image)>1);
    QString error;const auto linear=renderLinear(source,changed,0,error);QVERIFY(error.isEmpty());
    const auto encoded=workToSrgb(linear,error);QVERIFY(error.isEmpty());QVERIFY(codeDifference(encoded,fresh.image)<=1);
}
QTEST_MAIN(DistortionTests)
#include "distortion_tests.moc"

