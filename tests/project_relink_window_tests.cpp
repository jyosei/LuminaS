#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTabWidget>
#include <QSignalSpy>
#include <functional>
#include "app/window.h"
#include "app/project_persistence.h"
using namespace LocalEdit;
namespace {
bool waitFor(const std::function<bool()> &predicate){QElapsedTimer t;t.start();while(!predicate()){if(t.elapsed()>20000)return false;QTest::qWait(10);}return true;}
bool jpeg(const QString &path,QColor color,QSize size={96,64}){QImage image(size,QImage::Format_RGB32);image.fill(color);return image.save(path,"JPEG",98);}
QString hash(const QString &path){QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};QCryptographicHash h(QCryptographicHash::Sha256);if(!h.addData(&f))return {};return QString::fromLatin1(h.result().toHex());}
bool copyOver(const QString &from,const QString &to){QFile::remove(to);return QFile::copy(from,to);}
ProjectPersistence *storage(Window &w){return w.findChild<ProjectPersistence *>();}
bool ready(Window &w){return waitFor([&]{return storage(w)&&storage(w)->state();});}
bool parsed(Window &w,int count){return waitFor([&]{auto *p=storage(w);return p&&p->state()&&p->state()->pending.isEmpty()&&p->state()->project->photos.size()==count&&w.neutralPickerAvailable();});}
// Waits for a completion of the given operation and returns its result, whether
// it succeeded or failed. Never waits only for success: a relink rejection must
// be observable too. Clears nothing - the caller owns spy lifetime.
bool awaitOperation(QSignalSpy &spy,PersistenceOperation op,PersistenceResult &out){
    bool seen=false;
    const bool ok=waitFor([&]{for(const auto &row:spy){const auto r=qvariant_cast<PersistenceResult>(row[0]);if(r.operation==op){out=r;seen=true;return true;}}return false;});
    return ok&&seen;
}
// Copies the source content to a distinct path so the fixture keeps both files.
QString snapshotCopy(const QString &source,const QString &dir,const QString &name){
    const QString target=QDir(dir).filePath(name);
    return copyOver(source,target)?target:QString{};
}
}
class ProjectRelinkWindowTests:public QObject {
    Q_OBJECT
private slots:
    // A missing source is bound to a copied replacement only when its bytes match
    // the recorded fingerprint. A wrong file must change nothing at all.
    void relinkAdoptsOnlyFingerprintMatch(){
        QTemporaryDir dir;QVERIFY(dir.isValid());
        const auto source=dir.filePath("source.jpg"),target=dir.filePath("p.luminas");
        QVERIFY(jpeg(source,QColor(60,90,140)));
        // Capture the genuine bytes before the working copy is removed, so the
        // "correct" replacement is a byte-identical copy of the recorded source.
        const auto newDir=dir.filePath("relocated"),keptDir=dir.filePath("kept");
        QVERIFY(QDir().mkpath(newDir));QVERIFY(QDir().mkpath(keptDir));
        const auto originalBytes=hash(source);QVERIFY(!originalBytes.isEmpty());
        const auto correct=snapshotCopy(source,newDir,"name.jpg");QVERIFY(!correct.isEmpty());
        const auto correctCopy=snapshotCopy(source,keptDir,"kept.jpg");QVERIFY(!correctCopy.isEmpty());
        QCOMPARE(hash(correct),originalBytes);
        QString error;
        Recipe r;QString recordedHash,assetId;QVector<AdjustmentLayer> layers;
        {
            Window w(nullptr,QStringLiteral(""),dir.filePath("recovery"));w.show();QVERIFY(QTest::qWaitForWindowExposed(&w));QVERIFY(ready(w));
            w.addFiles({source});QVERIFY(parsed(w,1));
            r=w.currentRecipe();r.exposure=.5;r.crop={.1,.1,.8,.8};w.applyRecipe(r);w.mark(Pick::Keep);
            QVERIFY(waitFor([&]{return w.neutralPickerAvailable();}));
            const auto document=w.currentPhotoDocument();QVERIFY(document);
            recordedHash=document->asset.sourceHash;QVERIFY(!recordedHash.isEmpty());
            assetId=document->asset.id;QVERIFY(!assetId.isEmpty());
            layers=document->layers;
            auto *p=storage(w);QSignalSpy saved(p,&ProjectPersistence::completed);
            QVERIFY2(w.saveProjectFile(target,error),qPrintable(error));
            PersistenceResult saveResult;QVERIFY(awaitOperation(saved,PersistenceOperation::ProjectSave,saveResult));QVERIFY2(saveResult.error.isEmpty(),qPrintable(saveResult.error));
            QVERIFY(!p->dirty());
        }
        // Persist a real nonempty local payload as part of the source fixture.
        ProjectState fixture;QString fixtureHash;
        QVERIFY2(loadProject(target,fixture,fixtureHash,error),qPrintable(error));
        auto fixtureDocument=*fixture.photos[0].document;AdjustmentLayer layer;layer.id="layer";layer.name="local";layer.parameters.exposure=.2;
        MaskComponent component;component.id="stroke";BrushGeometry brush;brush.points={{48,32}};component.geometry=brush;layer.mask.components.append(component);
        fixtureDocument.layers.append(layer);++fixtureDocument.localIdentity;++fixtureDocument.revision;++fixture.revision;
        layers=fixtureDocument.layers;fixture.photos[0].document=std::make_shared<const PhotoDocument>(std::move(fixtureDocument));
        const auto oldFixtureHash=fixtureHash;
        QVERIFY2(saveProject(target,freezeProject(fixture,error),fixtureHash,error,oldFixtureHash),qPrintable(error));
        const auto untouchedProjectHash=hash(target);
        // The fixture now loses its source; the replacement bytes were captured first.
        QVERIFY(QFile::remove(source));
        QVERIFY(!QFileInfo::exists(source));
        QCOMPARE(hash(correctCopy),originalBytes);
        const auto wrong=dir.filePath("wrong.jpg");QVERIFY(jpeg(wrong,QColor(200,30,30),{96,64}));
        QVERIFY(hash(wrong)!=originalBytes);
        {
            Window w(nullptr,QStringLiteral(""),dir.filePath("restore"));QVERIFY(ready(w));
            QSignalSpy loaded(storage(w),&ProjectPersistence::completed);
            QVERIFY2(w.openProjectFile(target,false,error),qPrintable(error));
            PersistenceResult loadResult;QVERIFY(awaitOperation(loaded,PersistenceOperation::ProjectLoad,loadResult));QVERIFY2(loadResult.error.isEmpty(),qPrintable(loadResult.error));
            QCOMPARE(w.filmstrip()->count(),1);QVERIFY(w.currentPhotoDocument());
            QCOMPARE(w.filmstrip()->count(),1);
            QVERIFY(w.currentPhotoDocument());
            QCOMPARE(w.currentPhotoDocument()->asset.sourcePath,source);
            QCOMPARE(w.currentPhotoDocument()->asset.sourceHash,recordedHash);
            QCOMPARE(w.pickAt(0),Pick::Keep);
            auto *requests=w.findChild<EditLoader *>();QVERIFY(requests);
            QSignalSpy queued(requests,&EditLoader::requestQueued);
            QSignalSpy completions(storage(w),&ProjectPersistence::completed);
            QVERIFY(!QFileInfo::exists(source)); // the original really is gone
            QTest::qWait(600);
            QCOMPARE(queued.size(),0); // blocked source is never rendered
            QVERIFY(w.displayedPath().isEmpty());
            QVERIFY(!w.neutralPickerAvailable());

            // Round 1: wrong bytes must fail and preserve every recorded field.
            QVERIFY2(w.relinkCurrentSource(wrong,error),qPrintable(error));
            PersistenceResult rejected;QVERIFY(awaitOperation(completions,PersistenceOperation::SourceRelink,rejected));
            QVERIFY2(!rejected.error.isEmpty(),"a mismatching file must not be accepted");
            completions.clear();
            const auto kept=w.currentPhotoDocument();QVERIFY(kept);
            QCOMPARE(kept->asset.sourcePath,source);
            QCOMPARE(kept->asset.sourceHash,recordedHash);
            QCOMPARE(kept->asset.id,assetId);
            QCOMPARE(kept->global,r);
            QCOMPARE(kept->layers,layers);
            QCOMPARE(w.pickAt(0),Pick::Keep);
            QVERIFY(w.displayedPath().isEmpty());
            QVERIFY(!w.neutralPickerAvailable());
            QCOMPARE(queued.size(),0);
            QVERIFY(!hash(wrong).isEmpty());
            QCOMPARE(hash(correctCopy),originalBytes);
            QCOMPARE(hash(target),untouchedProjectHash);

            // Round 2: the matching copy is adopted; identity is preserved.
            QVERIFY2(w.relinkCurrentSource(correct,error),qPrintable(error));
            PersistenceResult accepted;QVERIFY(awaitOperation(completions,PersistenceOperation::SourceRelink,accepted));QVERIFY2(accepted.error.isEmpty(),qPrintable(accepted.error));
            completions.clear();
            QVERIFY(waitFor([&]{return w.currentPhotoDocument()&&w.currentPhotoDocument()->asset.sourcePath==correct;}));
            const auto relinked=w.currentPhotoDocument();QVERIFY(relinked);
            QCOMPARE(relinked->asset.sourcePath,correct);
            QCOMPARE(relinked->asset.id,assetId);
            QCOMPARE(relinked->asset.sourceHash,recordedHash);
            QCOMPARE(relinked->layers,layers);
            QCOMPARE(relinked->global,r);
            QCOMPARE(w.pickAt(0),Pick::Keep);
            QCOMPARE(w.filmstrip()->count(),1);
            QCOMPARE(w.currentIndex(),0);
            QVERIFY(storage(w)->dirty());
            QVERIFY(waitFor([&]{return w.neutralPickerAvailable();}));
            QVERIFY(waitFor([&]{return w.displayedPath()==correct;}));
            QCOMPARE(w.displayedPath(),correct);
            QCOMPARE(hash(correct),originalBytes);
            QCOMPARE(hash(target),untouchedProjectHash);
            QCOMPARE(storage(w)->state()->order,QStringList{correct});QCOMPARE(storage(w)->state()->activePath,correct);
            QVERIFY(waitFor([&]{return !storage(w)->busy()&&storage(w)->recoveredRevision()==storage(w)->state()->project->revision;}));
            RecoveryState recovered;QString recoveryHash;QVERIFY2(loadRecovery(w.projectRecoveryPath(),recovered,recoveryHash,error),qPrintable(error));
            QCOMPARE(recovered.order,QStringList{correct});QCOMPARE(recovered.activePath,correct);QCOMPARE(recovered.project->photos[0].document->layers,layers);

            // Saving must round-trip the relocated path and keep the same identity.
            QSignalSpy saved(storage(w),&ProjectPersistence::completed);
            QVERIFY2(w.saveProjectFile(target,error),qPrintable(error));
            PersistenceResult saveResult;QVERIFY(awaitOperation(saved,PersistenceOperation::ProjectSave,saveResult));QVERIFY2(saveResult.error.isEmpty(),qPrintable(saveResult.error));
            QVERIFY(!storage(w)->dirty());
            ProjectState actual;QString sha;QVERIFY2(loadProject(target,actual,sha,error),qPrintable(error));
            QCOMPARE(actual.photos.size(),1);
            QCOMPARE(actual.photos[0].document->asset.sourcePath,correct);
            QCOMPARE(actual.photos[0].document->asset.sourceHash,recordedHash);
            QCOMPARE(actual.photos[0].document->asset.id,assetId);
            QCOMPARE(hash(correct),originalBytes);
        }
    }
    // A Changed source cannot be unlocked by re-pointing at its own path: the
    // core same-path no-op must still verify the hash. Restoring real bytes does.
    void changedSourceRequiresRealBytes(){
        QTemporaryDir dir;QVERIFY(dir.isValid());
        const auto source=dir.filePath("source.jpg"),target=dir.filePath("p.luminas");
        QVERIFY(jpeg(source,QColor(30,120,70)));
        const auto originalBytes=hash(source);
        QString error;
        QByteArray pristine;
        {
            Window w(nullptr,QStringLiteral(""),dir.filePath("recovery"));w.show();QVERIFY(QTest::qWaitForWindowExposed(&w));QVERIFY(ready(w));
            w.addFiles({source});QVERIFY(parsed(w,1));
            w.mark(Pick::Keep);
            QSignalSpy saved(storage(w),&ProjectPersistence::completed);
            QVERIFY2(w.saveProjectFile(target,error),qPrintable(error));
            PersistenceResult saveResult;QVERIFY(awaitOperation(saved,PersistenceOperation::ProjectSave,saveResult));QVERIFY2(saveResult.error.isEmpty(),qPrintable(saveResult.error));
            QVERIFY(!storage(w)->dirty());
            // Capture the pristine bytes before corrupting the shared fixture.
            QFile pristineFile(source);QVERIFY(pristineFile.open(QIODevice::ReadOnly));pristine=pristineFile.readAll();pristineFile.close();
            QCOMPARE(QString::fromLatin1(QCryptographicHash::hash(pristine,QCryptographicHash::Sha256).toHex()),originalBytes);
        }
        QVERIFY(jpeg(source,QColor(220,210,40)));
        const auto changedBytes=hash(source);QVERIFY(!changedBytes.isEmpty());QVERIFY(changedBytes!=originalBytes);
        {
            Window w(nullptr,QStringLiteral(""),dir.filePath("restore"));QVERIFY(ready(w));
            QSignalSpy loaded(storage(w),&ProjectPersistence::completed);
            QVERIFY2(w.openProjectFile(target,false,error),qPrintable(error));
            PersistenceResult loadResult;QVERIFY(awaitOperation(loaded,PersistenceOperation::ProjectLoad,loadResult));QVERIFY2(loadResult.error.isEmpty(),qPrintable(loadResult.error));
            QCOMPARE(w.filmstrip()->count(),1);QVERIFY(w.currentPhotoDocument());
            QCOMPARE(hash(source),changedBytes);
            QVERIFY(w.currentPhotoDocument());
            QCOMPARE(w.currentPhotoDocument()->asset.sourcePath,source);
            QVERIFY(w.displayedPath().isEmpty());
            QVERIFY(!w.neutralPickerAvailable());
            QSignalSpy completions(storage(w),&ProjectPersistence::completed);
            // Same-path revisit of a Changed source must fail, not silently no-op.
            QVERIFY2(w.relinkCurrentSource(source,error),qPrintable(error));
            PersistenceResult rejected;QVERIFY(awaitOperation(completions,PersistenceOperation::SourceRelink,rejected));
            QVERIFY2(!rejected.error.isEmpty(),"a changed source must not be unlocked by its own path");
            completions.clear();
            QVERIFY(w.displayedPath().isEmpty());
            QVERIFY(!w.neutralPickerAvailable());
            QVERIFY(!storage(w)->dirty());
            QVERIFY(w.currentPhotoDocument());
            QCOMPARE(w.currentPhotoDocument()->asset.sourcePath,source);
            QCOMPARE(hash(source),changedBytes);
            // Restoring the real bytes lets the same path relink; the clean project
            // must stay clean because no path/version actually changed.
            {QFile restore(source);QVERIFY(restore.open(QIODevice::WriteOnly|QIODevice::Truncate));QCOMPARE(restore.write(pristine),qint64(pristine.size()));restore.close();}
            QCOMPARE(hash(source),originalBytes);
            const auto revisionBefore=storage(w)->state()->project->revision;
            QVERIFY(!storage(w)->dirty());
            QVERIFY2(w.relinkCurrentSource(source,error),qPrintable(error));
            PersistenceResult accepted;QVERIFY(awaitOperation(completions,PersistenceOperation::SourceRelink,accepted));QVERIFY2(accepted.error.isEmpty(),qPrintable(accepted.error));
            completions.clear();
            QVERIFY(waitFor([&]{return w.neutralPickerAvailable();}));
            QVERIFY(!w.displayedPath().isEmpty());
            QCOMPARE(w.displayedPath(),source);
            QVERIFY(!storage(w)->dirty());
            QVERIFY(w.currentPhotoDocument());
            QCOMPARE(w.currentPhotoDocument()->asset.sourcePath,source);
            QCOMPARE(storage(w)->state()->project->revision,revisionBefore);
            QCOMPARE(w.pickAt(0),Pick::Keep);
            QCOMPARE(hash(source),originalBytes);
#ifdef Q_OS_WIN
            QVERIFY2(w.relinkCurrentSource(source.toUpper(),error),qPrintable(error));
            PersistenceResult caseOnly;QVERIFY(awaitOperation(completions,PersistenceOperation::SourceRelink,caseOnly));QVERIFY2(caseOnly.error.isEmpty(),qPrintable(caseOnly.error));
            QCOMPARE(w.currentPhotoDocument()->asset.sourcePath,source);QCOMPARE(storage(w)->state()->order,QStringList{source});
            QVERIFY(waitFor([&]{return w.neutralPickerAvailable();}));QVERIFY(!storage(w)->dirty());
#endif
        }
    }
    // A relink that cannot be performed must be refused synchronously and must
    // never enqueue background work that could touch the project.
    void relinkWithoutIdentityIsRefused(){
        QTemporaryDir dir;QVERIFY(dir.isValid());
        const auto source=dir.filePath("source.jpg"),other=dir.filePath("other.jpg");
        QVERIFY(jpeg(source,QColor(90,40,160)));QVERIFY(jpeg(other,QColor(10,10,10)));
        Window w(nullptr,QStringLiteral(""),dir.filePath("recovery"));QVERIFY(ready(w));
        QSignalSpy completions(storage(w),&ProjectPersistence::completed);
        QSignalSpy queued(w.findChild<EditLoader *>(),&EditLoader::requestQueued);
        QString error;
        QVERIFY(!w.relinkCurrentSource(other,error)); // no current photo at all
        QVERIFY(!error.isEmpty());
        w.addFiles({source});QVERIFY(parsed(w,1));
        // Identity exists, but a non-absolute path can never be adopted.
        QVERIFY(!w.relinkCurrentSource(QStringLiteral("relative.jpg"),error));
        QVERIFY(!error.isEmpty());
        queued.clear();completions.clear();
        // A different file is rejected; the original valid source can resume.
        const auto recorded=w.currentPhotoDocument();QVERIFY(recorded);
        const auto identity=recorded->asset.id;QVERIFY(!identity.isEmpty());
        const auto path=recorded->asset.sourcePath;QVERIFY(!path.isEmpty());
        QVERIFY(waitFor([&]{return !w.displayedPath().isEmpty();}));
        QVERIFY2(w.relinkCurrentSource(other,error),qPrintable(error));
        PersistenceResult rejected;QVERIFY(awaitOperation(completions,PersistenceOperation::SourceRelink,rejected));
        QVERIFY2(!rejected.error.isEmpty(),"a different file must not be adopted");
        completions.clear();
        QVERIFY(w.currentPhotoDocument());
        QCOMPARE(w.currentPhotoDocument()->asset.id,identity);
        QCOMPARE(w.currentPhotoDocument()->asset.sourcePath,path);
        QVERIFY(waitFor([&]{return w.displayedPath()==path&&w.neutralPickerAvailable();}));
    }
};
QTEST_MAIN(ProjectRelinkWindowTests)
#include "project_relink_window_tests.moc"


