#include <QtTest>
#include <limits>
#include "app/local_brush_transaction.h"
#include "core/commands.h"
#include "render/cpu_renderer.h"

using namespace LocalEdit;
namespace {
PhotoDocument document(){
    PhotoDocument doc;doc.asset.id="asset";doc.asset.sourcePath="synthetic.jpg";
    doc.asset.sourceHash=QString(64,'a');doc.asset.normalizedSize={100,80};
    AdjustmentLayer layer;layer.id="layer";doc.layers={layer};return doc;
}
DocumentSnapshot snapshot(const PhotoDocument &doc){return std::make_shared<const PhotoDocument>(doc);}
BrushGeometry geometry(DocumentSnapshot doc){return std::get<BrushGeometry>(doc->layers[0].mask.components.last().geometry);}
}
class LocalBrushTransactionTests:public QObject {
    Q_OBJECT
private slots:
    void sourceGeometryAndFrozenSnapshot(){
        auto original=std::make_shared<PhotoDocument>(document());original->global.crop={.1,.1,.8,.8};
        original->global.angle=17;original->global.flipHorizontal=true;
        original->global.perspective={.04,-.03};original->global.distortion=.08;
        const auto frozen=*original;BrushTransaction transaction;QString error;
        QVERIFY2(transaction.begin(original,"layer","stroke",{.3,.4},8,.6,.7,MaskOperation::Add,error),qPrintable(error));
        original->global.angle=-20;original->layers[0].parameters.exposure=2;
        QVERIFY(transaction.append({.5,.6},error));const auto draft=transaction.preview();
        QCOMPARE(draft->global,frozen.global);QCOMPARE(draft->layers[0].parameters,frozen.layers[0].parameters);
        const auto brush=geometry(draft);QCOMPARE(brush.radius,8.);QCOMPARE(brush.hardness,.6);
        const auto &component=draft->layers[0].mask.components[0];QCOMPARE(component.strength,.7);
        QCOMPARE(component.operation,MaskOperation::Add);
        QCOMPARE(brush.points.first(),outputToSource({.3,.4},frozen.asset.normalizedSize,frozen.global));
        QCOMPARE(brush.points.last(),outputToSource({.5,.6},frozen.asset.normalizedSize,frozen.global));
        const auto previous=transaction.preview();
        QVERIFY(!transaction.append({std::numeric_limits<double>::quiet_NaN(),.4},error));
        QCOMPARE(transaction.preview().get(),previous.get());
        transaction.cancel();QVERIFY(!transaction.active());QVERIFY(!transaction.preview());
    }
    void eventDensityAndOneHistoryCommit(){
        const auto doc=document();BrushTransaction sparse,dense;QString error;
        QVERIFY(sparse.begin(snapshot(doc),"layer","stroke",{.2,.5},8,.6,1,MaskOperation::Add,error));
        QVERIFY(dense.begin(snapshot(doc),"layer","stroke",{.2,.5},8,.6,1,MaskOperation::Add,error));
        QVERIFY(sparse.append({.8,.5},error));
        for(int i=1;i<=100;++i)QVERIFY(dense.append({.2+.6*i/100,.5},error));
        const auto a=geometry(sparse.preview()),b=geometry(dense.preview());QCOMPARE(a.points.size(),b.points.size());
        for(qsizetype i=0;i<a.points.size();++i)QVERIFY(QLineF(a.points[i],b.points[i]).length()<1e-8);
        EditHistory history;QVERIFY(history.commitSnapshot(doc.asset.sourcePath,snapshot(doc),"新建层",&error));
        auto asset=doc.asset;asset.sourceFacts={{"refined",true}};
        QVERIFY(history.bindAsset(doc.asset.sourcePath,asset,error));
        const auto result=dense.finish(history.snapshot(doc.asset.sourcePath),error);
        QVERIFY2(result,qPrintable(error));QVERIFY(!dense.active());
        QVERIFY(result->asset.sourceFacts.value("refined").toBool());
        QVERIFY(history.commitSnapshot(doc.asset.sourcePath,result,"笔刷",&error));
        QCOMPARE(history.nextUndo()->name,QString("笔刷"));
        QCOMPARE(history.undo(),doc.asset.sourcePath);QVERIFY(history.snapshot(doc.asset.sourcePath)->layers[0].mask.components.isEmpty());
        QCOMPARE(history.redo(),doc.asset.sourcePath);QCOMPARE(history.snapshot(doc.asset.sourcePath)->layers[0].mask.components.size(),1);
        QVERIFY(!dense.finish(history.snapshot(doc.asset.sourcePath),error));QVERIFY(error.isEmpty());
    }
    void rejectsConflictsAndSkipsKnownNoops(){
        auto doc=document();QString error;
        for(const auto settings:{qMakePair(0.,MaskOperation::Add),qMakePair(1.,MaskOperation::Subtract)}){
            BrushTransaction transaction;
            QVERIFY(transaction.begin(snapshot(doc),"layer","stroke",{.5,.5},8,.6,settings.first,settings.second,error));
            QVERIFY(!transaction.finish(snapshot(doc),error));QVERIFY(error.isEmpty());
        }
        BrushTransaction outside;QVERIFY(outside.begin(snapshot(doc),"layer","outside",{5,.5},4,.6,1,MaskOperation::Add,error));
        QVERIFY(!outside.finish(snapshot(doc),error));QVERIFY(error.isEmpty());
        BrushTransaction changed;QVERIFY(changed.begin(snapshot(doc),"layer","changed",{.5,.5},8,.6,1,MaskOperation::Add,error));
        doc.global.exposure=1;QVERIFY(!changed.finish(snapshot(doc),error));QVERIFY(!error.isEmpty());QVERIFY(!changed.active());
        doc=document();doc.layers[0].mask.version=std::numeric_limits<quint64>::max();
        BrushTransaction invalid;QVERIFY(!invalid.begin(snapshot(doc),"layer","overflow",{.5,.5},8,.6,1,MaskOperation::Add,error));
        QVERIFY(!invalid.active());
        doc=document();QVERIFY(!invalid.begin(snapshot(doc),"layer","asset",{.5,.5},8,.6,1,MaskOperation::Add,error));
        QVERIFY(!invalid.begin(snapshot(doc),"layer","bad",{.5,.5},0,.6,1,MaskOperation::Add,error));
    }
};
QTEST_GUILESS_MAIN(LocalBrushTransactionTests)
#include "local_brush_transaction_tests.moc"
