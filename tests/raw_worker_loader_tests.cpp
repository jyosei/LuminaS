#include <QtTest>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include "app/edit_loader.h"
#include "raw/source_kernel.h"
#include "render/geometry_transform.h"
#include "render/local_renderer.h"
namespace {
QByteArray digest(const QImage &image){QCryptographicHash hash(QCryptographicHash::Sha256);for(int y=0;y<image.height();++y)hash.addData(QByteArrayView(reinterpret_cast<const char *>(image.constScanLine(y)),image.width()*image.depth()/8));return hash.result().toHex();}
}
class RawWorkerLoaderTests:public QObject {
    Q_OBJECT
private slots:
    void invalidRawDoesNotEnterPreviewCache(){
        QTemporaryDir directory;QVERIFY(directory.isValid());const auto path=directory.filePath(QStringLiteral("损坏样片.CR3"));QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write("invalid raw"),11);file.close();
        EditLoader loader;QSignalSpy ready(&loader,&EditLoader::ready);EditTag tag;tag.session=1;tag.job=1;loader.request(path,{},tag);QTRY_COMPARE_WITH_TIMEOUT(ready.size(),1,15000);
        const auto result=qvariant_cast<EditResult>(ready.takeFirst().at(0));QVERIFY(!result.error.isEmpty());QVERIFY(result.image.isNull());QVERIFY(!loader.preview(path,{}));
        QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),QByteArray("invalid raw"));file.close();
        const auto jpeg=directory.filePath("next.jpg");QImage input(96,64,QImage::Format_RGB32);input.fill(Qt::gray);QVERIFY(input.save(jpeg,"JPEG"));tag.job=2;loader.request(jpeg,{},tag);QTRY_COMPARE_WITH_TIMEOUT(ready.size(),1,15000);
        const auto next=qvariant_cast<EditResult>(ready.takeFirst().at(0));QVERIFY2(next.error.isEmpty(),qPrintable(next.error));QVERIFY(!next.image.isNull());
    }
    void actualRawPreparedAndFullSupport(){
        const auto path=qEnvironmentVariable("LUMINA_WORKER_SAMPLE");if(QFileInfo(path).suffix().compare("cr3",Qt::CaseInsensitive)!=0)QSKIP("Set a private CR3 sample for production-loader equivalence.");
        QFile file(path);QVERIFY(file.open(QIODevice::ReadOnly));QCryptographicHash hash(QCryptographicHash::Sha256);QVERIFY(hash.addData(&file));file.close();const auto sourceHash=QString::fromLatin1(hash.result().toHex());
        EditLoader loader;QSignalSpy ready(&loader,&EditLoader::ready);
        for(int mode=0;mode<2;++mode){
            auto document=std::make_shared<LocalEdit::PhotoDocument>();auto &recipe=document->global;recipe.warmth=.25;recipe.tint=-.1;recipe.exposure=.3;recipe.crop={.1,.1,.8,.8};recipe.angle=5;recipe.flipHorizontal=true;
            if(mode){recipe.perspective={.03,-.02};recipe.distortion=.1;recipe.chromaticAberration={.2,-.1};recipe.lensShading=.2;}
            document->asset.id="raw-loader-asset";document->asset.sourcePath=path;document->asset.sourceHash=sourceHash;document->asset.raw=true;document->revision=quint64(mode+1);document->localIdentity=quint64(mode+1);
            QByteArray expected,expectedMask,fullHash;QSize original;
            {
                const auto source=readSourceKernel(path,recipe.warmth,recipe.tint);QVERIFY2(source.source,qPrintable(source.error));original=source.source->originalSize;document->asset.normalizedSize=original;if(mode)fullHash=digest(source.source->linear);
                LocalEdit::AdjustmentLayer layer;layer.id="local-layer";layer.parameters.exposure=.4;LocalEdit::MaskComponent component;component.id="local-brush";LocalEdit::BrushGeometry brush;brush.points={{original.width()/2.,original.height()/2.}};brush.radius=400;brush.hardness=.6;component.geometry=brush;layer.mask.components={component};document->layers={layer};
                QString error;auto proxy=*source.source;const auto geometry=preparedGeometry(recipe);proxy.linear=renderLinear(*source.source,geometry,3840,error);QVERIFY2(!proxy.linear.isNull(),qPrintable(error));
                proxy.sourcePixelsPerPixelX=double(original.width())*geometry.crop.width()/proxy.linear.width();proxy.sourcePixelsPerPixelY=double(original.height())*geometry.crop.height()/proxy.linear.height();proxy.sourcePixelsPerPixel=std::max(proxy.sourcePixelsPerPixelX,proxy.sourcePixelsPerPixelY);
                auto applied=recipe;clearPreparedGeometry(applied);const auto reference=LocalEdit::renderDocumentPreview(proxy,*document,applied,3840);QVERIFY2(reference.error.isEmpty(),qPrintable(reference.error));expected=digest(reference.image);expectedMask=digest(reference.localMask);
                auto withoutLayers=*document;withoutLayers.layers.clear();const auto plain=LocalEdit::renderDocumentPreview(proxy,withoutLayers,applied,3840);QVERIFY2(plain.error.isEmpty(),qPrintable(plain.error));QVERIFY(expected!=digest(plain.image));
            } // Release the full reference before the real worker starts.
            EditTag tag;tag.session=73;tag.revision=quint64(mode+1);tag.job=quint64(mode+1);tag.geometry=quint64(mode+1);tag.localIdentity=document->localIdentity;QString error;
            QVERIFY2(loader.requestDocument(path,document,tag,error),qPrintable(error));QTRY_COMPARE_WITH_TIMEOUT(ready.size(),1,90000);const auto result=qvariant_cast<EditResult>(ready.takeFirst().at(0));QVERIFY2(result.error.isEmpty(),qPrintable(result.error));
            QCOMPARE(digest(result.image),expected);QCOMPARE(digest(result.localMask),expectedMask);QCOMPARE(result.originalSize,original);QCOMPARE(result.sourceHash,sourceHash);QCOMPARE(qMax(result.workFrame.linear.width(),result.workFrame.linear.height()),3840);
            QCOMPARE(result.workFrame.rawWarmth,.25);QCOMPARE(result.workFrame.rawTint,-.1);QVERIFY(result.sourceRebuilt);
            if(mode){QVERIFY(result.fullGeometry);QCOMPARE(result.fullGeometry->linear.size(),original);QCOMPARE(result.fullGeometry->linear.format(),QImage::Format_RGBA32FPx4);QCOMPARE(digest(result.fullGeometry->linear),fullHash);}else QVERIFY(!result.fullGeometry);
            qInfo()<<"production RAW loader mode"<<mode<<"pixelSha"<<expected<<"maskSha"<<expectedMask<<"fullSourceRetained"<<bool(result.fullGeometry)<<"elapsedMs"<<result.elapsedMs;
        }
        QVERIFY(file.open(QIODevice::ReadOnly));QCryptographicHash after(QCryptographicHash::Sha256);QVERIFY(after.addData(&file));QCOMPARE(QString::fromLatin1(after.result().toHex()),sourceHash);
    }
};
QTEST_GUILESS_MAIN(RawWorkerLoaderTests)
#include "raw_worker_loader_tests.moc"
