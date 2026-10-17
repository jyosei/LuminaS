#include "../src/export/export_types.h"
#include "../src/export/export_engine.h"
#include "../src/export/export_queue.h"
#include "../src/core/photo_document.h"
#include "../src/render/local_renderer.h"
#include "background_source.h"
#include "background_export.h"
#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QImage>
#include <QColorSpace>
#include <QSignalSpy>
#include <QCryptographicHash>
#include <tiffio.h>
#include <algorithm>
#include <cstring>
#include <memory>

using namespace Export;
namespace {
QByteArray digest(const QString &path){QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};QCryptographicHash h(QCryptographicHash::Sha256);if(!h.addData(&f))return {};return h.result().toHex();}
Settings settings(const QString &directory){Settings s;s.format=Format::Tiff;s.bits=16;s.profileName="ProPhoto RGB";QString e;s.icc=builtinProfile(s.profileName,e);s.besideSource=false;s.directory=directory;return s;}
QString sample(const QTemporaryDir &dir,QSize size={128,96}){
    QImage image(size,QImage::Format_RGB32);if(image.isNull())return {};
    for(int y=0;y<size.height();++y){auto *p=reinterpret_cast<QRgb *>(image.scanLine(y));for(int x=0;x<size.width();++x)p[x]=qRgb(x*255/size.width(),y*255/size.height(),90);}
    image.setColorSpace(QColorSpace::SRgb);const auto path=dir.filePath("source.jpg");return image.save(path,"JPEG",98)?path:QString();
}
LocalEdit::PhotoDocument document(const SourceFrame &frame){
    LocalEdit::PhotoDocument d;d.asset.id="export-test";d.asset.sourcePath=frame.path;d.asset.sourceHash=frame.sha256;d.asset.normalizedSize=frame.originalSize;d.asset.raw=frame.raw;d.revision=7;
    d.global.exposure=.25;d.global.contrast=.1;d.global.crop={.1,.1,.8,.8};d.global.angle=3;d.global.flipHorizontal=true;d.global.warmth=.25;d.global.tint=-.1;
    const double w=frame.originalSize.width(),h=frame.originalSize.height();
    LocalEdit::AdjustmentLayer brush;brush.id="brush";brush.name="Brush";brush.opacity=.8;brush.parameters.exposure=.6;brush.mask.feather=8;
    LocalEdit::MaskComponent stroke;stroke.id="stroke";LocalEdit::BrushGeometry b;b.points={{w*.2,h*.3},{w*.7,h*.6}};b.radius=std::min(w,h)*.15;b.hardness=.6;stroke.geometry=b;brush.mask.components={stroke};d.layers.append(brush);
    LocalEdit::AdjustmentLayer gradient;gradient.id="gradient";gradient.name="Gradient";gradient.opacity=.5;gradient.parameters.exposure=-.3;gradient.mask.invert=true;
    LocalEdit::MaskComponent line;line.id="line";line.geometry=LocalEdit::LinearGeometry{{w*.1,h*.1},{w*.9,h*.8}};
    LocalEdit::MaskComponent hole;hole.id="hole";hole.operation=LocalEdit::MaskOperation::Subtract;hole.strength=.7;hole.geometry=LocalEdit::RadialGeometry{{w*.5,h*.5},w*.2,h*.25,15};gradient.mask.components={line,hole};d.layers.append(gradient);
    auto disabled=brush;disabled.id="disabled";disabled.mask.components[0].id="disabled-stroke";disabled.enabled=false;disabled.parameters.exposure=-3;d.layers.append(disabled);return d;
}
bool verifyTiff(const QString &path,const Pixels &expected,QString &error){
    error.clear();if(expected.bits!=16||expected.channels!=3||expected.data.size()!=qint64(expected.size.width())*expected.size.height()*6){error="Invalid expected pixels";return false;}
#ifdef Q_OS_WIN
    auto native=QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());if(!native.startsWith("\\\\?\\"))native=native.startsWith("\\\\")?QStringLiteral("\\\\?\\UNC\\")+native.mid(2):QStringLiteral("\\\\?\\")+native;
    TIFF *file=TIFFOpenW(reinterpret_cast<const wchar_t *>(native.utf16()),"r");
#else
    TIFF *file=TIFFOpen(QFile::encodeName(path).constData(),"r");
#endif
    if(!file){error="TIFF open failed";return false;}std::unique_ptr<TIFF,decltype(&TIFFClose)> owner(file,TIFFClose);
    uint32_t w=0,h=0,n=0;uint16_t bits=0,channels=0,photo=0,planar=0,orientation=0,format=0;void *icc=nullptr;
    TIFFGetField(file,TIFFTAG_IMAGEWIDTH,&w);TIFFGetField(file,TIFFTAG_IMAGELENGTH,&h);TIFFGetFieldDefaulted(file,TIFFTAG_BITSPERSAMPLE,&bits);TIFFGetFieldDefaulted(file,TIFFTAG_SAMPLESPERPIXEL,&channels);TIFFGetField(file,TIFFTAG_PHOTOMETRIC,&photo);TIFFGetFieldDefaulted(file,TIFFTAG_PLANARCONFIG,&planar);TIFFGetFieldDefaulted(file,TIFFTAG_ORIENTATION,&orientation);TIFFGetFieldDefaulted(file,TIFFTAG_SAMPLEFORMAT,&format);
    if(QSize(int(w),int(h))!=expected.size||bits!=16||channels!=3||photo!=PHOTOMETRIC_RGB||planar!=PLANARCONFIG_CONTIG||orientation!=ORIENTATION_TOPLEFT||format!=SAMPLEFORMAT_UINT){error="TIFF tag mismatch";return false;}
    if(!TIFFGetField(file,TIFFTAG_ICCPROFILE,&n,&icc)||n>4*1024*1024||QByteArray(static_cast<const char *>(icc),n)!=expected.icc){error="TIFF ICC mismatch";return false;}
    const qint64 rowBytes=qint64(w)*6;if(TIFFScanlineSize(file)!=rowBytes){error="TIFF scanline size mismatch";return false;}QByteArray row(rowBytes,Qt::Uninitialized);
    for(uint32_t y=0;y<h;++y)if(TIFFReadScanline(file,row.data(),y)<0||memcmp(row.constData(),expected.data.constData()+qint64(y)*rowBytes,size_t(rowBytes))){error=QStringLiteral("TIFF pixels differ at row %1").arg(y);return false;}
    return true;
}
// Retain only quantized reference bytes before executing another full-source job.
Pixels reference(const QString &path,LocalEdit::PhotoDocument &d,const Settings &s,QString &error){
    auto frame=readTestSource(path,.25,-.1);if(!frame.source){error=frame.error;return {};}
    d=document(*frame.source);auto linear=LocalEdit::renderDocumentFullLinear(*frame.source,d,error);frame.source.reset();if(linear.isNull())return {};
    auto pixels=convert(linear,s,error);return pixels;
}
QList<Job> jobs(const QString &path,const LocalEdit::PhotoDocument &d,const Settings &s,QString &error){return freeze({Asset{path,d.asset.sourceHash,d.global,d.revision,false,false,std::make_shared<const LocalEdit::PhotoDocument>(d)}},s,error);}
}
class ExportLocalRenderTests:public QObject {
    Q_OBJECT
private slots:
    void fullSourceAndFrozenQueue(){
        QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=sample(dir);QVERIFY(!path.isEmpty());const auto before=digest(path);QString error;auto s=settings(dir.filePath("single"));LocalEdit::PhotoDocument d;
        const auto expected=reference(path,d,s,error);QVERIFY2(!expected.data.isEmpty(),qPrintable(error));auto frozen=jobs(path,d,s,error);QVERIFY2(frozen.size()==1,qPrintable(error));
        auto result=executeTestJob(frozen[0]);QVERIFY2(result.ok,qPrintable(result.error));QVERIFY2(verifyTiff(result.path,expected,error),qPrintable(error));
        // Prove this fixture is sensitive to dropping all local edits.
        auto legacy=frozen[0];legacy.document.reset();legacy.path=dir.filePath("legacy.tif");auto legacyResult=executeTestJob(legacy);QVERIFY2(legacyResult.ok,qPrintable(legacyResult.error));QVERIFY(!verifyTiff(legacyResult.path,expected,error));
        s.directory=dir.filePath("recovered");auto pending=jobs(path,d,s,error);QVERIFY2(pending.size()==1,qPrintable(error));const auto record=dir.filePath("queue.json");
        {Queue queue(record);QVERIFY2(queue.append(pending,error),qPrintable(error));d.layers[0].parameters.exposure=-2;d.global.exposure=-1;d.revision=99;}
        Queue restored(record);QVERIFY2(restored.load(error),qPrintable(error));QCOMPARE(restored.jobs().size(),1);QCOMPARE(restored.jobs()[0].document->revision,quint64(7));
        // Recovered pending work does not run until the user explicitly retries.
        QVERIFY(!restored.start(error));QVERIFY(!error.isEmpty());QVERIFY(!restored.busy());QVERIFY(!QFileInfo::exists(restored.jobs()[0].path));
        QVERIFY2(restored.retry(error),qPrintable(error));QVERIFY2(restored.start(error),qPrintable(error));QTRY_VERIFY_WITH_TIMEOUT(!restored.busy(),30000);QCOMPARE(restored.jobs()[0].status,Status::Succeeded);QVERIFY2(verifyTiff(restored.jobs()[0].path,expected,error),qPrintable(error));QCOMPARE(digest(path),before);
    }
    void batchMatchesSingle(){
        QTemporaryDir dir;const auto path=sample(dir);const auto second=dir.filePath("second.jpg");QVERIFY(QFile::copy(path,second));QString error;auto s=settings(dir.filePath("batch"));LocalEdit::PhotoDocument d;const auto expected=reference(path,d,s,error);QVERIFY2(!expected.data.isEmpty(),qPrintable(error));
        auto one=jobs(path,d,s,error);auto other=d;other.asset.sourcePath=second;auto two=jobs(second,other,s,error);QCOMPARE(one.size(),1);QCOMPARE(two.size(),1);one.append(two);Queue queue(dir.filePath("batch.json"));QVERIFY(queue.append(one,error));QVERIFY(queue.start(error));QTRY_VERIFY_WITH_TIMEOUT(!queue.busy(),30000);
        for(const auto &job:queue.jobs()){QCOMPARE(job.status,Status::Succeeded);QVERIFY2(verifyTiff(job.path,expected,error),qPrintable(error));}
    }
    void privateSamplesAnd45Mp(){
        auto paths=qEnvironmentVariable("LUMINA_S3_EXPORT_SAMPLES").split('|',Qt::SkipEmptyParts);const bool large=qEnvironmentVariable("LUMINA_S3_EXPORT_45MP")=="1";if(paths.isEmpty()&&!large)QSKIP("Explicit private samples / synthetic 45MP only");
        QTemporaryDir dir;QVERIFY(dir.isValid());if(large){auto path=sample(dir,{9000,5000});QVERIFY(!path.isEmpty());paths.append(path);}int i=0;
        for(const auto &path:paths){const auto before=digest(path);QVERIFY(!before.isEmpty());auto s=settings(dir.filePath(QString::number(i++)));LocalEdit::PhotoDocument d;QString error;auto expected=reference(path,d,s,error);QVERIFY2(!expected.data.isEmpty(),qPrintable(error));auto frozen=jobs(path,d,s,error);QVERIFY2(frozen.size()==1,qPrintable(error));auto result=executeTestJob(frozen[0]);QVERIFY2(result.ok,qPrintable(result.error));QVERIFY2(verifyTiff(result.path,expected,error),qPrintable(error));QCOMPARE(digest(path),before);qInfo()<<"Full-source local TIFF"<<d.asset.normalizedSize<<"output"<<expected.size<<"RAW"<<d.asset.raw<<"pixels SHA256"<<QCryptographicHash::hash(expected.data,QCryptographicHash::Sha256).toHex();}
    }
};
QTEST_MAIN(ExportLocalRenderTests)
#include "export_local_render_tests.moc"
