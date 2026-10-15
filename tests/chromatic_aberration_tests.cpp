#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QFile>
#include <QLineF>
#include <algorithm>
#include <limits>
#include "core/chromatic_aberration_json.h"
#include "core/presets.h"
#include "render/cpu_renderer.h"
#include "render/geometry_transform.h"
#include "render/chromatic_aberration_operator.h"
#include "render/spatial_operator.h"

class ChromaticAberrationTests:public QObject {
    Q_OBJECT
private slots:
    void parameterAndIdentity();
    void basisSafety();
    void cameraPlaneAndGeometry();
    void jpegBasisAndScale();
    void knownEdgeAlignment();
    void alphaZeroAndCache();
};
static const std::array<float,9> cameraMatrix{1.1f,-.2f,.1f,.1f,.8f,.1f,0,.15f,.9f};
static const float *pixel(const QImage &im,int x,int y){return reinterpret_cast<const float *>(im.constScanLine(y))+4*x;}
static SourceFrame cameraPlane(){
    SourceFrame s;s.raw=true;s.cameraToWork=cameraMatrix;s.originalSize={257,129};
    s.linear=QImage(s.originalSize,QImage::Format_RGBA32FPx4);
    for(int y=0;y<129;++y){auto *p=reinterpret_cast<float *>(s.linear.scanLine(y));
        for(int x=0;x<257;++x){const double cam[]{-.3+(x+.5)/257,1+(y+.5)/129,.4+(x+.5)/257-.5*(y+.5)/129};
            for(int c=0;c<3;++c)p[x*4+c]=float(cameraMatrix[c*3]*cam[0]+cameraMatrix[c*3+1]*cam[1]+cameraMatrix[c*3+2]*cam[2]);
            p[x*4+3]=1;}}
    return s;
}
void ChromaticAberrationTests::parameterAndIdentity(){
    QCOMPARE(Recipe{}.chromaticAberration,QPointF());QCOMPARE(recipeFromJson({}).chromaticAberration,QPointF());
    for(QPointF amount:{QPointF(-10,10),QPointF(),QPointF(1.234567,-.765432)}){
        Recipe r;r.chromaticAberration=amount;QVERIFY(validRecipe(r));QCOMPARE(recipeFromJson(recipeJson(r)),r);
        QPointF out;QVERIFY(chromaticAberrationFromJson(chromaticAberrationJson(amount),out));QCOMPARE(out,amount);
    }
    const QJsonValue malformed[]{QJsonValue::Null,QJsonValue(true),QJsonValue("1"),QJsonArray{1,2},
        QJsonObject{{"red",1}},QJsonObject{{"red",1},{"blue",false}},QJsonObject{{"red",1},{"blue",0},{"extra",0}},
        QJsonObject{{"red",10.001},{"blue",0}},QJsonObject{{"red",0},{"blue",std::numeric_limits<double>::quiet_NaN()}}};
    for(const auto &v:malformed){QPointF out(7,8);QVERIFY(!chromaticAberrationFromJson(v,out));QCOMPARE(out,QPointF(7,8));
        auto j=recipeJson({});j["chromaticAberration"]=v;QVERIFY(!validRecipe(recipeFromJson(j)));}
    Recipe base;base.chromaticAberration={1.234567,-.765432};base.distortion=.1;
    Recipe target;target.chromaticAberration={8,9};target.exposure=.2;AdjustmentPreset preset{QStringLiteral("CA boundary"),target};
    for(double strength:{0.,.5,1.}){Recipe mixed;QString error;QVERIFY(blendPreset(base,preset,strength,mixed,error));
        QCOMPARE(mixed.chromaticAberration,base.chromaticAberration);QCOMPARE(mixed.distortion,base.distortion);}
    QTemporaryDir dir;QVERIFY(dir.isValid());QString error;const auto path=dir.filePath("preset.json");
    QVERIFY(savePresetFile(path,preset,error));QFile file(path);QVERIFY(file.open(QIODevice::ReadOnly));
    QVERIFY(!QJsonDocument::fromJson(file.readAll()).object()["adjustments"].toObject().contains("chromaticAberration"));
    const auto prepared=preparedGeometry(base);QCOMPARE(prepared.chromaticAberration,base.chromaticAberration);QVERIFY(hasPreparedAdjustment(base));
    auto clear=base;clearPreparedGeometry(clear);QCOMPARE(clear.chromaticAberration,QPointF());QVERIFY(!hasPreparedAdjustment(clear));
    Recipe other=base;other.chromaticAberration.ry()+=.01;QVERIFY(!samePreparedGeometry(base,other));
}
void ChromaticAberrationTests::basisSafety(){
    const ChromaticBasis raw=ChromaticBasis::forSource(cameraPlane());QVERIFY(raw.valid);QVERIFY(raw.condition<10000);
    for(int i=0;i<3;++i)for(int j=0;j<3;++j){double v=0;for(int k=0;k<3;++k)v+=raw.forward[i*3+k]*raw.inverse[k*3+j];QVERIFY(std::abs(v-(i==j?1.:0.))<1e-12);}
    QVERIFY(!ChromaticBasis(std::array<double,9>{}).valid);
    QVERIFY(!ChromaticBasis(std::array<double,9>{1,0,0,0,1,0,0,0,1e-8}).valid);
    QVERIFY(ChromaticBasis(std::array<double,9>{1e-12,0,0,0,1e-12,0,0,0,1e-12}).valid);
    auto invalid=cameraPlane();invalid.cameraToWork.fill(0);QString error;Recipe active;active.chromaticAberration={1,0};
    QVERIFY(renderLinear(invalid,active,0,error).isNull());QVERIFY(!error.isEmpty());
    const auto zero=renderLinear(invalid,{},0,error);QVERIFY(error.isEmpty()&&!zero.isNull());
}
void ChromaticAberrationTests::cameraPlaneAndGeometry(){
    const auto s=cameraPlane();const auto original=s.linear;const double radius=.5*std::hypot(257.,129.);
    for(bool geometry:{false,true}){
        Recipe r;r.chromaticAberration={8,-6};
        if(geometry){r.crop={.1,.1,.8,.8};r.angle=11;r.perspective={.1,-.05};r.distortion=.1;r.flipHorizontal=true;}
        QString error;const auto result=renderLinear(s,r,0,error);QVERIFY2(error.isEmpty(),qPrintable(error));int count=0;
        for(int y=3;y<result.height()-3;y+=3)for(int x=3;x<result.width()-3;x+=3){
            // The common green coordinate chain is separately verified in V30.
            const auto p=outputToSource({(x+.5)/result.width(),(y+.5)/result.height()},s.linear.size(),r);
            const QPointF c(128.5,64.5),red=c+(p-c)*(1+8/radius),blue=c+(p-c)*(1-6/radius);
            if(std::min({p.x(),red.x(),blue.x()})<.5||std::max({p.x(),red.x(),blue.x()})>256.5||
               std::min({p.y(),red.y(),blue.y()})<.5||std::max({p.y(),red.y(),blue.y()})>128.5)continue;
            const double expectedCamera[]{-.3+red.x()/257,1+p.y()/129,.4+blue.x()/257-.5*blue.y()/129};
            const auto *actual=pixel(result,x,y);
            for(int k=0;k<3;++k){const double expected=cameraMatrix[k*3]*expectedCamera[0]+cameraMatrix[k*3+1]*expectedCamera[1]+cameraMatrix[k*3+2]*expectedCamera[2];
                QVERIFY(std::abs(actual[k]-expected)<2e-6);}
            QCOMPARE(actual[3],1.f);++count;
        }
        QVERIFY(count>500);
    }
    QCOMPARE(s.linear,original);
}
void ChromaticAberrationTests::jpegBasisAndScale(){
    auto s=cameraPlane();s.raw=false;const auto basis=ChromaticBasis::forSource(s);QVERIFY(basis.valid);
    for(int i=0;i<3;++i)for(int j=0;j<3;++j){double v=0;for(int k=0;k<3;++k)v+=basis.forward[i*3+k]*basis.inverse[k*3+j];QVERIFY(std::abs(v-(i==j?1.:0.))<1e-12);}
    Recipe r;r.chromaticAberration={2,-3};ChromaticSampler original(s,r);QVERIFY(original.valid());
    s.originalSize={2570,1290};s.sourcePixelsPerPixelX=s.sourcePixelsPerPixelY=10;
    ChromaticSampler proxy(s,r);const QPointF corner(0,0),centre(128.5,64.5);
    QVERIFY(std::abs(QLineF(original.position(corner,0),corner).length()-2)<1e-12);
    QVERIFY(std::abs(QLineF(proxy.position(corner,0),corner).length()-.2)<1e-12);
    QCOMPARE(proxy.position(centre,0),centre);QCOMPARE(proxy.position(corner,1),corner);
    s.originalSize={2,2};ChromaticSampler tiny(s,r);QVERIFY(tiny.valid());
    QVERIFY(QLineF(tiny.position(corner,0),centre).length()<=1.250001*QLineF(corner,centre).length());
}
void ChromaticAberrationTests::knownEdgeAlignment(){
    SourceFrame s;s.raw=true;s.cameraToWork={1,0,0,0,1,0,0,0,1};s.originalSize={257,129};
    s.linear=QImage(s.originalSize,QImage::Format_RGBA32FPx4);const double radius=.5*std::hypot(257.,129.),edge=185.;
    const double scales[]{1+10/radius,1.,1-10/radius};
    for(int y=0;y<129;++y){auto *p=reinterpret_cast<float *>(s.linear.scanLine(y));for(int x=0;x<257;++x){
        for(int c=0;c<3;++c){const double reference=128.5+(x+.5-128.5)/scales[c];p[4*x+c]=float(.5+.4*std::tanh((reference-edge)/4));}p[4*x+3]=1;}}
    QString error;const auto before=renderLinear(s,{},0,error);QVERIFY(error.isEmpty());
    Recipe correction;correction.chromaticAberration={10,-10};const auto after=renderLinear(s,correction,0,error);QVERIFY(error.isEmpty());
    double initial=0,aligned=0;
    for(int x=165;x<205;++x){const auto *a=pixel(before,x,64),*b=pixel(after,x,64);initial+=std::abs(a[0]-a[1])+std::abs(a[2]-a[1]);aligned+=std::abs(b[0]-b[1])+std::abs(b[2]-b[1]);}
    QVERIFY(initial>1);QVERIFY2(aligned<initial*.05,"known channel magnification is not corrected");
}
void ChromaticAberrationTests::alphaZeroAndCache(){
    auto s=cameraPlane();s.raw=false;for(int y=0;y<s.linear.height();++y){auto *p=reinterpret_cast<float *>(s.linear.scanLine(y));for(int x=0;x<s.linear.width();++x){p[4*x]=-.2f;p[4*x+1]=1.2f;p[4*x+2]=2;p[4*x+3]=.4f;}}
    const auto original=s.linear;QString error;Recipe r;r.chromaticAberration={8,-6};
    const auto image=renderLinear(s,r,0,error);QVERIFY(error.isEmpty());const auto *centre=pixel(image,128,64);
    QVERIFY(std::abs(centre[0]+.2)<1e-7);QVERIFY(std::abs(centre[1]-1.2)<1e-7);QCOMPARE(centre[2],2.f);QCOMPARE(centre[3],.4f);
    QCOMPARE(pixel(image,0,0)[3],0.f);QCOMPARE(s.linear,original);
    Recipe zero;zero.chromaticAberration={0,0};QCOMPARE(renderLinear(s,{},0,error),renderLinear(s,zero,0,error));
    auto plane=cameraPlane();r.crop={.1,.1,.8,.8};r.distortion=.05;r.lensShading=.1;r.texture=.2;r.sharpenAmount=.3;
    SpatialCache cache;const auto warm=renderFrame(plane,r,0,&cache);QVERIFY(warm.error.isEmpty());
    Recipe changed=r;changed.chromaticAberration={-8,6};const auto cached=renderFrame(plane,changed,0,&cache),fresh=renderFrame(plane,changed,0);
    QVERIFY(cached.error.isEmpty()&&fresh.error.isEmpty());QCOMPARE(cached.image,fresh.image);QVERIFY(warm.image!=fresh.image);
    const auto linear=renderLinear(plane,changed,0,error);QVERIFY(error.isEmpty());const auto encoded=workToSrgb(linear,error);QVERIFY(error.isEmpty());
    QCOMPARE(encoded,fresh.image);
}
QTEST_MAIN(ChromaticAberrationTests)
#include "chromatic_aberration_tests.moc"

