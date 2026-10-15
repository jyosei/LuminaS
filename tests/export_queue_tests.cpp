#include "export/export_queue.h"
#include <QTest>
#include <QTemporaryDir>
#include <QCryptographicHash>
#include <QFile>
#include <QSaveFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QColorSpace>
using namespace Export;
class ExportQueueTests:public QObject {
 Q_OBJECT
private slots:
 void freezingRecoveryAndIndependentResults(){
    QTemporaryDir dir;QVERIFY(dir.isValid());QImage image(200,100,QImage::Format_RGB32);image.fill(Qt::cyan);image.setColorSpace(QColorSpace::SRgb);
    QList<Asset> assets;for(int i=0;i<3;++i){const auto path=dir.filePath(QString::number(i)+".jpg");QVERIFY(image.save(path,"JPEG"));QFile f(path);QVERIFY(f.open(QIODevice::ReadOnly));assets.append({path,QString::fromLatin1(QCryptographicHash::hash(f.readAll(),QCryptographicHash::Sha256).toHex()),{},quint64(i+1)});}
    Settings s;QString e;s.icc=builtinProfile("sRGB",e);s.besideSource=false;s.directory=dir.filePath("out");
    auto jobs=freeze(assets,s,e);QCOMPARE(jobs.size(),3);jobs[1].hash=QString(64,'0');const auto old=jobs[0].recipe;assets[0].recipe.exposure=2;QCOMPARE(jobs[0].recipe,old);
    const auto record=dir.filePath("queue.json");{Queue queue(record);QVERIFY2(queue.append(jobs,e),qPrintable(e));QVERIFY2(queue.start(e),qPrintable(e));QTRY_VERIFY_WITH_TIMEOUT(!queue.busy(),30000);QCOMPARE(queue.jobs()[0].status,Status::Succeeded);QCOMPARE(queue.jobs()[1].status,Status::Failed);QCOMPARE(queue.jobs()[2].status,Status::Succeeded);}
    Queue recovered(record);QVERIFY2(recovered.load(e),qPrintable(e));QVERIFY(!recovered.busy());QCOMPARE(recovered.jobs().size(),3);const auto bytes=QFileInfo(jobs[0].path).size();QVERIFY(recovered.retry(e));QVERIFY(recovered.start(e));QTRY_VERIFY_WITH_TIMEOUT(!recovered.busy(),30000);QCOMPARE(recovered.jobs()[0].status,Status::Succeeded);QCOMPARE(QFileInfo(jobs[0].path).size(),bytes);QCOMPARE(recovered.jobs()[1].status,Status::Failed);
    auto interrupted=jobs[0];interrupted.status=Status::Running;QSaveFile f(record);QVERIFY(f.open(QIODevice::WriteOnly));f.write(QJsonDocument(QJsonObject{{"version",1},{"jobs",QJsonArray{jobJson(interrupted)}}}).toJson());QVERIFY(f.commit());Queue restoreInterrupted(record);QVERIFY(restoreInterrupted.load(e));QCOMPARE(restoreInterrupted.jobs()[0].status,Status::Pending);QVERIFY(!restoreInterrupted.busy());QVERIFY(!restoreInterrupted.start(e));
    auto another=s;another.suffix="-new";auto fresh=freeze({assets[0]},another,e);QCOMPARE(fresh.size(),1);QVERIFY(restoreInterrupted.append(fresh,e));QVERIFY(restoreInterrupted.start(e));QTRY_VERIFY_WITH_TIMEOUT(!restoreInterrupted.busy(),30000);QCOMPARE(restoreInterrupted.jobs()[0].status,Status::Pending);QCOMPARE(restoreInterrupted.jobs()[1].status,Status::Succeeded);

    const QByteArray unknown("{\"version\":999,\"jobs\":[]}");QFile unknownFile(record);QVERIFY(unknownFile.open(QIODevice::WriteOnly|QIODevice::Truncate));unknownFile.write(unknown);unknownFile.close();Queue futureRecord(record);QVERIFY(!futureRecord.load(e));QVERIFY(!futureRecord.append(jobs,e));QVERIFY(!futureRecord.start(e));QVERIFY(unknownFile.open(QIODevice::ReadOnly));QCOMPARE(unknownFile.readAll(),unknown);
 }
 void cancellationAndRecordFailure(){
    QTemporaryDir dir;auto source=dir.filePath("photo.jpg");QImage image(100,60,QImage::Format_RGB32);image.fill(Qt::red);image.setColorSpace(QColorSpace::SRgb);QVERIFY(image.save(source,"JPEG"));QFile f(source);QVERIFY(f.open(QIODevice::ReadOnly));auto hash=QString::fromLatin1(QCryptographicHash::hash(f.readAll(),QCryptographicHash::Sha256).toHex());f.close();Settings s;QString e;s.icc=builtinProfile("sRGB",e);s.besideSource=false;s.directory=dir.filePath("out");auto jobs=freeze({{source,hash,{},1}},s,e);QCOMPARE(jobs.size(),1);
    Queue queue(dir.filePath("q.json"));QVERIFY(queue.append(jobs,e));QVERIFY(queue.start(e));queue.cancel();QTRY_VERIFY_WITH_TIMEOUT(!queue.busy(),30000);QVERIFY(queue.jobs()[0].status==Status::Cancelled||queue.jobs()[0].status==Status::Succeeded);if(queue.jobs()[0].status==Status::Cancelled)QVERIFY(!QFileInfo::exists(jobs[0].path));
    auto blocker=dir.filePath("blocker");QFile b(blocker);QVERIFY(b.open(QIODevice::WriteOnly));b.write("x");b.close();Queue failure(blocker+"/q.json");QVERIFY(!failure.append(jobs,e));QVERIFY(!e.isEmpty());QVERIFY(failure.jobs().isEmpty());
 }
 void collisionSizeAndSettingsValidation(){
    QTemporaryDir dir;QImage image(20,30,QImage::Format_RGB32);image.fill(Qt::green);const auto a=dir.filePath("same.jpg");QVERIFY(image.save(a,"JPEG"));QFile f(a);QVERIFY(f.open(QIODevice::ReadOnly));auto hash=QString::fromLatin1(QCryptographicHash::hash(f.readAll(),QCryptographicHash::Sha256).toHex());f.close();Settings s;QString e;s.icc=builtinProfile("sRGB",e);s.subfolder.clear();auto jobs=freeze({{a,hash,{},0}},s,e);QCOMPARE(jobs.size(),1);QVERIFY(jobs[0].path!=a);s.collision=Collision::Skip;jobs=freeze({{a,hash,{},0}},s,e);QCOMPARE(jobs[0].status,Status::Skipped);
    s.prefix="../unsafe";s.sequence=true;QVERIFY(freeze({{a,hash,{},0}},s,e).isEmpty());s.sequence=false;auto json=settingsJson(s);json["bits"]=12;Settings parsed;QVERIFY(!settingsFromJson(json,parsed,e));json=settingsJson(s);json["version"]=99;QVERIFY(!settingsFromJson(json,parsed,e));s.sizeMode=SizeMode::Fit;s.width=400;s.height=100;QCOMPARE(outputSize({600,400},s,e),QSize(150,100));s.sizeMode=SizeMode::Percent;s.percent=200;s.dontEnlarge=false;QCOMPARE(outputSize({600,400},s,e),QSize(1200,800));QVERIFY(!outputSize({9001,5000},s,e).isValid());
 }
};
QTEST_MAIN(ExportQueueTests)
#include "export_queue_tests.moc"
