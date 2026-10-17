#include <QtTest>
#include <QProcess>
#include <QTemporaryDir>
#include <QUuid>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include "raw/worker_session.h"
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
using namespace RawWorker;
namespace {
Request identity(){Request request;request.id=QUuid::createUuid().toString(QUuid::WithoutBraces);request.token=QUuid::createUuid().toString(QUuid::WithoutBraces);return request;}
bool write(const QString &path,const QByteArray &bytes){QFile file(path);return file.open(QIODevice::WriteOnly|QIODevice::NewOnly)&&file.write(bytes)==bytes.size()&&file.flush();}
bool cached(const QString &directory,const QString &token){return write(QDir(directory).filePath(token+".frame"),QByteArray(128,'f'))&&write(QDir(directory).filePath(".raw-stage-abc123"),QByteArray(64,'s'));}
QJsonObject crashedOwner(const QString &root){
    QProcess process;process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args){args->flags|=CREATE_NO_WINDOW;});
    process.start(QCoreApplication::applicationFilePath(),{"--crashed-owner",root});
    if(!process.waitForStarted(5000)||!process.waitForFinished(10000)||process.exitCode()!=5){process.kill();process.waitForFinished(5000);return {};}
    return QJsonDocument::fromJson(process.readAllStandardOutput()).object();
}
}
class RawWorkerSessionTests:public QObject {
    Q_OBJECT
private slots:
    void liveOwnerIsPreserved(){
        QTemporaryDir root;QVERIFY(root.isValid());QTemporaryDir session(root.filePath("session-XXXXXX"));QVERIFY(session.isValid());
        const auto request=identity();QString error;QVERIFY2(recordSessionOwner(root.path(),session.path(),request,error),qPrintable(error));QVERIFY(cached(session.path(),request.token));
        const auto result=cleanAbandonedSessions(root.path(),0);QCOMPARE(result.removed,0);QCOMPARE(result.kept,1);QVERIFY(result.errors.isEmpty());QVERIFY(QFile::exists(session.filePath(request.token+".frame")));
        QVERIFY(!recordSessionOwner(root.path(),session.path(),request,error)); // Existing marker is never overwritten.
    }
    void crashedOwnerIsRemovedAfterAgeGate(){
        QTemporaryDir root;QVERIFY(root.isValid());const auto report=crashedOwner(root.path());QVERIFY(!report.isEmpty());const auto directory=report["directory"].toString();
        QVERIFY(QFileInfo(directory).isDir());const auto young=cleanAbandonedSessions(root.path());QCOMPARE(young.removed,0);QCOMPARE(young.kept,1);
        const auto result=cleanAbandonedSessions(root.path(),0);QCOMPARE(result.removed,1);QCOMPARE(result.kept,0);QVERIFY2(result.errors.isEmpty(),qPrintable(result.errors.join(';')));QVERIFY(!QFile::exists(directory));
    }
    void unknownFilesOrBadMarkerArePreserved(){
        for(int mode=0;mode<3;++mode){
            QTemporaryDir root;QVERIFY(root.isValid());const auto report=crashedOwner(root.path());QVERIFY(!report.isEmpty());const auto directory=report["directory"].toString();
            if(mode==0)QVERIFY(write(QDir(directory).filePath("original.jpg"),"unknown-original"));
            else {
                QFile marker(QDir(directory).filePath("owner.json"));QVERIFY(marker.open(QIODevice::ReadWrite));const auto bytes=marker.readAll();auto owner=QJsonDocument::fromJson(bytes).object();
                owner[mode==1?"pid":"root"]=mode==1?QJsonValue(QString::number(GetCurrentProcessId())):QJsonValue(root.path()+"-other");
                QVERIFY(marker.resize(0));QCOMPARE(marker.write(QJsonDocument(owner).toJson(QJsonDocument::Compact)),QJsonDocument(owner).toJson(QJsonDocument::Compact).size());marker.close();
            }
            const auto result=cleanAbandonedSessions(root.path(),0);QCOMPARE(result.removed,0);QCOMPARE(result.kept,1);QVERIFY(QFile::exists(QDir(directory).filePath(report["token"].toString()+".frame")));
        }
    }
    void foreignDirectoryCannotBeMarked(){
        QTemporaryDir root,other;QTemporaryDir session(other.filePath("session-XXXXXX"));QVERIFY(root.isValid()&&other.isValid()&&session.isValid());QString error;
        QVERIFY(!recordSessionOwner(root.path(),session.path(),identity(),error));QVERIFY(!QFile::exists(session.filePath("owner.json")));
        const auto invalid=cleanAbandonedSessions(root.path(),-1);QVERIFY(!invalid.errors.isEmpty());
    }
    void missingWorkerProofPreservesPayload(){
        QTemporaryDir root;QVERIFY(root.isValid());const auto report=crashedOwner(root.path());QVERIFY(!report.isEmpty());const auto directory=report["directory"].toString();
        QVERIFY(QFile::remove(QDir(directory).filePath("worker.json")));const auto result=cleanAbandonedSessions(root.path(),0);QCOMPARE(result.removed,0);QCOMPARE(result.kept,1);
        QVERIFY(QFile::exists(QDir(directory).filePath(report["token"].toString()+".frame")));
    }
    void liveWorkerIdentityPreservesDeadOwnersCache(){
        QTemporaryDir root;QVERIFY(root.isValid());const auto report=crashedOwner(root.path());QVERIFY(!report.isEmpty());const auto directory=report["directory"].toString();
        QFile file(QDir(directory).filePath("worker.json"));QVERIFY(file.open(QIODevice::ReadWrite));auto worker=QJsonDocument::fromJson(file.readAll()).object();
        FILETIME created{},exited{},kernel{},user{};QVERIFY(GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user));
        worker["pid"]=QString::number(GetCurrentProcessId());worker["processCreated"]=QString::number((quint64(created.dwHighDateTime)<<32)|created.dwLowDateTime);worker.remove("sha256");
        worker["sha256"]=QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(worker).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());
        QVERIFY(file.resize(0));const auto payload=QJsonDocument(worker).toJson(QJsonDocument::Compact);QCOMPARE(file.write(payload),payload.size());file.close();
        const auto result=cleanAbandonedSessions(root.path(),0);QCOMPARE(result.removed,0);QCOMPARE(result.kept,1);QVERIFY(QFile::exists(QDir(directory).filePath(report["token"].toString()+".frame")));
    }
};
int main(int argc,char **argv){
    QCoreApplication app(argc,argv);
    if(app.arguments().size()==3&&app.arguments()[1]=="--crashed-owner"){
        const auto root=app.arguments()[2];QTemporaryDir session(QDir(root).filePath("session-XXXXXX"));if(!session.isValid())return 2;
        const auto request=identity();QString error;if(!recordSessionOwner(root,session.path(),request,error)||!recordSessionWorker(root,session.path(),GetCurrentProcessId(),error)||!cached(session.path(),request.token))return 2;
        session.setAutoRemove(false);const auto report=QJsonDocument(QJsonObject{{"directory",session.path()},{"token",request.token}}).toJson(QJsonDocument::Compact);
        DWORD written=0;if(!WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),report.constData(),DWORD(report.size()),&written,nullptr)||written!=DWORD(report.size()))return 2;
        TerminateProcess(GetCurrentProcess(),5);return 5;
    }
    RawWorkerSessionTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "raw_worker_session_tests.moc"
