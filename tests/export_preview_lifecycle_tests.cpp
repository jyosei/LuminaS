#include <QtTest>
#include <QTemporaryDir>
#include <QSemaphore>
#include <QScopeGuard>
#include <QElapsedTimer>
#include <thread>
#include "app/export_dialog.h"
#include "app/canvas.h"
#include "app/display_presenter.h"
#include "io/source_reader.h"
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
namespace {
Export::Asset asset(const QString &path){Export::Asset result;result.path=path;result.hash=QString(64,path.contains("blue")?'b':'a');return result;}
struct Gate {QSemaphore entered,release;};
QString hashFile(const QString &path){QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};QCryptographicHash hash(QCryptographicHash::Sha256);if(!hash.addData(&file))return {};return QString::fromLatin1(hash.result().toHex());}
}
class ExportPreviewLifecycleTests:public QObject {
    Q_OBJECT
private slots:
    void localDocumentInvalidatesLinearCache(){
        QTemporaryDir root;QVERIFY(root.isValid());const auto old=qgetenv("LUMINA_EXPORT_DATA_DIR");const bool had=qEnvironmentVariableIsSet("LUMINA_EXPORT_DATA_DIR");qputenv("LUMINA_EXPORT_DATA_DIR",root.path().toUtf8());
        const auto restore=qScopeGuard([&]{if(had)qputenv("LUMINA_EXPORT_DATA_DIR",old);else qunsetenv("LUMINA_EXPORT_DATA_DIR");});
        Export::Queue queue(root.filePath("queue.json"));auto sample=asset(root.filePath("synthetic.jpg"));sample.revision=1;
        LocalEdit::PhotoDocument document;document.asset.id="asset";document.asset.sourcePath=sample.path;document.asset.sourceHash=sample.hash;document.asset.normalizedSize={32,24};document.revision=1;
        LocalEdit::AdjustmentLayer layer;layer.id="layer";layer.parameters.exposure=1;LocalEdit::MaskComponent component;component.id="stroke";LocalEdit::BrushGeometry brush;brush.points={{16,12}};brush.radius=20;brush.hardness=1;component.geometry=brush;layer.mask.components={component};document.layers={layer};
        sample.document=std::make_shared<const LocalEdit::PhotoDocument>(document);std::atomic_int calls{0};
        ExportDialog::PreviewReader reader=[&](const QString &path,double,double,const CancellationToken &,const QString &hash)->FrameResult{
            ++calls;auto frame=std::make_shared<SourceFrame>();frame->path=path;frame->sha256=hash;frame->originalSize={32,24};frame->linear=QImage(32,24,QImage::Format_RGBA32FPx4);
            for(int y=0;y<24;++y){auto *p=reinterpret_cast<float *>(frame->linear.scanLine(y));for(int x=0;x<32;++x){p[x*4]=.12f;p[x*4+1]=.18f;p[x*4+2]=.22f;p[x*4+3]=1;}}return {frame,{}};
        };
        auto dialog=std::make_unique<ExportDialog>(QList<Export::Asset>{sample},0,&queue,nullptr,reader);QSignalSpy accepted(dialog.get(),&ExportDialog::previewSourceCached);dialog->show();
        const auto image=[&]{return dialog->findChild<Canvas *>("export-preview")->presenter()->canonicalImage();};
        QTRY_COMPARE_WITH_TIMEOUT(accepted.size(),1,10000);QTRY_VERIFY_WITH_TIMEOUT(!image().isNull(),10000);const auto first=image().copy();QCOMPARE(calls.load(),1);
        document.layers[0].parameters.exposure=-1;++document.revision;sample.revision=document.revision;sample.document=std::make_shared<const LocalEdit::PhotoDocument>(document);dialog->replaceAssets({sample},0);
        QTRY_COMPARE_WITH_TIMEOUT(accepted.size(),2,10000);QTRY_VERIFY_WITH_TIMEOUT(image()!=first,10000);QCOMPARE(calls.load(),2);const auto second=image().copy();
        sample.document=std::make_shared<const LocalEdit::PhotoDocument>(document);dialog->replaceAssets({sample},0);QTRY_COMPARE_WITH_TIMEOUT(accepted.size(),3,10000);QCOMPARE(calls.load(),2);QCOMPARE(image(),second);dialog.reset();
    }
    void actualRawCloseCancelsAfterDevelopMilestone(){
        const auto path=qEnvironmentVariable("LUMINA_WORKER_SAMPLE");if(QFileInfo(path).suffix().compare("cr3",Qt::CaseInsensitive)!=0)QSKIP("Set a private R7 CR3 for native cancellation verification.");
        QTemporaryDir root;QVERIFY(root.isValid());const auto old=qgetenv("LUMINA_EXPORT_DATA_DIR");const bool had=qEnvironmentVariableIsSet("LUMINA_EXPORT_DATA_DIR");qputenv("LUMINA_EXPORT_DATA_DIR",root.path().toUtf8());
        const auto restore=qScopeGuard([&]{if(had)qputenv("LUMINA_EXPORT_DATA_DIR",old);else qunsetenv("LUMINA_EXPORT_DATA_DIR");});
        const auto before=hashFile(path);QVERIFY(!before.isEmpty());auto sample=asset(path);sample.hash=before;
        Export::Queue queue(root.filePath("queue.json"));std::atomic_bool developing{false},returned{false};QString readerError;
        ExportDialog::PreviewReader reader=[&](const QString &source,double warmth,double tint,const CancellationToken &cancel,const QString &hash){
            auto result=readSource(source,warmth,tint,cancel,hash,[&](const QString &stage,int current,int){if(stage=="rawDevelop"&&current==0)developing=true;});readerError=result.error;returned=true;return result;
        };
        auto dialog=std::make_unique<ExportDialog>(QList<Export::Asset>{sample},0,&queue,nullptr,reader);dialog->show();
        QTRY_VERIFY_WITH_TIMEOUT(developing.load(),60000);QElapsedTimer watch;watch.start();dialog.reset();const auto elapsed=watch.elapsed();QVERIFY(returned.load());QCOMPARE(readerError,QStringLiteral("Canceled"));QVERIFY2(elapsed<2500,QByteArray::number(elapsed));QCOMPARE(hashFile(path),before);qInfo()<<"R7 preview closed after actual worker rawDevelop milestone; cancel/join ms"<<elapsed;
    }
    void closeCancelsInFlightReader(){
        QTemporaryDir root;QVERIFY(root.isValid());const auto old=qgetenv("LUMINA_EXPORT_DATA_DIR");const bool had=qEnvironmentVariableIsSet("LUMINA_EXPORT_DATA_DIR");qputenv("LUMINA_EXPORT_DATA_DIR",root.path().toUtf8());
        const auto restore=qScopeGuard([&]{if(had)qputenv("LUMINA_EXPORT_DATA_DIR",old);else qunsetenv("LUMINA_EXPORT_DATA_DIR");});
        Export::Queue queue(root.filePath("queue.json"));std::atomic_bool entered{false},canceled{false},wrongThread{false};const auto gui=std::this_thread::get_id();
        ExportDialog::PreviewReader reader=[&](const QString &,double,double,const CancellationToken &token,const QString &)->FrameResult{
            wrongThread=std::this_thread::get_id()==gui;entered=true;
            while(!isCanceled(token))std::this_thread::sleep_for(std::chrono::milliseconds(1));canceled=true;return {{},"Canceled"};
        };
        auto dialog=std::make_unique<ExportDialog>(QList<Export::Asset>{asset(root.filePath("source.CR3"))},0,&queue,nullptr,reader);dialog->show();
        QTRY_VERIFY_WITH_TIMEOUT(entered.load(),5000);QElapsedTimer clock;clock.start();dialog.reset();QVERIFY2(clock.elapsed()<1500,QByteArray::number(clock.elapsed()));QVERIFY(canceled.load());QVERIFY(!wrongThread.load());
    }
    void staleSourceCannotCacheIntoReplacedAssetList(){
        QTemporaryDir root;QVERIFY(root.isValid());const auto old=qgetenv("LUMINA_EXPORT_DATA_DIR");const bool had=qEnvironmentVariableIsSet("LUMINA_EXPORT_DATA_DIR");qputenv("LUMINA_EXPORT_DATA_DIR",root.path().toUtf8());
        const auto restore=qScopeGuard([&]{if(had)qputenv("LUMINA_EXPORT_DATA_DIR",old);else qunsetenv("LUMINA_EXPORT_DATA_DIR");});
        Export::Queue queue(root.filePath("queue.json"));const auto red=asset(root.filePath("red.jpg")),blue=asset(root.filePath("blue.jpg"));auto gate=std::make_shared<Gate>();
        ExportDialog::PreviewReader reader=[gate](const QString &path,double,double,const CancellationToken &,const QString &hash)->FrameResult{
            const bool first=path.contains("red");auto *frame=new SourceFrame;frame->path=path;frame->sha256=hash;frame->originalSize={32,24};frame->linear=QImage(32,24,QImage::Format_RGBA32FPx4);frame->linear.fill(first?Qt::red:Qt::blue);
            // Hold after r.linear has been computed, at the source-frame release
            // boundary. Cancellation then leaves a non-null stale linear cache,
            // which the source generation/index guards must reject.
            return {std::shared_ptr<const SourceFrame>(frame,[gate,first](const SourceFrame *p){if(first){gate->entered.release();gate->release.tryAcquire(1,3000);}delete p;}),{}};
        };
        auto dialog=std::make_unique<ExportDialog>(QList<Export::Asset>{asset(root.filePath("unused.jpg")),red},1,&queue,nullptr,reader);QSignalSpy accepted(dialog.get(),&ExportDialog::previewSourceCached);const auto release=qScopeGuard([&]{gate->release.release(4);});dialog->show();
        QTRY_VERIFY_WITH_TIMEOUT(gate->entered.available()>0,5000);gate->entered.acquire();dialog->replaceAssets({blue},0);gate->release.release();
        const auto image=[&]{return dialog->findChild<Canvas *>("export-preview")->presenter()->canonicalImage();};
        QTRY_VERIFY_WITH_TIMEOUT(!image().isNull(),10000);const auto pixel=image().pixelColor(16,12);QVERIFY2(pixel.blue()>pixel.red(),qPrintable(pixel.name()));QCOMPARE(accepted.size(),1);QCOMPARE(accepted[0][0].toString(),blue.path);QCOMPARE(accepted[0][1].toString(),blue.hash);
        dialog.reset();
    }
};
QTEST_MAIN(ExportPreviewLifecycleTests)
#include "export_preview_lifecycle_tests.moc"
