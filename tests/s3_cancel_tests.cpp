#include "core/cancellation.h"
#include "render/cpu_renderer.h"
#include "color/color_pipeline.h"
#include "io/source_reader.h"
#include <QtTest>
#include <QtConcurrentRun>
#include <QCryptographicHash>
#include <QTemporaryDir>
#include <QFileInfo>
#include <QSemaphore>
#include <QFile>

namespace {
SourceFrame sample(QSize size){
    SourceFrame source;source.originalSize=size;source.linear=QImage(size,QImage::Format_RGBA32FPx4);
    for(int y=0;y<size.height();++y){auto *row=reinterpret_cast<float *>(source.linear.scanLine(y));
        for(int x=0;x<size.width();++x){row[x*4]=float(x%101)/33-.2f;row[x*4+1]=float(y%89)/67;
            row[x*4+2]=float((x+y)%73)/48;row[x*4+3]=x%19==0?0.f:x%13==0?.25f:1.f;}}
    return source;
}
QByteArray hash(const QImage &image){return QCryptographicHash::hash(QByteArrayView(image.constBits(),image.sizeInBytes()),QCryptographicHash::Sha256);}
}
class S3CancelTests:public QObject {
    Q_OBJECT
private slots:
    void canceledBeforeIoOrAllocation(){
        CancellationSource cancel;cancel.cancel();QString error;
        const auto input=readSource("does-not-exist.CR3",0,0,cancel.token());
        QVERIFY(!input.source);QCOMPARE(input.error,QString("Canceled"));
        QVERIFY(inputToWork({}, {},error,cancel.token()).isNull());QCOMPARE(error,QString("Canceled"));
        QVERIFY(workToSrgb({},error,cancel.token()).isNull());QCOMPARE(error,QString("Canceled"));
        QVERIFY(renderLinear({}, {},0,error,{},cancel.token()).isNull());QCOMPARE(error,QString("Canceled"));
        const auto frame=renderFrame({}, {},0,nullptr,{},cancel.token());QVERIFY(frame.image.isNull());QCOMPARE(frame.error,QString("Canceled"));
    }
    void unusedTokenKeepsExactPixels(){
        const auto source=sample({131,97});const auto original=hash(source.linear);CancellationSource cancel;
        for(int mode=0;mode<4;++mode){Recipe recipe;
            if(mode>0){recipe.exposure=.7;recipe.contrast=.2;recipe.saturation=1.3;recipe.warmth=.15;recipe.tint=-.1;recipe.curves[1]={{0,0},{.35,.42},{1,1}};recipe.hsl[3]={.2,.1,-.1};}
            if(mode>1){recipe.crop={.1,.07,.8,.85};recipe.angle=13;recipe.flipHorizontal=true;recipe.perspective={.03,-.02};recipe.distortion=.07;}
            if(mode>2){recipe.texture=.15;recipe.clarity=.1;recipe.sharpenAmount=.2;recipe.sharpenMask=.3;recipe.noiseChroma=.1;}
            QString aError,bError;const auto a=renderLinear(source,recipe,0,aError),b=renderLinear(source,recipe,0,bError,{},cancel.token());
            QVERIFY2(aError.isEmpty(),qPrintable(aError));QCOMPARE(bError,aError);QVERIFY(!a.isNull());QCOMPARE(hash(b),hash(a));
            const auto aDisplay=renderFrame(source,recipe,0),bDisplay=renderFrame(source,recipe,0,nullptr,{},cancel.token());
            QVERIFY2(aDisplay.error.isEmpty(),qPrintable(aDisplay.error));QCOMPARE(bDisplay.error,aDisplay.error);QCOMPARE(bDisplay.image,aDisplay.image);
            QCOMPARE(hash(source.linear),original);
        }
    }
    void inFlightRenderReturnsNoPartialFrame(){
        const auto source=sample({3840,2560});const auto original=hash(source.linear);
        Recipe recipe;recipe.exposure=.6;recipe.angle=17;recipe.perspective={.1,-.1};
        CancellationSource cancel;auto started=std::make_shared<QSemaphore>();
        auto future=QtConcurrent::run([source,recipe,token=cancel.token(),started]{
            started->release();return renderFrame(source,recipe,3840,nullptr,{},token);
        });
        QVERIFY(started->tryAcquire(1,5000));QTest::qWait(3);
        QVERIFY2(!future.isFinished(),"The fixture must still be rendering to exercise in-flight cancellation");
        cancel.cancel();QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(),15000);
        const auto result=future.result();QVERIFY(result.image.isNull());QCOMPARE(result.error,QString("Canceled"));
        QCOMPARE(hash(source.linear),original);
    }
    void sourceConversionCancellation(){
        QTemporaryDir directory;QVERIFY(directory.isValid());
        QImage encoded(3840,2560,QImage::Format_RGB32);encoded.fill(QColor(130,100,70));
        const auto path=directory.filePath("cancel-input.jpg");QVERIFY(encoded.save(path,"JPEG",95));
        const auto before=QFileInfo(path).size();CancellationSource cancel;auto started=std::make_shared<QSemaphore>();
        auto future=QtConcurrent::run([path,token=cancel.token(),started]{started->release();return readSource(path,0,0,token);});
        QVERIFY(started->tryAcquire(1,5000));QTest::qWait(3);
        QVERIFY2(!future.isFinished(),"The input fixture must still be reading/converting");
        cancel.cancel();QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(),15000);
        QVERIFY(!future.result().source);QCOMPARE(future.result().error,QString("Canceled"));QCOMPARE(QFileInfo(path).size(),before);
    }
    void realRawCooperativeCancel(){
        const auto path=qEnvironmentVariable("LUMINA_S3_RAW_CANCEL_SAMPLE");
        if(path.isEmpty())QSKIP("Opt-in local RAW cooperative cancellation; not a worker isolation audit");
        const auto fileHash=[&]{QFile file(path);if(!file.open(QIODevice::ReadOnly))return QByteArray{};QCryptographicHash digest(QCryptographicHash::Sha256);if(!digest.addData(&file))return QByteArray{};return digest.result();};
        const auto before=fileHash();QVERIFY(!before.isEmpty());
        CancellationSource cancel;auto started=std::make_shared<QSemaphore>();
        auto future=QtConcurrent::run([path,token=cancel.token(),started]{started->release();return readSource(path,0,0,token);});
        QVERIFY(started->tryAcquire(1,5000));QTest::qWait(100);
        QVERIFY2(!future.isFinished(),"RAW must still be in flight at cancellation");
        QElapsedTimer response;response.start();cancel.cancel();
        QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(),15000);
        QVERIFY(!future.result().source);QCOMPARE(future.result().error,QString("Canceled"));
        QCOMPARE(fileHash(),before);qInfo()<<"RAW cooperative cancel completion ms"<<response.elapsed();
    }
};
QTEST_MAIN(S3CancelTests)
#include "s3_cancel_tests.moc"
