#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QCryptographicHash>
#include <QSemaphore>
#include <QThread>
#include <atomic>
#include "app/project_persistence.h"
using namespace LocalEdit;
namespace {
QString hashOf(const QByteArray &bytes){return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());}
bool writeText(const QString &path,const QByteArray &bytes){QFile file(path);return file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size();}
QByteArray readText(const QString &path){QFile file(path);return file.open(QIODevice::ReadOnly)?file.readAll():QByteArray{};}
RecoverySnapshot makeState(const QString &source,quint64 revision=1){
    PhotoDocument doc;doc.asset.id="asset";doc.asset.sourcePath=source;doc.asset.sourceHash=hashOf("source");doc.asset.normalizedSize={16,12};
    ProjectState project;project.id="test-project";project.name="test";project.revision=revision;project.activeAssetId=doc.asset.id;
    project.photos={{std::make_shared<const PhotoDocument>(doc),PhotoMark::Keep}};
    RecoveryState state;state.project=std::make_shared<const ProjectState>(project);state.order={source};state.activePath=source;
    return std::make_shared<const RecoveryState>(state);
}
RecoverySnapshot nextState(RecoverySnapshot state,quint64 revision,double exposure=0){
    auto out=*state;auto project=*out.project;project.revision=revision;
    auto doc=*project.photos[0].document;doc.global.exposure=exposure;
    project.photos[0].document=std::make_shared<const PhotoDocument>(doc);out.project=std::make_shared<const ProjectState>(project);
    return std::make_shared<const RecoveryState>(out);
}
struct Gate {
    QSemaphore release;
    std::atomic<int> calls{0},live{0},maxLive{0};
    std::atomic<bool> entered{false},exited{false},timedOut{false};
    RecoverySnapshot first;
};
struct ReleaseOnExit {std::shared_ptr<Gate> gate;~ReleaseOnExit(){gate->release.release(16);}};
ProjectPersistence::Executor blocked(std::shared_ptr<Gate> gate,RecoverySnapshot loaded={}){
    return [gate,loaded](const PersistenceJob &job){
        const int live=++gate->live;int previous=gate->maxLive.load();while(previous<live&&!gate->maxLive.compare_exchange_weak(previous,live)){}
        const int call=++gate->calls;
        if(call==1){gate->first=job.state;gate->entered=true;if(!gate->release.tryAcquire(1,3000))gate->timedOut=true;gate->exited=true;}
        PersistenceResult result;result.state=loaded?loaded:job.state;result.hash=QString(64,'a');
        if(result.state)result.revision=result.state->project->revision;
        if(gate->timedOut)result.error="blocked executor timed out";
        --gate->live;return result; // models a commit acknowledgment, even after cancellation
    };
}
}
class ProjectPersistenceTests:public QObject {
    Q_OBJECT
private slots:
    void latestRecoveryCoalescesAndExplicitSaveHasPriority(){
        QTemporaryDir dir;QVERIFY(dir.isValid());QString error;const auto one=makeState(dir.filePath("source.txt"));
        auto gate=std::make_shared<Gate>();ProjectPersistence persistence(nullptr,blocked(gate));ReleaseOnExit safety{gate};
        QSignalSpy started(&persistence,&ProjectPersistence::started),done(&persistence,&ProjectPersistence::completed);
        QVERIFY(persistence.reset(one,dir.filePath("recovery.json"),false,error));persistence.retryRecovery();
        QTRY_VERIFY(gate->entered.load());QVERIFY(persistence.busy());
        QVERIFY(persistence.update(nextState(one,2,.2),error));QVERIFY(persistence.update(nextState(one,3,.3),error));
        persistence.retryRecovery();QVERIFY(persistence.save(dir.filePath("explicit.luminas"),error));
        QVERIFY(persistence.update(nextState(one,4,.4),error));persistence.retryRecovery();
        gate->release.release();QTRY_COMPARE(done.count(),3);QTRY_VERIFY(!persistence.busy());
        QCOMPARE(started.count(),3);QCOMPARE(qvariant_cast<PersistenceOperation>(started[0][0]),PersistenceOperation::RecoverySave);QCOMPARE(started[0][1].toULongLong(),1);
        QCOMPARE(qvariant_cast<PersistenceOperation>(started[1][0]),PersistenceOperation::ProjectSave);QCOMPARE(started[1][1].toULongLong(),3);
        QCOMPARE(qvariant_cast<PersistenceOperation>(started[2][0]),PersistenceOperation::RecoverySave);QCOMPARE(started[2][1].toULongLong(),4);
        QCOMPARE(gate->maxLive.load(),1);QVERIFY(!gate->timedOut);QVERIFY(persistence.dirty());QCOMPARE(persistence.recoveredRevision(),4);
        QCOMPARE(persistence.state()->project->revision,4);QCOMPARE(persistence.state()->explicitPath,dir.filePath("explicit.luminas"));
        const auto saved=qvariant_cast<PersistenceResult>(done[1][0]);QCOMPARE(saved.state->project->revision,3);QCOMPARE(saved.state->project->photos[0].document->global.exposure,.3);
        QCOMPARE(gate->first->project->photos[0].document->global.exposure,0.);
    }
    void snapshotsDoNotRetainMutableAliases(){
        QTemporaryDir dir;QVERIFY(dir.isValid());auto source=makeState(dir.filePath("source.txt"));
        auto doc=std::make_shared<PhotoDocument>(*source->project->photos[0].document);doc->global.exposure=.25;
        auto project=std::make_shared<ProjectState>(*source->project);project->photos[0].document=doc;
        auto state=std::make_shared<RecoveryState>(*source);state->project=project;
        auto gate=std::make_shared<Gate>();ProjectPersistence persistence(nullptr,blocked(gate));ReleaseOnExit safety{gate};QString error;
        QVERIFY(persistence.reset(state,dir.filePath("recovery.json"),false,error));
        doc->global.exposure=2;project->name="mutated";state->order.clear();
        persistence.retryRecovery();QTRY_VERIFY(gate->entered.load());QVERIFY(gate->first);
        QCOMPARE(gate->first->project->photos[0].document->global.exposure,.25);QCOMPARE(gate->first->project->name,QString("test"));QCOMPARE(gate->first->order.size(),1);
        gate->release.release();QTRY_VERIFY(!persistence.busy());
    }
    void resetDiscardsOldContextWithoutOverlappingJobs(){
        QTemporaryDir dir;QVERIFY(dir.isValid());auto old=makeState(dir.filePath("old.txt"));QString error;
        auto gate=std::make_shared<Gate>();ProjectPersistence persistence(nullptr,blocked(gate));ReleaseOnExit safety{gate};QSignalSpy done(&persistence,&ProjectPersistence::completed);
        QVERIFY(persistence.reset(old,dir.filePath("old.json"),false,error));persistence.retryRecovery();QTRY_VERIFY(gate->entered.load());
        QVERIFY(!persistence.reset(old,dir.filePath("old.json"),true,error));
        auto state=*makeState(dir.filePath("new.txt"));auto project=*state.project;project.id="new-project";state.project=std::make_shared<const ProjectState>(project);
        auto fresh=std::make_shared<const RecoveryState>(state);QVERIFY(persistence.reset(fresh,dir.filePath("new.json"),true,error));
        gate->release.release();QTRY_VERIFY(!persistence.busy());QCOMPARE(done.count(),0);QVERIFY(!persistence.dirty());QVERIFY(persistence.recoveryHash().isEmpty());
        QVERIFY(persistence.update(nextState(fresh,2),error));persistence.retryRecovery();QTRY_COMPARE(done.count(),1);QTRY_VERIFY(!persistence.busy());
        QCOMPARE(persistence.state()->project->id,QString("new-project"));QCOMPARE(persistence.recoveredRevision(),2);QCOMPARE(gate->maxLive.load(),1);
    }
    void newEditInvalidatesRunningLoad(){
        QTemporaryDir dir;QVERIFY(dir.isValid());auto initial=makeState(dir.filePath("source.txt"));QString error;
        auto gate=std::make_shared<Gate>();ProjectPersistence persistence(nullptr,blocked(gate,nextState(initial,99,1)));ReleaseOnExit safety{gate};QSignalSpy done(&persistence,&ProjectPersistence::completed);
        QVERIFY(persistence.reset(initial,dir.filePath("recovery.json"),true,error));QVERIFY(persistence.open(dir.filePath("project.luminas"),false,error));QTRY_VERIFY(gate->entered.load());
        QVERIFY(persistence.update(nextState(initial,2,.5),error));gate->release.release();QTRY_COMPARE(done.count(),1);QTRY_VERIFY(!persistence.busy());
        const auto result=qvariant_cast<PersistenceResult>(done[0][0]);QVERIFY(result.discarded);QVERIFY(!result.state);QVERIFY(result.hash.isEmpty());
        QCOMPARE(persistence.state()->project->revision,2);QCOMPARE(persistence.state()->project->photos[0].document->global.exposure,.5);QVERIFY(persistence.dirty());
    }
    void cancelCannotUndoAcknowledgedCommit(){
        QTemporaryDir dir;QVERIFY(dir.isValid());const auto initial=makeState(dir.filePath("source.txt"));QString error;
        auto gate=std::make_shared<Gate>();ProjectPersistence persistence(nullptr,blocked(gate));ReleaseOnExit safety{gate};QSignalSpy done(&persistence,&ProjectPersistence::completed);
        QVERIFY(persistence.reset(initial,dir.filePath("recovery.json"),false,error));QVERIFY(persistence.save(dir.filePath("project.luminas"),error));QTRY_VERIFY(gate->entered.load());
        persistence.cancel();gate->release.release();QTRY_COMPARE(done.count(),1);QTRY_VERIFY(!persistence.busy());
        const auto result=qvariant_cast<PersistenceResult>(done[0][0]);QVERIFY(result.error.isEmpty());QVERIFY(!persistence.dirty());QCOMPARE(persistence.state()->explicitHash,QString(64,'a'));
    }
    void destructionDoesNotJoinBlockedStorage(){
        QTemporaryDir dir;QVERIFY(dir.isValid());QString error;auto gate=std::make_shared<Gate>();
        auto persistence=std::make_unique<ProjectPersistence>(nullptr,blocked(gate));ReleaseOnExit safety{gate};
        QVERIFY(persistence->reset(makeState(dir.filePath("source.txt")),dir.filePath("recovery.json"),false,error));persistence->retryRecovery();QTRY_VERIFY(gate->entered.load());
        QElapsedTimer elapsed;elapsed.start();persistence.reset();QVERIFY2(elapsed.elapsed()<1000,"GUI destruction waited for storage");
        gate->release.release();QTRY_VERIFY(gate->exited.load());QTest::qWait(30);QVERIFY(!gate->timedOut);
    }
    void failureDoesNotRetryWithoutAnEditOrRequest(){
        QTemporaryDir dir;QVERIFY(dir.isValid());auto calls=std::make_shared<std::atomic<int>>(0);
        ProjectPersistence persistence(nullptr,[calls](const PersistenceJob &job){PersistenceResult r;r.state=job.state;if(++*calls==1)r.error="injected write failure";else r.hash=QString(64,'b');return r;});
        QString error;QSignalSpy done(&persistence,&ProjectPersistence::completed);QVERIFY(persistence.reset(makeState(dir.filePath("source.txt")),dir.filePath("recovery.json"),false,error));
        persistence.retryRecovery();QTRY_COMPARE(done.count(),1);QVERIFY(persistence.dirty());QVERIFY(!persistence.lastError().isEmpty());
        QTest::qWait(2100);QCOMPARE(calls->load(),1);persistence.retryRecovery();QTRY_COMPARE(done.count(),2);QVERIFY(persistence.lastError().isEmpty());QVERIFY(persistence.dirty());QCOMPARE(persistence.recoveredRevision(),1);
    }
void realIdleDebounceAndExplicitSave()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString source=dir.filePath("source.txt");
    QVERIFY(writeText(source,"source"));
    const QString journal=dir.filePath("journal.luminas");
    const QString explicitPath=dir.filePath("explicit.luminas");
    const QString foreignPath=dir.filePath("foreign.luminas");
    const RecoverySnapshot s1=makeState(source,1);

    ProjectPersistence p;
    QSignalSpy started(&p,&ProjectPersistence::started);
    QElapsedTimer sinceEdit;qint64 idleDelay=-1;
    connect(&p,&ProjectPersistence::started,&p,[&](PersistenceOperation op,quint64){if(op==PersistenceOperation::RecoverySave&&idleDelay<0)idleDelay=sinceEdit.elapsed();});
    QString error;
    QVERIFY2(p.reset(s1,journal,false,error),qPrintable(error));
    QVERIFY(!p.busy());

    QTest::qWait(1100);
    const RecoverySnapshot s2=nextState(s1,2,0.5);
    sinceEdit.start();QVERIFY2(p.update(s2,error),qPrintable(error));
    QTest::qWait(1100);

    QVERIFY(!QFile::exists(journal));QCOMPARE(started.count(),0);

    QSignalSpy spy(&p,&ProjectPersistence::completed);
    QTRY_VERIFY_WITH_TIMEOUT(spy.count()>=1,4000);
    QTRY_VERIFY_WITH_TIMEOUT(!p.busy(),4000);
    QCOMPARE(spy.count(),1);QCOMPARE(started.count(),1);QVERIFY(idleDelay>=1950);qInfo()<<"actual recovery idle delay"<<idleDelay;

    {
        RecoveryState loaded;
        QString fileHash,loadError;
        QVERIFY2(loadRecovery(journal,loaded,fileHash,loadError),qPrintable(loadError));
        QVERIFY(loaded.project);
        QCOMPARE(loaded.project->revision,quint64(2));
        QVERIFY(!loaded.project->photos.isEmpty());
        QVERIFY(loaded.project->photos[0].document);
        QCOMPARE(loaded.project->photos[0].document->global.exposure,0.5);
        QCOMPARE(p.recoveredRevision(),quint64(2));
        QCOMPARE(p.recoveryHash(),fileHash);
        QVERIFY(!fileHash.isEmpty());
    }
    QVERIFY(p.dirty());

    QSignalSpy saveSpy(&p,&ProjectPersistence::completed);
    QVERIFY2(p.save(explicitPath,error),qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(saveSpy.count()>=1,4000);
    QTRY_VERIFY_WITH_TIMEOUT(!p.busy(),4000);
    QVERIFY(!p.dirty());

    {
        ProjectState loaded;
        QString fileHash,loadError;
        QVERIFY2(loadProject(explicitPath,loaded,fileHash,loadError),qPrintable(loadError));
        QCOMPARE(loaded.revision,quint64(2));
        QVERIFY(!loaded.photos.isEmpty());
        QVERIFY(loaded.photos[0].document);
        QCOMPARE(loaded.photos[0].document->global.exposure,0.5);
        QCOMPARE(p.state()->explicitPath,explicitPath);
        QCOMPARE(p.state()->explicitHash,fileHash);
        QCOMPARE(fileHash,hashOf(readText(explicitPath)));
    }

    const RecoverySnapshot s3=nextState(s1,3,0.75);
    QVERIFY2(p.update(s3,error),qPrintable(error));
    QSignalSpy overwriteSpy(&p,&ProjectPersistence::completed);
    QVERIFY2(p.save(explicitPath,error),qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(overwriteSpy.count()>=1,4000);
    QTRY_VERIFY_WITH_TIMEOUT(!p.busy(),4000);
    QVERIFY(!p.dirty());
    {
        ProjectState loaded;
        QString fileHash,loadError;
        QVERIFY2(loadProject(explicitPath,loaded,fileHash,loadError),qPrintable(loadError));
        QCOMPARE(loaded.revision,quint64(3));
        QCOMPARE(p.state()->explicitHash,fileHash);
        QCOMPARE(fileHash,hashOf(readText(explicitPath)));
    }

    const auto successfulHash=p.state()->explicitHash;
    const RecoverySnapshot s4=nextState(s1,4,1.0);
    QVERIFY2(p.update(s4,error),qPrintable(error));
    QVERIFY(writeText(foreignPath,"foreign"));
    const QByteArray foreignBefore=readText(foreignPath);
    QCOMPARE(foreignBefore,QByteArray("foreign"));

    QSignalSpy failSpy(&p,&ProjectPersistence::completed);
    QVERIFY2(p.save(foreignPath,error),qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(failSpy.count()>=1,4000);
    QTRY_VERIFY_WITH_TIMEOUT(!p.busy(),4000);

    bool sawForeignFailure=false;
    for(int i=0;i<failSpy.count();++i){
        const PersistenceResult r=qvariant_cast<PersistenceResult>(failSpy[i][0]);
        if(r.operation==PersistenceOperation::ProjectSave&&r.path==foreignPath){
            sawForeignFailure=true;
            QVERIFY(!r.error.isEmpty());
        }
    }
    QVERIFY(sawForeignFailure);
    QVERIFY(!p.lastError().isEmpty());
    QVERIFY(p.dirty());
    QCOMPARE(p.state()->explicitPath,explicitPath);
    QCOMPARE(p.state()->explicitHash,successfulHash);
    QCOMPARE(readText(foreignPath),foreignBefore);
}

void loadsDeliverHashAndSourceStatusWithoutReplacingLiveState()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString source=dir.filePath("source.txt");
    QVERIFY(writeText(source,"source"));
    const QString explicitPath=dir.filePath("explicit.luminas");
    const QString recoveryPath=dir.filePath("recovery.luminas");

    const RecoverySnapshot base=makeState(source,1);
    QString setupError;
    ProjectState seeded=*base->project;
    ProjectSnapshot frozen=freezeProject(seeded,setupError);
    QVERIFY2(bool(frozen),qPrintable(setupError));
    QString explicitHash,recoveryHash;
    QVERIFY2(saveProject(explicitPath,frozen,explicitHash,setupError),qPrintable(setupError));
    QVERIFY2(saveRecovery(recoveryPath,base,recoveryHash,setupError),qPrintable(setupError));
    const QString preservedSourceHash=base->project->photos[0].document->asset.sourceHash;
    QCOMPARE(preservedSourceHash,hashOf("source"));

    QVERIFY(writeText(source,"changed"));
    const QByteArray explicitBytes=readText(explicitPath);
    const QByteArray recoveryBytes=readText(recoveryPath);
    QVERIFY(!explicitBytes.isEmpty());
    QVERIFY(!recoveryBytes.isEmpty());
    const QString explicitFileHash=hashOf(explicitBytes);
    const QString recoveryFileHash=hashOf(recoveryBytes);

    const RecoverySnapshot live=nextState(base,4,0.25);
    ProjectPersistence p;
    QString error;
    QVERIFY2(p.reset(live,recoveryPath,true,error),qPrintable(error));
    QVERIFY(!p.busy());

    QSignalSpy loadSpy(&p,&ProjectPersistence::completed);
    QVERIFY2(p.open(explicitPath,false,error),qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(loadSpy.count()>=1,4000);
    QTRY_VERIFY_WITH_TIMEOUT(!p.busy(),4000);
    {
        const PersistenceResult r=qvariant_cast<PersistenceResult>(loadSpy[0][0]);
        QCOMPARE(r.operation,PersistenceOperation::ProjectLoad);
        QVERIFY2(r.error.isEmpty(),qPrintable(r.error));
        QVERIFY(r.state);
        QVERIFY(r.state->project);
        QCOMPARE(r.state->project->revision,quint64(1));
        QCOMPARE(r.hash,explicitFileHash);
        QCOMPARE(r.sources.size(),1);
        QCOMPARE(r.sources[0].state,SourceState::Changed);
        QVERIFY(!r.state->project->photos.isEmpty());
        QVERIFY(r.state->project->photos[0].document);
        QCOMPARE(r.state->project->photos[0].document->asset.sourceHash,preservedSourceHash);
    }
    QVERIFY(p.state());
    QCOMPARE(p.state()->project->revision,quint64(4));
    QCOMPARE(p.state()->project->photos[0].document->global.exposure,0.25);

    QVERIFY(QFile::remove(source));
    QVERIFY(!QFile::exists(source));

    QSignalSpy recoverySpy(&p,&ProjectPersistence::completed);
    QVERIFY2(p.open(recoveryPath,true,error),qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(recoverySpy.count()>=1,4000);
    QTRY_VERIFY_WITH_TIMEOUT(!p.busy(),4000);
    {
        const PersistenceResult r=qvariant_cast<PersistenceResult>(recoverySpy[0][0]);
        QCOMPARE(r.operation,PersistenceOperation::RecoveryLoad);
        QVERIFY2(r.error.isEmpty(),qPrintable(r.error));
        QVERIFY(r.state);
        QCOMPARE(r.hash,recoveryFileHash);
        QVERIFY(!r.hash.isEmpty());
        QCOMPARE(r.sources.size(),1);
        QCOMPARE(r.sources[0].state,SourceState::Missing);
        QVERIFY(r.state->project);
    }
    QVERIFY(p.state());
    QCOMPARE(p.state()->project->revision,quint64(4));
    QCOMPARE(p.state()->project->photos[0].document->global.exposure,0.25);
}
};
QTEST_GUILESS_MAIN(ProjectPersistenceTests)
#include "project_persistence_tests.moc"
