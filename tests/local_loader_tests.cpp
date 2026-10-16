#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QCryptographicHash>
#include "app/edit_loader.h"
#include "render/local_renderer.h"
#include "render/geometry_transform.h"

using namespace LocalEdit;
namespace {
QByteArray hash(const QImage &image){
    return QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char *>(image.constBits()),
        image.sizeInBytes()),QCryptographicHash::Sha256);
}
PhotoDocument document(const EditResult &prepared){
    PhotoDocument doc;doc.asset.id="loader-asset";doc.asset.sourcePath=prepared.path;
    doc.asset.sourceHash=prepared.sourceHash;doc.asset.normalizedSize=prepared.originalSize;
    doc.asset.sourceFacts=prepared.workFrame.facts;doc.asset.raw=prepared.workFrame.raw;
    AdjustmentLayer layer;layer.id="loader-layer";layer.parameters.exposure=1;
    MaskComponent component;component.id="loader-stroke";
    BrushGeometry brush;brush.points={{48,32}};brush.radius=24;brush.hardness=.5;
    component.geometry=brush;layer.mask.components={component};doc.layers={layer};return doc;
}
}
class LocalLoaderTests:public QObject {
    Q_OBJECT
private slots:
    void queueObserverCanReplaceRequest(){
        QTemporaryDir directory;QVERIFY(directory.isValid());const auto path=directory.filePath("queue.jpg");
        QImage input(64,48,QImage::Format_RGB32);input.fill(Qt::gray);QVERIFY(input.save(path));
        EditLoader loader;QSignalSpy ready(&loader,&EditLoader::ready),queued(&loader,&EditLoader::requestQueued);
        connect(&loader,&EditLoader::requestQueued,&loader,[&](const QString &source,EditTag tag){
            if(tag.job==1){tag.job=2;Recipe next;next.exposure=.5;loader.request(source,next,tag);}
        });
        EditTag tag;tag.session=5;tag.job=1;loader.request(path,{},tag);
        QCOMPARE(queued.size(),2);QTRY_COMPARE_WITH_TIMEOUT(ready.size(),1,15000);
        const auto result=qvariant_cast<EditResult>(ready.last()[0]);QVERIFY2(result.error.isEmpty(),qPrintable(result.error));
        QCOMPARE(result.tag.job,quint64(2));QCOMPARE(result.recipe.exposure,.5);
    }
    void frozenDocumentFeedsActualPreview(){
        QTemporaryDir directory;QVERIFY(directory.isValid());const auto path=directory.filePath("synthetic.jpg");
        QImage input(96,64,QImage::Format_RGB32);input.fill(QColor(70,90,110));QVERIFY(input.save(path,"JPEG",98));
        EditLoader loader;QSignalSpy ready(&loader,&EditLoader::ready),queued(&loader,&EditLoader::requestQueued);
        EditTag tag;tag.session=42;tag.revision=1;tag.job=1;loader.request(path,{},tag);
        QCOMPARE(queued.size(),1);QCOMPARE(queued.last()[0].toString(),path);QCOMPARE(qvariant_cast<EditTag>(queued.last()[1]),tag);
        QTRY_VERIFY_WITH_TIMEOUT(ready.size()==1,15000);
        const auto initial=qvariant_cast<EditResult>(ready.takeFirst().at(0));
        QVERIFY2(initial.error.isEmpty(),qPrintable(initial.error));QVERIFY(!initial.workFrame.linear.isNull());
        auto mutableDocument=std::make_shared<PhotoDocument>(document(initial));const auto frozen=*mutableDocument;
        tag.job=2;tag.revision=2;tag.localIdentity=1;QString error;
        QVERIFY2(loader.requestDocument(path,mutableDocument,tag,error),qPrintable(error));
        mutableDocument->layers[0].parameters.exposure=-1;
        std::get<BrushGeometry>(mutableDocument->layers[0].mask.components[0].geometry).points={{1,1}};
        QTRY_VERIFY_WITH_TIMEOUT(ready.size()==1,15000);
        const auto result=qvariant_cast<EditResult>(ready.takeFirst().at(0));
        QVERIFY2(result.error.isEmpty(),qPrintable(result.error));QVERIFY(result.document);
        QCOMPARE(result.document->layers,frozen.layers);QCOMPARE(result.tag,tag);
        const auto expected=renderDocumentPreview(initial.workFrame,frozen,frozen.global);
        QVERIFY2(expected.error.isEmpty(),qPrintable(expected.error));
        QCOMPARE(hash(result.image),hash(expected.image));QVERIFY(hash(result.image)!=hash(initial.image));
        auto live=std::make_shared<PhotoDocument>(frozen);live->layers[0].parameters.exposure=.5;
        tag.job=3;tag.revision=3;tag.localIdentity=2;tag.interactive=true;
        QVERIFY2(loader.requestDocument(path,live,tag,error),qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(ready.size()==1,15000);
        const auto interactive=qvariant_cast<EditResult>(ready.takeFirst().at(0));
        QVERIFY2(interactive.error.isEmpty(),qPrintable(interactive.error));QVERIFY(!interactive.sourceRebuilt);
        QCOMPARE(hash(interactive.image),hash(renderDocumentPreview(initial.workFrame,*live,live->global).image));
        auto invalid=std::make_shared<PhotoDocument>(*live);invalid->asset.sourcePath="wrong-path";
        const auto beforeInvalid=queued.size();
        QVERIFY(!loader.requestDocument(path,invalid,tag,error));QVERIFY(!error.isEmpty());
        QCOMPARE(queued.size(),beforeInvalid);
    }
    void supersededLocalResultCannotCommit(){
        QTemporaryDir directory;QVERIFY(directory.isValid());const auto path=directory.filePath("gate.jpg");
        QImage input(96,64,QImage::Format_RGB32);input.fill(QColor(90,80,70));QVERIFY(input.save(path,"JPEG",98));
        EditLoader loader;QSignalSpy ready(&loader,&EditLoader::ready);EditTag tag;tag.session=7;tag.job=1;
        loader.request(path,{},tag);QTRY_VERIFY_WITH_TIMEOUT(ready.size()==1,15000);
        const auto initial=qvariant_cast<EditResult>(ready.takeFirst().at(0));QVERIFY(initial.error.isEmpty());
        auto local=std::make_shared<PhotoDocument>(document(initial));QString error;
        tag.job=2;tag.localIdentity=1;QVERIFY(loader.requestDocument(path,local,tag,error));
        tag.job=3;tag.localIdentity=2;loader.request(path,{},tag);
        QTRY_VERIFY_WITH_TIMEOUT(ready.size()==1,15000);
        const auto result=qvariant_cast<EditResult>(ready.takeFirst().at(0));
        QCOMPARE(result.tag,tag);QVERIFY(!result.document);QCOMPARE(hash(result.image),hash(initial.image));
        loader.invalidate();
    }
};
QTEST_MAIN(LocalLoaderTests)
#include "local_loader_tests.moc"
