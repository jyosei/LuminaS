#include "io/atomic_file.h"
#include <QTest>
#include <QDir>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#endif
using namespace Persistence;

namespace {
QByteArray readAll(const QString &path){
    QFile f(path);
    if(!f.open(QIODevice::ReadOnly))return QByteArray();
    return f.readAll();
}
int fileCount(const QString &dirPath){
    return QDir(dirPath).entryList(QDir::Files|QDir::NoDotAndDotDot).size();
}
#ifdef Q_OS_WIN
struct NativeHandle {
    HANDLE value=INVALID_HANDLE_VALUE;
    ~NativeHandle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
};
#endif
}

class AtomicFileTests:public QObject {
 Q_OBJECT
private slots:
 void repeatedSmallJsonReplaceAlwaysReadsBackEqual(){
    QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=dir.filePath("queue.json");
    int totalAttempts=0;
    for(int i=0;i<100;++i){
        const auto payload=QJsonDocument(QJsonObject{{"version",1},{"jobs",QJsonArray{}},{"i",i}}).toJson(QJsonDocument::Compact);
        QString e;AtomicWriteStats s;
        QVERIFY2(writeAtomicBytes(path,payload,e,{},&s),qPrintable(QStringLiteral("i=%1 err=%2").arg(i).arg(e)));
        QVERIFY2(e.isEmpty(),qPrintable(e));
        QVERIFY(s.attempts>=1&&s.attempts<=8);
        totalAttempts+=s.attempts;
        const auto onDisk=readAll(path);
        QCOMPARE(onDisk,payload);
        QCOMPARE(QJsonDocument::fromJson(onDisk).object().value("i").toInt(-1),i);
    }
    qInfo("atomic replacements=100 attempts=%d",totalAttempts);
 }
 void failingBodyKeepsOldContentAndFileCount(){
    QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=dir.filePath("queue.json");
    const QByteArray good("{\"version\":1,\"keep\":true}");
    QString e;QVERIFY2(writeAtomicBytes(path,good,e),qPrintable(e));
    const int before=fileCount(dir.path());
    AtomicWriteStats s;
    const bool ok=writeAtomicFile(path,[&](QIODevice &d,QString &){d.write("{\"partial\"");return false;},e,{},&s);
    QVERIFY(!ok);QVERIFY(!e.isEmpty());QCOMPARE(s.attempts,1);
    QCOMPARE(readAll(path),good);
    QCOMPARE(fileCount(dir.path()),before);
 }
 void bodyErrorIsPreservedWhenProvided(){
    QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=dir.filePath("queue.json");
    const QByteArray good("old");QString e;QVERIFY2(writeAtomicBytes(path,good,e),qPrintable(e));
    const QString marker=QStringLiteral("自定义写出失败标记");
    QVERIFY(!writeAtomicFile(path,[&](QIODevice &,QString &bodyError){bodyError=marker;return false;},e));
    QVERIFY(e.contains(marker));
    QCOMPARE(readAll(path),good);
    QVERIFY(!writeAtomicFile(path,[&](QIODevice &,QString &){return false;},e));
    QVERIFY(!e.isEmpty());
    QCOMPARE(readAll(path),good);
 }
 void preCanceledTokenLeavesOldContent(){
    QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=dir.filePath("queue.json");
    const QByteArray good("{\"old\":1}");QString e;QVERIFY2(writeAtomicBytes(path,good,e),qPrintable(e));
    CancellationSource source;source.cancel();
    AtomicWriteStats s;
    QVERIFY(!writeAtomicFile(path,[&](QIODevice &,QString &){return true;},e,source.token(),&s));
    QVERIFY(!e.isEmpty());QCOMPARE(s.attempts,0);
    QCOMPARE(readAll(path),good);
 }
 void invalidParentDirectoryFails(){
    QTemporaryDir dir;QVERIFY(dir.isValid());
    QFile blocker(dir.filePath("blocker"));QVERIFY(blocker.open(QIODevice::WriteOnly));blocker.write("x");blocker.close();
    const auto path=blocker.fileName()+"/queue.json";
    QString e;AtomicWriteStats s;
    QVERIFY(!writeAtomicBytes(path,QByteArray("{}"),e,{},&s));
    QVERIFY(!e.isEmpty());QCOMPARE(s.attempts,1);
    QVERIFY(e.contains(blocker.fileName()));
 }
 void cancellationAfterBodyAndMissingBodyPreserveOldContent(){
    QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=dir.filePath("record.json");
    QString error;const QByteArray old("original");
    QVERIFY2(writeAtomicBytes(path,old,error),qPrintable(error));
    CancellationSource cancel;AtomicWriteStats stats;
    QVERIFY(!writeAtomicFile(path,[&](QIODevice &file,QString &){
        file.write("partial");cancel.cancel();return true;
    },error,cancel.token(),&stats));
    QCOMPARE(stats.attempts,1);QCOMPARE(readAll(path),old);
    QCOMPARE(fileCount(dir.path()),1);
    QVERIFY(!writeAtomicFile(path,{},error,{},&stats));
    QCOMPARE(stats.attempts,0);QCOMPARE(readAll(path),old);
 }
#ifdef Q_OS_WIN
 void windowsLockedTargetRetriesThenSucceedsAfterRelease(){
    QTemporaryDir dir;QVERIFY(dir.isValid());const QString path=dir.filePath("queue.json");
    const QByteArray good("{\"old\":1}");QString e;QVERIFY2(writeAtomicBytes(path,good,e),qPrintable(e));
    const QByteArray payload("{\"version\":1,\"i\":7}");
    // Holding the target without FILE_SHARE_DELETE makes the atomic replace fail for real.
    NativeHandle handle{CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()),GENERIC_READ|GENERIC_WRITE,
        FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr)};
    QVERIFY2(handle.value!=INVALID_HANDLE_VALUE,qPrintable(QStringLiteral("CreateFileW err=%1").arg(GetLastError())));
    AtomicWriteStats locked;
    const bool lockedOk=writeAtomicBytes(path,payload,e,{},&locked);
    QVERIFY2(!lockedOk,qPrintable(QStringLiteral("应因目标被占用而提交失败")));
    QVERIFY(!e.isEmpty());
    QVERIFY2(locked.attempts>=1&&locked.attempts<=8,qPrintable(QStringLiteral("attempts=%1").arg(locked.attempts)));
    QVERIFY(locked.elapsedMs>=0);
    QCOMPARE(readAll(path),good);
    const bool closed=CloseHandle(handle.value);
    handle.value=INVALID_HANDLE_VALUE;
    QVERIFY(closed);
    AtomicWriteStats released;
    QVERIFY2(writeAtomicBytes(path,payload,e,{},&released),qPrintable(e));
    QVERIFY(e.isEmpty());
    QVERIFY(released.attempts>=1&&released.attempts<=8);
    QCOMPARE(readAll(path),payload);
 }
#endif
};
QTEST_APPLESS_MAIN(AtomicFileTests)
#include "atomic_file_tests.moc"
