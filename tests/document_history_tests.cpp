#include <QtTest>
#include <QJsonObject>
#include <limits>
#include "core/commands.h"
#include "core/photo_document.h"

using namespace LocalEdit;

// S3 A2 EditHistory suite. Observes only the declared public API in commands.h
// and photo_document.h; no renderer, no fake picture algorithm.
namespace {
QString hash64(QChar c){return QString(64,c);}

// Handcrafted valid asset identity: 64 hex hash, positive dims, raw facts.
AssetIdentity assetFor(const QString &id,const QString &path){
    AssetIdentity asset;
    asset.id=id;asset.sourcePath=path;
    asset.sourceHash=hash64(QChar('a'));
    asset.normalizedSize=QSize(6000,4000);
    asset.raw=true;
    asset.sourceFacts=QJsonObject{{QStringLiteral("sourceKind"),QStringLiteral("raw")},
        {QStringLiteral("orientation"),1}};
    return asset;
}

// One valid non-empty brush layer, used to distinguish locals.
QVector<AdjustmentLayer> brushLayers(double radius){
    AdjustmentLayer layer;
    layer.id=QStringLiteral("layer-1");layer.name=QStringLiteral("brush");
    MaskComponent component;
    component.id=QStringLiteral("component-1");
    component.operation=MaskOperation::Add;
    component.strength=.8;
    BrushGeometry brush;
    brush.points={{1.5,2.5},{3,4}};brush.radius=radius;brush.hardness=.5;
    component.geometry=brush;
    Mask mask;mask.components={component};
    layer.mask=mask;
    return {layer};
}

DocumentSnapshot makeDocument(const AssetIdentity &asset,double exposure,
    QVector<AdjustmentLayer> layers={},quint64 revision=1,quint64 local=0){
    auto document=std::make_shared<PhotoDocument>();
    document->asset=asset;
    document->global=Recipe{};document->global.exposure=exposure;
    document->layers=std::move(layers);
    document->revision=revision;document->localIdentity=local;
    return document;
}
} // namespace

class DocumentHistoryTests : public QObject {
    Q_OBJECT
private slots:
    void legacyGlobalCommitPreservesLocals(){
        EditHistory history;
        const QString path=QStringLiteral("C:/photos/one.cr3");
        const auto asset=assetFor(QStringLiteral("asset-1"),path);
        QVERIFY(history.commitSnapshot(path,makeDocument(asset,.5,brushLayers(120)),QStringLiteral("global"),nullptr));
        Recipe next;next.exposure=1.5;
        QVERIFY(history.commit(path,next,QStringLiteral("legacy")));
        // Global-only legacy edit keeps locals/asset/algorithm identity intact.
        const auto snapshot=history.snapshot(path);
        QVERIFY(snapshot!=nullptr);
        QCOMPARE(snapshot->global.exposure,1.5);
        QCOMPARE(snapshot->layers.size(),1);
        QCOMPARE(snapshot->asset.id,asset.id);
        QCOMPARE(snapshot->asset.sourceHash,asset.sourceHash);
        QCOMPARE(snapshot->algorithmVersion,localAlgorithmVersion);
    }

    void legacyGlobalUnchangedBeforeAfterAcrossPaths(){
        EditHistory history;
        const QString a=QStringLiteral("C:/photos/a.cr3"),b=QStringLiteral("C:/photos/b.NEF");
        QVERIFY(!history.commit(a,Recipe{},QStringLiteral("noop")));
        QVERIFY(!history.canUndo());
        Recipe one;one.exposure=1;Recipe two;two.exposure=2;
        QVERIFY(history.commit(a,one,QStringLiteral("a1")));
        QVERIFY(history.commit(b,two,QStringLiteral("b1")));
        QCOMPARE(history.value(a).exposure,1);
        QCOMPARE(history.value(b).exposure,2);
        // One chronological timeline across photos.
        QCOMPARE(history.undo(),b);
        QCOMPARE(history.value(b).exposure,0);
        QCOMPARE(history.value(a).exposure,1);
        QCOMPARE(history.undo(),a);
        QCOMPARE(history.value(a).exposure,0);
    }

    void revisionsAndLocalIdentityIncrease(){
        EditHistory history;
        const QString path=QStringLiteral("C:/photos/one.cr3");
        const auto asset=assetFor(QStringLiteral("asset-1"),path);
        QVERIFY(history.commitSnapshot(path,makeDocument(asset,.5,brushLayers(100)),QStringLiteral("l1"),nullptr));
        const quint64 revision1=history.snapshot(path)->revision;
        const quint64 local1=history.snapshot(path)->localIdentity;
        QVERIFY(history.commitSnapshot(path,makeDocument(asset,.9,brushLayers(200)),QStringLiteral("l2"),nullptr));
        const quint64 revision2=history.snapshot(path)->revision;
        const quint64 local2=history.snapshot(path)->localIdentity;
        QVERIFY(revision2>revision1);
        QVERIFY(local2>local1);
        QCOMPARE(history.undo(),path);
        const quint64 revision3=history.snapshot(path)->revision;
        const quint64 local3=history.snapshot(path)->localIdentity;
        QVERIFY(revision3>revision2); // revision never rewinds
        QVERIFY(local3>local2);       // locals changed on undo
        QCOMPARE(history.redo(),path);
        QVERIFY(history.snapshot(path)->revision>revision3);
        QVERIFY(history.snapshot(path)->localIdentity>local3);
    }

    void frozenSnapshotsRemainIdentical(){
        EditHistory history;
        const QString path=QStringLiteral("C:/photos/one.cr3");
        const auto asset=assetFor(QStringLiteral("asset-1"),path);
        QVERIFY(history.commitSnapshot(path,makeDocument(asset,.5,brushLayers(100)),QStringLiteral("l1"),nullptr));
        const auto frozen=history.snapshot(path);
        const quint64 frozenRevision=frozen->revision;
        QVERIFY(history.commitSnapshot(path,makeDocument(asset,1.5,brushLayers(300)),QStringLiteral("l2"),nullptr));
        QCOMPARE(frozen->global.exposure,.5);
        QCOMPARE(frozen->layers.first().id,QStringLiteral("layer-1"));
        QCOMPARE(frozen->revision,frozenRevision);
    }

    void redoTruncatedOnlyOnRealCommit(){
        EditHistory history;
        const QString path=QStringLiteral("C:/photos/one.cr3");
        const auto asset=assetFor(QStringLiteral("asset-1"),path);
        QVERIFY(history.commitSnapshot(path,makeDocument(asset,.5),QStringLiteral("g1"),nullptr));
        QVERIFY(history.commitSnapshot(path,makeDocument(asset,1.),QStringLiteral("g2"),nullptr));
        QCOMPARE(history.undo(),path);
        QVERIFY(history.canRedo());
        QString error;
        QVERIFY(!history.commitSnapshot(path,makeDocument(asset,.5),QStringLiteral("noop"),&error));
        QVERIFY(history.canRedo()); // a no-op keeps redo intact
        QVERIFY(history.commitSnapshot(path,makeDocument(asset,2.),QStringLiteral("g3"),nullptr));
        QVERIFY(!history.canRedo()); // a real commit drops the redo branch
    }

    void invalidCommitLeavesStateUntouched(){
        EditHistory history;
        const QString path=QStringLiteral("C:/photos/one.cr3");
        const auto asset=assetFor(QStringLiteral("asset-1"),path);
        QVERIFY(history.commitSnapshot(path,makeDocument(asset,.5),QStringLiteral("g1"),nullptr));
        const auto before=history.snapshot(path);
        QString error;
        QVERIFY(!history.commitSnapshot(path,DocumentSnapshot{},QStringLiteral("null"),&error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!history.commitSnapshot(QStringLiteral("C:/other.cr3"),makeDocument(asset,1.),QStringLiteral("mismatch"),&error));
        QVERIFY(!history.commitSnapshot(path,makeDocument(assetFor(QStringLiteral("other"),path),1.),QStringLiteral("id"),&error));
        QCOMPARE(history.snapshot(path),before); // same object, untouched
    }

    void valueIsSingleSourceOfTruth(){
        EditHistory history;
        const QString path=QStringLiteral("C:/photos/one.cr3");
        const auto asset=assetFor(QStringLiteral("asset-1"),path);
        QVERIFY(history.commitSnapshot(path,makeDocument(asset,1.25,brushLayers(150)),QStringLiteral("l1"),nullptr));
        QCOMPARE(history.value(path).exposure,1.25);
        QCOMPARE(history.snapshot(path)->global.exposure,1.25);
    }

    void bindAssetFillsKnownMetadataAndHistory(){
        EditHistory history;
        const QString path=QStringLiteral("C:/photos/one.cr3");
        AssetIdentity pending;pending.id=QStringLiteral("asset-1");pending.sourcePath=path;
        pending.raw=true;
        pending.sourceFacts=QJsonObject{{QStringLiteral("sourceKind"),QStringLiteral("raw")},
            {QStringLiteral("icc"),QStringLiteral("profile")}};
        QVERIFY(history.commitSnapshot(path,makeDocument(pending,.5),QStringLiteral("g1"),nullptr));
        const auto oldExternal=history.snapshot(path);
        AssetIdentity ready=pending;
        ready.sourceHash=hash64(QChar('b'));ready.normalizedSize=QSize(6000,4000);
        ready.sourceFacts=QJsonObject{{QStringLiteral("wb"),QStringLiteral("daylight")}};
        QString error;
        QVERIFY(history.bindAsset(path,ready,error));
        QVERIFY(error.isEmpty());
        // Known facts filled; unknown old keys retained in the merged facts.
        QCOMPARE(history.snapshot(path)->asset.sourceHash,ready.sourceHash);
        QCOMPARE(history.snapshot(path)->asset.normalizedSize,ready.normalizedSize);
        QVERIFY(history.snapshot(path)->asset.sourceFacts.contains(QStringLiteral("icc")));
        QVERIFY(history.snapshot(path)->asset.sourceFacts.contains(QStringLiteral("wb")));
        QCOMPARE(history.undo(),path);
        QCOMPARE(history.snapshot(path)->asset.sourceHash,ready.sourceHash);
        // Externally held old snapshot is unchanged (copy-on-write).
        QVERIFY(oldExternal->asset.sourceHash.isEmpty());
    }

    void bindAssetRejectsIdentityChanges(){
        EditHistory history;
        const QString path=QStringLiteral("C:/photos/one.cr3");
        const auto asset=assetFor(QStringLiteral("asset-1"),path);
        QVERIFY(history.commitSnapshot(path,makeDocument(asset,.5),QStringLiteral("g1"),nullptr));
        QString error;
        QVERIFY(!history.bindAsset(path,assetFor(QStringLiteral("asset-2"),path),error));
        QVERIFY(!error.isEmpty());
        auto wrongHash=asset;wrongHash.sourceHash=hash64(QChar('c'));
        QVERIFY(!history.bindAsset(path,wrongHash,error));
        QVERIFY(!history.bindAsset(path,assetFor(QStringLiteral("asset-1"),QStringLiteral("C:/other.cr3")),error));
        QCOMPARE(history.snapshot(path)->asset.id,QStringLiteral("asset-1"));
        QCOMPARE(history.snapshot(path)->asset.sourceHash,hash64(QChar('a')));
    }

    void resetDocumentsIsAtomic(){
        EditHistory history;
        const QString path=QStringLiteral("C:/photos/one.cr3");
        const auto asset=assetFor(QStringLiteral("asset-1"),path);
        QVERIFY(history.commitSnapshot(path,makeDocument(asset,.5),QStringLiteral("g1"),nullptr));
        const auto before=history.snapshot(path);
        QString error;
        QList<DocumentSnapshot> duplicates{makeDocument(asset,.1),makeDocument(assetFor(QStringLiteral("asset-2"),path),.2)};
        QVERIFY(!history.resetDocuments(duplicates,error)); // duplicate path
        QVERIFY(!error.isEmpty());
        auto unknownAlgorithm=std::make_shared<PhotoDocument>(*makeDocument(assetFor(QStringLiteral("asset-3"),QStringLiteral("C:/photos/t.cr3")),.3));
        unknownAlgorithm->algorithmVersion=99;
        QVERIFY(!history.resetDocuments({unknownAlgorithm},error));
        QVERIFY(!history.resetDocuments({DocumentSnapshot{}},error));
        QCOMPARE(history.snapshot(path),before); // previous state untouched
        QVERIFY(history.canUndo());
        QVERIFY(history.resetDocuments({},error)); // empty reset allowed
        QVERIFY(history.snapshot(path)==nullptr);
        QVERIFY(!history.canUndo());
    }

    void overflowRejectsWithoutCursorWrap(){
        const QString path=QStringLiteral("C:/photos/one.cr3");
        const auto asset=assetFor(QStringLiteral("asset-1"),path);
        const quint64 maximum=std::numeric_limits<quint64>::max();
        // Load a document sitting at the revision ceiling: a further commit must
        // be rejected and must not wrap the counter or move the cursor.
        EditHistory history;
        QString error;
        QVERIFY(history.resetDocuments({makeDocument(asset,.5,{},maximum)},error));
        QVERIFY(error.isEmpty());
        QCOMPARE(history.snapshot(path)->revision,maximum);
        QVERIFY(!history.commitSnapshot(path,makeDocument(asset,1.),QStringLiteral("overflow"),&error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(history.snapshot(path)->revision,maximum); // no wrap
        QCOMPARE(history.snapshot(path)->global.exposure,.5); // no mutation
        QVERIFY(!history.canUndo()&&!history.canRedo()); // cursor untouched
    }

    void incomingMutableAliasesCannotChangeHistory(){
        EditHistory history;
        const QString path=QStringLiteral("C:/photos/alias.cr3");
        auto source=std::make_shared<PhotoDocument>(*makeDocument(assetFor("alias",path),.5,brushLayers(100)));
        QString error;
        QVERIFY(history.resetDocuments({source},error));
        source->global.exposure=2;
        source->layers.clear();
        QCOMPARE(history.value(path).exposure,.5);
        QCOMPARE(history.snapshot(path)->layers.size(),1);
        auto edit=std::make_shared<PhotoDocument>(*history.snapshot(path));
        edit->global.exposure=1;
        QVERIFY(history.commitSnapshot(path,edit,"edit",&error));
        edit->global.exposure=3;
        QCOMPARE(history.value(path).exposure,1.);
        QCOMPARE(history.undo(),path);
        QCOMPARE(history.value(path).exposure,.5);
        QCOMPARE(history.redo(),path);
        QCOMPARE(history.value(path).exposure,1.);
    }
};

QTEST_APPLESS_MAIN(DocumentHistoryTests)
#include "document_history_tests.moc"
