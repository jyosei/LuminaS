#include <QtTest>
#include <limits>
#include <QLineF>
#include "app/local_gradient_transaction.h"
#include "core/commands.h"
#include "render/cpu_renderer.h"
using namespace LocalEdit;
namespace {
DocumentSnapshot baseDocument(){
    auto doc=std::make_shared<PhotoDocument>();doc->asset.id="asset";doc->asset.sourcePath="synthetic";doc->asset.sourceHash=QString(64,'a');doc->asset.normalizedSize={6000,4000};
    doc->global.crop={.1,.1,.8,.8};doc->global.angle=9;doc->global.perspective={.03,-.02};doc->global.distortion=.06;doc->global.flipHorizontal=true;
    AdjustmentLayer layer;layer.id="layer";layer.name="Local";layer.parameters.exposure=.5;doc->layers={layer};return doc;
}
}
class LocalGradientTransactionTests:public QObject {
    Q_OBJECT
private slots:
    void createEventDensityFrozenGeometryAndOneUndo(){
        auto base=baseDocument();QString error;GradientTransaction sparse,dense;
        QVERIFY(sparse.beginCreate(base,"layer","gradient",GradientKind::Linear,{.2,.3},.7,MaskOperation::Add,error));
        QVERIFY(dense.beginCreate(base,"layer","gradient",GradientKind::Linear,{.2,.3},.7,MaskOperation::Add,error));
        QCOMPARE(sparse.preview()->layers,base->layers); // a click has no valid line yet
        const auto click=sparse.preview();QVERIFY(sparse.append({.2,.3},error));QCOMPARE(sparse.preview(),click);
        QVERIFY(sparse.append({.8,.7},error));
        for(int i=1;i<=20;++i)QVERIFY(dense.append({.2+.6*i/20.,.3+.4*i/20.},error));
        // Assign the exact same endpoint: event history is not the geometry.
        QVERIFY(dense.append({.8,.7},error));
        const auto stationary=dense.preview();QVERIFY(dense.append({.8,.7},error));QCOMPARE(dense.preview(),stationary);
        QCOMPARE(sparse.preview()->layers,dense.preview()->layers);
        const auto &g=std::get<LinearGeometry>(sparse.preview()->layers[0].mask.components[0].geometry);
        QCOMPARE(g.start,outputToSource({.2,.3},base->asset.normalizedSize,base->global));
        QCOMPARE(g.end,outputToSource({.8,.7},base->asset.normalizedSize,base->global));
        auto refined=*base;refined.asset.sourceFacts.insert("refined",true);
        const auto finished=sparse.finish(std::make_shared<const PhotoDocument>(refined),error);QVERIFY2(finished,qPrintable(error));QVERIFY(!sparse.active());
        QCOMPARE(finished->asset.sourceFacts,refined.asset.sourceFacts);
        EditHistory history;QVERIFY(history.commitSnapshot(base->asset.sourcePath,base,"setup",&error));
        QVERIFY(history.commitSnapshot(base->asset.sourcePath,finished,"gradient",&error));QCOMPARE(history.undo(),base->asset.sourcePath);
        QVERIFY(history.snapshot(base->asset.sourcePath)->layers[0].mask.components.isEmpty());QCOMPARE(history.redo(),base->asset.sourcePath);
        QCOMPARE(history.snapshot(base->asset.sourcePath)->layers[0].mask.components,finished->layers[0].mask.components);
    }
    void editLineEndpointTranslationCancelAndNoop(){
        auto doc=*baseDocument();doc.global={};MaskComponent c;c.id="line";c.geometry=LinearGeometry{{1000,1000},{4000,3000}};doc.layers[0].mask.components={c};
        auto base=std::make_shared<const PhotoDocument>(doc);GradientTransaction edit;QString error;
        QVERIFY(edit.beginEdit(base,"layer","line",0,{.2,.3},error));QVERIFY(edit.append({.3,.4},error));
        auto g=std::get<LinearGeometry>(edit.preview()->layers[0].mask.components[0].geometry);
        QCOMPARE(g.start,QPointF(1600,1400));QCOMPARE(g.end,QPointF(4000,3000));
        edit.cancel();QVERIFY(!edit.active());QCOMPARE(base->layers,doc.layers);
        QVERIFY(edit.beginEdit(base,"layer","line",2,{.2,.3},error));QVERIFY(edit.append({.3,.4},error));
        g=std::get<LinearGeometry>(edit.preview()->layers[0].mask.components[0].geometry);QCOMPARE(g.start,QPointF(1600,1400));QCOMPARE(g.end,QPointF(4600,3400));
        QVERIFY(edit.append({.2,.3},error));QVERIFY(!edit.finish(base,error));QVERIFY(error.isEmpty());QVERIFY(!edit.active());
    }
    void radialCreationAxisResizeRotationAndStableIds(){
        auto doc=*baseDocument();doc.global={};auto base=std::make_shared<const PhotoDocument>(doc);GradientTransaction create;QString error;
        QVERIFY(create.beginCreate(base,"layer","radial",GradientKind::Radial,{.5,.5},1,MaskOperation::Add,error));
        QVERIFY(create.append({.6,.5},error));auto created=create.finish(base,error);QVERIFY2(created,qPrintable(error));
        auto g=std::get<RadialGeometry>(created->layers[0].mask.components[0].geometry);QCOMPARE(g.center,QPointF(3000,2000));QVERIFY(std::abs(g.radiusX-600)<1e-8);QCOMPARE(g.radiusX,g.radiusY);
        GradientTransaction resize;QVERIFY(resize.beginEdit(created,"layer","radial",1,{.6,.5},error));QVERIFY(resize.append({.65,.5},error));
        auto resized=resize.finish(created,error);QVERIFY(resized);g=std::get<RadialGeometry>(resized->layers[0].mask.components[0].geometry);QVERIFY(std::abs(g.radiusX-900)<1e-8);QVERIFY(std::abs(g.radiusY-600)<1e-8);
        GradientTransaction rotate;QVERIFY(rotate.beginEdit(resized,"layer","radial",3,{.6875,.5},error));QVERIFY(rotate.append({.5,.78125},error));
        const auto rotated=rotate.finish(resized,error);QVERIFY(rotated);g=std::get<RadialGeometry>(rotated->layers[0].mask.components[0].geometry);QVERIFY(std::abs(g.angleDegrees-90)<1e-10);QCOMPARE(rotated->layers[0].mask.components[0].id,QString("radial"));
        const auto handles=gradientHandlePoints(g);QCOMPARE(handles.size(),4);QVERIFY(QLineF(handles[1],g.center+QPointF(0,900)).length()<1e-8);
        auto tiny=*rotated;auto &small=std::get<RadialGeometry>(tiny.layers[0].mask.components[0].geometry);small.radiusX=.1;small.angleDegrees=1e8;
        const auto tinyBase=std::make_shared<const PhotoDocument>(tiny);GradientTransaction noMove;
        QVERIFY(noMove.beginEdit(tinyBase,"layer","radial",1,{.5,.5},error));QVERIFY(noMove.append({.5,.5},error));
        QVERIFY(!noMove.finish(tinyBase,error));QVERIFY(error.isEmpty());
    }
    void invalidInputConflictsAndKnownZeroNeverCommit(){
        const auto base=baseDocument();QString error;GradientTransaction gesture;
        QVERIFY(!gesture.beginCreate(base,"layer","",GradientKind::Linear,{.2,.3},1,MaskOperation::Add,error));QVERIFY(!gesture.active());
        QVERIFY(gesture.beginCreate(base,"layer","line",GradientKind::Linear,{.2,.3},1,MaskOperation::Add,error));
        QVERIFY(!gesture.finish(base,error));QVERIFY(error.isEmpty());
        QVERIFY(gesture.beginCreate(base,"layer","line",GradientKind::Linear,{.2,.3},0,MaskOperation::Add,error));QVERIFY(gesture.append({.8,.7},error));QVERIFY(!gesture.finish(base,error));QVERIFY(error.isEmpty());
        QVERIFY(gesture.beginCreate(base,"layer","line",GradientKind::Linear,{.2,.3},1,MaskOperation::Subtract,error));QVERIFY(gesture.append({.8,.7},error));QVERIFY(!gesture.finish(base,error));QVERIFY(error.isEmpty());
        QVERIFY(gesture.beginCreate(base,"layer","line",GradientKind::Linear,{.2,.3},1,MaskOperation::Add,error));QVERIFY(gesture.append({.8,.7},error));const auto before=gesture.preview();
        QVERIFY(!gesture.append({std::numeric_limits<double>::quiet_NaN(),.2},error));QCOMPARE(gesture.preview(),before);
        auto conflict=*base;conflict.global.exposure=.1;QVERIFY(!gesture.finish(std::make_shared<const PhotoDocument>(conflict),error));QVERIFY(!error.isEmpty());QVERIFY(!gesture.active());
        auto overflow=*base;overflow.layers[0].mask.version=std::numeric_limits<quint64>::max();QVERIFY(!gesture.beginCreate(std::make_shared<const PhotoDocument>(overflow),"layer","line",GradientKind::Linear,{.2,.3},1,MaskOperation::Add,error));
    }
};
QTEST_MAIN(LocalGradientTransactionTests)
#include "local_gradient_transaction_tests.moc"
