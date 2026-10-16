#include <QtTest>
#include <QColorSpace>
#include <QScopeGuard>
#include <QSemaphore>
#include <QSignalSpy>
#include <atomic>
#include "app/display_presenter.h"
#include "app/canvas.h"

namespace {
QImage image(QColor color){
    QImage result(64,32,QImage::Format_RGBA8888);result.fill(color);
    result.setColorSpace(QColorSpace(QColorSpace::SRgb));return result;
}
}
class DisplayProvenanceTests:public QObject {
    Q_OBJECT
private slots:
    void intermediateFrameKeepsItsActualSourceKey(){
        QSemaphore entered,release;std::atomic<int> calls{0};
        DisplayPresenter presenter([&](const QImage &source,const QByteArray &,const auto &)->DisplayTransformResult{
            if(++calls==1){entered.release();if(!release.tryAcquire(1,2000))return {{},"test barrier timeout"};}
            return {source,{}};
        });
        const auto cleanup=qScopeGuard([&]{release.release(4);});
        presenter.setMonitorProfile({},"provenance-test");
        QSignalSpy ready(&presenter,&DisplayPresenter::imageReadyTagged);
        const auto a=image(Qt::red),b=image(Qt::blue);const auto keyA=a.cacheKey(),keyB=b.cacheKey();
        presenter.setImage(a,"one-generation");QVERIFY(entered.tryAcquire(1,2000));
        presenter.setImage(b,"one-generation");QCOMPARE(presenter.canonicalImage().cacheKey(),keyB);
        release.release();QTRY_COMPARE_WITH_TIMEOUT(ready.size(),2,5000);
        QCOMPARE(ready[0][2].toLongLong(),keyA);QCOMPARE(ready[1][2].toLongLong(),keyB);
        QCOMPARE(ready[0][0].value<QImage>(),a);QCOMPARE(ready[1][0].value<QImage>(),b);
    }
    void fallbackReportsThePixelsActuallyDelivered(){
        QSemaphore entered,release;std::atomic<int> calls{0};
        DisplayPresenter presenter([&](const QImage &source,const QByteArray &,const auto &)->DisplayTransformResult{
            if(++calls==1){entered.release();if(!release.tryAcquire(1,2000))return {{},"test barrier timeout"};
                return {{},"synthetic conversion failure"};}
            return {source,{}};
        });
        const auto cleanup=qScopeGuard([&]{release.release(4);});
        presenter.setMonitorProfile({},"provenance-test");
        QSignalSpy ready(&presenter,&DisplayPresenter::imageReadyTagged);
        const auto a=image(Qt::red),b=image(Qt::blue);const auto keyB=b.cacheKey();
        presenter.setImage(a,"same");QVERIFY(entered.tryAcquire(1,2000));presenter.setImage(b,"same");
        release.release();QTRY_COMPARE_WITH_TIMEOUT(ready.size(),2,5000);
        QCOMPARE(ready[0][2].toLongLong(),keyB);QCOMPARE(ready[1][2].toLongLong(),keyB);
        QCOMPARE(ready[0][0].value<QImage>(),b);
    }
    void photoAndMaskPaintsHaveIndependentProvenance(){
        Canvas canvas;canvas.resize(400,300);canvas.setMonitorProfile({},"provenance-test");
        QSignalSpy photo(&canvas,&Canvas::canonicalFramePainted),maskPainted(&canvas,&Canvas::localMaskPainted);
        canvas.show();QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        const auto a=image(Qt::darkGray);const auto keyA=a.cacheKey();canvas.setImage(a,"same");
        QTRY_COMPARE_WITH_TIMEOUT(photo.size(),1,5000);QCOMPARE(photo[0][0].toLongLong(),keyA);
        QImage mask(a.size(),QImage::Format_Indexed8);mask.fill(255);const auto maskKey=mask.cacheKey();
        canvas.setLocalMask(mask);QTRY_COMPARE_WITH_TIMEOUT(maskPainted.size(),1,5000);
        QCOMPARE(maskPainted[0][0].toLongLong(),maskKey);QCOMPARE(photo.size(),1);
        const auto b=image(Qt::darkBlue);const auto keyB=b.cacheKey();canvas.setImage(b,"same");
        QTRY_COMPARE_WITH_TIMEOUT(photo.size(),2,5000);QCOMPARE(photo[1][0].toLongLong(),keyB);
        QCOMPARE(maskPainted.size(),1); // repaint of an unchanged mask is not a new mask delivery
    }
};
QTEST_MAIN(DisplayProvenanceTests)
#include "display_provenance_tests.moc"
