#include <QtTest>
#include <QAction>
#include <QCryptographicHash>
#include <QLineEdit>
#include <QListWidget>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTabWidget>
#include <QMouseEvent>
#include <functional>
#include <cmath>
#include "app/window.h"
#include "app/display_presenter.h"
namespace {
bool waitFor(const std::function<bool()> &test){QElapsedTimer timer;timer.start();while(!test()){if(timer.elapsed()>90000)return false;QTest::qWait(10);}return true;}
bool painted(Window &window,Canvas *canvas,const QSignalSpy &paint){
    const auto doc=window.currentPhotoDocument();const auto result=qvariant_cast<EditResult>(window.property("layer-test-last-result"));
    if(!doc||paint.isEmpty()||!result.error.isEmpty()||result.image.isNull()||result.recipe!=doc->global)return false;
    if(doc->layers.isEmpty()?bool(result.document&&!result.document->layers.isEmpty()):(!result.document||result.document->layers!=doc->layers))return false;
    return paint.last()[0].toLongLong()==result.image.cacheKey()&&result.image.cacheKey()==canvas->presenter()->canonicalImage().cacheKey();
}
QPoint point(Canvas *canvas,double u,double v){const auto size=canvas->presenter()->canonicalImage().size();return {int(std::lround(canvas->width()/2.+(u-.5)*size.width()*canvas->zoom())),int(std::lround(canvas->height()/2.+(v-.5)*size.height()*canvas->zoom()))};}
void move(Canvas *canvas,QPoint p){QMouseEvent e(QEvent::MouseMove,QPointF(p),canvas->mapToGlobal(p),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(canvas,&e);}
QByteArray hash(Canvas *canvas){const auto image=canvas->presenter()->canonicalImage();return QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char *>(image.constBits()),image.sizeInBytes()),QCryptographicHash::Sha256);}
const LocalEdit::AdjustmentLayer *layer(const LocalEdit::DocumentSnapshot &doc,const QString &id){if(doc)for(const auto &l:doc->layers)if(l.id==id)return &l;return nullptr;}
QListWidgetItem *row(QListWidget *list,const QString &id){for(int i=0;i<list->count();++i)if(list->item(i)->data(Qt::UserRole).toString()==id)return list->item(i);return nullptr;}
void commitSpin(QDoubleSpinBox *spin,double value){spin->setValue(value);QMetaObject::invokeMethod(spin,"editingFinished",Qt::DirectConnection);}
bool setup(Window &window,Canvas *canvas,QSignalSpy &paint,QTemporaryDir &directory){
    QObject::connect(window.findChild<EditLoader *>(),&EditLoader::ready,&window,[&window](const EditResult &result){window.setProperty("layer-test-last-result",QVariant::fromValue(result));});
    if(!directory.isValid())return false;const auto path=directory.filePath("layers.jpg");QImage image(96,64,QImage::Format_RGB32);image.fill(QColor(51,89,127));if(!image.save(path,"JPEG",98))return false;
    canvas->setMonitorProfile({},"local-layers-test");window.show();if(!QTest::qWaitForWindowExposed(&window))return false;window.addFiles({path});
    if(!waitFor([&]{return painted(window,canvas,paint)&&window.neutralPickerAvailable();}))return false;
    paint.clear();window.findChild<QTabWidget *>("adjustment-scope-tabs")->setCurrentIndex(1);
    window.findChild<QPushButton *>("local-new-brush")->click();
    return waitFor([&]{return painted(window,canvas,paint)&&canvas->brushMode()&&window.currentPhotoDocument()->layers.size()==1;});
}
}
class LocalLayerWindowTests:public QObject {
    Q_OBJECT
private slots:
    void layerLifecycleAndRenderedOrder(){
        QTemporaryDir directory;Window window(nullptr,QStringLiteral(""));auto *canvas=window.findChild<Canvas *>("photo-canvas");QVERIFY(canvas);
        QSignalSpy paint(canvas,&Canvas::canonicalFramePainted);QVERIFY(setup(window,canvas,paint,directory));
        const auto idA=window.currentPhotoDocument()->layers[0].id;
        window.findChild<QDoubleSpinBox *>("local-radius")->setValue(100);
        QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,point(canvas,.3,.5));move(canvas,point(canvas,.7,.5));
        QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,point(canvas,.7,.5));
        paint.clear();commitSpin(window.findChild<QDoubleSpinBox *>("local-contrast"),.5);
        QVERIFY(waitFor([&]{return painted(window,canvas,paint);}));
        auto *opacity=window.findChild<QSlider *>("slider-local-layer-opacity");QVERIFY(opacity);
        paint.clear();opacity->setSliderDown(true);opacity->setValue(300);opacity->setValue(600);opacity->setSliderDown(false);
        QVERIFY(waitFor([&]{return layer(window.currentPhotoDocument(),idA)->opacity==.6&&painted(window,canvas,paint);}));
        paint.clear();window.undoEdit();QCOMPARE(layer(window.currentPhotoDocument(),idA)->opacity,1.);
        QVERIFY(waitFor([&]{return painted(window,canvas,paint);}));
        paint.clear();window.redoEdit();QCOMPARE(layer(window.currentPhotoDocument(),idA)->opacity,.6);
        QVERIFY(waitFor([&]{return painted(window,canvas,paint);}));
        const auto frozen=window.currentPhotoDocument();const auto frozenValue=*frozen;
        auto *copy=window.findChild<QAction *>("local-layer-duplicate");auto *remove=window.findChild<QAction *>("local-layer-delete");
        auto *down=window.findChild<QAction *>("local-layer-down");QVERIFY(copy);QVERIFY(remove);QVERIFY(down);QVERIFY(copy->isEnabled());
        paint.clear();copy->trigger();QVERIFY(waitFor([&]{return window.currentPhotoDocument()->layers.size()==2&&painted(window,canvas,paint);}));
        const auto duplicated=window.currentPhotoDocument();const auto idB=duplicated->layers[1].id;QVERIFY(idA!=idB);
        QCOMPARE(duplicated->layers[0].parameters,duplicated->layers[1].parameters);QCOMPARE(duplicated->layers[0].opacity,duplicated->layers[1].opacity);
        const auto &a=duplicated->layers[0].mask.components;const auto &b=duplicated->layers[1].mask.components;
        QVERIFY(!a.isEmpty());QCOMPARE(a.size(),b.size());for(int i=0;i<a.size();++i){QVERIFY(a[i].id!=b[i].id);QCOMPARE(a[i].geometry,b[i].geometry);}
        paint.clear();commitSpin(window.findChild<QDoubleSpinBox *>("local-exposure"),.7);
        QVERIFY(waitFor([&]{return painted(window,canvas,paint);}));
        QCOMPARE(layer(window.currentPhotoDocument(),idB)->parameters.exposure,.7);QCOMPARE(layer(window.currentPhotoDocument(),idA)->parameters.exposure,0.);
        QCOMPARE(*frozen,frozenValue);
        auto *list=window.findChild<QListWidget *>("local-layer-list");QVERIFY(list);QCOMPARE(list->item(0)->data(Qt::UserRole).toString(),idB);
        list->setFocus();QTest::keyClick(list,Qt::Key_F2);
        const auto editor=[&]()->QLineEdit *{for(auto *edit:list->findChildren<QLineEdit *>())if(edit->isVisible())return edit;return nullptr;};
        QVERIFY(waitFor([&]{return editor()!=nullptr;}));editor()->setText(QStringLiteral("天空 修正"));QTest::keyClick(editor(),Qt::Key_Return);
        QVERIFY(waitFor([&]{return layer(window.currentPhotoDocument(),idB)->name==QStringLiteral("天空 修正");}));
        paint.clear();const auto before=hash(canvas);QVERIFY(down->isEnabled());down->trigger();
        QVERIFY(waitFor([&]{return window.currentPhotoDocument()->layers[0].id==idB&&painted(window,canvas,paint);}));
        QCOMPARE(list->item(0)->data(Qt::UserRole).toString(),idA);QVERIFY(hash(canvas)!=before);
        const auto after=hash(canvas);paint.clear();window.undoEdit();
        QCOMPARE(window.currentPhotoDocument()->layers[0].id,idA);QVERIFY(waitFor([&]{return painted(window,canvas,paint);}));QCOMPARE(hash(canvas),before);
        paint.clear();window.redoEdit();QCOMPARE(window.currentPhotoDocument()->layers[0].id,idB);
        QVERIFY(waitFor([&]{return painted(window,canvas,paint);}));QCOMPARE(hash(canvas),after);
        paint.clear();QVERIFY(row(list,idA));row(list,idA)->setCheckState(Qt::Unchecked);
        QVERIFY(waitFor([&]{return !layer(window.currentPhotoDocument(),idA)->enabled&&painted(window,canvas,paint);}));
        QCOMPARE(list->currentItem()->data(Qt::UserRole).toString(),idB);
        paint.clear();window.undoEdit();QVERIFY(layer(window.currentPhotoDocument(),idA)->enabled);QVERIFY(waitFor([&]{return painted(window,canvas,paint);}));
        paint.clear();remove->trigger();QVERIFY(waitFor([&]{return window.currentPhotoDocument()->layers.size()==1&&painted(window,canvas,paint);}));
        QCOMPARE(window.currentPhotoDocument()->layers[0].id,idA);
        paint.clear();window.undoEdit();QCOMPARE(layer(window.currentPhotoDocument(),idB)->name,QStringLiteral("天空 修正"));
        QCOMPARE(layer(window.currentPhotoDocument(),idB)->parameters.exposure,.7);QVERIFY(waitFor([&]{return painted(window,canvas,paint);}));
        paint.clear();window.redoEdit();QVERIFY(waitFor([&]{return window.currentPhotoDocument()->layers.size()==1&&painted(window,canvas,paint);}));
        paint.clear();QVERIFY(remove->isEnabled());remove->trigger();QVERIFY(waitFor([&]{return window.currentPhotoDocument()->layers.isEmpty()&&painted(window,canvas,paint);}));
        QVERIFY(!canvas->brushMode());QVERIFY(window.findChild<QPushButton *>("local-new-brush")->isEnabled());QVERIFY(!opacity->isEnabled());
        paint.clear();window.undoEdit();QCOMPARE(window.currentPhotoDocument()->layers[0].id,idA);QVERIFY(waitFor([&]{return painted(window,canvas,paint);}));
    }
    void opacityReturnToBaseAndStaleFinishDoNotAddHistoryOrCloseBrush(){
        QTemporaryDir directory;Window window(nullptr,QStringLiteral(""));auto *canvas=window.findChild<Canvas *>("photo-canvas");QVERIFY(canvas);
        QSignalSpy paint(canvas,&Canvas::canonicalFramePainted);QVERIFY(setup(window,canvas,paint,directory));
        QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,point(canvas,.5,.5));
        QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,point(canvas,.5,.5));
        paint.clear();QVERIFY(waitFor([&]{return painted(window,canvas,paint);}));
        const auto originalMask=window.currentPhotoDocument()->layers[0].mask;QVERIFY(!originalMask.components.isEmpty());
        auto *exposure=window.findChild<QDoubleSpinBox *>("local-exposure");auto *opacity=window.findChild<QSlider *>("slider-local-layer-opacity");QVERIFY(exposure);QVERIFY(opacity);
        paint.clear();commitSpin(exposure,.4);QVERIFY(waitFor([&]{return painted(window,canvas,paint);}));
        paint.clear();opacity->setSliderDown(true);opacity->setValue(500);opacity->setValue(1000);opacity->setSliderDown(false);
        QVERIFY(waitFor([&]{return painted(window,canvas,paint);}));
        window.undoEdit();QCOMPARE(window.currentPhotoDocument()->layers[0].parameters.exposure,0.);QCOMPARE(window.currentPhotoDocument()->layers[0].opacity,1.);
        QCOMPARE(window.currentPhotoDocument()->layers[0].mask,originalMask);
        paint.clear();QVERIFY(waitFor([&]{return painted(window,canvas,paint);}));
        const auto before=window.currentPhotoDocument()->layers[0].mask.components.size();
        exposure->setFocus();QTest::keyClick(exposure,Qt::Key_Up);
        QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,point(canvas,.3,.4));QTest::qWait(30);
        QVERIFY(canvas->brushGestureActive());move(canvas,point(canvas,.7,.4));QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,point(canvas,.7,.4));
        QCOMPARE(window.currentPhotoDocument()->layers[0].mask.components.size(),before+1);
        QVERIFY(std::get<LocalEdit::BrushGeometry>(window.currentPhotoDocument()->layers[0].mask.components.last().geometry).points.size()>2);
    }
};
QTEST_MAIN(LocalLayerWindowTests)
#include "local_layer_window_tests.moc"
