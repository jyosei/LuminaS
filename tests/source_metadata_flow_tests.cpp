#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QUuid>
#include <QtEndian>
#include <libraw/libraw.h>
#include "background_source.h"
#include "export/export_metadata.h"
#include "raw/worker_payload.h"
namespace {
QByteArray read(const QString &path){QFile file(path);return file.open(QIODevice::ReadOnly)?file.readAll():QByteArray{};}
QString hash(QByteArrayView bytes){return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());}
bool write(const QString &path,const QByteArray &bytes){QFile file(path);return file.open(QIODevice::WriteOnly|QIODevice::Truncate)&&file.write(bytes)==bytes.size();}
SourceMetadata metadata(){SourceMetadata m;m.make="Camera";m.artist=QStringLiteral("测试作者");m.copyright=QStringLiteral("版权所有");m.lens="Lens";m.exposure=.008;m.aperture=2.8;m.focalLength=35;m.iso=400;m.hasGps=true;m.latitude=31;m.longitude=121;m.altitude=-12.5;return m;}
}
class SourceMetadataFlowTests:public QObject {
    Q_OBJECT
private slots:
    void rawMetadataRequiresBoundSnapshotAndFiltersGps(){
        QTemporaryDir root;QVERIFY(root.isValid());const auto path=root.filePath("source.CR3");QVERIFY(write(path,"not-a-raw-image"));QString error;
        QVERIFY(Export::readMetadata(path,Export::Metadata::Camera,false,error).make.isEmpty());QVERIFY(!error.isEmpty());
        Export::SourceMetadataSnapshot snapshot{path,hash(read(path)),metadata()};
        const auto common=Export::readMetadata(path,Export::Metadata::Camera,false,error,&snapshot);QVERIFY2(error.isEmpty(),qPrintable(error));QCOMPARE(common.make,snapshot.values.make);QVERIFY(!common.hasGps);QCOMPARE(common.latitude,0.);QCOMPARE(common.longitude,0.);QCOMPARE(common.altitude,0.);
        const auto copy=Export::readMetadata(path,Export::Metadata::Copyright,false,error,&snapshot);QVERIFY(error.isEmpty());QCOMPARE(copy.artist,snapshot.values.artist);QCOMPARE(copy.copyright,snapshot.values.copyright);QVERIFY(copy.make.isEmpty());QCOMPARE(copy.iso,0);
        const auto gps=Export::readMetadata(path,Export::Metadata::Camera,true,error,&snapshot);QVERIFY(error.isEmpty());QCOMPARE(gps,snapshot.values);
        snapshot.sourcePath=root.filePath("other.CR3");Export::readMetadata(path,Export::Metadata::Camera,true,error,&snapshot);QVERIFY(!error.isEmpty());snapshot.sourcePath=path;snapshot.sourceHash=QString(64,'z');Export::readMetadata(path,Export::Metadata::Camera,true,error,&snapshot);QVERIFY(!error.isEmpty());
        // Excluding all metadata does not reopen or parse RAW at all.
        QCOMPARE(Export::readMetadata(path,Export::Metadata::None,false,error),SourceMetadata{});QVERIFY(error.isEmpty());QCOMPARE(read(path),QByteArray("not-a-raw-image"));
    }
    void payloadPreservesAndStrictlyValidatesMetadata(){
        QTemporaryDir root;QVERIFY(root.isValid());RawWorker::Request request;request.id=QUuid::createUuid().toString(QUuid::WithoutBraces);request.token=QUuid::createUuid().toString(QUuid::WithoutBraces);request.sourcePath=root.filePath("source.CR3");QVERIFY(write(request.sourcePath,"fixture"));request.sourceHash=hash(read(request.sourcePath));
        SourceFrame frame;frame.path=request.sourcePath;frame.sha256=request.sourceHash;frame.raw=true;frame.originalSize={16,12};frame.linear=QImage(16,12,QImage::Format_RGBA32FPx4);frame.linear.fill(Qt::gray);frame.cameraWB={1,1,1,1};frame.cameraToWork={1,0,0,0,1,0,0,0,1};frame.hasDeliveryMetadata=true;frame.deliveryMetadata=metadata();
        QString error;QJsonObject done;QVERIFY2(RawWorker::writePayload(root.path(),request,frame,done,error),qPrintable(error));const auto result=RawWorker::readPayload(root.path(),request,done);QVERIFY2(result.source,qPrintable(result.error));QVERIFY(result.source->hasDeliveryMetadata);QCOMPARE(result.source->deliveryMetadata,frame.deliveryMetadata);QVERIFY(!result.source->facts.contains("deliveryMetadata"));
        // Forge all outer hashes correctly; the inner typed whitelist must still
        // reject a string ISO rather than silently defaulting it to zero.
        const auto path=root.filePath(request.token+".frame");const auto original=read(path);const auto oldLength=qFromLittleEndian<quint32>(original.constData()+12);auto header=original.left(32);auto object=done["frame"].toObject();auto values=object["deliveryMetadata"].toObject();values["iso"]="400";object["deliveryMetadata"]=values;const auto json=QJsonDocument(object).toJson(QJsonDocument::Compact);qToLittleEndian<quint32>(quint32(json.size()),header.data()+12);const auto forged=header+json+original.mid(32+oldLength);QVERIFY(write(path,forged));done["frame"]=object;done["fileBytes"]=QString::number(forged.size());done["fileSha256"]=hash(forged);
        const auto rejected=RawWorker::readPayload(root.path(),request,done);QVERIFY(!rejected.source);QVERIFY(!rejected.error.isEmpty());
        auto hello=RawWorker::hello();hello.remove("sourceMetadata");QVERIFY(!RawWorker::validHello(hello,error));
    }
    void prepareRejectsGradingAndDoesNotUpscaleSmallImages(){
        QTemporaryDir root;QVERIFY(root.isValid());const auto path=root.filePath("small.jpg");QImage input(32,24,QImage::Format_RGB32);input.fill(Qt::gray);QVERIFY(input.save(path,"JPEG"));Recipe geometry;geometry.exposure=.5;QVERIFY(!prepareSource(path,geometry).source);geometry={};geometry.crop={.25,.25,.5,.5};const auto prepared=prepareSource(path,geometry);QVERIFY2(prepared.source,qPrintable(prepared.error));QCOMPARE(prepared.source->linear.size(),QSize(16,12));QCOMPARE(prepared.source->originalSize,QSize(32,24));
    }
    void actualR7SnapshotMatchesIndependentHeader(){
        const auto path=qEnvironmentVariable("LUMINA_WORKER_SAMPLE");if(QFileInfo(path).suffix().compare("cr3",Qt::CaseInsensitive)!=0)QSKIP("Set a private R7 CR3 for delivery metadata/header verification.");
        const auto before=hash(read(path));const auto loaded=readTestSource(path);QVERIFY2(loaded.source,qPrintable(loaded.error));QVERIFY(loaded.source->hasDeliveryMetadata);QCOMPARE(loaded.source->sha256,before);
        LibRaw raw;
#ifdef Q_OS_WIN
        const auto status=raw.open_file(reinterpret_cast<const wchar_t *>(path.utf16()));
#else
        const auto status=raw.open_file(QFile::encodeName(path).constData());
#endif
        QCOMPARE(status,LIBRAW_SUCCESS);const auto &m=loaded.source->deliveryMetadata;QCOMPARE(m.make,QString::fromUtf8(raw.imgdata.idata.make));QCOMPARE(m.model,QString::fromUtf8(raw.imgdata.idata.model));QCOMPARE(m.lens,QString::fromUtf8(raw.imgdata.lens.Lens));QCOMPARE(m.iso,int(raw.imgdata.other.iso_speed));QCOMPARE(m.exposure,double(raw.imgdata.other.shutter));QCOMPARE(m.aperture,double(raw.imgdata.other.aperture));QCOMPARE(m.focalLength,double(raw.imgdata.other.focal_len));
        Export::SourceMetadataSnapshot snapshot{loaded.source->path,loaded.source->sha256,m};QString error;const auto filtered=Export::readMetadata(path,Export::Metadata::Camera,false,error,&snapshot);QVERIFY2(error.isEmpty(),qPrintable(error));QVERIFY(!filtered.hasGps);QCOMPARE(filtered.latitude,0.);QCOMPARE(filtered.longitude,0.);QCOMPARE(filtered.altitude,0.);QCOMPARE(hash(read(path)),before);qInfo()<<"R7 isolated delivery metadata/header verified; GPS-off coordinates cleared";
    }
};
QTEST_GUILESS_MAIN(SourceMetadataFlowTests)
#include "source_metadata_flow_tests.moc"
