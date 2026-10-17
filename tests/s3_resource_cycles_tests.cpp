#include "app/window.h"
#include "app/edit_loader.h"
#include "app/canvas.h"
#include "app/display_presenter.h"
#include "inspector_navigation_support.h"
#include <QtTest>
#include <QCryptographicHash>
#include <QDoubleSpinBox>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPushButton>
#include <QStandardPaths>
#include <QTabWidget>
#include <cmath>

namespace {
bool newJson(const QString &path,const QJsonObject &object){
    QFile file(path);if(!file.open(QIODevice::WriteOnly|QIODevice::NewOnly))return false;
    const auto bytes=QJsonDocument(object).toJson();return file.write(bytes)==bytes.size()&&file.flush();
}
QByteArray sourceHash(const QString &path){
    QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);if(!hash.addData(&file))return {};return hash.result();
}
}
class S3ResourceCyclesTests:public QObject {
    Q_OBJECT
private slots:
    void resourceCycles(){
        const auto requested=qEnvironmentVariable("LUMINA_RESOURCE_CYCLES");
        if(requested.isEmpty()||requested=="0"||qEnvironmentVariableIsEmpty("LUMINA_REAL_SAMPLES"))QSKIP("Explicit real-sample resource cycles only");
        bool ok=false;const int cycles=requested.toInt(&ok);QVERIFY(ok&&cycles>=1&&cycles<=5);
        QStringList paths;const auto directory=QDir::fromNativeSeparators(qEnvironmentVariable("LUMINA_REAL_SAMPLES"));
        for(const auto &name:{"4X1A0705","4X1A9413","4X1A9579","4X1A9817","4X1A9827","4X1A9954"})for(const auto &extension:{"CR3","JPG"}){
            const auto path=QFileInfo(directory+"/"+name+"."+extension).canonicalFilePath();QVERIFY(!path.isEmpty());paths.append(path);
        }
        const auto startedUnixMs=QDateTime::currentMSecsSinceEpoch();QElapsedTimer clock;clock.start();QJsonArray entries;
        const auto record=[&](const char *phase,int cycle){entries.append(QJsonObject{{"phase",phase},{"cycle",cycle},{"elapsedMs",double(clock.elapsed())}});};
        const auto control=qEnvironmentVariable("LUMINA_RESOURCE_CONTROL");
        if(!control.isEmpty()){
            const auto rawRoot=QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+"/raw-worker";QVERIFY(QDir().mkpath(rawRoot));
            QVERIFY(newJson(control,{{"pid",double(QCoreApplication::applicationPid())},{"tempRoot",QDir::tempPath()},{"rawCacheRoot",rawRoot},{"startedUnixMs",double(startedUnixMs)}}));QTest::qWait(1500);
        }
        QVector<QByteArray> hashes;for(const auto &path:paths){const auto hash=sourceHash(path);QCOMPARE(hash.size(),32);hashes.append(hash);}
        record("start",-1);
        for(int cycle=0;cycle<cycles;++cycle){
            record("opening",cycle);
            {
                // All heavy holders precede Window, so its teardown happens
                // while observers are still alive. No image escapes this block.
                EditResult last;int prepared=0,ready=0;qint64 paintedKey=0;
                Window window(nullptr,QStringLiteral(""),QStringLiteral(""));window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));QVERIFY(openAllInspectorSections(window));
                auto *loader=window.findChild<EditLoader *>();auto *canvas=window.findChild<Canvas *>("photo-canvas");QVERIFY(loader);QVERIFY(canvas);
                connect(loader,&EditLoader::prefetched,&window,[&](const EditResult &r){QVERIFY2(r.error.isEmpty(),qPrintable(r.error));++prepared;});
                connect(loader,&EditLoader::ready,&window,[&](const EditResult &r){last=r;++ready;});
                connect(canvas,&Canvas::canonicalFramePainted,&window,[&](qint64 key){paintedKey=key;});
                window.addFiles(paths);QTRY_COMPARE_WITH_TIMEOUT(prepared,12,240000);record("prepared",cycle);
                ready=0;window.activate(1);QTRY_VERIFY_WITH_TIMEOUT(ready>0&&last.path==paths[1],90000);QVERIFY2(last.error.isEmpty(),qPrintable(last.error));QVERIFY(!last.sourceRebuilt);
                ready=0;window.activate(0);QTRY_VERIFY_WITH_TIMEOUT(ready>0&&last.path==paths[0],90000);QVERIFY2(last.error.isEmpty(),qPrintable(last.error));
                Recipe recipe;recipe.texture=.4;recipe.clarity=.3;recipe.perspective={.03,-.02};ready=0;window.applyRecipe(recipe);
                QTRY_VERIFY_WITH_TIMEOUT(ready>0&&last.recipe==recipe&&last.fullGeometry,90000);QVERIFY2(last.error.isEmpty(),qPrintable(last.error));record("full_geometry",cycle);
                auto *tabs=window.findChild<QTabWidget *>("adjustment-scope-tabs");auto *brush=window.findChild<QPushButton *>("local-new-brush");auto *exposure=window.findChild<QDoubleSpinBox *>("local-exposure");QVERIFY(tabs);QVERIFY(brush);QVERIFY(exposure);
                tabs->setCurrentIndex(1);brush->click();QTRY_VERIFY_WITH_TIMEOUT(canvas->brushMode(),90000);
                // Controller/resource exercise; pointer hit testing is covered
                // by the separate native brush tests, not claimed here.
                canvas->brushPressed({.3,.4},false);canvas->brushMoved({.6,.5});canvas->brushFinished(false);
                exposure->setValue(.3);QVERIFY(QMetaObject::invokeMethod(exposure,"editingFinished",Qt::DirectConnection));
                const auto complete=[&]{
                    const auto doc=window.currentPhotoDocument();
                    return doc&&!doc->layers.isEmpty()&&!doc->layers[0].mask.components.isEmpty()&&std::abs(doc->layers[0].parameters.exposure-.3)<1e-9&&
                        last.document&&last.document->layers==doc->layers&&last.recipe==doc->global&&last.path==paths[0]&&last.error.isEmpty()&&!last.image.isNull()&&last.fullGeometry&&
                        paintedKey==last.image.cacheKey()&&canvas->presenter()->canonicalImage().cacheKey()==paintedKey;
                };
                QTRY_VERIFY_WITH_TIMEOUT(complete(),90000);QCOMPARE(qMax(last.image.width(),last.image.height()),3840);
                entries.append(QJsonObject{{"phase","local_ready"},{"cycle",cycle},{"elapsedMs",double(clock.elapsed())},{"longEdge4k",true},{"fullGeometry",true},{"nonemptyBrush",true},{"paintVerified",true}});
                QTest::qWait(1000);
            }
            record("destroyed",cycle);QTest::qWait(1000);record("released",cycle);
        }
        for(int i=0;i<paths.size();++i)QCOMPARE(sourceHash(paths[i]),hashes[i]);record("sources_verified",cycles);
        const auto report=qEnvironmentVariable("LUMINA_RESOURCE_CYCLES_REPORT");if(!report.isEmpty())QVERIFY(newJson(report,{{"cycles",cycles},{"entries",entries},{"sourcesUnchanged",true},{"startedUnixMs",double(startedUnixMs)}}));
    }
};
QTEST_MAIN(S3ResourceCyclesTests)
#include "s3_resource_cycles_tests.moc"
