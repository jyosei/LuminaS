#include <QtTest>
#include <bit>
#include <cstring>
#include <cmath>
#include "render/local_renderer.h"
#include "render/local_reference.h"
#include "render/vignette_operator.h"
#include "render/grain_operator.h"
#include "render/geometry_transform.h"

using namespace LocalEdit;
namespace {
SourceFrame source(){
    SourceFrame frame;frame.path="synthetic";frame.sha256=QString(64,'a');frame.originalSize={13,9};
    frame.linear=QImage(frame.originalSize,QImage::Format_RGBA32FPx4);
    for(int y=0;y<9;++y)for(int x=0;x<13;++x){
        auto *p=reinterpret_cast<float *>(frame.linear.scanLine(y))+4*x;
        p[0]=-.1f+.02f*x+.01f*y;p[1]=.05f+.03f*x-.02f*y;p[2]=1.5f-.04f*x+.025f*y;
        p[3]=x%3==0?0.f:x%3==1?.4f:1.f;
    }
    return frame;
}
PhotoDocument document(const SourceFrame &frame){
    PhotoDocument doc;doc.asset.id="asset";doc.asset.sourcePath=frame.path;
    doc.asset.sourceHash=frame.sha256;doc.asset.normalizedSize=frame.originalSize;return doc;
}
AdjustmentLayer layer(QString id){
    AdjustmentLayer result;result.id=id;
    MaskComponent component;component.id=id+"-brush";component.strength=.7;
    BrushGeometry brush;brush.points={{6.5,4.5}};brush.radius=4;brush.hardness=.4;
    component.geometry=brush;result.mask.components={component};return result;
}
bool exact(const QImage &a,const QImage &b){
    if(a.isNull()||b.isNull()||a.size()!=b.size()||a.format()!=b.format())return false;
    const qsizetype rowBytes=qsizetype(a.width())*a.depth()/8;
    for(int y=0;y<a.height();++y)if(std::memcmp(a.constScanLine(y),b.constScanLine(y),size_t(rowBytes)))return false;
    return true;
}
}
class LocalRendererTests:public QObject {
    Q_OBJECT
private slots:
    void emptyUsesLegacy(){
        const auto frame=source();auto doc=document(frame);doc.global.contrast=.2;doc.global.saturation=.8;
        QString error;const auto full=renderDocumentFullLinear(frame,doc,error);
        QVERIFY2(!full.isNull(),qPrintable(error));
        QVERIFY(exact(full,renderLinear(frame,doc.global,0,error)));
        const auto preview=renderDocumentPreview(frame,doc,doc.global);
        QVERIFY2(!preview.image.isNull(),qPrintable(preview.error));
        QVERIFY(exact(preview.image,renderFrame(frame,doc.global).image));
        auto neutral=layer("neutral");doc.layers={neutral};
        QVERIFY(exact(full,renderDocumentFullLinear(frame,doc,error)));
    }
    void localBeforePostEffects(){
        const auto frame=source();auto doc=document(frame);auto adjustment=layer("adjustment");
        adjustment.parameters.exposure=.6;adjustment.parameters.saturation=1.2;adjustment.opacity=.65;
        doc.layers={adjustment};doc.global.exposure=.3;doc.global.vignetteAmount=-.4;doc.global.grainAmount=.2;
        auto prefix=doc.global;prefix.vignetteAmount=prefix.grainAmount=0;
        QString error;auto expected=renderLinear(frame,prefix,0,error);QVERIFY2(!expected.isNull(),qPrintable(error));
        QVector<float> coverage;const BrushMaskReference reference(adjustment.mask);
        QVERIFY(reference.rasterize({frame.originalSize,expected.size(),doc.global},expected,
            QRect(QPoint(),expected.size()),coverage,error));
        const LocalPixelTransform transform(adjustment.parameters);
        const PostCropVignette vignette(doc.global,expected.size());
        const ProceduralGrain grain(doc.global,expected.size(),1,1);QVERIFY(grain.valid());
        int nonzero=0,fractional=0;
        for(int y=0;y<9;++y)for(int x=0;x<13;++x){
            auto *p=reinterpret_cast<float *>(expected.scanLine(y))+4*x;
            const float mask=coverage[y*13+x];nonzero+=mask>0;fractional+=mask>0&&mask<1;
            QVERIFY(transform.apply(p,mask,float(adjustment.opacity)));
            if(p[3]>1e-8f){vignette.apply(p,x,y);grain.apply(p,x,y);}
        }
        QVERIFY(nonzero>0);QVERIFY(fractional>0);
        const auto actual=renderDocumentFullLinear(frame,doc,error);QVERIFY2(!actual.isNull(),qPrintable(error));
        QVERIFY(exact(actual,expected));
        QVERIFY(exact(actual,renderDocumentPreviewLinear(frame,doc,doc.global,3840,error)));
        const auto frozen=doc;doc.layers[0].parameters.exposure=-1;
        QVERIFY(exact(actual,renderDocumentFullLinear(frame,frozen,error)));
        const auto changed=renderDocumentFullLinear(frame,doc,error);QVERIFY(!exact(actual,changed));
    }
    void orderedLayers(){
        const auto frame=source();auto doc=document(frame);auto a=layer("contrast"),b=layer("exposure");
        a.parameters.contrast=.4;b.parameters.exposure=.7;doc.layers={a,b};
        QString error;const auto ab=renderDocumentFullLinear(frame,doc,error);QVERIFY2(!ab.isNull(),qPrintable(error));
        std::swap(doc.layers[0],doc.layers[1]);const auto ba=renderDocumentFullLinear(frame,doc,error);
        QVERIFY2(!ba.isNull(),qPrintable(error));QVERIFY(!exact(ab,ba));
        for(int y=0;y<9;++y)for(int x=0;x<13;++x){
            const auto alpha=[&](const QImage &image){return reinterpret_cast<const float *>(image.constScanLine(y))[4*x+3];};
            QCOMPARE(std::bit_cast<quint32>(alpha(ab)),std::bit_cast<quint32>(alpha(ba)));
        }
    }
    void preparedPreviewKeepsOriginalMaskCoordinates(){
        const auto frame=source();auto doc=document(frame);
        doc.global.crop={.1,.08,.8,.84};doc.global.angle=9;doc.global.perspective={.03,-.02};
        doc.global.distortion=.06;doc.global.flipHorizontal=true;doc.global.exposure=.3;
        auto adjustment=layer("mapped");adjustment.parameters.exposure=.8;doc.layers={adjustment};
        QString error;const auto full=renderDocumentFullLinear(frame,doc,error);QVERIFY2(!full.isNull(),qPrintable(error));
        auto prepared=frame;prepared.linear=renderLinear(frame,preparedGeometry(doc.global),0,error);
        QVERIFY2(!prepared.linear.isNull(),qPrintable(error));
        prepared.sourcePixelsPerPixelX=double(frame.linear.width())*doc.global.crop.width()/prepared.linear.width();
        prepared.sourcePixelsPerPixelY=double(frame.linear.height())*doc.global.crop.height()/prepared.linear.height();
        auto remaining=doc.global;clearPreparedGeometry(remaining);
        const auto preview=renderDocumentPreviewLinear(prepared,doc,remaining,3840,error);
        QVERIFY2(!preview.isNull(),qPrintable(error));QCOMPARE(preview.size(),full.size());
        for(int y=0;y<full.height();++y)for(int x=0;x<full.width()*4;++x){
            const float a=reinterpret_cast<const float *>(full.constScanLine(y))[x];
            const float b=reinterpret_cast<const float *>(preview.constScanLine(y))[x];
            QVERIFY2(std::abs(a-b)<=2e-6f+2e-6f*std::abs(a),"prepared mask/RGB mapping drift");
        }
        QVERIFY(renderDocumentFullLinear(prepared,doc,error).isNull());
    }
    void rejectsWrongSourceAndCancellation(){
        auto frame=source();const auto doc=document(frame);QString error;
        auto bad=frame;bad.sha256=QString(64,'b');QVERIFY(renderDocumentFullLinear(bad,doc,error).isNull());QVERIFY(!error.isEmpty());
        bad=frame;bad.sourcePixelsPerPixel=2;QVERIFY(renderDocumentFullLinear(bad,doc,error).isNull());
        bad=frame;bad.originalSize={12,9};QVERIFY(renderDocumentFullLinear(bad,doc,error).isNull());
        bad=frame;bad.linear=bad.linear.scaled(6,4);QVERIFY(renderDocumentFullLinear(bad,doc,error).isNull());
        CancellationSource cancel;cancel.cancel();QVERIFY(renderDocumentFullLinear(frame,doc,error,cancel.token()).isNull());
        auto unrelated=doc.global;unrelated.exposure=1;
        QVERIFY(renderDocumentPreviewLinear(frame,doc,unrelated,3840,error).isNull());
        QVERIFY(renderDocumentPreviewLinear(frame,doc,doc.global,4000,error).isNull());
    }
};
QTEST_GUILESS_MAIN(LocalRendererTests)
#include "local_renderer_tests.moc"
