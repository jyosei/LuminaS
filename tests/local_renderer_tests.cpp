#include <QtTest>
#include <bit>
#include <cstring>
#include <cmath>
#include <functional>
#include "render/local_renderer.h"
#include "render/local_reference.h"
#include "render/vignette_operator.h"
#include "render/grain_operator.h"
#include "render/geometry_transform.h"
#include "render/local_preview_cache.h"

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
    void sourceDependencyMatrixMatchesIndependentRender(){
        using Change=std::function<void(SourceFrame &,PhotoDocument &)>;
        struct Case {const char *name;Change setup,change;bool changesPixels,newCoverage;};
        const Change neutral=[](SourceFrame &,PhotoDocument &){};
        const QVector<Case> cases{
            {"draft-balance",neutral,[](SourceFrame &s,PhotoDocument &){s.draftBalance[0]=1.1f;},true,false},
            {"raw-interpretation",[](SourceFrame &,PhotoDocument &d){d.global.warmth=.3;},
                [](SourceFrame &s,PhotoDocument &d){s.raw=true;d.asset.raw=true;},true,true},
            {"camera-matrix-with-CA",[](SourceFrame &s,PhotoDocument &d){
                s.raw=true;d.asset.raw=true;s.cameraToWork={1,0,0,0,1,0,0,0,1};
                d.global.chromaticAberration={.2,-.1};},
                [](SourceFrame &s,PhotoDocument &){s.cameraToWork={1,.15f,0,0,1,.1f,.05f,0,1};},true,false},
            {"camera-WB-provenance",neutral,[](SourceFrame &s,PhotoDocument &){s.cameraWB={1.9,1,.7,1};},false,false},
            {"raw-warmth-provenance",neutral,[](SourceFrame &s,PhotoDocument &){s.rawWarmth=.2;},false,false},
            {"raw-tint-provenance",neutral,[](SourceFrame &s,PhotoDocument &){s.rawTint=-.1;},false,false},
            {"new-alpha-frame",neutral,[](SourceFrame &s,PhotoDocument &){
                reinterpret_cast<float *>(s.linear.scanLine(4))[6*4+3]=1;},true,true},
            {"same-pixels-new-buffer",neutral,[](SourceFrame &s,PhotoDocument &){s.linear=s.linear.copy();},false,true},
            {"source-hash",neutral,[](SourceFrame &s,PhotoDocument &d){s.sha256=QString(64,'c');d.asset.sourceHash=s.sha256;},false,true},
            {"axis-support",[](SourceFrame &,PhotoDocument &d){d.global.grainAmount=.2;},
                [](SourceFrame &s,PhotoDocument &){s.sourcePixelsPerPixelX=2;s.sourcePixelsPerPixelY=3;},true,false}
        };
        for(const auto &item:cases){
            qInfo()<<"source dependency"<<item.name;
            auto frame=source();auto doc=document(frame);doc.layers={layer("dependency")};
            doc.layers[0].parameters.exposure=.5;item.setup(frame,doc);
            LocalPreviewCache cache;QString error;
            const auto render=[&](LocalPreviewCache *selected){
                return renderDocumentPreviewLinear(frame,doc,doc.global,3840,error,{},selected);
            };
            const auto before=render(&cache);QVERIFY2(!before.isNull(),qPrintable(error));
            QVERIFY(exact(before,render(nullptr)));const auto builds=cache.stats().builds;
            item.change(frame,doc);
            const auto independent=render(nullptr);QVERIFY2(!independent.isNull(),qPrintable(error));
            if(item.changesPixels)QVERIFY2(!exact(before,independent),item.name);
            const auto actual=render(&cache);QVERIFY2(!actual.isNull(),qPrintable(error));
            QVERIFY2(exact(actual,independent),item.name);
            QCOMPARE(cache.stats().builds,builds+(item.newCoverage?2:1));
            const auto warmBuilds=cache.stats().builds;QVERIFY(exact(actual,render(&cache)));
            QCOMPARE(cache.stats().builds,warmBuilds);
        }
    }
    void legacySpatialScaleInvalidatesBaseWithExplicitAxes(){
        auto frame=source();frame.sourcePixelsPerPixelX=frame.sourcePixelsPerPixelY=1;
        auto doc=document(frame);doc.global.sharpenAmount=.7;doc.global.sharpenRadius=2;
        doc.layers={layer("scale")};doc.layers[0].parameters.exposure=.5;
        LocalPreviewCache cache;QString error;
        const auto render=[&](LocalPreviewCache *selected){
            return renderDocumentPreviewLinear(frame,doc,doc.global,3840,error,{},selected);
        };
        const auto initial=render(&cache);QVERIFY2(!initial.isNull(),qPrintable(error));
        QVERIFY(exact(initial,render(nullptr)));const auto builds=cache.stats().builds;
        frame.sourcePixelsPerPixel=4;
        QCOMPARE(frame.pixelScaleX(),1.);QCOMPARE(frame.pixelScaleY(),1.);
        const auto independent=render(nullptr);QVERIFY2(!independent.isNull(),qPrintable(error));
        QVERIFY(!exact(initial,independent)); // fixture actually exercises physical filter support
        const auto actual=render(&cache);QVERIFY2(!actual.isNull(),qPrintable(error));
        QVERIFY2(exact(actual,independent),"cached base reused obsolete legacy spatial scale");
        QCOMPARE(cache.stats().builds,builds+1); // coverage is independent of spatial filter radius
    }
    void indexedMaskReusesMetadataButInvalidatesActualMask(){
        const auto frame=source();auto doc=document(frame);doc.layers={layer("cached")};doc.layers[0].parameters.exposure=.5;
        RenderView view;view.localMaskVisible=true;view.localMaskLayer="cached";LocalPreviewCache cache;
        const auto render=[&](const PhotoDocument &d){return renderDocumentPreview(frame,d,d.global,3840,nullptr,view,{},&cache);};
        const auto reference=[&](const PhotoDocument &d){return renderDocumentPreview(frame,d,d.global,3840,nullptr,view);};
        const auto first=render(doc);QVERIFY2(first.error.isEmpty(),qPrintable(first.error));QVERIFY(!first.localMask.isNull());
        const auto key=first.localMask.cacheKey();const auto builds=cache.stats().builds;
        doc.layers[0].parameters.exposure=.8;doc.layers[0].name="Renamed";doc.layers[0].opacity=.4;++doc.layers[0].version;
        const auto metadata=render(doc);QVERIFY(exact(metadata.localMask,reference(doc).localMask));QCOMPARE(metadata.localMask.cacheKey(),key);QCOMPARE(cache.stats().builds,builds);
        doc.layers[0].enabled=false;const auto hidden=render(doc);QCOMPARE(hidden.localMask.cacheKey(),key);QCOMPARE(cache.stats().builds,builds);doc.layers[0].enabled=true;
        const QVector<std::function<void(PhotoDocument &)>> changes{
            [](PhotoDocument &d){d.layers[0].mask.components[0].strength=.9;},
            [](PhotoDocument &d){d.layers[0].mask.components[0].operation=MaskOperation::Subtract;},
            [](PhotoDocument &d){std::get<BrushGeometry>(d.layers[0].mask.components[0].geometry).radius=2;},
            [](PhotoDocument &d){d.layers[0].mask.feather=2;},
            [](PhotoDocument &d){d.layers[0].mask.invert=true;},
            [](PhotoDocument &d){++d.layers[0].mask.version;}
        };
        for(const auto &change:changes){
            auto modified=doc;change(modified);const auto before=cache.stats().builds;const auto actual=render(modified),expected=reference(modified);
            QVERIFY2(actual.error.isEmpty(),qPrintable(actual.error));QVERIFY(exact(actual.image,expected.image));QVERIFY(exact(actual.localMask,expected.localMask));
            QVERIFY(actual.localMask.cacheKey()!=key);QVERIFY(cache.stats().builds>before);QCOMPARE(actual.localMask.colorCount(),0);
        }
        cache.clear();const auto refreshed=render(doc);QVERIFY(exact(refreshed.localMask,first.localMask));QVERIFY(refreshed.localMask.cacheKey()!=key);
        QVERIFY(cache.stats().retainedBytes<=256*1024*1024);
    }
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
    void cacheKeysReuseAndInvalidateActualPixels(){
        auto frame=source();auto doc=document(frame);auto adjustment=layer("cached");
        adjustment.parameters.exposure=.6;doc.layers={adjustment};
        LocalPreviewCache cache;QString error;
        auto render=[&]{return renderDocumentPreviewLinear(frame,doc,doc.global,3840,error,{},&cache);};
        auto first=render();QVERIFY2(!first.isNull(),qPrintable(error));QCOMPARE(cache.stats().builds,quint64(2));
        QVERIFY(exact(first,renderDocumentFullLinear(frame,doc,error)));
        doc.layers[0].parameters.exposure=1;doc.layers[0].version++;doc.revision++;doc.localIdentity++;
        const auto changed=render();QVERIFY2(!changed.isNull(),qPrintable(error));QVERIFY(!exact(first,changed));
        QCOMPARE(cache.stats().builds,quint64(2)); // both base and mask reused
        QVERIFY(exact(changed,renderDocumentFullLinear(frame,doc,error)));
        doc.global.exposure=.3;const auto global=render();QVERIFY2(!global.isNull(),qPrintable(error));
        QCOMPARE(cache.stats().builds,quint64(3)); // mask independent of global colour
        QVERIFY(exact(global,renderDocumentFullLinear(frame,doc,error)));
        doc.layers[0].mask.version++;doc.layers[0].mask.components[0].strength=.2;
        const auto remasked=render();QVERIFY2(!remasked.isNull(),qPrintable(error));
        QCOMPARE(cache.stats().builds,quint64(4));QVERIFY(!exact(global,remasked));
        doc.global.grainAmount=.1;doc.global.vignetteAmount=-.4;
        const auto post=render();QVERIFY2(!post.isNull(),qPrintable(error));
        QCOMPARE(cache.stats().builds,quint64(4)); // post-local style does not rebuild prefix/mask
        QVERIFY(exact(post,renderDocumentFullLinear(frame,doc,error)));
        doc.global.crop={.1,.1,.8,.8};doc.global.angle=8;doc.global.flipHorizontal=true;
        const auto geometry=render();QVERIFY2(!geometry.isNull(),qPrintable(error));
        QCOMPARE(cache.stats().builds,quint64(6));QVERIFY(exact(geometry,renderDocumentFullLinear(frame,doc,error)));
        reinterpret_cast<float *>(frame.linear.scanLine(4))[6*4]=.7f;
        const auto newFrame=render();QVERIFY2(!newFrame.isNull(),qPrintable(error));
        QCOMPARE(cache.stats().builds,quint64(8));QVERIFY(exact(newFrame,renderDocumentFullLinear(frame,doc,error)));
        cache.clear();QVERIFY(exact(newFrame,render()));QCOMPARE(cache.stats().builds,quint64(10));
        QVERIFY(cache.stats().retainedBytes<=256*1024*1024);
    }
    void neutralLayerMaskIsSeparateAndReusesFloatCoverage(){
        const auto frame=source();auto doc=document(frame);doc.layers={layer("overlay")};
        RenderView view;view.localMaskVisible=true;view.localMaskLayer="overlay";LocalPreviewCache cache;
        const auto plain=renderDocumentPreview(frame,doc,doc.global);
        const auto shown=renderDocumentPreview(frame,doc,doc.global,3840,nullptr,view,{},&cache);
        QVERIFY2(shown.error.isEmpty(),qPrintable(shown.error));QVERIFY(exact(plain.image,shown.image));
        QCOMPARE(shown.localMask.format(),QImage::Format_Indexed8);QCOMPARE(shown.localMask.size(),plain.image.size());
        QString error;QVector<float> expected;const BrushMaskReference reference(doc.layers[0].mask);
        QVERIFY(reference.rasterize({frame.originalSize,frame.linear.size(),doc.global},frame.linear,
            QRect(QPoint(),frame.linear.size()),expected,error));
        int covered=0;
        for(int y=0;y<shown.localMask.height();++y)for(int x=0;x<shown.localMask.width();++x){
            const auto value=shown.localMask.constScanLine(y)[x];covered+=value>0;
            QCOMPARE(value,uchar(std::lround(expected[y*shown.localMask.width()+x]*255)));
        }
        QVERIFY(covered>0);QCOMPARE(cache.stats().builds,quint64(2));
        doc.layers[0].parameters.exposure=.5;
        const auto adjusted=renderDocumentPreview(frame,doc,doc.global,3840,nullptr,view,{},&cache);
        QVERIFY2(adjusted.error.isEmpty(),qPrintable(adjusted.error));QVERIFY(exact(shown.localMask,adjusted.localMask));
        QCOMPARE(cache.stats().builds,quint64(3)); // effect builds prefix, reuses float and indexed mask
        QCOMPARE(shown.localMask.cacheKey(),adjusted.localMask.cacheKey());
        QCOMPARE(adjusted.localMask.colorCount(),0); // Canvas alone supplies the palette
        QVERIFY(exact(adjusted.image,renderDocumentPreview(frame,doc,doc.global).image));
        doc.layers[0].enabled=false;
        const auto hidden=renderDocumentPreview(frame,doc,doc.global,3840,nullptr,view,{},&cache);
        QVERIFY(exact(hidden.localMask,shown.localMask));QVERIFY(exact(hidden.image,plain.image));
        view.sharpenMask=true;
        const auto invalid=renderDocumentPreview(frame,doc,doc.global,3840,nullptr,view);
        QVERIFY(invalid.image.isNull());QVERIFY(!invalid.error.isEmpty());
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
