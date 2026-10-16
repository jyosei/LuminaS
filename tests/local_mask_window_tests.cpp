#include <QtTest>
#include <QTemporaryDir>
#include <QTabWidget>
#include <QSignalSpy>
#include <QCryptographicHash>
#include <QMouseEvent>
#include <QScrollArea>
#include <functional>
#include "app/window.h"
#include "app/local_mask_options.h"
#include "app/display_presenter.h"
#include "io/source_reader.h"
#include "render/local_renderer.h"
using namespace LocalEdit;
namespace {
bool waitFor(const std::function<bool()> &test){QElapsedTimer t;t.start();while(!test()){if(t.elapsed()>90000)return false;QTest::qWait(10);}return true;}
bool painted(Window &w,Canvas *c,const QSignalSpy &paint){
    const auto doc=w.currentPhotoDocument();const auto r=qvariant_cast<EditResult>(w.property("mask-last-result"));
    return doc&&!paint.isEmpty()&&r.path==doc->asset.sourcePath&&r.error.isEmpty()&&!r.image.isNull()&&r.recipe==doc->global&&r.document&&r.document->layers==doc->layers&&
        paint.last()[0].toLongLong()==r.image.cacheKey()&&r.image.cacheKey()==c->presenter()->canonicalImage().cacheKey();
}
QPoint point(Canvas *c,QPointF uv){const auto s=c->presenter()->canonicalImage().size();return {int(std::lround(c->width()/2.+(uv.x()-.5)*s.width()*c->zoom())),int(std::lround(c->height()/2.+(uv.y()-.5)*s.height()*c->zoom()))};}
void move(Canvas *c,QPoint p){QMouseEvent e(QEvent::MouseMove,QPointF(p),c->mapToGlobal(p),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(c,&e);}
QByteArray pixels(const QImage &i){return QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char *>(i.constBits()),i.sizeInBytes()),QCryptographicHash::Sha256).toHex();}
bool setup(Window &w,Canvas *c,QSignalSpy &paint,QTemporaryDir &dir){
    QObject::connect(w.findChild<EditLoader *>(),&EditLoader::ready,&w,[&w](const EditResult &r){w.setProperty("mask-last-result",QVariant::fromValue(r));});
    if(!dir.isValid())return false;auto path=qEnvironmentVariable("LUMINA_MASK_SAMPLE");
    if(path.isEmpty()){path=dir.filePath("mask.jpg");QImage image(96,64,QImage::Format_RGB32);image.fill(QColor(50,80,120));if(!image.save(path,"JPEG",98))return false;}
    c->setMonitorProfile({},"mask-native-test");w.show();if(!QTest::qWaitForWindowExposed(&w))return false;w.addFiles({path});
    if(!waitFor([&]{return w.neutralPickerAvailable();}))return false;
    w.findChild<QTabWidget *>("adjustment-scope-tabs")->setCurrentIndex(1);w.findChild<QPushButton *>("local-new-brush")->click();
    return waitFor([&]{return painted(w,c,paint)&&c->brushMode();});
}
void spin(QDoubleSpinBox *s,double value){s->setValue(value);QMetaObject::invokeMethod(s,"editingFinished",Qt::DirectConnection);}
}
class LocalMaskWindowTests:public QObject {
    Q_OBJECT
private slots:
    void featherInvertHistoryAndDisplayIsolation(){
        QTemporaryDir dir;Window w(nullptr,QStringLiteral(""));auto *c=w.findChild<Canvas *>("photo-canvas");QVERIFY(c);QSignalSpy paint(c,&Canvas::canonicalFramePainted);QVERIFY(setup(w,c,paint,dir));
        w.findChild<QPushButton *>("local-gradient-radial")->click();
        const auto from=point(c,{.5,.5}),to=point(c,{.7,.5});QTest::mousePress(c,Qt::LeftButton,Qt::NoModifier,from);move(c,to);QTest::mouseRelease(c,Qt::LeftButton,Qt::NoModifier,to);
        spin(w.findChild<QDoubleSpinBox *>("local-exposure"),.7);paint.clear();QVERIFY(waitFor([&]{return painted(w,c,paint);}));
        const auto sharp=w.currentPhotoDocument();const auto sharpHash=pixels(c->presenter()->canonicalImage());
        auto *feather=w.findChild<QDoubleSpinBox *>("local-mask-feather");auto *slider=w.findChild<QSlider *>("slider-local-mask-feather");auto *inverted=w.findChild<QCheckBox *>("local-mask-invert");QVERIFY(feather);QVERIFY(slider);QVERIFY(inverted);
        paint.clear();slider->setSliderDown(true);feather->setValue(2);feather->setValue(3.7);QMetaObject::invokeMethod(feather,"editingFinished",Qt::DirectConnection);QCoreApplication::processEvents();slider->setSliderDown(false);
        QTRY_COMPARE(w.currentPhotoDocument()->layers[0].mask.feather,3.7);QTest::qWait(10);QVERIFY(waitFor([&]{return painted(w,c,paint);}));
        const auto soft=w.currentPhotoDocument();QCOMPARE(soft->layers[0].mask.components,sharp->layers[0].mask.components);QVERIFY(pixels(c->presenter()->canonicalImage())!=sharpHash);
        paint.clear();w.undoEdit();QCOMPARE(w.currentPhotoDocument()->layers,sharp->layers);QVERIFY(waitFor([&]{return painted(w,c,paint);}));
        paint.clear();w.redoEdit();QCOMPARE(w.currentPhotoDocument()->layers,soft->layers);QVERIFY(waitFor([&]{return painted(w,c,paint);}));
        paint.clear();inverted->click();QVERIFY(w.currentPhotoDocument()->layers[0].mask.invert);QVERIFY(waitFor([&]{return painted(w,c,paint);}));
        const auto frozen=w.currentPhotoDocument();const auto frozenValue=*frozen;const auto image=c->presenter()->canonicalImage();const auto hash=pixels(image);
        auto *bw=w.findChild<QCheckBox *>("local-mask-blackwhite");auto *color=w.findChild<QComboBox *>("local-mask-color");QVERIFY(bw);QVERIFY(color);
        QSignalSpy frames(c,&Canvas::canonicalFramePainted),masks(c,&Canvas::localMaskPainted),requests(w.findChild<EditLoader *>(),&EditLoader::requestQueued);
        color->setCurrentIndex(1);bw->setChecked(true);c->repaint();QCoreApplication::processEvents();
        QCOMPARE(*w.currentPhotoDocument(),frozenValue);QCOMPARE(pixels(c->presenter()->canonicalImage()),hash);QCOMPARE(frames.count(),0);QCOMPARE(masks.count(),0);QCOMPARE(requests.count(),0);
        const auto shot=qEnvironmentVariable("LUMINA_MASK_SCREENSHOT");if(!shot.isEmpty())QVERIFY(w.grab().save(shot));
        auto *overlay=w.findChild<QCheckBox *>("local-overlay");QVERIFY(overlay);
        paint.clear();overlay->setChecked(false);QVERIFY(waitFor([&]{return painted(w,c,paint);}));
        requests.clear();bw->setChecked(false);QVERIFY(!overlay->isChecked());QCOMPARE(requests.count(),0);QCOMPARE(*w.currentPhotoDocument(),frozenValue);
        paint.clear();w.undoEdit();QVERIFY(!w.currentPhotoDocument()->layers[0].mask.invert);QCOMPARE(w.currentPhotoDocument()->layers[0].mask.feather,3.7);QVERIFY(waitFor([&]{return painted(w,c,paint);}));
        if(qEnvironmentVariableIsSet("LUMINA_MASK_FULL_REPLAY")){
            QString error;const auto source=readSource(frozen->asset.sourcePath,frozen->global.warmth,frozen->global.tint);QVERIFY2(source.source,qPrintable(source.error));
            auto full=renderDocumentFullLinear(*source.source,*frozen,error);QVERIFY2(!full.isNull(),qPrintable(error));QCOMPARE(full.size(),source.source->originalSize);const auto first=pixels(full);full={};
            full=renderDocumentFullLinear(*source.source,*frozen,error);QVERIFY2(!full.isNull(),qPrintable(error));QCOMPARE(pixels(full),first);
            QCOMPARE(*frozen,frozenValue);qInfo().noquote()<<"feather-full-source"<<full.size()<<first;
        }
    }
    void featherReturnToBaseAndPendingFinishDoesNotCloseNewGesture(){
        QTemporaryDir dir;Window w(nullptr,QStringLiteral(""));auto *c=w.findChild<Canvas *>("photo-canvas");QSignalSpy paint(c,&Canvas::canonicalFramePainted);QVERIFY(setup(w,c,paint,dir));
        auto *feather=w.findChild<QDoubleSpinBox *>("local-mask-feather");auto *slider=w.findChild<QSlider *>("slider-local-mask-feather");
        spin(w.findChild<QDoubleSpinBox *>("local-exposure"),.4);paint.clear();QVERIFY(waitFor([&]{return painted(w,c,paint);}));
        slider->setSliderDown(true);feather->setValue(5);feather->setValue(0);slider->setSliderDown(false);QTest::qWait(10);
        w.undoEdit();QCOMPARE(w.currentPhotoDocument()->layers[0].parameters.exposure,0.);QCOMPARE(w.currentPhotoDocument()->layers[0].mask.feather,0.);
        paint.clear();QVERIFY(waitFor([&]{return painted(w,c,paint);}));
        feather->setValue(2.5);QMetaObject::invokeMethod(feather,"editingFinished",Qt::DirectConnection);
        const auto from=point(c,{.3,.4}),to=point(c,{.7,.4});QTest::mousePress(c,Qt::LeftButton,Qt::NoModifier,from);QTest::qWait(20);QVERIFY(c->brushGestureActive());move(c,to);QTest::mouseRelease(c,Qt::LeftButton,Qt::NoModifier,to);
        QCOMPARE(w.currentPhotoDocument()->layers[0].mask.components.size(),1);QCOMPARE(w.currentPhotoDocument()->layers[0].mask.feather,2.5);
        w.undoEdit();QVERIFY(w.currentPhotoDocument()->layers[0].mask.components.isEmpty());QCOMPARE(w.currentPhotoDocument()->layers[0].mask.feather,2.5);
    }
};
QTEST_MAIN(LocalMaskWindowTests)
#include "local_mask_window_tests.moc"
