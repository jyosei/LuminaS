#include "app/window.h"
#include "app/edit_loader.h"
#include "app/canvas.h"
#include "app/display_presenter.h"
#include "inspector_navigation_support.h"
#include <QtTest>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QImage>
#include <QColorSpace>
#include <QJsonArray>
#include <QPushButton>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>
#include <cmath>
#include <memory>

namespace {
bool newJson(const QString &path,const QJsonObject &object){
    QFile file(path);if(!file.open(QIODevice::WriteOnly|QIODevice::NewOnly))return false;
    const auto bytes=QJsonDocument(object).toJson();return file.write(bytes)==bytes.size()&&file.flush();
}
bool replaceJson(const QString &path,const QJsonObject &object){
    // Only called after this test created the same path with NewOnly.
    QSaveFile file(path);file.setDirectWriteFallback(false);if(!file.open(QIODevice::WriteOnly))return false;
    const auto bytes=QJsonDocument(object).toJson();
    return file.write(bytes)==bytes.size()&&file.commit();
}
QByteArray sourceHash(const QString &path){
    QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);if(!hash.addData(&file))return {};return hash.result();
}
// Creates one synthetic JPEG in `dir` and returns its path; the QImage is
// released when this helper returns.
QString makeSyntheticJpeg(const QString &dir){
    QImage image(4500,3000,QImage::Format_RGB32);
    if(image.isNull())return {};
    image.setColorSpace(QColorSpace::SRgb);
    for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x)
        image.setPixel(x,y,qRgb((x*255)/image.width(),(y*255)/image.height(),((x+y)*255)/(image.width()+image.height())));
    const auto path=dir+"/release-stress-synthetic.jpg";
    if(!image.save(path,"JPG",92))return {};
    return QFileInfo(path).canonicalFilePath();
}
}
class S3ReleaseStressTests:public QObject {
    Q_OBJECT
private slots:
    void releaseStress(){
        if(qEnvironmentVariable("LUMINA_RELEASE_STRESS")!="1")QSKIP("Explicit LUMINA_RELEASE_STRESS=1 only");
        const auto requested=qEnvironmentVariable("LUMINA_RELEASE_CYCLES");
        int cycles=100;
        if(!requested.isEmpty()){bool ok=false;cycles=requested.toInt(&ok);QVERIFY(ok&&cycles>=1&&cycles<=100);}
        QTemporaryDir temporary;QVERIFY(temporary.isValid());
        const auto synthetic=makeSyntheticJpeg(temporary.path());QVERIFY(!synthetic.isEmpty());QVERIFY(QFileInfo::exists(synthetic));
        QString raw;
        const auto rawRequested=qEnvironmentVariable("LUMINA_RELEASE_RAW_SAMPLE");
        if(!rawRequested.isEmpty()){
            raw=QFileInfo(QDir::fromNativeSeparators(rawRequested)).canonicalFilePath();
            QVERIFY2(!raw.isEmpty()&&QFileInfo(raw).isFile(),"LUMINA_RELEASE_RAW_SAMPLE must be an existing canonical file");
            QVERIFY(QFileInfo(raw).suffix().compare("CR3",Qt::CaseInsensitive)==0);
        }
        const auto startedUnixMs=QDateTime::currentMSecsSinceEpoch();QElapsedTimer clock;clock.start();
        const auto report=qEnvironmentVariable("LUMINA_RELEASE_REPORT");
        const auto runId=QString::number(startedUnixMs)+"-"+QString::number(QCoreApplication::applicationPid());
        QJsonObject state{{"runId",runId},{"startedUnixMs",double(startedUnixMs)},{"targetCycles",cycles},{"completedCycles",0},{"rawCount",0},{"syntheticCount",0},{"entries",QJsonArray()},{"sourcesUnchanged",false}};
        const auto writeReport=[&]{
            if(report.isEmpty())return true;
            state["elapsedMs"]=double(clock.elapsed());
            return replaceJson(report,state);
        };
        const auto control=qEnvironmentVariable("LUMINA_RESOURCE_CONTROL");
        if(!control.isEmpty()){
            const auto rawRoot=QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+"/raw-worker";QVERIFY(QDir().mkpath(rawRoot));
            QVERIFY(newJson(control,{{"pid",double(QCoreApplication::applicationPid())},{"tempRoot",QDir::tempPath()},{"rawCacheRoot",rawRoot},{"startedUnixMs",double(startedUnixMs)}}));QTest::qWait(1500);
        }
        if(!report.isEmpty())QVERIFY2(newJson(report,state),"Could not create a new release report path");
        const auto syntheticHash=sourceHash(synthetic);QCOMPARE(syntheticHash.size(),32);
        QByteArray rawHash;if(!raw.isEmpty()){rawHash=sourceHash(raw);QCOMPARE(rawHash.size(),32);}
        QJsonArray entries;int syntheticCount=0,rawCount=0;
        for(int cycle=0;cycle<cycles;++cycle){
            const bool useRaw=!raw.isEmpty()&&(cycle+1)%10==0;
            const auto path=useRaw?raw:synthetic;
            QJsonObject entry{{"cycle",cycle},{"source",useRaw?"raw":"synthetic"}};
            std::weak_ptr<const SourceFrame> fullGeometryWeak;
            {
                // Every heavy holder lives in this block and is declared before
                // Window, so teardown happens while the observer is alive.
                EditResult last;int prepared=0,ready=0;qint64 paintedKey=0;
                Window window(nullptr,QStringLiteral(""),QStringLiteral(""));window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));
                QVERIFY(openAllInspectorSections(window));
                auto *loader=window.findChild<EditLoader *>();auto *canvas=window.findChild<Canvas *>("photo-canvas");QVERIFY(loader);QVERIFY(canvas);
                connect(loader,&EditLoader::prefetched,&window,[&](const EditResult &r){QVERIFY2(r.error.isEmpty(),qPrintable(r.error));++prepared;});
                connect(loader,&EditLoader::ready,&window,[&](const EditResult &r){last=r;++ready;});
                connect(canvas,&Canvas::canonicalFramePainted,&window,[&](qint64 key){paintedKey=key;});
                window.addFiles({path});QTRY_COMPARE_WITH_TIMEOUT(prepared,1,240000);QTRY_VERIFY_WITH_TIMEOUT(ready>0,90000);
                entry["prefetchReadyMs"]=double(clock.elapsed());
                QVERIFY2(last.error.isEmpty(),qPrintable(last.error));QCOMPARE(last.path,path);
                Recipe recipe;recipe.texture=.2;recipe.clarity=.1;recipe.perspective={.03,-.02};ready=0;window.applyRecipe(recipe);
                QTRY_VERIFY_WITH_TIMEOUT(ready>0&&last.recipe==recipe&&last.fullGeometry,90000);
                QVERIFY2(last.error.isEmpty(),qPrintable(last.error));QCOMPARE(last.path,path);
                auto *tabs=window.findChild<QTabWidget *>("adjustment-scope-tabs");auto *brush=window.findChild<QPushButton *>("local-new-brush");auto *exposure=window.findChild<QDoubleSpinBox *>("local-exposure");QVERIFY(tabs);QVERIFY(brush);QVERIFY(exposure);
                tabs->setCurrentIndex(1);brush->click();QTRY_VERIFY_WITH_TIMEOUT(canvas->brushMode(),90000);
                // Controller/resource exercise; pointer hit testing is covered
                // by the separate native brush tests, not claimed here.
                canvas->brushPressed({.3,.4},false);canvas->brushMoved({.6,.5});canvas->brushFinished(false);
                exposure->setValue(.3);QVERIFY(QMetaObject::invokeMethod(exposure,"editingFinished",Qt::DirectConnection));
                const auto complete=[&]{
                    const auto doc=window.currentPhotoDocument();
                    return doc&&!doc->layers.isEmpty()&&!doc->layers[0].mask.components.isEmpty()&&std::abs(doc->layers[0].parameters.exposure-.3)<1e-9&&
                        last.document&&last.document->layers==doc->layers&&last.recipe==doc->global&&last.path==path&&last.error.isEmpty()&&!last.image.isNull()&&last.fullGeometry&&
                        paintedKey==last.image.cacheKey()&&canvas->presenter()->canonicalImage().cacheKey()==paintedKey;
                };
                QTRY_VERIFY_WITH_TIMEOUT(complete(),90000);
                QCOMPARE(qMax(last.image.width(),last.image.height()),3840);
                QVERIFY(last.fullGeometry);fullGeometryWeak=last.fullGeometry;
                entry["elapsedMs"]=double(clock.elapsed());entry["longEdge4k"]=true;entry["fullGeometry"]=true;entry["nonemptyBrush"]=true;entry["paintVerified"]=true;
                QTest::qWait(1000);
            }
            QTRY_VERIFY_WITH_TIMEOUT(fullGeometryWeak.expired(),5000);
            QTest::qWait(1000);
            entry["releasedMs"]=double(clock.elapsed());entry["weakExpired"]=true;
            useRaw?++rawCount:++syntheticCount;entries.append(entry);
            state["completedCycles"]=cycle+1;state["rawCount"]=rawCount;state["syntheticCount"]=syntheticCount;state["entries"]=entries;
            QVERIFY2(writeReport(),"Could not atomically update the release report");
            if(report.isEmpty()&&(cycle+1)%10==0)qInfo("release stress: %d/%d cycles (raw %d, synthetic %d, %lld ms)",cycle+1,cycles,rawCount,syntheticCount,clock.elapsed());
        }
        QCOMPARE(sourceHash(synthetic),syntheticHash);
        if(!raw.isEmpty())QCOMPARE(sourceHash(raw),rawHash);
        state["completedCycles"]=cycles;state["rawCount"]=rawCount;state["syntheticCount"]=syntheticCount;state["entries"]=entries;state["sourcesUnchanged"]=true;
        QVERIFY2(writeReport(),"Could not finalize the release report");
        qInfo("release stress: %d cycles complete (raw %d, synthetic %d, sourcesUnchanged true)",cycles,rawCount,syntheticCount);
    }
};
QTEST_MAIN(S3ReleaseStressTests)
#include "s3_release_stress_tests.moc"
