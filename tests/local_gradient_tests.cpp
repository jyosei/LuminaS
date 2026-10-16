#include <QtTest>
#include <QRandomGenerator>
#include <QLineF>
#include <algorithm>
#include <bit>
#include <cstring>
#include <cmath>
#include <limits>
#include "render/local_mask.h"
#include "render/local_renderer.h"
#include "render/local_preview_cache.h"
#include "render/source_coordinate_map.h"
using namespace LocalEdit;
namespace {
MaskComponent linear(QString id="line"){MaskComponent c;c.id=id;c.geometry=LinearGeometry{{0,0},{10,0}};return c;}
MaskComponent radial(){MaskComponent c;c.id="ellipse";c.geometry=RadialGeometry{{20,30},10,4,0};return c;}
QImage alphaImage(QSize size){QImage result(size,QImage::Format_RGBA32FPx4);for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x){auto *p=reinterpret_cast<float *>(result.scanLine(y))+4*x;p[0]=.18f;p[1]=.3f;p[2]=1.5f;p[3]=x%7==0?0.f:x%7==1?.4f:1.f;}return result;}
}
class LocalGradientTests:public QObject {
    Q_OBJECT
private slots:
    void cachedGeometryUsesActualGradientContent(){
        SourceFrame source;source.path="cache-gradient";source.sha256=QString(64,'a');source.originalSize={32,24};source.linear=alphaImage(source.originalSize);
        PhotoDocument doc;doc.asset.id="asset";doc.asset.sourcePath=source.path;doc.asset.sourceHash=source.sha256;doc.asset.normalizedSize=source.originalSize;
        AdjustmentLayer layer;layer.id="layer";layer.parameters.exposure=.5;layer.mask.components={linear()};doc.layers={layer};
        LocalPreviewCache cache;QString error;
        const auto render=[&]{return renderDocumentPreviewLinear(source,doc,doc.global,3840,error,{},&cache);};
        const auto first=render();QVERIFY2(!first.isNull(),qPrintable(error));QCOMPARE(cache.stats().builds,quint64(2));
        doc.layers[0].mask.components[0].geometry=LinearGeometry{{8,0},{32,0}}; // same versions, new contents
        const auto moved=render();QVERIFY(!moved.isNull());QVERIFY(moved!=first);QCOMPARE(cache.stats().builds,quint64(3));
        QCOMPARE(moved,renderDocumentFullLinear(source,doc,error));
        doc.layers[0].parameters.exposure=.8;const auto adjusted=render();QVERIFY(!adjusted.isNull());QCOMPARE(cache.stats().builds,quint64(3));
        QCOMPARE(adjusted,renderDocumentFullLinear(source,doc,error));
        doc.layers[0].mask.components[0].geometry=RadialGeometry{{16,12},18,9,32};
        const auto changedKind=render();QVERIFY(!changedKind.isNull());QCOMPARE(cache.stats().builds,quint64(4));QVERIFY(changedKind!=adjusted);
        QCOMPARE(changedKind,renderDocumentFullLinear(source,doc,error));
    }
    void knownLinearRampPlanesAndDirection(){
        Mask mask;mask.components={linear()};BrushMaskReference scalar(mask);PreparedBrushMask indexed(mask);QVERIFY(scalar.valid());QVERIFY(indexed.valid());
        for(double x:{-10.,0.,2.5,5.,7.5,10.,20.}){
            const float expected=float(std::clamp(x/10.,0.,1.));
            QCOMPARE(scalar.sample({x,100}),expected);QCOMPARE(indexed.sample({x,-100}),expected);
        }
        mask.components[0].geometry=LinearGeometry{{10,0},{0,0}};QCOMPARE(BrushMaskReference(mask).sample({2.5,7}),.75f);
        mask.components[0].geometry=LinearGeometry{{2,3},{12,13}};QCOMPARE(BrushMaskReference(mask).sample({17,-2}),.5f);
    }
    void knownRadialAxesRotationAndProfile(){
        Mask mask;mask.components={radial()};
        for(double degrees:{0.,30.,90.,180.}){
            auto &g=std::get<RadialGeometry>(mask.components[0].geometry);g.angleDegrees=degrees;
            const double a=degrees*3.14159265358979323846/180;const QPointF axis(std::cos(a),std::sin(a));
            const BrushMaskReference scalar(mask);const PreparedBrushMask indexed(mask);QVERIFY(scalar.valid());QVERIFY(indexed.valid());
            const double distances[]{0,.25,.5,.75,1,2};const float expected[]{1,.84375f,.5f,.15625f,0,0};
            for(int i=0;i<6;++i){const auto p=g.center+axis*(10*distances[i]);
                QVERIFY(std::abs(scalar.sample(p)-expected[i])<=2e-7f);QCOMPARE(indexed.sample(p),scalar.sample(p));}
        }
        auto &g=std::get<RadialGeometry>(mask.components[0].geometry);g.angleDegrees=90;
        QVERIFY(std::abs(BrushMaskReference(mask).sample({20,35})-.5f)<1e-7f);QCOMPARE(BrushMaskReference(mask).sample({25,30}),0.f);
    }
    void orderedMixedComponentsAlphaAndInvalidGeometry(){
        Mask mask;auto l=linear();l.strength=.8;auto r=radial();r.geometry=RadialGeometry{{5,0},4,2,0};r.operation=MaskOperation::Subtract;r.strength=.25;
        auto b=l;b.id="brush";b.strength=.5;BrushGeometry brush;brush.points={{5,0}};brush.radius=1;brush.hardness=1;b.geometry=brush;
        mask.components={l,r,b};QVERIFY(std::abs(BrushMaskReference(mask).sample({5,0})-.65f)<1e-7f);
        QCOMPARE(PreparedBrushMask(mask).sample({5,0}),BrushMaskReference(mask).sample({5,0}));
        std::reverse(mask.components.begin(),mask.components.end());QVERIFY(std::abs(BrushMaskReference(mask).sample({5,0})-.625f)<1e-7f);
        QCOMPARE(BrushMaskReference(mask).sample({5,0},0),0.f);QCOMPARE(BrushMaskReference(mask).sample({5,0},.4f),BrushMaskReference(mask).sample({5,0},1));
        QVERIFY(std::isnan(PreparedBrushMask(mask).sample({5,0},std::numeric_limits<float>::quiet_NaN())));
        mask.components={linear()};mask.components[0].geometry=LinearGeometry{{2,3},{2,3}};QVERIFY(!PreparedBrushMask(mask).valid());
        mask.components={radial()};std::get<RadialGeometry>(mask.components[0].geometry).radiusY=0;QVERIFY(!BrushMaskReference(mask).valid());
        std::get<RadialGeometry>(mask.components[0].geometry).radiusY=4;mask.components[0].strength=std::numeric_limits<double>::infinity();QVERIFY(!PreparedBrushMask(mask).valid());
    }
    void mixedTilesAndPreparedGeometryMatchScalar(){
        Mask mask;auto l=linear();l.geometry=LinearGeometry{{1200,1000},{4500,2800}};l.strength=.7;
        auto r=radial();r.geometry=RadialGeometry{{3200,2100},2200,900,35};r.operation=MaskOperation::Subtract;r.strength=.8;
        MaskComponent b;b.id="brush";b.strength=.5;BrushGeometry brush;brush.points={{1800,1600},{2100,1900}};brush.radius=700;b.geometry=brush;
        mask.components={l,r,b};const BrushMaskReference scalar(mask);const PreparedBrushMask indexed(mask);QVERIFY(scalar.valid());QVERIFY(indexed.valid());
        Recipe geometry;geometry.crop={.1,.08,.85,.84};geometry.angle=9;geometry.perspective={.025,-.03};geometry.distortion=.1;geometry.flipHorizontal=geometry.flipVertical=true;
        const QSize output(530,519);const auto alpha=alphaImage(output);MaskMapping mapping{{6000,4000},output,geometry};
        QString error;QVector<float> expected;QVERIFY2(scalar.rasterize(mapping,alpha,QRect(QPoint(),output),expected,error),qPrintable(error));
        for(int tileSize:{256,512}){
            QVector<float> actual;QVERIFY2(rasterizeMaskPreview(indexed,mapping,alpha,actual,error,{},tileSize),qPrintable(error));
            QCOMPARE(actual.size(),expected.size());QVERIFY(std::memcmp(actual.constData(),expected.constData(),size_t(actual.size())*sizeof(float))==0);
        }
        QVector<float> sentinel{123};CancellationSource canceled;canceled.cancel();
        QVERIFY(!indexed.rasterize(mapping,alpha,QRect(0,0,256,256),sentinel,error,canceled.token()));QCOMPARE(sentinel,QVector<float>{123});
    }
    void controlMappingRoundTripsAllGeometry(){
        const QSize size(6960,4640);
        for(double angle:{-45.,0.,29.})for(double distortion:{-.4,0.,.4})for(int flips=0;flips<4;++flips){
            Recipe r;r.crop={.15,.12,.72,.76};r.angle=angle;r.distortion=distortion;r.perspective={.18,-.16};r.flipHorizontal=flips&1;r.flipVertical=flips&2;
            for(QPointF uv:{QPointF(.1,.1),QPointF(.5,.5),QPointF(.9,.85),QPointF(-.2,.4),QPointF(1.2,.6)}){
                const auto source=outputToSource(uv,size,r);QPointF restored(99,98);
                QVERIFY2(sourceToOutput(source,size,r,restored),qPrintable(QString("angle=%1 k=%2 flips=%3").arg(angle).arg(distortion).arg(flips)));
                QVERIFY(QLineF(uv,restored).length()<1e-10);QVERIFY(QLineF(source,outputToSource(restored,size,r)).length()<1e-6);
            }
        }
        QPointF sentinel(99,98);Recipe invalid;invalid.crop.setWidth(0);QVERIFY(!sourceToOutput({1,2},size,invalid,sentinel));QCOMPARE(sentinel,QPointF(99,98));
    }
    void fullSourceAndPreparedPreviewReplaySameGradients(){
        SourceFrame source;source.path="synthetic";source.sha256=QString(64,'a');source.originalSize={32,24};source.linear=alphaImage(source.originalSize);
        PhotoDocument doc;doc.asset.id="asset";doc.asset.sourcePath=source.path;doc.asset.sourceHash=source.sha256;doc.asset.normalizedSize=source.originalSize;
        doc.global.crop={.1,.08,.8,.84};doc.global.angle=9;doc.global.perspective={.03,-.02};doc.global.distortion=.06;doc.global.flipHorizontal=true;doc.global.exposure=.3;
        AdjustmentLayer layer;layer.id="layer";layer.parameters.exposure=.6;layer.parameters.saturation=1.2;
        auto l=linear();l.geometry=LinearGeometry{{8,5},{24,18}};auto r=radial();r.geometry=RadialGeometry{{18,12},12,6,24};r.operation=MaskOperation::Subtract;r.strength=.7;
        layer.mask.components={l,r};doc.layers={layer};QString error;
        const auto full=renderDocumentFullLinear(source,doc,error);QVERIFY2(!full.isNull(),qPrintable(error));
        auto prepared=source;Recipe prepare=doc.global;prepare.exposure=0;
        prepare.flipHorizontal=false;prepare.flipVertical=false;
        prepared.linear=renderLinear(source,prepare,0,error);QVERIFY(!prepared.linear.isNull());
        prepared.sourcePixelsPerPixelX=double(source.linear.width())*doc.global.crop.width()/prepared.linear.width();
        prepared.sourcePixelsPerPixelY=double(source.linear.height())*doc.global.crop.height()/prepared.linear.height();
        auto remaining=doc.global;remaining.crop={0,0,1,1};remaining.angle=0;remaining.perspective={0,0};remaining.distortion=0;
        const auto preview=renderDocumentPreviewLinear(prepared,doc,remaining,3840,error);QVERIFY2(!preview.isNull(),qPrintable(error));QCOMPARE(preview.size(),full.size());
        for(int y=0;y<full.height();++y)for(int x=0;x<full.width()*4;++x){const float a=reinterpret_cast<const float *>(full.constScanLine(y))[x],b=reinterpret_cast<const float *>(preview.constScanLine(y))[x];QVERIFY(std::abs(a-b)<=2e-6f+2e-6f*std::abs(a));}
        const auto frozen=doc;doc.layers[0].mask.components[0].geometry=LinearGeometry{{0,0},{32,0}};
        const auto changed=renderDocumentFullLinear(source,doc,error);QVERIFY(!changed.isNull());QVERIFY(changed!=full);QCOMPARE(frozen.layers[0].mask.components[0].geometry,MaskGeometry(LinearGeometry{{8,5},{24,18}}));
    }
};
QTEST_MAIN(LocalGradientTests)
#include "local_gradient_tests.moc"
