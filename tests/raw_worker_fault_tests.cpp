#include <QtTest>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QProcess>
#include <QSaveFile>
#include <QJsonDocument>
#include <QDir>
#include <QUuid>
#include <future>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "raw/worker_client_testing.h"
#include "raw/worker_session.h"
using namespace RawWorker;
namespace {
QByteArray bytes(const QString &path){QFile file(path);return file.open(QIODevice::ReadOnly)?file.readAll():QByteArray{};}
QString sha(const QByteArray &data){return QString::fromLatin1(QCryptographicHash::hash(data,QCryptographicHash::Sha256).toHex());}
struct Fixture {
    QTemporaryDir directory;Request request;
    bool init(){
        if(!directory.isValid())return false;request.id=QUuid::createUuid().toString(QUuid::WithoutBraces);request.token=QUuid::createUuid().toString(QUuid::WithoutBraces);
        request.sourcePath=directory.filePath("source.jpg");QImage image(96,64,QImage::Format_RGB32);image.fill(QColor(45,90,140));
        if(!image.save(request.sourcePath,"JPEG",98))return false;request.sourceHash=sha(bytes(request.sourcePath));return true;
    }
};
class ProcessHandle {
    HANDLE handle=nullptr;
public:
    ~ProcessHandle(){if(handle)CloseHandle(handle);}
    bool open(qint64 pid){handle=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,DWORD(pid));return handle!=nullptr;}
    bool valid()const{return handle!=nullptr;}
    bool terminated(){return handle&&WaitForSingleObject(handle,5000)==WAIT_OBJECT_0;}
};
struct Outcome {FrameResult result;qint64 elapsed=0;bool started=false,handleOpened=false,terminated=false,sessionRemoved=false;QString session;QStringList phases;};
Outcome fake(const Request &request,const QString &mode,bool reject=false,const WorkerProgress &observer={}){
    Outcome out;QTemporaryDir root;ProcessHandle process;
    Testing::Options options;options.executable=QCoreApplication::applicationDirPath()+"/raw_worker_fault_helper.exe";options.arguments={"--mode",mode};options.sessionRoot=root.path();options.timeoutMs=1000;options.rejectJobAssignment=reject;
    options.sessionCreated=[&](const QString &path){out.session=path;};options.started=[&](qint64 pid){out.started=true;out.handleOpened=process.open(pid);};
    QElapsedTimer clock;clock.start();out.result=std::async(std::launch::async,[&]{return Testing::run(request,options,{},[&](const QString &phase,int current,int total){out.phases<<phase;if(observer)observer(phase,current,total);});}).get();out.elapsed=clock.elapsed();
    out.terminated=process.terminated();out.sessionRemoved=!out.session.isEmpty()&&!QFileInfo::exists(out.session);
    // Observe before root's destructor: a test must not hide client leaks by
    // removing its own root first. Preserve it if a process is still alive.
    if((out.started&&!out.terminated)||!out.sessionRemoved)root.setAutoRemove(false);return out;
}
FrameResult followUp(){Fixture f;if(!f.init())return {{},"fixture failed"};return std::async(std::launch::async,[request=f.request]{return runWorker(request);}).get();}
}
class RawWorkerFaultTests:public QObject {
    Q_OBJECT
private slots:
    void initTestCase(){QStandardPaths::setTestModeEnabled(true);QVERIFY(QFileInfo(QCoreApplication::applicationDirPath()+"/raw_worker_fault_helper.exe").isFile());}
    void faults_data(){QTest::addColumn<QString>("mode");for(const auto mode:{"valid","badHello","noHello","hang","crash","wrongId","duplicateDone","noiseAfterDone","truncatedDone","doneCrash","badPayload","oversize"})QTest::newRow(mode)<<QString::fromLatin1(mode);}
    void faults(){
        QFETCH(QString,mode);Fixture f;QVERIFY(f.init());const auto before=bytes(f.request.sourcePath);const auto out=fake(f.request,mode);
        QVERIFY(out.started);QVERIFY(out.handleOpened);QVERIFY(out.terminated);QVERIFY2(out.elapsed<6000,QByteArray::number(out.elapsed));QVERIFY2(out.sessionRemoved,qPrintable(out.session));
        QCOMPARE(bytes(f.request.sourcePath),before);QCOMPARE(sha(bytes(f.request.sourcePath)),f.request.sourceHash);
        if(mode=="valid"){QVERIFY2(out.result.source,qPrintable(out.result.error));QVERIFY(out.result.error.isEmpty());QCOMPARE(out.result.source->linear.size(),QSize(96,64));QCOMPARE(out.phases,QStringList{"payloadReady"});}
        else {QVERIFY(!out.result.source);QVERIFY(!out.result.error.isEmpty());if(mode=="hang"||mode=="noHello")QVERIFY2(out.result.error.contains(QStringLiteral("超时")),qPrintable(out.result.error));if(mode=="oversize")QVERIFY2(out.result.error.contains(QStringLiteral("超限")),qPrintable(out.result.error));if(mode=="doneCrash")QVERIFY2(out.result.error.contains(QStringLiteral("异常退出")),qPrintable(out.result.error));}
        qInfo()<<"fault mode"<<mode<<"elapsedMs"<<out.elapsed<<"sessionRemoved"<<out.sessionRemoved<<"processEnded"<<out.terminated;
        const auto next=followUp();QVERIFY2(next.source,qPrintable(next.error));
    }
    void rejectedAssignmentNeverDispatches(){
        Fixture f;QVERIFY(f.init());const auto before=bytes(f.request.sourcePath);const auto out=fake(f.request,"hang",true);
        QVERIFY(!out.started);QVERIFY(!out.result.source);QVERIFY(out.result.error.contains(QStringLiteral("进程树保护")));QVERIFY(out.sessionRemoved);QVERIFY(out.elapsed<6000);QCOMPARE(bytes(f.request.sourcePath),before);
        const auto next=followUp();QVERIFY2(next.source,qPrintable(next.error));
    }
    void sourceChangeAfterPayload(){
        Fixture f;QVERIFY(f.init());const auto before=bytes(f.request.sourcePath);bool changed=false;
        const auto out=fake(f.request,"valid",false,[&](const QString &phase,int,int){if(phase=="payloadReady"&&!changed){QFile file(f.request.sourcePath);if(file.open(QIODevice::Append)){changed=file.write("x",1)==1;file.flush();}}});
        QVERIFY(changed);QVERIFY(!out.result.source);QVERIFY2(out.result.error.contains(QStringLiteral("RAW源")),qPrintable(out.result.error));QVERIFY(out.terminated);QVERIFY(out.sessionRemoved);QCOMPARE(bytes(f.request.sourcePath).size(),before.size()+1);QVERIFY(sha(bytes(f.request.sourcePath))!=f.request.sourceHash);
        const auto next=followUp();QVERIFY2(next.source,qPrintable(next.error));
    }
    void timeoutKillsDescendant(){
        Fixture f;QVERIFY(f.init());ProcessHandle child;bool reported=false;
        const auto out=fake(f.request,"descendant",false,[&](const QString &phase,int,int){if(phase.startsWith("grandchild-")){reported=true;child.open(phase.mid(11).toLongLong());}});
        QVERIFY(reported);QVERIFY(child.valid());QVERIFY(child.terminated());QVERIFY(out.terminated);QVERIFY(out.sessionRemoved);QVERIFY(!out.result.source);QVERIFY(out.result.error.contains(QStringLiteral("超时")));
    }
    void parentKillClosesJobAndRecoversSession(){
        QTemporaryDir root;QVERIFY(root.isValid());QProcess driver;ProcessHandle worker,grandchild;
        struct Guard {QProcess &driver;QTemporaryDir &root;ProcessHandle &worker,&grandchild;~Guard(){if(driver.state()!=QProcess::NotRunning){driver.kill();driver.waitForFinished(5000);}if(!worker.valid()||!grandchild.valid()||!worker.terminated()||!grandchild.terminated())root.setAutoRemove(false);}}guard{driver,root,worker,grandchild};
        driver.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args){args->flags|=CREATE_NO_WINDOW;});driver.start(QCoreApplication::applicationFilePath(),{"--parent-driver",root.path()});QVERIFY(driver.waitForStarted(5000));
        const auto readyPath=root.filePath("ready.json");QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(readyPath),5000);const auto report=QJsonDocument::fromJson(bytes(readyPath)).object();
        const auto session=report["session"].toString();QVERIFY(session.startsWith(root.path()+"/session-"));QVERIFY(worker.open(report["worker"].toInteger()));QVERIFY(grandchild.open(report["grandchild"].toInteger()));
        QVERIFY(QFile::exists(QDir(session).filePath(report["token"].toString()+".frame")));const auto sourceBefore=bytes(root.filePath("driver.jpg"));QVERIFY(!sourceBefore.isEmpty());
        driver.kill();QVERIFY(driver.waitForFinished(5000));QVERIFY(worker.terminated());QVERIFY(grandchild.terminated());
        const auto cleanup=cleanAbandonedSessions(root.path(),0);QCOMPARE(cleanup.removed,1);QVERIFY2(cleanup.errors.isEmpty(),qPrintable(cleanup.errors.join(';')));QVERIFY(!QFile::exists(session));QCOMPARE(bytes(root.filePath("driver.jpg")),sourceBefore);
        qInfo()<<"parent killed; worker and descendant signaled; owned session recovered";
    }
};
int main(int argc,char **argv){
    QCoreApplication app(argc,argv);
    if(app.arguments().size()==3&&app.arguments()[1]=="--parent-driver"){
        const auto root=app.arguments()[2];if(!QDir::isAbsolutePath(root)||!QFileInfo(root).isDir()||QFileInfo(root).isSymLink()||!QDir(root).entryList(QDir::AllEntries|QDir::NoDotAndDotDot|QDir::Hidden|QDir::System).isEmpty())return 2;
        Request request;request.id=QUuid::createUuid().toString(QUuid::WithoutBraces);request.token=QUuid::createUuid().toString(QUuid::WithoutBraces);request.sourcePath=QDir(root).filePath("driver.jpg");
        QImage image(96,64,QImage::Format_RGB32);image.fill(QColor(45,90,140));if(!image.save(request.sourcePath,"JPEG",98))return 2;request.sourceHash=sha(bytes(request.sourcePath));
        Testing::Options options;options.executable=QCoreApplication::applicationDirPath()+"/raw_worker_fault_helper.exe";options.arguments={"--mode","descendant"};options.sessionRoot=root;options.timeoutMs=30000;
        qint64 worker=0;QString session;options.started=[&](qint64 pid){worker=pid;};options.sessionCreated=[&](const QString &path){session=path;};
        const auto result=std::async(std::launch::async,[&]{return Testing::run(request,options,{},[&](const QString &phase,int,int){
            if(!phase.startsWith("grandchild-"))return;QSaveFile file(QDir(root).filePath("ready.json"));file.setDirectWriteFallback(false);if(!file.open(QIODevice::WriteOnly))return;
            const auto report=QJsonDocument(QJsonObject{{"worker",worker},{"grandchild",phase.mid(11).toLongLong()},{"session",session},{"token",request.token}}).toJson(QJsonDocument::Compact);if(file.write(report)==report.size())file.commit();
        });}).get();return result.source?0:2;
    }
    RawWorkerFaultTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "raw_worker_fault_tests.moc"
