#include "app/display_queue.h"
#include <QtTest>
#include <QSemaphore>
#include <QColorSpace>
#include <atomic>
#include <memory>
struct Gate {
    QSemaphore entered,release;
    std::atomic<int> calls{0},active{0},maximum{0};
    std::atomic<bool> cancelled{false};bool ignoreCancel=false;
};
static DisplayQueue::Transform operation(const std::shared_ptr<Gate> &gate){
    return [gate](const QImage &image,const QByteArray &,const std::shared_ptr<std::atomic<bool>> &cancel){
        const int call=++gate->calls;const int running=++gate->active;
        gate->maximum.store(std::max(gate->maximum.load(),running));
        if(call==1){gate->entered.release();while(!gate->release.tryAcquire(1,10)){
            if(!gate->ignoreCancel&&cancel->load()){gate->cancelled=true;--gate->active;return DisplayTransformResult{{},"cancelled"};}
        }}
        --gate->active;return DisplayTransformResult{image,{}};
    };
}
static QImage sample(int red){QImage image(1,1,QImage::Format_RGBA8888);image.fill(QColor(red,20,30,128));image.setColorSpace(QColorSpace::SRgb);return image;}
static DisplayFrame frame(const QSignalSpy &spy,int index){return spy[index][0].value<DisplayFrame>();}
class DisplayQueueTests:public QObject {
    Q_OBJECT
private slots:
    void latestPendingKeepsIntermediate();void obsoleteContextCannotCommit();void invalidateReusedContext();
    void destructionCancels();void reentrantRequestReplacesPending();
};
void DisplayQueueTests::latestPendingKeepsIntermediate(){
    auto gate=std::make_shared<Gate>();DisplayQueue queue(nullptr,operation(gate));QSignalSpy ready(&queue,&DisplayQueue::frameReady);
    const auto a=sample(1),unchanged=a;queue.request(a,{},7);QTRY_VERIFY(gate->entered.available()>0);gate->entered.acquire();
    queue.request(sample(2),{},7);queue.request(sample(3),{},7);QVERIFY(queue.hasPending());gate->release.release();
    QTRY_COMPARE(ready.count(),2);QCOMPARE(frame(ready,0).image.pixelColor(0,0).red(),1);QCOMPARE(frame(ready,1).image.pixelColor(0,0).red(),3);
    QVERIFY(frame(ready,1).serial>frame(ready,0).serial);QCOMPARE(gate->calls.load(),2);QCOMPARE(gate->maximum.load(),1);QCOMPARE(a,unchanged);
}
void DisplayQueueTests::obsoleteContextCannotCommit(){
    auto gate=std::make_shared<Gate>();gate->ignoreCancel=true;DisplayQueue queue(nullptr,operation(gate));QSignalSpy ready(&queue,&DisplayQueue::frameReady);
    queue.request(sample(1),{},7);QTRY_VERIFY(gate->entered.available()>0);gate->entered.acquire();queue.request(sample(2),{},8);gate->release.release();
    QTRY_COMPARE(ready.count(),1);QCOMPARE(frame(ready,0).context,quint64(8));QCOMPARE(frame(ready,0).image.pixelColor(0,0).red(),2);QCOMPARE(gate->maximum.load(),1);
}
void DisplayQueueTests::invalidateReusedContext(){
    auto gate=std::make_shared<Gate>();gate->ignoreCancel=true;DisplayQueue queue(nullptr,operation(gate));QSignalSpy ready(&queue,&DisplayQueue::frameReady);
    queue.request(sample(1),{},7);QTRY_VERIFY(gate->entered.available()>0);gate->entered.acquire();queue.invalidate();queue.request(sample(2),{},7);gate->release.release();
    QTRY_COMPARE(ready.count(),1);QCOMPARE(frame(ready,0).context,quint64(7));QCOMPARE(frame(ready,0).image.pixelColor(0,0).red(),2);
}
void DisplayQueueTests::destructionCancels(){
    auto gate=std::make_shared<Gate>();auto queue=std::make_unique<DisplayQueue>(nullptr,operation(gate));
    queue->request(sample(1),{},7);QTRY_VERIFY(gate->entered.available()>0);gate->entered.acquire();QElapsedTimer timer;timer.start();queue.reset();
    QVERIFY(gate->cancelled.load());QCOMPARE(gate->active.load(),0);QVERIFY(timer.elapsed()<1000);
}
void DisplayQueueTests::reentrantRequestReplacesPending(){
    auto gate=std::make_shared<Gate>();DisplayQueue queue(nullptr,operation(gate));QSignalSpy ready(&queue,&DisplayQueue::frameReady);
    connect(&queue,&DisplayQueue::frameReady,&queue,[&](DisplayFrame result){if(result.image.pixelColor(0,0).red()==1)queue.request(sample(3),{},7);});
    queue.request(sample(1),{},7);QTRY_VERIFY(gate->entered.available()>0);gate->entered.acquire();queue.request(sample(2),{},7);gate->release.release();
    QTRY_COMPARE(ready.count(),2);QCOMPARE(frame(ready,1).image.pixelColor(0,0).red(),3);QCOMPARE(gate->calls.load(),2);QCOMPARE(gate->maximum.load(),1);
}
QTEST_MAIN(DisplayQueueTests)
#include "display_queue_tests.moc"
