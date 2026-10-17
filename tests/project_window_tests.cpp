#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTabWidget>
#include <QMessageBox>
#include <QTimer>
#include <functional>
#include "app/window.h"
#include "app/project_persistence.h"
#include "app/display_presenter.h"
#include "io/source_reader.h"
#include "render/local_renderer.h"
using namespace LocalEdit;
namespace {
bool waitFor(const std::function<bool()> &predicate){QElapsedTimer t;t.start();while(!predicate()){if(t.elapsed()>20000)return false;QTest::qWait(10);}return true;}
bool jpeg(const QString &path,QColor color,QSize size={96,64}){QImage image(size,QImage::Format_RGB32);image.fill(color);return image.save(path,"JPEG",98);}
QString hash(const QString &path){QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};QCryptographicHash h(QCryptographicHash::Sha256);if(!h.addData(&f))return {};return QString::fromLatin1(h.result().toHex());}
ProjectPersistence *storage(Window &w){return w.findChild<ProjectPersistence *>();}
bool ready(Window &w){return waitFor([&]{return storage(w)&&storage(w)->state();});}
bool parsed(Window &w,int count){return waitFor([&]{auto *p=storage(w);return p&&p->state()&&p->state()->pending.isEmpty()&&p->state()->project->photos.size()==count&&w.neutralPickerAvailable();});}
bool completion(QSignalSpy &spy,PersistenceOperation op){return waitFor([&]{for(const auto &row:spy){const auto r=qvariant_cast<PersistenceResult>(row[0]);if(r.operation==op){if(!r.error.isEmpty())qWarning()<<r.error;return r.error.isEmpty();}}return false;});}
}
class ProjectWindowTests:public QObject {
    Q_OBJECT
private slots:
    void unselectedIdentityAndActualRoundTrip(){
        QTemporaryDir dir;QVERIFY(dir.isValid());
        const auto a=dir.filePath("a.jpg"),b=dir.filePath("b.jpg"),target=dir.filePath(QStringLiteral("中文项目.luminas"));
        QVERIFY(jpeg(a,QColor(50,80,120)));QVERIFY(jpeg(b,QColor(100,70,20),{64,96}));
        const auto ha=hash(a),hb=hash(b);Window w(nullptr,QStringLiteral(""),dir.filePath("recovery"));w.show();QVERIFY(QTest::qWaitForWindowExposed(&w));QVERIFY(ready(w));
        w.addFiles({a,b});QVERIFY(parsed(w,2));QCOMPARE(w.currentIndex(),0); // Never click b.
        auto r=w.currentRecipe();r.exposure=.5;r.angle=4;r.crop={.1,.1,.8,.8};w.applyRecipe(r);w.mark(Pick::Keep);
        QVERIFY(waitFor([&]{return w.neutralPickerAvailable();}));
        QSignalSpy renders(w.findChild<EditLoader *>(),&EditLoader::ready);
        const auto settled=[&]{if(renders.isEmpty()||!w.currentPhotoDocument())return false;const auto frame=qvariant_cast<EditResult>(renders.last()[0]);return frame.error.isEmpty()&&frame.document&&frame.document->layers==w.currentPhotoDocument()->layers&&frame.recipe==w.currentRecipe();};
        w.findChild<QTabWidget *>("adjustment-scope-tabs")->setCurrentIndex(1);
        w.findChild<QPushButton *>("local-new-brush")->click();
        QVERIFY(waitFor([&]{return settled()&&w.findChild<Canvas *>()->brushMode();}));
        auto *canvas=w.findChild<Canvas *>();canvas->brushPressed({.5,.5},false);canvas->brushFinished(false);
        QVERIFY(w.currentPhotoDocument());QCOMPARE(w.currentPhotoDocument()->layers.size(),1);QCOMPARE(w.currentPhotoDocument()->layers[0].mask.components.size(),1);
        QVERIFY(waitFor(settled));
        auto *localExposure=w.findChild<QDoubleSpinBox *>("local-exposure");QVERIFY(localExposure);localExposure->setValue(.4);QMetaObject::invokeMethod(localExposure,"editingFinished",Qt::DirectConnection);
        const auto frozen=w.currentPhotoDocument();QCOMPARE(frozen->layers[0].parameters.exposure,.4);
        auto *p=storage(w);QVERIFY(waitFor([&]{return p->state()->project->photos[0].document->global==r&&p->state()->project->photos[0].mark==PhotoMark::Keep;}));
        QVERIFY(waitFor([&]{return !p->busy()&&p->recoveredRevision()==p->state()->project->revision;}));QVERIFY(p->dirty());
        RecoveryState recovered;QString sha,error;QVERIFY2(loadRecovery(w.projectRecoveryPath(),recovered,sha,error),qPrintable(error));
        QCOMPARE(recovered.order,QStringList({a,b}));QCOMPARE(recovered.project->photos[0].document->global,r);QCOMPARE(recovered.project->photos[0].mark,PhotoMark::Keep);
        QCOMPARE(recovered.project->photos[0].document->layers,frozen->layers);
        QSignalSpy saved(p,&ProjectPersistence::completed);QVERIFY2(w.saveProjectFile(target,error),qPrintable(error));QVERIFY(completion(saved,PersistenceOperation::ProjectSave));QVERIFY(!p->dirty());
        ProjectState actual;QVERIFY2(loadProject(target,actual,sha,error),qPrintable(error));QCOMPARE(actual.photos.size(),2);
        QCOMPARE(actual.photos[0].document->asset.sourceHash,ha);QCOMPARE(actual.photos[1].document->asset.sourceHash,hb);QCOMPARE(actual.photos[1].document->asset.normalizedSize,QSize(64,96));
        QVERIFY(!actual.photos[0].document->asset.sourceFacts.contains("readStages"));QVERIFY(!actual.photos[0].document->asset.sourceFacts.contains("elapsedMs"));
        QVERIFY(actual.photos[0].document->asset.sourceFacts.contains("iccSha256"));
        // An older project can contain read telemetry. New decoding must not
        // replace it with today's timings and manufacture a document edit.
        auto legacy=*actual.photos[0].document;legacy.asset.sourceFacts["elapsedMs"]=123456;
        legacy.asset.sourceFacts["readStages"]=QJsonObject{{"hashAfterMs",999}};
        actual.photos[0].document=std::make_shared<const PhotoDocument>(std::move(legacy));
        const auto legacyPath=dir.filePath("legacy.luminas");QVERIFY2(saveProject(legacyPath,freezeProject(actual,error),sha,error),qPrintable(error));
        Window restored(nullptr,QStringLiteral(""),dir.filePath("restore"));QVERIFY(ready(restored));QSignalSpy loaded(storage(restored),&ProjectPersistence::completed);
        QVERIFY2(restored.openProjectFile(legacyPath,false,error),qPrintable(error));QVERIFY(completion(loaded,PersistenceOperation::ProjectLoad));
        QVERIFY(parsed(restored,2));QCOMPARE(restored.currentIndex(),0);QCOMPARE(restored.currentRecipe(),r);QCOMPARE(restored.pickAt(0),Pick::Keep);
        QCOMPARE(restored.currentPhotoDocument()->layers,frozen->layers);
        const auto source=readSource(a);QVERIFY2(source.source,qPrintable(source.error));
        QString renderError;const auto before=renderDocumentFullLinear(*source.source,*frozen,renderError);QVERIFY2(!before.isNull(),qPrintable(renderError));
        const auto after=renderDocumentFullLinear(*source.source,*restored.currentPhotoDocument(),renderError);QVERIFY2(!after.isNull(),qPrintable(renderError));
        QCOMPARE(after,before); // Actual full-source global + brush replay, not a 4K upsample.
        QTest::qWait(600);
        if(storage(restored)->dirty()){
            const auto changed=storage(restored)->state();qWarning()<<"loaded revision"<<actual.revision<<"current revision"<<changed->project->revision<<"active"<<actual.activeAssetId<<changed->project->activeAssetId;
            for(int i=0;i<actual.photos.size();++i){
                qWarning()<<"photo"<<i<<"mark"<<int(actual.photos[i].mark)<<int(changed->project->photos[i].mark);
                qWarning().noquote()<<"saved"<<QJsonDocument(documentJson(*actual.photos[i].document,error)).toJson(QJsonDocument::Compact);
                qWarning().noquote()<<"current"<<QJsonDocument(documentJson(*changed->project->photos[i].document,error)).toJson(QJsonDocument::Compact);
            }
        }
        QVERIFY(!storage(restored)->dirty()); // No phantom edit from cloned snapshots.
        QCOMPARE(restored.currentPhotoDocument()->asset.sourceFacts.value("elapsedMs").toInt(),123456);
        QCOMPARE(hash(a),ha);QCOMPARE(hash(b),hb);
    }
    void changedAndMissingSourcesStayBlocked(){
        QTemporaryDir dir;QVERIFY(dir.isValid());const auto a=dir.filePath("a.jpg"),b=dir.filePath("b.jpg"),target=dir.filePath("p.luminas");
        QVERIFY(jpeg(a,QColor(20,60,100)));QVERIFY(jpeg(b,QColor(70,20,90)));
        QString error;
        {Window w(nullptr,QStringLiteral(""),dir.filePath("recovery"));QVERIFY(ready(w));w.addFiles({a,b});QVERIFY(parsed(w,2));
            auto r=w.currentRecipe();r.exposure=1;w.applyRecipe(r);QSignalSpy saved(storage(w),&ProjectPersistence::completed);
            QVERIFY2(w.saveProjectFile(target,error),qPrintable(error));QVERIFY(completion(saved,PersistenceOperation::ProjectSave));}
        QVERIFY(jpeg(a,QColor(240,220,80)));QVERIFY(QFile::remove(b));
        Window w(nullptr,QStringLiteral(""),dir.filePath("restore"));QVERIFY(ready(w));
        QSignalSpy requests(w.findChild<EditLoader *>(),&EditLoader::requestQueued),loaded(storage(w),&ProjectPersistence::completed);
        QVERIFY2(w.openProjectFile(target,false,error),qPrintable(error));QVERIFY(completion(loaded,PersistenceOperation::ProjectLoad));
        QCOMPARE(w.filmstrip()->count(),2);QVERIFY(w.currentPhotoDocument());QCOMPARE(w.currentPhotoDocument()->global.exposure,1.);
        const auto result=qvariant_cast<PersistenceResult>(loaded.last()[0]);QCOMPARE(result.sources[0].state,SourceState::Changed);QCOMPARE(result.sources[1].state,SourceState::Missing);
        w.activate(1);QVERIFY(w.currentPhotoDocument());w.activate(0);QTest::qWait(600);
        QCOMPARE(requests.size(),0);QVERIFY(w.displayedPath().isEmpty());QVERIFY(w.findChild<Canvas *>()->presenter()->canonicalImage().isNull());
        QCOMPARE(storage(w)->state()->project->photos.size(),2);QCOMPARE(storage(w)->state()->project->photos[0].document->global.exposure,1.);
    }
    void failedImportRetainsMarkAndLegacyJson(){
        QTemporaryDir dir;QVERIFY(dir.isValid());const auto bad=dir.filePath("bad.jpg");{QFile f(bad);QVERIFY(f.open(QIODevice::WriteOnly));QCOMPARE(f.write("bad"),3);}
        Window w(nullptr,QStringLiteral(""),dir.filePath("recovery"));QVERIFY(ready(w));w.addFiles({bad});w.mark(Pick::Keep);auto *p=storage(w);
        QVERIFY(waitFor([&]{return p->state()->pending.size()==1&&!p->state()->pending[0].error.isEmpty()&&p->state()->pending[0].mark==PhotoMark::Keep&&p->recoveredRevision()==p->state()->project->revision&&!p->busy();}));
        QString error,sha;RecoveryState state;QVERIFY2(loadRecovery(w.projectRecoveryPath(),state,sha,error),qPrintable(error));QCOMPARE(state.pending[0].mark,PhotoMark::Keep);QVERIFY(p->dirty());
        QVERIFY(!w.saveProjectFile(dir.filePath("p.luminas"),error));QVERIFY(error.contains("1"));
        auto json=recoveryJson(state,error);auto pending=json["pending"].toArray();auto entry=pending[0].toObject();
        entry.remove("mark");pending[0]=entry;json["pending"]=pending;RecoveryState old;QVERIFY(recoveryFromJson(json,old,error));QCOMPARE(old.pending[0].mark,PhotoMark::Unmarked);
        for(const auto value:{QJsonValue(1.5),QJsonValue("1"),QJsonValue(3),QJsonValue(QJsonValue::Null)}){entry["mark"]=value;pending[0]=entry;json["pending"]=pending;QVERIFY(!recoveryFromJson(json,old,error));}
        Window restored(nullptr,QStringLiteral(""),dir.filePath("restore"));QVERIFY(ready(restored));QSignalSpy loaded(storage(restored),&ProjectPersistence::completed);
        QVERIFY2(restored.openProjectFile(w.projectRecoveryPath(),true,error),qPrintable(error));QVERIFY(completion(loaded,PersistenceOperation::RecoveryLoad));
        QCOMPARE(restored.currentIndex(),0);QCOMPARE(restored.pickAt(0),Pick::Keep);QVERIFY(storage(restored)->dirty());
    }
    void failedOpenAndCloseCancelKeepLiveProject(){
        QTemporaryDir dir;QVERIFY(dir.isValid());const auto a=dir.filePath("a.jpg");QVERIFY(jpeg(a,QColor(30,60,90)));
        Window w(nullptr,QStringLiteral(""),dir.filePath("recovery"));w.show();QVERIFY(QTest::qWaitForWindowExposed(&w));QVERIFY(ready(w));w.addFiles({a});QVERIFY(parsed(w,1));
        auto r=w.currentRecipe();r.exposure=.25;w.applyRecipe(r);w.mark(Pick::Reject);
        QSignalSpy loaded(storage(w),&ProjectPersistence::completed);QString error;QVERIFY(w.openProjectFile(dir.filePath("missing.luminas"),false,error));
        QVERIFY(waitFor([&]{return !loaded.isEmpty();}));QVERIFY(!qvariant_cast<PersistenceResult>(loaded.last()[0]).error.isEmpty());
        QVERIFY(w.isEnabled());QCOMPARE(w.currentRecipe(),r);QCOMPARE(w.pickAt(0),Pick::Reject);QCOMPARE(w.filmstrip()->count(),1);QVERIFY(storage(w)->dirty());
        bool canceled=false;
        QTimer::singleShot(50,&w,[&]{auto *dialog=qobject_cast<QMessageBox *>(QApplication::activeModalWidget());if(dialog){canceled=true;dialog->button(QMessageBox::Cancel)->click();}});
        QVERIFY(!w.close());QVERIFY(canceled);QVERIFY(w.isVisible());QCOMPARE(w.currentRecipe(),r);QVERIFY(storage(w)->dirty());
    }
    void unavailableStorageStillProtectsClose(){
        QTemporaryDir dir;QVERIFY(dir.isValid());const auto root=dir.filePath("occupied"),a=dir.filePath("a.jpg");
        {QFile f(root);QVERIFY(f.open(QIODevice::WriteOnly));QCOMPARE(f.write("x"),1);}QVERIFY(jpeg(a,QColor(40,80,120)));
        Window w(nullptr,QStringLiteral(""),root);w.show();QVERIFY(QTest::qWaitForWindowExposed(&w));
        QVERIFY(waitFor([&]{return w.findChild<QLabel *>("project-status")->text().contains(QStringLiteral("不可用"));}));
        w.addFiles({a});QVERIFY(waitFor([&]{return w.neutralPickerAvailable();}));
        bool canceled=false;QTimer::singleShot(50,&w,[&]{auto *dialog=qobject_cast<QMessageBox *>(QApplication::activeModalWidget());if(dialog){canceled=true;dialog->button(QMessageBox::Cancel)->click();}});
        QVERIFY(!w.close());QVERIFY(canceled);QVERIFY(w.isVisible());QCOMPARE(hash(root),QString::fromLatin1(QCryptographicHash::hash(QByteArray("x"),QCryptographicHash::Sha256).toHex()));
    }
    void actualSampleProjectReplay(){
        const auto sample=qEnvironmentVariable("LUMINA_D1_SAMPLE");if(sample.isEmpty())QSKIP("Set LUMINA_D1_SAMPLE for private R7 project replay.");
        QVERIFY(QFileInfo(sample).isAbsolute());QVERIFY(QFile::exists(sample));const auto sourceHash=hash(sample);QVERIFY(!sourceHash.isEmpty());
        QTemporaryDir dir;QVERIFY(dir.isValid());const auto target=dir.filePath("r7.luminas");
        DocumentSnapshot frozen;QByteArray expectedPixels;QString error;
        const auto pixels=[](const QImage &image){return QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char *>(image.constBits()),image.sizeInBytes()),QCryptographicHash::Sha256).toHex();};
        {
            Window w(nullptr,QStringLiteral(""),dir.filePath("recovery"));w.show();QVERIFY(QTest::qWaitForWindowExposed(&w));QVERIFY(ready(w));
            w.addFiles({sample});QVERIFY(parsed(w,1));auto recipe=w.currentRecipe();recipe.exposure=.3;recipe.crop={.1,.1,.8,.8};w.applyRecipe(recipe);
            QVERIFY(waitFor([&]{return w.neutralPickerAvailable();}));QSignalSpy renders(w.findChild<EditLoader *>(),&EditLoader::ready);
            const auto settled=[&]{if(renders.isEmpty()||!w.currentPhotoDocument())return false;const auto frame=qvariant_cast<EditResult>(renders.last()[0]);return !frame.tag.interactive&&frame.error.isEmpty()&&frame.document&&frame.document->layers==w.currentPhotoDocument()->layers&&frame.recipe==w.currentRecipe();};
            w.findChild<QTabWidget *>("adjustment-scope-tabs")->setCurrentIndex(1);w.findChild<QPushButton *>("local-new-brush")->click();
            QVERIFY(waitFor([&]{return settled()&&w.findChild<Canvas *>()->brushMode();}));
            auto *canvas=w.findChild<Canvas *>();canvas->brushPressed({.5,.5},false);canvas->brushMoved({.55,.55});canvas->brushFinished(false);
            QVERIFY(waitFor(settled));auto *local=w.findChild<QDoubleSpinBox *>("local-exposure");QVERIFY(local);local->setValue(.25);QMetaObject::invokeMethod(local,"editingFinished",Qt::DirectConnection);
            QVERIFY(waitFor(settled));frozen=w.currentPhotoDocument();QVERIFY(frozen);QCOMPARE(frozen->layers.size(),1);QVERIFY(!frozen->layers[0].mask.components.isEmpty());
            const auto image=canvas->presenter()->canonicalImage();QCOMPARE(qMax(image.width(),image.height()),3840);expectedPixels=pixels(image);QVERIFY(!expectedPixels.isEmpty());
            w.mark(Pick::Keep);QSignalSpy saved(storage(w),&ProjectPersistence::completed);QVERIFY2(w.saveProjectFile(target,error),qPrintable(error));QVERIFY(completion(saved,PersistenceOperation::ProjectSave));QVERIFY(!storage(w)->dirty());
        }
        {
            Window w(nullptr,QStringLiteral(""),dir.filePath("restored"));w.show();QVERIFY(QTest::qWaitForWindowExposed(&w));QVERIFY(ready(w));
            QSignalSpy loaded(storage(w),&ProjectPersistence::completed);QVERIFY2(w.openProjectFile(target,false,error),qPrintable(error));QVERIFY(completion(loaded,PersistenceOperation::ProjectLoad));
            QVERIFY(parsed(w,1));QCOMPARE(w.pickAt(0),Pick::Keep);QVERIFY(w.currentPhotoDocument());QCOMPARE(w.currentPhotoDocument()->global,frozen->global);QCOMPARE(w.currentPhotoDocument()->layers,frozen->layers);
            const auto image=w.findChild<Canvas *>()->presenter()->canonicalImage();QCOMPARE(qMax(image.width(),image.height()),3840);QCOMPARE(pixels(image),expectedPixels);
            QTest::qWait(600);QVERIFY(!storage(w)->dirty());QCOMPARE(hash(sample),sourceHash);
            qInfo()<<"R7 project replay"<<QFileInfo(sample).fileName()<<"sourceSha"<<sourceHash<<"previewSha"<<expectedPixels<<"sourceSize"<<frozen->asset.normalizedSize<<"previewSize"<<image.size();
        }
    }
};
QTEST_MAIN(ProjectWindowTests)
#include "project_window_tests.moc"
