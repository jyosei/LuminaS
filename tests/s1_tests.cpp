#include "raw/raw_decoder.h"
#include "raw/packed_rgb16.h"
#include "io/preview_size.h"
#include "app/window.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QLabel>
#include <QJsonDocument>
#include <QRegularExpression>
class S1Tests : public QObject {
    Q_OBJECT
private slots:
    void packedRowsDoNotOverwriteUnreadSamples() {
        for(const int width : {1,2,3,17,4640}) {
            QVector<quint16> row(width*4+2,0xA55A);
            for(int x=0;x<width;++x) for(int c=0;c<3;++c) row[1+x*3+c]=quint16((x*173+c*1023)%65536);
            expandPackedRgb16(row.data()+1,width);
            QCOMPARE(row.first(),quint16(0xA55A)); QCOMPARE(row.last(),quint16(0xA55A));
            for(int x=0;x<width;++x) {
                for(int c=0;c<3;++c) QCOMPARE(row[1+x*4+c],quint16((x*173+c*1023)%65536));
                QCOMPARE(row[1+x*4+3],quint16(65535));
            }
        }
    }
    void sizePolicyAnd45Mp() {
        QCOMPARE(previewSize(QSize(6960,4640)),QSize(3840,2560));
        QCOMPARE(previewSize(QSize(4640,6960)),QSize(2560,3840));
        QCOMPARE(previewSize(QSize(1600,900)),QSize(1600,900));
        QCOMPARE(previewSize(QSize(9000,5000)),QSize(3840,2133));
        QVERIFY(acceptedImageSize(QSize(9000,5000)));
        QVERIFY(!acceptedImageSize(QSize(9001,5000)));
        QVERIFY(!acceptedImageSize(QSize(INT_MAX,INT_MAX)));
        QVERIFY(!acceptedImageSize(QSize(0,10)));
    }
    void corruptRawAndMissing() {
        QTemporaryDir temp; QVERIFY(temp.isValid());
        const auto bad=temp.path()+QStringLiteral("/坏文件.CR3");
        QFile file(bad); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("invalid cr3"); file.close();
        QVERIFY(!developRaw(bad).error.isEmpty());
        QVERIFY(!developRaw(temp.path()+"/missing.CR3").error.isEmpty());
    }
    void realRawPreviewAndStaleJobs() {
        const auto directory=qEnvironmentVariable("LUMINA_REAL_SAMPLES");
        if (directory.isEmpty()) QSKIP("Real CR3 required; no quality claim without samples.");
        const auto originalRaw=QDir(directory).absoluteFilePath("4X1A9817.CR3");
        QTemporaryDir unicodeDirectory(QDir::tempPath()+QStringLiteral("/琉明RAW测试-XXXXXX"));
        QVERIFY(unicodeDirectory.isValid());
        const auto raw=unicodeDirectory.path()+QStringLiteral("/中文样片.CR3");
        QVERIFY(QFile::copy(originalRaw,raw));
        const auto jpg=QDir(directory).absoluteFilePath("4X1A9817.JPG");
        Window window(nullptr,QStringLiteral("")); window.show(); QSignalSpy spy(&window,&Window::previewChanged);
        window.addFiles({raw,jpg});
        QTRY_COMPARE_WITH_TIMEOUT(window.displayedPath(),raw,120000);
        const auto labels=window.findChildren<QLabel *>(); bool sizeShown=false;
        for (auto *label:labels) if(label->text().contains("2560 × 3840")) sizeShown=true;
        QVERIFY(sizeShown);
        window.mark(Pick::Keep);
        const auto shot=qEnvironmentVariable("LUMINA_RAW_SCREENSHOT");
        if(!shot.isEmpty()) { QTest::qWait(100); QVERIFY(window.grab().save(shot)); }
        // Start a real RAW job and steer to JPEG while it is still in flight.
        window.activate(0); QTest::qWait(50); spy.clear(); window.activate(1);
        QTRY_COMPARE_WITH_TIMEOUT(window.displayedPath(),jpg,120000);
        QTRY_COMPARE_WITH_TIMEOUT(spy.size(),1,90000); QCOMPARE(window.pickAt(0),Pick::Keep);
    }
    void expandedLocalSetWindow() {
        const auto directory=qEnvironmentVariable("LUMINA_REAL_SAMPLES");
        if(directory.isEmpty()) QSKIP("Local photo set required.");
        QStringList paths;
        for(const auto &file:QDir(directory).entryInfoList(QDir::Files,QDir::Name)) {
            const auto suffix=file.suffix().toLower();
            if(suffix=="cr3" || suffix=="jpg" || suffix=="jpeg") paths.append(file.absoluteFilePath());
        }
        QVERIFY(paths.size()>=2);
        Window window(nullptr,QStringLiteral("")); window.show(); window.addFiles(paths);
        const QRegularExpression dimensions(QStringLiteral("预览 (\\d+) × (\\d+)"));
        for(int i=0;i<paths.size();++i) {
            window.activate(i);
            QTRY_COMPARE_WITH_TIMEOUT(window.displayedPath(),paths[i],120000);
            bool fourK=false;
            for(auto *label:window.findChildren<QLabel *>()) {
                const auto match=dimensions.match(label->text());
                if(match.hasMatch()) fourK=qMax(match.captured(1).toInt(),match.captured(2).toInt())==3840;
            }
            QVERIFY2(fourK,qPrintable(paths[i]));
            window.mark(Pick::Keep);
        }
        for(int i=0;i<paths.size();++i) QCOMPARE(window.pickAt(i),Pick::Keep);
        window.filter(2);
        QCOMPARE(window.filmstrip()->count(),paths.size());
        window.activate(0); QTest::qWait(50);
        QSignalSpy spy(&window,&Window::previewChanged);
        for(int i=0;i<30;++i) window.activate(i%paths.size());
        window.activate(paths.size()-1);
        QTRY_COMPARE_WITH_TIMEOUT(window.displayedPath(),paths.last(),120000);
        QTRY_COMPARE_WITH_TIMEOUT(spy.size(),1,90000);
        for(int i=0;i<paths.size();++i) QCOMPARE(window.pickAt(i),Pick::Keep);
        const auto screenshot=qEnvironmentVariable("LUMINA_SET_SCREENSHOT");
        if(!screenshot.isEmpty()) { QTest::qWait(100); QVERIFY(window.grab().save(screenshot)); }
    }
};
QTEST_MAIN(S1Tests)
#include "s1_tests.moc"
