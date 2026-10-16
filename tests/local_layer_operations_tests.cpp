#include <QtTest>
#include <limits>
#include "core/local_layer_operations.h"
#include "core/commands.h"
#include "render/local_renderer.h"
using namespace LocalEdit;
namespace {
SourceFrame frame(){
    SourceFrame source;source.path="synthetic";source.sha256=QString(64,'a');source.originalSize={32,24};
    source.linear=QImage(source.originalSize,QImage::Format_RGBA32FPx4);
    for(int y=0;y<24;++y)for(int x=0;x<32;++x){auto *p=reinterpret_cast<float *>(source.linear.scanLine(y))+4*x;p[0]=.12f;p[1]=.2f;p[2]=.4f;p[3]=1;}
    return source;
}
PhotoDocument document(){
    const auto source=frame();PhotoDocument doc;doc.asset.id="asset";doc.asset.sourcePath=source.path;
    doc.asset.sourceHash=source.sha256;doc.asset.normalizedSize=source.originalSize;
    doc.global.exposure=.1;
    AdjustmentLayer a;a.id="a";a.name="Contrast";a.parameters.contrast=.5;
    MaskComponent brush;brush.id="stroke-a";BrushGeometry shape;shape.points={{16,12}};shape.radius=100;shape.hardness=1;brush.geometry=shape;a.mask.components={brush};
    auto b=a;b.id="b";b.name="Exposure";b.mask.components[0].id="stroke-b";b.parameters={};b.parameters.exposure=1;
    doc.layers={a,b};return doc;
}
LayerChange duplicate(){LayerChange c;c.kind=LayerChangeKind::Duplicate;c.layerId="a";c.name="Contrast copy";c.duplicateId="copy";c.componentIds={"copy-stroke"};return c;}
QByteArray pixels(const QImage &image){return QByteArray(reinterpret_cast<const char *>(image.constBits()),image.sizeInBytes());}
}
class LocalLayerOperationsTests:public QObject {
    Q_OBJECT
private slots:
    void renameVisibilityAndOpacityHaveOneHistoryStep(){
        const auto base=document();EditHistory history;QString error;
        QVERIFY(history.commitSnapshot(base.asset.sourcePath,std::make_shared<const PhotoDocument>(base),"setup",&error));
        LayerChange c;c.layerId="a";c.kind=LayerChangeKind::Rename;c.name=QStringLiteral("  天空  ");
        auto changed=changedLayers(*history.snapshot(base.asset.sourcePath),c,error);QVERIFY2(changed,qPrintable(error));
        QCOMPARE(changed->layers[0].name,QStringLiteral("天空"));QCOMPARE(changed->global,base.global);QCOMPARE(changed->asset,base.asset);
        QVERIFY(history.commitSnapshot(base.asset.sourcePath,changed,"rename",&error));
        QCOMPARE(history.undo(),base.asset.sourcePath);QCOMPARE(history.snapshot(base.asset.sourcePath)->layers,base.layers);
        QCOMPARE(history.redo(),base.asset.sourcePath);QCOMPARE(history.snapshot(base.asset.sourcePath)->layers[0].name,QStringLiteral("天空"));
        c.kind=LayerChangeKind::Visibility;c.visible=false;changed=changedLayers(base,c,error);QVERIFY(changed);QVERIFY(!changed->layers[0].enabled);
        QCOMPARE(changed->layers[0].mask,base.layers[0].mask);
        c.kind=LayerChangeKind::Opacity;c.opacity=.25;changed=changedLayers(base,c,error);QVERIFY(changed);QCOMPARE(changed->layers[0].opacity,.25);
        QCOMPARE(changed->layers[0].parameters,base.layers[0].parameters);QCOMPARE(changed->layers[0].mask,base.layers[0].mask);
    }
    void duplicateIdsGeometryAndIndependentMutation(){
        const auto base=document();QString error;const auto copy=changedLayers(base,duplicate(),error);QVERIFY2(copy,qPrintable(error));
        QCOMPARE(copy->layers.size(),3);QCOMPARE(copy->layers[1].id,QString("copy"));
        QCOMPARE(copy->layers[1].mask.components[0].id,QString("copy-stroke"));
        QCOMPARE(copy->layers[1].mask.components[0].geometry,base.layers[0].mask.components[0].geometry);
        QCOMPARE(copy->layers[1].parameters,base.layers[0].parameters);QCOMPARE(copy->global,base.global);QCOMPARE(copy->asset,base.asset);
        auto modified=*copy;std::get<BrushGeometry>(modified.layers[1].mask.components[0].geometry).points[0]={1,2};
        modified.layers[1].parameters.exposure=2;
        QCOMPARE(base.layers[0].mask,copy->layers[0].mask);QCOMPARE(base.layers[0].parameters,copy->layers[0].parameters);
        QCOMPARE(std::get<BrushGeometry>(copy->layers[1].mask.components[0].geometry).points[0],QPointF(16,12));
        QCOMPARE(copy->layers[0],base.layers[0]);
    }
    void orderAndOpacityAffectActualLinearOutput(){
        const auto base=document();const auto source=frame();QString error;
        const auto ab=renderDocumentFullLinear(source,base,error);QVERIFY2(!ab.isNull(),qPrintable(error));
        LayerChange c;c.kind=LayerChangeKind::Reorder;c.order={"b","a"};
        auto changed=changedLayers(base,c,error);QVERIFY2(changed,qPrintable(error));
        const auto ba=renderDocumentFullLinear(source,*changed,error);QVERIFY(!ba.isNull());QVERIFY(pixels(ab)!=pixels(ba));
        c.kind=LayerChangeKind::Opacity;c.layerId="a";c.opacity=0;changed=changedLayers(base,c,error);QVERIFY(changed);
        const auto zero=renderDocumentFullLinear(source,*changed,error);QVERIFY(!zero.isNull());QVERIFY(pixels(zero)!=pixels(ab));
        c.kind=LayerChangeKind::Remove;const auto removed=changedLayers(base,c,error);QVERIFY(removed);
        QCOMPARE(pixels(zero),pixels(renderDocumentFullLinear(source,*removed,error)));
    }
    void badIdsInvalidPermutationsAndOverflowLeaveBase(){
        const auto base=document();const auto frozen=base;QString error;
        auto c=duplicate();c.duplicateId="a";QVERIFY(!changedLayers(base,c,error));QVERIFY(!error.isEmpty());
        c=duplicate();c.componentIds={"asset"};QVERIFY(!changedLayers(base,c,error));QVERIFY(!error.isEmpty());
        c=duplicate();c.componentIds.clear();QVERIFY(!changedLayers(base,c,error));QVERIFY(!error.isEmpty());
        c.kind=LayerChangeKind::Reorder;c.order={"a","a"};QVERIFY(!changedLayers(base,c,error));QVERIFY(!error.isEmpty());
        c.order={"a","missing"};QVERIFY(!changedLayers(base,c,error));QVERIFY(!error.isEmpty());
        c.order={"a","b"};QVERIFY(!changedLayers(base,c,error));QVERIFY(error.isEmpty());
        c.kind=LayerChangeKind::Rename;c.layerId="a";c.name="   ";QVERIFY(!changedLayers(base,c,error));QVERIFY(!error.isEmpty());
        c.name=QString(4097,'a');QVERIFY(!changedLayers(base,c,error));QVERIFY(!error.isEmpty());
        c.kind=LayerChangeKind::Opacity;c.opacity=std::numeric_limits<double>::quiet_NaN();QVERIFY(!changedLayers(base,c,error));QVERIFY(!error.isEmpty());
        c.opacity=1;QVERIFY(!changedLayers(base,c,error));QVERIFY(error.isEmpty());
        auto exhausted=base;exhausted.layers[0].version=std::numeric_limits<quint64>::max();c.opacity=.5;
        QVERIFY(!changedLayers(exhausted,c,error));QVERIFY(!error.isEmpty());QCOMPARE(base,frozen);
        auto full=base;full.layers.clear();for(int i=0;i<maximumLayers;++i){auto layer=base.layers[0];layer.id=QString("layer%1").arg(i);layer.mask.components[0].id=QString("stroke%1").arg(i);full.layers.append(layer);}
        c=duplicate();c.layerId="layer0";QVERIFY(!changedLayers(full,c,error));QVERIFY(!error.isEmpty());
    }
};
QTEST_MAIN(LocalLayerOperationsTests)
#include "local_layer_operations_tests.moc"
