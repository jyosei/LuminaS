#include <QtTest>
#include <cmath>
#include <limits>
#include "render/local_mask_feather.h"
#include "render/local_mask.h"
#include "render/local_renderer.h"
#include <bit>
using namespace LocalEdit;
namespace {
QImage alpha(QSize size){QImage image(size,QImage::Format_RGBA32FPx4);for(int y=0;y<size.height();++y){auto *r=reinterpret_cast<float *>(image.scanLine(y));for(int x=0;x<size.width();++x){r[4*x]=-.1f;r[4*x+1]=3;r[4*x+2]=.5f;r[4*x+3]=1;}}return image;}
Mask mixed(){Mask m;MaskComponent a;a.id="add";a.geometry=LinearGeometry{{400,0},{650,80}};
    MaskComponent b;b.id="subtract";b.geometry=RadialGeometry{{520,40},42,25,18};b.operation=MaskOperation::Subtract;b.strength=.8;m.components={a,b};return m;}
}
class LocalMaskFeatherTests:public QObject {
    Q_OBJECT
private slots:
    void knownStepAndConstantPreservation(){
        const auto step=[](QPointF p){return p.x()>=2.5?1.f:0.f;};
        QCOMPARE(Detail::featherSample({.5,.5},2,step),0.f);
        QCOMPARE(Detail::featherSample({1.5,.5},2,step),.25f);
        QCOMPARE(Detail::featherSample({2.5,.5},2,step),.75f);
        QCOMPARE(Detail::featherSample({3.5,.5},2,step),1.f);
        QVector<QPointF> points{{-.4,.3},{255.9,255.7},{256.1,3.2},{100000000.3,5}};QVector<quint8> active(points.size(),1);QString error;
        for(double feather:{.1,.5,2.,3.7,32.})for(float constant:{0.f,.25f,1.f}){
            const auto raw=[constant](QPointF){return constant;};QVector<float> actual;
            QVERIFY2(Detail::featherPoints(points,active,feather,raw,actual,error),qPrintable(error));
            for(int i=0;i<points.size();++i){QVERIFY(std::abs(actual[i]-constant)<1e-6f);QVERIFY(std::abs(actual[i]-Detail::featherSample(points[i],feather,raw))<1e-6f);}
        }
    }
    void combinedThenFeatherThenInvertAndAlpha(){
        auto m=mixed();auto plain=m;const PreparedBrushMask original(plain);m.feather=6;
        const PreparedBrushMask blurred(m);auto inverted=m;inverted.invert=true;const PreparedBrushMask inverse(inverted);
        QVERIFY(blurred.valid());QVERIFY(inverse.valid());bool changed=false;
        for(int x=450;x<590;x+=3){const QPointF p(x+.5,40.5);const float v=blurred.sample(p);changed|=std::abs(v-original.sample(p))>1e-4;
            QCOMPARE(inverse.sample(p),1-v);QCOMPARE(blurred.sample(p,.25f),v);QCOMPARE(inverse.sample(p,0),0.f);}
        QVERIFY(changed);
        Mask first;first.components={m.components[0]};first.feather=6;Mask second;second.components={m.components[1]};second.components[0].operation=MaskOperation::Add;second.feather=6;
        const PreparedBrushMask a(first),b(second);bool differsFromPerComponent=false;
        for(int x=450;x<590;++x){const QPointF p(x+.5,40.5);differsFromPerComponent|=std::abs(blurred.sample(p)-a.sample(p)*(1-b.sample(p)))>1e-4;}
        QVERIFY(differsFromPerComponent);
        Mask empty;empty.feather=16384;empty.invert=true;const PreparedBrushMask full(empty);QVERIFY(full.valid());QCOMPARE(full.sample({1,1}),1.f);QCOMPARE(full.sample({1,1},0),0.f);
        QVERIFY(std::isnan(full.sample({1,1},std::numeric_limits<float>::quiet_NaN())));
    }
    void referenceTilesGeometryAndAlphaHoles(){
        auto m=mixed();m.feather=3.7;m.invert=true;const PreparedBrushMask prepared(m);const BrushMaskReference reference(m);
        Recipe geometry;geometry.crop={.07,.04,.86,.9};geometry.angle=7;geometry.perspective={.05,-.04};geometry.distortion=.1;geometry.flipHorizontal=true;
        const MaskMapping mapping{{1060,80},{530,40},geometry};auto image=alpha(mapping.outputSize);auto holes=image.copy();
        for(int y=0;y<holes.height();++y)for(int x=0;x<holes.width();++x)if((x+y)%17==0)reinterpret_cast<float *>(holes.scanLine(y))[4*x+3]=0;
        QString error;QVector<float> scalar,small,large,withHoles;MaskPreviewStats stats;
        QVERIFY2(reference.rasterize(mapping,image,QRect(QPoint(),mapping.outputSize),scalar,error),qPrintable(error));
        QVERIFY2(rasterizeMaskPreview(prepared,mapping,image,small,error,{},256,&stats),qPrintable(error));
        QVERIFY(stats.maximumScratchBytes>0);QVERIFY(stats.maximumScratchBytes<16*1024*1024);
        QVERIFY2(rasterizeMaskPreview(prepared,mapping,image,large,error,{},512),qPrintable(error));
        QVERIFY2(rasterizeMaskPreview(prepared,mapping,holes,withHoles,error),qPrintable(error));
        QCOMPARE(scalar.size(),small.size());
        for(int i=0;i<small.size();++i){QVERIFY2(std::abs(small[i]-scalar[i])<2e-6f,qPrintable(QString::number(i)));QVERIFY(std::abs(large[i]-scalar[i])<2e-6f);
            const int x=i%530,y=i/530;if((x+y)%17==0)QCOMPARE(withHoles[i],0.f);else QVERIFY(std::abs(withHoles[i]-small[i])<2e-6f);}
    }
    void canceledOrInvalidNeverPublishes(){
        QVector<QPointF> points{{.5,.5},{256.5,1.5}};QVector<quint8> active{1,1};QVector<float> result{42,43};const auto saved=result;QString error;CancellationSource cancel;int samples=0;
        const auto raw=[&](QPointF){if(++samples==5)cancel.cancel();return .5f;};
        qsizetype peak=0;
        QVERIFY(!Detail::featherPoints(points,active,16384,raw,result,error,cancel.token(),&peak));QCOMPARE(error,QString("Canceled"));QCOMPARE(result,saved);QVERIFY(samples<400);
        QVERIFY(peak<5*1024*1024);QVERIFY(peak>3*1024*1024);
        QVERIFY(!Detail::featherPoints(points,active,-1,raw,result,error));QCOMPARE(result,saved);
        QVERIFY(!Detail::featherPoints(points,active,4,[](QPointF){return std::numeric_limits<float>::quiet_NaN();},result,error));QCOMPARE(result,saved);
        auto m=mixed();m.feather=std::numeric_limits<double>::quiet_NaN();QVERIFY(!PreparedBrushMask(m).valid());m.feather=16385;QVERIFY(!BrushMaskReference(m).valid());
    }
    void wholeMaskActuallyBlendsHdrAndKeepsAlpha(){
        SourceFrame source;source.path="synthetic";source.sha256=QString(64,'a');source.originalSize={24,18};source.linear=alpha(source.originalSize);
        for(int y=0;y<18;++y)for(int x=0;x<24;++x){auto *p=reinterpret_cast<float *>(source.linear.scanLine(y))+4*x;p[3]=x%3==0?0.f:x%3==1?.25f:1.f;}
        PhotoDocument doc;doc.asset.id="asset";doc.asset.sourcePath=source.path;doc.asset.sourceHash=source.sha256;doc.asset.normalizedSize=source.originalSize;
        AdjustmentLayer layer;layer.id="layer";layer.name="Mask";layer.parameters.exposure=.7;
        MaskComponent component;component.id="brush";BrushGeometry brush;brush.points={{12.5,9.5}};brush.radius=6;brush.hardness=1;component.geometry=brush;
        layer.mask.components={component};layer.mask.feather=3.5;layer.mask.invert=true;doc.layers={layer};
        QString error;const auto before=renderLinear(source,doc.global,0,error);QVERIFY2(!before.isNull(),qPrintable(error));
        const auto actual=renderDocumentFullLinear(source,doc,error);QVERIFY2(!actual.isNull(),qPrintable(error));
        const BrushMaskReference mask(layer.mask);const LocalPixelTransform transform(layer.parameters);bool changed=false;
        for(int y=0;y<18;++y)for(int x=0;x<24;++x){
            const auto *base=reinterpret_cast<const float *>(before.constScanLine(y))+4*x;
            const auto *p=reinterpret_cast<const float *>(actual.constScanLine(y))+4*x;
            float expected[]{base[0],base[1],base[2],base[3]};const float coverage=mask.sample({x+.5,y+.5},base[3]);QVERIFY(transform.apply(expected,coverage,1));
            for(int c=0;c<3;++c){QVERIFY(std::abs(p[c]-expected[c])<2e-6f);changed|=p[c]!=base[c];}
            QCOMPARE(std::bit_cast<quint32>(p[3]),std::bit_cast<quint32>(base[3]));
            if(base[3]>0&&base[0]<0)QVERIFY(p[0]<0);if(base[3]>0&&base[1]>1)QVERIFY(p[1]>1);
        }
        QVERIFY(changed);
    }
};
QTEST_MAIN(LocalMaskFeatherTests)
#include "local_mask_feather_tests.moc"
