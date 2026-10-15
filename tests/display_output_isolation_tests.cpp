#include <QtTest>
#include <QBuffer>
#include <QColorSpace>
#include <QCryptographicHash>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <lcms2.h>
#include "app/display_presenter.h"
#include "io/source_reader.h"
#include "render/cpu_renderer.h"

namespace {
QByteArray profile(double gamma){
    cmsCIExyY white{.3127,.3290,1};cmsCIExyYTRIPLE primaries{{.64,.33,1},{.21,.71,1},{.15,.06,1}};
    auto *curve=cmsBuildGamma(nullptr,gamma);cmsToneCurve *curves[]{curve,curve,curve};
    auto handle=cmsCreateRGBProfile(&white,&primaries,curves);cmsFreeToneCurve(curve);
    cmsUInt32Number count=0;cmsSaveProfileToMem(handle,nullptr,&count);QByteArray bytes(count,Qt::Uninitialized);
    cmsSaveProfileToMem(handle,bytes.data(),&count);cmsCloseProfile(handle);return bytes;
}
QByteArray digest(const QImage &image){return QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char *>(image.constBits()),image.sizeInBytes()),QCryptographicHash::Sha256);}
QByteArray png(const QImage &image){QByteArray bytes;QBuffer buffer(&bytes);buffer.open(QIODevice::WriteOnly);if(!image.save(&buffer,"PNG"))return {};return bytes;}
QByteArray fileHash(const QString &path){QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};QCryptographicHash hash(QCryptographicHash::Sha256);hash.addData(&file);return hash.result();}
}
class DisplayOutputIsolationTests:public QObject {
    Q_OBJECT
private slots:
    void monitorDoesNotChangeFullSourceOutput_data(){
        QTest::addColumn<QSize>("size");QTest::newRow("landscape")<<QSize(240,160);QTest::newRow("portrait")<<QSize(160,240);
    }
    void monitorDoesNotChangeFullSourceOutput(){
        QFETCH(QSize,size);QTemporaryDir directory;QVERIFY(directory.isValid());
        QImage original(size,QImage::Format_RGB888);original.setColorSpace(QColorSpace::SRgb);
        for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x)original.setPixelColor(x,y,QColor(40+x*150/size.width(),45+y*140/size.height(),65+(x+y)%90));
        const auto path=directory.filePath(QStringLiteral("synthetic.jpg"));QVERIFY(original.save(path,"JPEG",95));const auto jpegHash=fileHash(path);
        const auto loaded=readSource(path);QVERIFY2(loaded.error.isEmpty(),qPrintable(loaded.error));QVERIFY(loaded.source);
        const auto sourceHash=digest(loaded.source->linear);
        Recipe recipe;recipe.exposure=.25;recipe.contrast=.12;recipe.saturation=1.15;recipe.crop={.1,.1,.8,.8};recipe.angle=3;recipe.curves[0]={{0,0},{.45,.52},{1,1}};
        const auto recipeBefore=recipeJson(recipe);const auto full=renderFrame(*loaded.source,recipe,0);const auto preview=renderFrame(*loaded.source,recipe,3840);
        QVERIFY2(full.error.isEmpty(),qPrintable(full.error));QVERIFY2(preview.error.isEmpty(),qPrintable(preview.error));
        QCOMPARE(full.image.size(),renderedSize(size,recipe));QVERIFY(full.image.colorSpace().isValid());const auto output=png(full.image);QVERIFY(!output.isEmpty());
        DisplayPresenter presenter;presenter.setMonitorProfile(profile(2.2));QSignalSpy ready(&presenter,&DisplayPresenter::imageReady);
        presenter.setImage(preview.image,QStringLiteral("photo-1/revision-1"));QTRY_VERIFY_WITH_TIMEOUT(ready.count()>0,10000);
        QVERIFY(digest(ready.last().at(0).value<QImage>())!=digest(preview.image));
        for(const auto &target:QList<QByteArray>{QByteArray{},profile(1.8),profile(2.6)}){
            const auto before=ready.count();presenter.setMonitorProfile(target);QTRY_VERIFY_WITH_TIMEOUT(ready.count()>before,10000);
            QCOMPARE(presenter.canonicalImage(),preview.image);QCOMPARE(recipeJson(recipe),recipeBefore);
            const auto again=renderFrame(*loaded.source,recipe,0);QVERIFY2(again.error.isEmpty(),qPrintable(again.error));
            QCOMPARE(again.fullSize,full.fullSize);QCOMPARE(again.image,full.image);QCOMPARE(again.image.colorSpace().iccProfile(),full.image.colorSpace().iccProfile());QCOMPARE(png(again.image),output);
            QCOMPARE(digest(loaded.source->linear),sourceHash);QCOMPARE(fileHash(path),jpegHash);
        }
    }
};
QTEST_MAIN(DisplayOutputIsolationTests)
#include "display_output_isolation_tests.moc"
