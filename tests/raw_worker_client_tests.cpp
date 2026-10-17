#include <QtTest>
#include <QtConcurrentRun>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>
#include <cstring>
#include <future>
#include "raw/worker_client.h"
#include "raw/source_kernel.h"
#include "render/cpu_renderer.h"
using namespace RawWorker;
namespace {
struct Fixture {
    QTemporaryDir directory;Request request;
    bool init(QSize size={96,64}){
        if(!directory.isValid())return false;QImage image(size,QImage::Format_RGB32);image.fill(QColor(45,90,140));
        request.sourcePath=directory.filePath("source.jpg");if(!image.save(request.sourcePath,"JPEG",98))return false;
        QFile file(request.sourcePath);if(!file.open(QIODevice::ReadOnly))return false;
        request.sourceHash=QString::fromLatin1(QCryptographicHash::hash(file.readAll(),QCryptographicHash::Sha256).toHex());
        request.id=QUuid::createUuid().toString(QUuid::WithoutBraces);request.token=QUuid::createUuid().toString(QUuid::WithoutBraces);return true;
    }
};
FrameResult background(const Request &request,const CancellationToken &cancel={}){
    // QFuture::waitForFinished may steal a queued QtConcurrent task onto the
    // waiting main thread. std::launch::async guarantees a separate test thread.
    return std::async(std::launch::async,[request,cancel]{return runWorker(request,cancel);}).get();
}
bool samePixels(const QImage &a,const QImage &b){
    if(a.size()!=b.size()||a.format()!=QImage::Format_RGBA32FPx4||b.format()!=a.format())return false;
    for(int y=0;y<a.height();++y)if(std::memcmp(a.constScanLine(y),b.constScanLine(y),size_t(a.width())*16))return false;return true;
}
}
class RawWorkerClientTests:public QObject {
    Q_OBJECT
private slots:
    void initTestCase(){QStandardPaths::setTestModeEnabled(true);}
    void rejectsMainThread(){Fixture f;QVERIFY(f.init());const auto result=runWorker(f.request);QVERIFY(!result.source);QVERIFY(result.error.contains(QStringLiteral("后台")));}
    void backgroundFullAndPrepare(){
        Fixture f;QVERIFY(f.init({320,240}));const auto reference=readSourceKernel(f.request.sourcePath);QVERIFY2(reference.source,qPrintable(reference.error));
        for(int mode=0;mode<2;++mode){
            if(mode){f.request.operation=Operation::Prepare;f.request.longEdge=3840;f.request.geometry.crop={.1,.15,.8,.7};f.request.geometry.angle=5;}
            const auto result=background(f.request);QVERIFY2(result.source,qPrintable(result.error));
            QString error;const auto expected=mode?renderLinear(*reference.source,f.request.geometry,3840,error):reference.source->linear;
            QVERIFY2(error.isEmpty(),qPrintable(error));QVERIFY(samePixels(result.source->linear,expected));QCOMPARE(result.source->originalSize,reference.source->originalSize);
            QCOMPARE(result.source->sha256,f.request.sourceHash);QCOMPARE(result.source->cameraToWork,reference.source->cameraToWork);QCOMPARE(result.source->cameraWB,reference.source->cameraWB);
        }
    }
    void wrongSourceAndPreCanceled(){
        Fixture f;QVERIFY(f.init());auto wrong=f.request;wrong.sourceHash=QString(64,'0');const auto mismatch=background(wrong);QVERIFY(!mismatch.source);QVERIFY(!mismatch.error.isEmpty());
        CancellationSource canceled;canceled.cancel();const auto result=background(f.request,canceled.token());QVERIFY(!result.source);QVERIFY(!result.error.isEmpty());
    }
    void cancellationReleasesSlot(){
        Fixture large;QVERIFY(large.init({3840,2160}));CancellationSource cancel;
        auto future=std::async(std::launch::async,[request=large.request,token=cancel.token()]{return runWorker(request,token);});
        QTest::qWait(1);cancel.cancel();const auto result=future.get();QVERIFY(!result.source);QVERIFY(!result.error.isEmpty());
        Fixture small;QVERIFY(small.init());const auto next=background(small.request);QVERIFY2(next.source,qPrintable(next.error));
    }
    void cancelAfterChildProgress(){
        Fixture large;QVERIFY(large.init({3840,2160}));CancellationSource cancel;bool childSeen=false;
        const auto result=std::async(std::launch::async,[&]{return runWorker(large.request,cancel.token(),[&](const QString &phase,int,int){if(phase=="hashBefore"){childSeen=true;cancel.cancel();}});}).get();
        QVERIFY(childSeen);QVERIFY(!result.source);QVERIFY(!result.error.isEmpty());
        Fixture small;QVERIFY(small.init());const auto next=background(small.request);QVERIFY2(next.source,qPrintable(next.error));
    }
    void queuedCancellationDoesNotDispatch(){
        Fixture first,second;QVERIFY(first.init()&&second.init());std::promise<void> entered,release;auto enteredFuture=entered.get_future();auto gate=release.get_future().share();bool signaled=false;
        auto firstFuture=std::async(std::launch::async,[&]{return runWorker(first.request,{},[&](const QString &phase,int,int){if(phase=="hashBefore"&&!signaled){signaled=true;entered.set_value();gate.wait_for(std::chrono::seconds(5));}});});
        struct Release {std::promise<void> &gate;bool released=false;void now(){if(!released){released=true;gate.set_value();}}~Release(){now();}}releaseGuard{release};
        const auto ready=enteredFuture.wait_for(std::chrono::seconds(2));if(ready!=std::future_status::ready)releaseGuard.now();QVERIFY(ready==std::future_status::ready);
        CancellationSource cancel;bool dispatched=false;QElapsedTimer clock;clock.start();
        auto secondFuture=std::async(std::launch::async,[&]{return runWorker(second.request,cancel.token(),[&](const QString &,int,int){dispatched=true;});});
        QTest::qWait(20);cancel.cancel();const auto canceled=secondFuture.wait_for(std::chrono::seconds(2));if(canceled!=std::future_status::ready)releaseGuard.now();
        QVERIFY(canceled==std::future_status::ready);const auto result=secondFuture.get();QVERIFY(!result.source);QVERIFY(!result.error.isEmpty());QVERIFY(!dispatched);QVERIFY(clock.elapsed()<4000);
        releaseGuard.now();const auto completed=firstFuture.get();QVERIFY2(completed.source,qPrintable(completed.error));
    }
    void actualRawStageCancellation(){
        const auto sample=qEnvironmentVariable("LUMINA_WORKER_SAMPLE");if(QFileInfo(sample).suffix().compare("cr3",Qt::CaseInsensitive)!=0)QSKIP("Set a private CR3 sample to verify received RAW milestones.");
        QFile file(sample);QVERIFY(file.open(QIODevice::ReadOnly));QCryptographicHash hash(QCryptographicHash::Sha256);QVERIFY(hash.addData(&file));file.close();const auto before=QString::fromLatin1(hash.result().toHex());
        for(const auto phase:{"rawOpen","rawUnpack","rawDevelop","write"}){
            Fixture f;QVERIFY(f.init());f.request.sourcePath=sample;f.request.sourceHash=before;CancellationSource cancel;bool observed=false;QElapsedTimer clock;clock.start();
            const auto result=std::async(std::launch::async,[&]{return runWorker(f.request,cancel.token(),[&](const QString &name,int current,int){if(name==QLatin1String(phase)&&current==0){observed=true;cancel.cancel();}});}).get();
            QVERIFY(observed);QVERIFY(!result.source);QVERIFY(!result.error.isEmpty());QVERIFY(clock.elapsed()<30000);qInfo()<<"RAW received milestone cancellation"<<phase<<"elapsedMs"<<clock.elapsed();
            Fixture small;QVERIFY(small.init());const auto next=background(small.request);QVERIFY2(next.source,qPrintable(next.error));
        }
        QVERIFY(file.open(QIODevice::ReadOnly));QCryptographicHash after(QCryptographicHash::Sha256);QVERIFY(after.addData(&file));QCOMPARE(QString::fromLatin1(after.result().toHex()),before);
    }
    void actualSampleParentEquivalence(){
        const auto path=qEnvironmentVariable("LUMINA_WORKER_SAMPLE");if(path.isEmpty())QSKIP("Set LUMINA_WORKER_SAMPLE for parent/worker private sample validation.");
        QFile file(path);QVERIFY(file.open(QIODevice::ReadOnly));QCryptographicHash sourceDigest(QCryptographicHash::Sha256);QVERIFY(sourceDigest.addData(&file));file.close();
        const auto sourceSha=QString::fromLatin1(sourceDigest.result().toHex());
        const auto digest=[](const QImage &image){QCryptographicHash hash(QCryptographicHash::Sha256);for(int y=0;y<image.height();++y)hash.addData(QByteArrayView(reinterpret_cast<const char *>(image.constScanLine(y)),image.width()*16));return hash.result().toHex();};
        for(int mode=0;mode<2;++mode){
            Fixture f;QVERIFY(f.init());f.request.sourcePath=path;f.request.sourceHash=sourceSha;
            if(mode){f.request.operation=Operation::Prepare;f.request.longEdge=3840;f.request.geometry.crop={.1,.1,.8,.8};f.request.geometry.angle=5;
                if(QFileInfo(path).suffix().compare("cr3",Qt::CaseInsensitive)==0){f.request.warmth=.25;f.request.tint=-.1;}}
            QByteArray expected;QSize size;std::array<float,9> matrix{};std::array<double,4> wb{};
            {
                const auto reference=readSourceKernel(path,f.request.warmth,f.request.tint);QVERIFY2(reference.source,qPrintable(reference.error));QString error;
                const auto image=mode?renderLinear(*reference.source,f.request.geometry,3840,error):reference.source->linear;QVERIFY2(!image.isNull(),qPrintable(error));
                expected=digest(image);size=image.size();matrix=reference.source->cameraToWork;wb=reference.source->cameraWB;
            }
            const auto result=background(f.request);QVERIFY2(result.source,qPrintable(result.error));QCOMPARE(result.source->linear.size(),size);QCOMPARE(digest(result.source->linear),expected);
            QCOMPARE(result.source->sha256,sourceSha);QCOMPARE(result.source->cameraToWork,matrix);QCOMPARE(result.source->cameraWB,wb);
            qInfo()<<"parent worker equivalence"<<QFileInfo(path).fileName()<<"operation"<<mode<<"sourceSha"<<sourceSha<<"pixelsSha"<<expected<<"size"<<size;
        }
        QVERIFY(file.open(QIODevice::ReadOnly));QCryptographicHash after(QCryptographicHash::Sha256);QVERIFY(after.addData(&file));QCOMPARE(QString::fromLatin1(after.result().toHex()),sourceSha);
    }
};
QTEST_GUILESS_MAIN(RawWorkerClientTests)
#include "raw_worker_client_tests.moc"
