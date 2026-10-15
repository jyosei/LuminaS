#include "export/export_engine.h"
#include "export/export_metadata.h"
#include "export/export_writer.h"
#include "color/color_pipeline.h"
#include "io/source_reader.h"
#include "app/canvas.h"
#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <QImageReader>
#include <QCryptographicHash>
#include <QColorSpace>
#include <QDir>
#include <QBuffer>
#include <tiffio.h>

using namespace Export;
namespace {
QByteArray digest(const QString &p){QFile f(p);if(!f.open(QIODevice::ReadOnly))return {};QCryptographicHash h(QCryptographicHash::Sha256);h.addData(&f);return h.result().toHex();}
QImage gradient(){QImage img(321,201,QImage::Format_RGBA32FPx4);for(int y=0;y<img.height();++y){auto *p=reinterpret_cast<float *>(img.scanLine(y));for(int x=0;x<img.width();++x){p[x*4]=float(x)/320;p[x*4+1]=float(y)/200;p[x*4+2]=.15f;p[x*4+3]=1;}}return img;}
QByteArray cmyk(){const auto root=qEnvironmentVariable("LCMS_SOURCE_DIR");for(const auto &n:{"test5.icc","test4.icc","test3.icc","test2.icc","test1.icc"}){QFile f(root+"/testbed/"+n);if(f.open(QIODevice::ReadOnly)){auto b=f.readAll();QString e;if(validProfile(b,Mode::Cmyk,e))return b;}}return {};}
QString sample(const QTemporaryDir &dir){QImage i(321,201,QImage::Format_RGB32);for(int y=0;y<i.height();++y)for(int x=0;x<i.width();++x)i.setPixelColor(x,y,QColor(x*255/320,y*255/200,90));i.setColorSpace(QColorSpace::SRgb);const auto p=dir.filePath(QStringLiteral("中文照片.jpg"));if(!i.save(p,"JPEG",98))return {};return p;}
}
class ExportEngineTests:public QObject {
    Q_OBJECT
private slots:
    void profileMathAndNoSrgbBottleneck(){
        auto linear=gradient();Settings s;s.format=Format::Tiff;s.bits=16;QString error;
        for(const auto &name:{"sRGB","Display P3","Adobe RGB","ProPhoto RGB"}){
            s.profileName=name;s.icc=builtinProfile(name,error);QVERIFY2(validProfile(s.icc,Mode::Rgb,error),qPrintable(error));
            const auto p=convert(linear,s,error);QVERIFY2(!p.data.isEmpty(),qPrintable(error));QCOMPARE(p.channels,3);QCOMPARE(p.bits,16);
            auto in=workProfile(),out=cmsOpenProfileFromMem(s.icc.constData(),s.icc.size());auto t=cmsCreateTransform(in,TYPE_RGBA_FLT,out,TYPE_RGB_16,s.intent,cmsFLAGS_NOOPTIMIZE|cmsFLAGS_NOCACHE|cmsFLAGS_BLACKPOINTCOMPENSATION);
            QByteArray reference(p.data.size(),0);for(int y=0;y<linear.height();++y)cmsDoTransform(t,linear.constScanLine(y),reference.data()+qint64(y)*linear.width()*6,linear.width());
            QCOMPARE(p.data,reference);cmsDeleteTransform(t);cmsCloseProfile(in);cmsCloseProfile(out);
        }
        s.mode=Mode::Cmyk;s.icc=cmyk();QVERIFY2(!s.icc.isEmpty(),"LCMS CMYK test ICC required");auto p=convert(linear,s,error);QVERIFY2(!p.data.isEmpty(),qPrintable(error));QCOMPARE(p.channels,4);
        auto in=workProfile(),out=cmsOpenProfileFromMem(s.icc.constData(),s.icc.size());auto t=cmsCreateTransform(in,TYPE_RGBA_FLT,out,TYPE_CMYK_16,s.intent,cmsFLAGS_NOOPTIMIZE|cmsFLAGS_NOCACHE|cmsFLAGS_BLACKPOINTCOMPENSATION);QVERIFY(t);
        QByteArray reference(p.data.size(),0);for(int y=0;y<linear.height();++y)cmsDoTransform(t,linear.constScanLine(y),reference.data()+qint64(y)*linear.width()*8,linear.width());QCOMPARE(p.data,reference);cmsDeleteTransform(t);cmsCloseProfile(in);cmsCloseProfile(out);
        QVERIFY(!preview(linear,s,error).isNull());s.icc="bad";QVERIFY(convert(linear,s,error).data.isEmpty());
    }
    void formatsFullSourceAndTransactions(){
        QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=sample(dir);QVERIFY(!path.isEmpty());const auto before=digest(path);int i=0;
        for(auto format:{Format::Jpeg,Format::Png,Format::Tiff})for(int bits:{8,16}){
            if(format==Format::Jpeg&&bits==16)continue;Settings s;s.format=format;s.bits=bits;s.besideSource=false;s.directory=dir.filePath("output");
            QString e;s.icc=builtinProfile("sRGB",e);auto jobs=freeze({{path,QString::fromLatin1(before),{},7}},s,e);QCOMPARE(jobs.size(),1);
            auto r=execute(jobs[0]);QVERIFY2(r.ok,qPrintable(r.error));QCOMPARE(r.size,QSize(321,201));auto check=readOutput(r.path,e);QVERIFY2(!check.isNull(),qPrintable(e));
            QCOMPARE(digest(path),before);const auto outputHash=digest(r.path);auto again=execute(jobs[0]);QVERIFY(again.skipped);QCOMPARE(digest(r.path),outputHash);
            if(format!=Format::Tiff){QImageReader reader(r.path);auto img=reader.read();QCOMPARE(img.depth(),bits==16?64:32);QVERIFY(img.colorSpace().isValid());}
            else{
#ifdef Q_OS_WIN
                auto *t=TIFFOpenW(reinterpret_cast<const wchar_t *>(r.path.utf16()),"r");
#else
                auto *t=TIFFOpen(QFile::encodeName(r.path).constData(),"r");
#endif
                QVERIFY(t);uint16_t actualBits=0,samples=0;uint32_t count=0;void *icc=nullptr;QVERIFY(TIFFGetField(t,TIFFTAG_BITSPERSAMPLE,&actualBits));QCOMPARE(int(actualBits),bits);QVERIFY(TIFFGetField(t,TIFFTAG_SAMPLESPERPIXEL,&samples));QCOMPARE(samples,uint16_t(3));QVERIFY(TIFFGetField(t,TIFFTAG_ICCPROFILE,&count,&icc));QCOMPARE(QByteArray(static_cast<const char *>(icc),count),s.icc);TIFFClose(t);
            }
            ++i;
        }QCOMPARE(i,5);
        Settings s;s.format=Format::Tiff;s.mode=Mode::Cmyk;s.bits=16;s.icc=cmyk();s.besideSource=false;s.directory=dir.filePath("cmyk");QString e;
        auto jobs=freeze({{path,QString::fromLatin1(before),{},8}},s,e);QCOMPARE(jobs.size(),1);auto r=execute(jobs[0]);QVERIFY2(r.ok,qPrintable(r.error));QVERIFY(!readOutput(r.path,e).isNull());
        auto wrong=jobs[0];wrong.path=dir.filePath("changed.tif");wrong.hash=QString(64,'0');QVERIFY(!execute(wrong).ok);QVERIFY(!QFileInfo::exists(wrong.path));
        wrong=jobs[0];wrong.path=path;QVERIFY(!execute(wrong).ok);QCOMPARE(digest(path),before);
        std::atomic_bool cancelled{true};wrong.path=dir.filePath("cancel.tif");QVERIFY(execute(wrong,&cancelled).cancelled);QVERIFY(!QFileInfo::exists(wrong.path));
        auto block=dir.filePath("block");QFile f(block);QVERIFY(f.open(QIODevice::WriteOnly));f.write("file");f.close();wrong.path=block+"/fail.tif";QVERIFY(!execute(wrong).ok);QCOMPARE(digest(path),before);
    }
    void metadataAndDimensions(){
        QTemporaryDir dir;const auto path=sample(dir);PhotoMetadata m;m.make="Canon";m.model="Synthetic camera";m.artist="Artist";m.copyright="Copyright";m.date="2026:10:15 12:34:56";m.iso=400;m.exposure=1./125;m.aperture=2.8;m.focalLength=50;m.lens="Test lens";m.hasGps=true;m.latitude=31.23;m.longitude=121.47;m.altitude=12.5;
        const auto exif=exifBlock(m,{321,201},300);QFile f(path);QVERIFY(f.open(QIODevice::ReadOnly));auto bytes=f.readAll();f.close();QByteArray segment=QByteArray::fromHex("ffe1");int n=exif.size()+8;segment+=char(n>>8);segment+=char(n);segment.append("Exif\0\0",6);segment+=exif;bytes.insert(2,segment);QVERIFY(f.open(QIODevice::WriteOnly|QIODevice::Truncate));QCOMPARE(f.write(bytes),bytes.size());f.close();
        QString e;auto decoded=readMetadata(path,Metadata::Camera,true,e);QVERIFY2(e.isEmpty(),qPrintable(e));QCOMPARE(decoded.make,m.make);QCOMPARE(decoded.iso,400);QVERIFY(decoded.hasGps);QVERIFY(qAbs(decoded.latitude-m.latitude)<1e-6);
        auto privacy=readMetadata(path,Metadata::Copyright,false,e);QCOMPARE(privacy.artist,m.artist);QVERIFY(privacy.make.isEmpty());QVERIFY(!privacy.hasGps);
        for(auto fmt:{Format::Jpeg,Format::Png,Format::Tiff}){Settings s;s.format=fmt;s.icc=builtinProfile("sRGB",e);s.besideSource=false;s.directory=dir.filePath(QString::number(int(fmt)));s.sizeMode=SizeMode::LongEdge;s.width=160;
            auto jobs=freeze({{path,QString::fromLatin1(digest(path)),{},1}},s,e);QCOMPARE(jobs.size(),1);auto r=execute(jobs[0]);QVERIFY2(r.ok,qPrintable(r.error));QCOMPARE(r.size,QSize(160,100));
            if(fmt!=Format::Png){auto out=readMetadata(r.path,Metadata::Camera,true,e);QVERIFY2(e.isEmpty(),qPrintable(e));QCOMPARE(out.make,m.make);QVERIFY(!out.hasGps);}
        }
        Settings s;QCOMPARE(outputSize({6000,4000},s,e),QSize(6000,4000));s.ppi=72;QCOMPARE(outputSize({6000,4000},s,e),QSize(6000,4000));s.sizeMode=SizeMode::Fit;s.width=1000;s.height=1000;QCOMPARE(outputSize({6000,4000},s,e),QSize(1000,667));s.sizeMode=SizeMode::LongEdge;s.width=9999;QCOMPARE(outputSize({6000,4000},s,e),QSize(6000,4000));s.sizeMode=SizeMode::Print;s.width=254;s.height=254;s.ppi=100;QCOMPARE(outputSize({6000,4000},s,e),QSize(1000,667));
    }
    void actualPhysicalPixels(){Canvas c;c.resize(640,480);c.setImage(QImage(2000,1000,QImage::Format_RGB32));c.actualPixels();QCOMPARE(c.zoom()*c.devicePixelRatioF(),1.0);}
    void shortWriteAndMidJobCancellation(){
        class FullDevice:public QBuffer { protected:qint64 writeData(const char *,qint64)override{setErrorString("No space left on device");return -1;} } full;
        QVERIFY(full.open(QIODevice::ReadWrite));Settings s;s.format=Format::Tiff;QString e;s.icc=builtinProfile("sRGB",e);auto pixels=convert(gradient(),s,e);QVERIFY(!pixels.data.isEmpty());QVERIFY(!writeTiff(full,pixels,s,{},e));QVERIFY(!e.isEmpty());
        QTemporaryDir dir;const auto path=sample(dir);auto before=digest(path);s.besideSource=false;s.directory=dir.filePath("out");auto jobs=freeze({{path,QString::fromLatin1(before),{},0}},s,e);QCOMPARE(jobs.size(),1);std::atomic_bool cancel{false};auto r=execute(jobs[0],&cancel,[&](const QString &stage){if(stage==QStringLiteral("编码写入"))cancel=true;});QVERIFY(r.cancelled);QVERIFY(!QFileInfo::exists(jobs[0].path));QCOMPARE(digest(path),before);QVERIFY(QDir(s.directory).entryList({"*.tmp"},QDir::Files|QDir::Hidden).isEmpty());
    }
    void cmykDepthCompressionGpsAndLongPath(){
        QTemporaryDir dir;QString e;const auto path=sample(dir);const auto original=digest(path);
        for(int bits:{8,16})for(bool zipped:{false,true}){
            Settings s;s.format=Format::Tiff;s.mode=Mode::Cmyk;s.bits=bits;s.zip=zipped;s.icc=cmyk();s.profileName="LCMS test CMYK";s.intent=zipped?0:1;s.besideSource=false;s.directory=dir.filePath("cmyk-"+QString::number(bits)+QString::number(zipped));
            auto jobs=freeze({{path,QString::fromLatin1(original),{},0}},s,e);QCOMPARE(jobs.size(),1);auto result=execute(jobs[0]);QVERIFY2(result.ok,qPrintable(result.error));QVERIFY(!readOutput(result.path,e).isNull());
        }
        Settings s;s.format=Format::Tiff;s.bits=16;s.icc=builtinProfile("sRGB",e);s.besideSource=false;s.directory=dir.path();while(s.directory.size()<280)s.directory+="/long-export-folder-123456789";
        auto jobs=freeze({{path,QString::fromLatin1(original),{},0}},s,e);QCOMPARE(jobs.size(),1);auto result=execute(jobs[0]);QVERIFY2(result.ok,qPrintable(result.error));QVERIFY(!readOutput(result.path,e).isNull());QCOMPARE(digest(path),original);
        // Verify TIFF channel/bit-depth/ICC/EXIF/GPS tags using LibTIFF itself.
        PhotoMetadata m;m.hasGps=true;m.latitude=-33.86;m.longitude=151.2;m.altitude=10;m.iso=800;m.copyright="Test copyright";
        QBuffer buffer;QVERIFY(buffer.open(QIODevice::ReadWrite));auto pixels=convert(gradient(),s,e);s.gps=true;QVERIFY2(writeTiff(buffer,pixels,s,m,e),qPrintable(e));
        const auto tagged=dir.filePath("gps.tif");QFile out(tagged);QVERIFY(out.open(QIODevice::WriteOnly));out.write(buffer.data());out.close();auto decoded=readMetadata(tagged,Metadata::Camera,true,e);QVERIFY2(e.isEmpty(),qPrintable(e));QVERIFY(decoded.hasGps);QVERIFY(qAbs(decoded.latitude-m.latitude)<1e-5);QCOMPARE(decoded.iso,800);QCOMPARE(decoded.copyright,m.copyright);
    }
    void realSamplesAnd45Mp(){
        auto paths=qEnvironmentVariable("LUMINA_EXPORT_TEST_SOURCES").split('|',Qt::SkipEmptyParts);if(paths.isEmpty())QSKIP("Opt-in real/45MP export audit");
        QTemporaryDir dir;QVERIFY(dir.isValid());
        if(qEnvironmentVariableIntValue("LUMINA_EXPORT_TEST_45MP")==1){
            QImage image(9000,5000,QImage::Format_RGB32);image.fill(QColor(80,140,210));image.setColorSpace(QColorSpace::SRgb);
            const auto p=dir.filePath("synthetic-45mp.jpg");QVERIFY(image.save(p,"JPEG",98));paths.append(p);image={};
            QImage over(9001,5000,QImage::Format_RGB32);over.fill(Qt::gray);const auto pOver=dir.filePath("over-45mp.jpg");QVERIFY(over.save(pOver,"JPEG"));over={};QVERIFY(!readSource(pOver).source);
        }
        int index=0;for(const auto &path:paths){const auto original=digest(path);QVERIFY(!original.isEmpty());Settings s;s.format=Format::Tiff;s.bits=16;s.profileName="ProPhoto RGB";QString e;s.icc=builtinProfile(s.profileName,e);s.besideSource=false;s.directory=dir.filePath(QString::number(index++));Recipe recipe;recipe.exposure=.25;auto jobs=freeze({{path,QString::fromLatin1(original),recipe,1}},s,e);QCOMPARE(jobs.size(),1);QElapsedTimer timer;timer.start();auto result=execute(jobs[0]);QVERIFY2(result.ok,qPrintable(result.error));qInfo()<<"full source export"<<QFileInfo(path).fileName()<<result.size<<"elapsedMs"<<timer.elapsed();QCOMPARE(digest(path),original);}
    }
};
QTEST_MAIN(ExportEngineTests)
#include "export_engine_tests.moc"
