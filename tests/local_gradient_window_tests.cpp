#include "background_source.h"
#include <QtTest>
#include <QCryptographicHash>
#include <QComboBox>
#include <QTemporaryDir>
#include <QTabWidget>
#include <QMouseEvent>
#include <QFocusEvent>
#include <QLineEdit>
#include <QScrollArea>
#include <QWheelEvent>
#include <functional>
#include <cmath>
#include "app/window.h"
#include "app/display_presenter.h"
#include "render/source_coordinate_map.h"
#include "render/local_renderer.h"
#include "render/geometry_transform.h"
#include "io/source_reader.h"
using namespace LocalEdit;
namespace {
bool waitFor(const std::function<bool()> &test){QElapsedTimer t;t.start();while(!test()){if(t.elapsed()>90000)return false;QTest::qWait(10);}return true;}
bool painted(Window &w,Canvas *c,const QSignalSpy &paint){
    const auto doc=w.currentPhotoDocument();const auto r=qvariant_cast<EditResult>(w.property("gradient-last-result"));
    if(!doc||paint.isEmpty()||r.path!=doc->asset.sourcePath||!r.error.isEmpty()||r.image.isNull()||r.recipe!=doc->global)return false;
    if(doc->layers.isEmpty()?bool(r.document&&!r.document->layers.isEmpty()):(!r.document||r.document->layers!=doc->layers))return false;
    return paint.last()[0].toLongLong()==r.image.cacheKey()&&r.image.cacheKey()==c->presenter()->canonicalImage().cacheKey();
}
QPoint point(Canvas *c,QPointF uv){const auto s=c->presenter()->canonicalImage().size();return {int(std::lround(c->width()/2.+(uv.x()-.5)*s.width()*c->zoom())),int(std::lround(c->height()/2.+(uv.y()-.5)*s.height()*c->zoom()))};}
void move(Canvas *c,QPoint p){QMouseEvent e(QEvent::MouseMove,QPointF(p),c->mapToGlobal(p),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(c,&e);}
void drag(Canvas *c,QPoint from,QPoint to,Qt::KeyboardModifiers mods=Qt::NoModifier){QTest::mousePress(c,Qt::LeftButton,mods,from);move(c,to);QTest::mouseRelease(c,Qt::LeftButton,mods,to);}
QPoint handle(Window &w,Canvas *c,int component,int index){
    const auto doc=w.currentPhotoDocument();const auto source=gradientHandlePoints(doc->layers[0].mask.components[component].geometry)[index];
    QPointF uv;const bool ok=sourceToOutput(source,doc->asset.normalizedSize,doc->global,uv);Q_ASSERT(ok);return point(c,uv);
}
QByteArray hash(Canvas *c){const auto i=c->presenter()->canonicalImage();return QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char *>(i.constBits()),i.sizeInBytes()),QCryptographicHash::Sha256);}
bool setup(Window &w,Canvas *c,QSignalSpy &paint,QTemporaryDir &dir){
    QObject::connect(w.findChild<EditLoader *>(),&EditLoader::ready,&w,[&w](const EditResult &r){w.setProperty("gradient-last-result",QVariant::fromValue(r));});
    if(!dir.isValid())return false;
    auto path=qEnvironmentVariable("LUMINA_GRADIENT_SAMPLE");
    if(path.isEmpty()){path=dir.filePath("gradient.jpg");QImage image(96,64,QImage::Format_RGB32);image.fill(QColor(51,89,127));if(!image.save(path,"JPEG",98))return false;}
    c->setMonitorProfile({},"gradient-native-test");w.show();if(!QTest::qWaitForWindowExposed(&w))return false;w.addFiles({path});
    if(!waitFor([&]{return painted(w,c,paint)&&w.neutralPickerAvailable();}))return false;
    paint.clear();w.findChild<QTabWidget *>("adjustment-scope-tabs")->setCurrentIndex(1);w.findChild<QPushButton *>("local-new-brush")->click();
    return waitFor([&]{return painted(w,c,paint)&&c->brushMode();});
}
void spin(Window &w,const char *name,double value){auto *s=w.findChild<QDoubleSpinBox *>(name);Q_ASSERT(s);s->setValue(value);QMetaObject::invokeMethod(s,"editingFinished",Qt::DirectConnection);}
}
class LocalGradientWindowTests:public QObject {
    Q_OBJECT
private slots:
    void createBothKindsEditEveryHandleAndRemove(){
        QTemporaryDir dir;Window w(nullptr,QStringLiteral(""));auto *c=w.findChild<Canvas *>("photo-canvas");QVERIFY(c);QSignalSpy paint(c,&Canvas::canonicalFramePainted);QVERIFY(setup(w,c,paint,dir));
        const auto base=w.currentPhotoDocument();const auto baseHash=hash(c);
        qInfo()<<"native-gradient-dpr"<<c->devicePixelRatioF();
        if(qEnvironmentVariableIsSet("LUMINA_GRADIENT_EXPECT_DPR"))QVERIFY(std::abs(c->devicePixelRatioF()-qEnvironmentVariable("LUMINA_GRADIENT_EXPECT_DPR").toDouble())<1e-6);
        auto *linear=w.findChild<QPushButton *>("local-gradient-linear"),*radial=w.findChild<QPushButton *>("local-gradient-radial");QVERIFY(linear);QVERIFY(radial);
        linear->click();QVERIFY(c->brushMode());
        drag(c,point(c,{.2,.35}),point(c,{.65,.45}));
        QCOMPARE(w.currentPhotoDocument()->layers[0].mask.components.size(),1);
        QVERIFY(waitFor([&]{return painted(w,c,paint);}));
        const auto initial=w.currentPhotoDocument();const auto id=initial->layers[0].mask.components[0].id;
        QVERIFY(std::holds_alternative<LinearGeometry>(initial->layers[0].mask.components[0].geometry));
        paint.clear();spin(w,"local-exposure",.7);QVERIFY(waitFor([&]{return painted(w,c,paint);}));QVERIFY(hash(c)!=baseHash);
        for(int index=0;index<3;++index){
            const auto before=w.currentPhotoDocument();const auto geometry=before->layers[0].mask.components[0].geometry;
            const auto from=handle(w,c,0,index);paint.clear();drag(c,from,from+QPoint(16,12));
            QCOMPARE(c->pressedGradientHandle(),index);QCOMPARE(w.currentPhotoDocument()->layers[0].mask.components.size(),1);
            QCOMPARE(w.currentPhotoDocument()->layers[0].mask.components[0].id,id);
            QVERIFY(w.currentPhotoDocument()->layers[0].mask.components[0].geometry!=geometry);QVERIFY(waitFor([&]{return painted(w,c,paint);}));
            const auto changed=w.currentPhotoDocument()->layers[0].mask.components[0].geometry;
            paint.clear();w.undoEdit();QCOMPARE(w.currentPhotoDocument()->layers[0].mask.components[0].geometry,geometry);QVERIFY(waitFor([&]{return painted(w,c,paint);}));
            paint.clear();w.redoEdit();QCOMPARE(w.currentPhotoDocument()->layers[0].mask.components[0].geometry,changed);QVERIFY(waitFor([&]{return painted(w,c,paint);}));
        }
        radial->click();paint.clear();drag(c,point(c,{.5,.5}),point(c,{.65,.5}),Qt::AltModifier);
        QCOMPARE(w.currentPhotoDocument()->layers[0].mask.components.size(),2);QVERIFY(waitFor([&]{return painted(w,c,paint);}));
        const auto radialId=w.currentPhotoDocument()->layers[0].mask.components[1].id;
        QCOMPARE(w.currentPhotoDocument()->layers[0].mask.components[1].operation,MaskOperation::Subtract);
        for(int index=0;index<4;++index){
            const auto geometry=w.currentPhotoDocument()->layers[0].mask.components[1].geometry;
            const auto from=handle(w,c,1,index);paint.clear();drag(c,from,from+QPoint(13,11));
            QCOMPARE(c->pressedGradientHandle(),index);QCOMPARE(w.currentPhotoDocument()->layers[0].mask.components.size(),2);
            QCOMPARE(w.currentPhotoDocument()->layers[0].mask.components[1].id,radialId);
            QVERIFY(w.currentPhotoDocument()->layers[0].mask.components[1].geometry!=geometry);QVERIFY(waitFor([&]{return painted(w,c,paint);}));
        }
        auto *combo=w.findChild<QComboBox *>("local-mask-components");QVERIFY(combo);const int row=combo->findData(id);QVERIFY(row>0);combo->setCurrentIndex(row);
        QVERIFY(linear->isChecked());QVERIFY(!radial->isChecked());
        const auto from=handle(w,c,0,0);drag(c,from,from+QPoint(-10,8));QCOMPARE(c->pressedGradientHandle(),0);
        paint.clear();QVERIFY(waitFor([&]{return painted(w,c,paint);}));
        const auto preRemove=w.currentPhotoDocument();paint.clear();w.findChild<QPushButton *>("local-mask-remove")->click();
        QCOMPARE(w.currentPhotoDocument()->layers[0].mask.components.size(),1);QCOMPARE(w.currentPhotoDocument()->layers[0].mask.components[0].id,radialId);
        QVERIFY(waitFor([&]{return painted(w,c,paint);}));paint.clear();w.undoEdit();QCOMPARE(w.currentPhotoDocument()->layers,preRemove->layers);QVERIFY(waitFor([&]{return painted(w,c,paint);}));
        QCOMPARE(w.currentPhotoDocument()->global,base->global);QCOMPARE(w.currentPhotoDocument()->asset.sourceHash,base->asset.sourceHash);
        const auto screenshot=qEnvironmentVariable("LUMINA_GRADIENT_QA_SCREENSHOT");
        if(!screenshot.isEmpty()){
            const int radialRow=combo->findData(radialId);QVERIFY(radialRow>0);combo->setCurrentIndex(radialRow);
            for(auto *area:w.findChildren<QScrollArea *>())if(area->widget()==combo->parentWidget()->parentWidget())area->ensureVisible(0,0);
            QCoreApplication::processEvents();QVERIFY(w.grab().save(screenshot));
        }
        if(qEnvironmentVariableIsSet("LUMINA_GRADIENT_FULL_REPLAY")){
            const auto frozen=w.currentPhotoDocument();const auto frozenValue=*frozen;QElapsedTimer timer;timer.start();
            const auto source=readTestSource(frozen->asset.sourcePath,frozen->global.warmth,frozen->global.tint);QVERIFY2(source.source,qPrintable(source.error));
            QCOMPARE(source.source->linear.size(),frozen->asset.normalizedSize);QCOMPARE(source.source->sha256,frozen->asset.sourceHash);
            QString error;auto full=renderDocumentFullLinear(*source.source,*frozen,error);QVERIFY2(!full.isNull(),qPrintable(error));
            QCOMPARE(full.size(),renderedSize(frozen->asset.normalizedSize,frozen->global));
            const auto pixelHash=[](const QImage &image){return QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char *>(image.constBits()),image.sizeInBytes()),QCryptographicHash::Sha256).toHex();};
            const auto frozenHash=pixelHash(full);const auto fullSize=full.size();full={};
            paint.clear();spin(w,"local-exposure",.9);QVERIFY(waitFor([&]{return painted(w,c,paint);}));QCOMPARE(*frozen,frozenValue);
            full=renderDocumentFullLinear(*source.source,*frozen,error);QVERIFY2(!full.isNull(),qPrintable(error));QCOMPARE(pixelHash(full),frozenHash);full={};
            full=renderDocumentFullLinear(*source.source,*w.currentPhotoDocument(),error);QVERIFY2(!full.isNull(),qPrintable(error));QVERIFY(pixelHash(full)!=frozenHash);
            qInfo().noquote()<<"frozen-full-replay"<<fullSize<<"sha256"<<frozenHash<<"elapsed-ms"<<timer.elapsed();
        }
    }
    void cancelSpaceNoMovementAndGeometryProjection(){
        QTemporaryDir dir;Window w(nullptr,QStringLiteral(""));auto *c=w.findChild<Canvas *>("photo-canvas");QVERIFY(c);QSignalSpy paint(c,&Canvas::canonicalFramePainted);QVERIFY(setup(w,c,paint,dir));
        w.findChild<QPushButton *>("local-gradient-linear")->click();const auto base=w.currentPhotoDocument();
        const auto from=point(c,{.2,.3}),to=point(c,{.7,.6});
        QTest::mousePress(c,Qt::LeftButton,Qt::NoModifier,from);move(c,to);QVERIFY(c->brushGestureActive());QTest::keyClick(c,Qt::Key_Escape);QTest::mouseRelease(c,Qt::LeftButton,Qt::NoModifier,to);
        QCOMPARE(w.currentPhotoDocument()->layers,base->layers);QCOMPARE(w.currentPhotoDocument()->revision,base->revision);
        paint.clear();QVERIFY(waitFor([&]{return painted(w,c,paint);}));
        QTest::mousePress(c,Qt::LeftButton,Qt::NoModifier,from);move(c,to);QTest::keyPress(c,Qt::Key_Space);QVERIFY(!c->brushGestureActive());
        QTest::mouseRelease(c,Qt::LeftButton,Qt::NoModifier,to);QTest::keyRelease(c,Qt::Key_Space);
        QCOMPARE(w.currentPhotoDocument()->layers[0].mask.components.size(),1);paint.clear();QVERIFY(waitFor([&]{return painted(w,c,paint);}));
        const auto committed=w.currentPhotoDocument();
        drag(c,point(c,{.9,.2}),point(c,{.9,.2}));QCOMPARE(w.currentPhotoDocument()->layers,committed->layers);QCOMPARE(w.currentPhotoDocument()->revision,committed->revision);
        QCOMPARE(w.findChild<QComboBox *>("local-mask-components")->currentData().toString(),committed->layers[0].mask.components[0].id);
        paint.clear();QVERIFY(waitFor([&]{return painted(w,c,paint);}));
        auto recipe=w.currentRecipe();recipe.crop={.1,.08,.8,.82};recipe.angle=8;recipe.perspective={0,.08};recipe.distortion=.12;
        paint.clear();w.applyRecipe(recipe,"geometry");QVERIFY(waitFor([&]{return painted(w,c,paint);}));
        const auto old=w.currentPhotoDocument()->layers[0].mask.components[0].geometry;
        const auto projected=handle(w,c,0,1);paint.clear();drag(c,projected,projected+QPoint(14,9));
        QCOMPARE(c->pressedGradientHandle(),1);QCOMPARE(w.currentPhotoDocument()->layers[0].mask.components.size(),1);
        QVERIFY(w.currentPhotoDocument()->layers[0].mask.components[0].geometry!=old);QVERIFY(waitFor([&]{return painted(w,c,paint);}));
        paint.clear();w.undoEdit();QCOMPARE(w.currentPhotoDocument()->layers[0].mask.components[0].geometry,old);QVERIFY(waitFor([&]{return painted(w,c,paint);}));
        const auto beforeFocus=w.currentPhotoDocument();const auto centre=handle(w,c,0,2);
        QTest::mousePress(c,Qt::LeftButton,Qt::NoModifier,centre);move(c,centre+QPoint(12,7));
        QFocusEvent focus(QEvent::FocusOut);QApplication::sendEvent(c,&focus);QVERIFY(!c->brushGestureActive());
        QTest::mouseRelease(c,Qt::LeftButton,Qt::NoModifier,centre+QPoint(12,7));
        QVERIFY(w.currentPhotoDocument()->layers!=beforeFocus->layers);paint.clear();QVERIFY(waitFor([&]{return painted(w,c,paint);}));
        const auto start=point(c,{.15,.8}),end=point(c,{.55,.8});
        QTest::mousePress(c,Qt::LeftButton,Qt::NoModifier,start);move(c,end);QVERIFY(c->brushGestureActive());
        const auto beforeWheel=w.currentPhotoDocument()->layers;const auto oldZoom=c->zoom();
        QWheelEvent wheel(QPointF(end),c->mapToGlobal(end),QPoint(),QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
        QApplication::sendEvent(c,&wheel);QVERIFY(!c->brushGestureActive());QVERIFY(c->zoom()>oldZoom);
        QTest::mouseRelease(c,Qt::LeftButton,Qt::NoModifier,end);QCOMPARE(w.currentPhotoDocument()->layers,beforeWheel);
        paint.clear();QVERIFY(waitFor([&]{return painted(w,c,paint);}));
        c->fit();const auto count=w.currentPhotoDocument()->layers[0].mask.components.size();
        const auto toolStart=point(c,{.9,.7}),toolEnd=point(c,{.8,.9});
        QTest::mousePress(c,Qt::LeftButton,Qt::NoModifier,toolStart);move(c,toolEnd);QVERIFY(c->brushGestureActive());
        w.findChild<QPushButton *>("local-gradient-radial")->click();QVERIFY(!c->brushGestureActive());
        QTest::mouseRelease(c,Qt::LeftButton,Qt::NoModifier,toolEnd);QCOMPARE(w.currentPhotoDocument()->layers[0].mask.components.size(),count+1);
        paint.clear();QVERIFY(waitFor([&]{return painted(w,c,paint);}));
    }
    void pendingParameterTextFocusEraseAndPhotoSwitch(){
        QTemporaryDir dir;Window w(nullptr,QStringLiteral(""));auto *c=w.findChild<Canvas *>("photo-canvas");QVERIFY(c);QSignalSpy paint(c,&Canvas::canonicalFramePainted);QVERIFY(setup(w,c,paint,dir));
        auto *linear=w.findChild<QPushButton *>("local-gradient-linear");linear->click();drag(c,point(c,{.2,.3}),point(c,{.6,.4}));
        paint.clear();QVERIFY(waitFor([&]{return painted(w,c,paint);}));
        auto *exposure=w.findChild<QDoubleSpinBox *>("local-exposure");exposure->setFocus();QTest::keyClick(exposure,Qt::Key_Up);
        const auto from=handle(w,c,0,1);const auto old=w.currentPhotoDocument()->layers[0].mask.components[0].geometry;
        QTest::mousePress(c,Qt::LeftButton,Qt::NoModifier,from);QTest::qWait(30);QVERIFY(c->brushGestureActive());
        move(c,from+QPoint(15,10));QTest::mouseRelease(c,Qt::LeftButton,Qt::NoModifier,from+QPoint(15,10));
        QCOMPARE(w.currentPhotoDocument()->layers[0].mask.components.size(),1);QVERIFY(w.currentPhotoDocument()->layers[0].mask.components[0].geometry!=old);
        paint.clear();QVERIFY(waitFor([&]{return painted(w,c,paint);}));
        const auto value=w.currentPhotoDocument()->layers[0].parameters.exposure;QVERIFY(value>0);
        w.undoEdit();QCOMPARE(w.currentPhotoDocument()->layers[0].mask.components[0].geometry,old);QCOMPARE(w.currentPhotoDocument()->layers[0].parameters.exposure,value);
        paint.clear();QVERIFY(waitFor([&]{return painted(w,c,paint);}));
        w.redoEdit();paint.clear();QVERIFY(waitFor([&]{return painted(w,c,paint);}));
        QLineEdit text(&w);text.show();text.setFocus();QVERIFY(waitFor([&]{return text.hasFocus();}));
        const auto beforeText=w.currentPhotoDocument()->layers;QTest::keyClicks(&text,"O[]");QTest::keyClick(&text,Qt::Key_Delete);QCOMPARE(w.currentPhotoDocument()->layers,beforeText);
        auto *erase=w.findChild<QPushButton *>("local-erase");QVERIFY(erase);erase->click();QVERIFY(linear->isChecked());QVERIFY(c->brushMode());
        drag(c,point(c,{.25,.7}),point(c,{.7,.7}));
        QCOMPARE(w.currentPhotoDocument()->layers[0].mask.components.size(),2);QCOMPARE(w.currentPhotoDocument()->layers[0].mask.components[1].operation,MaskOperation::Subtract);
        paint.clear();QVERIFY(waitFor([&]{return painted(w,c,paint);}));erase->click();
        const auto a=w.currentPhotoDocument()->asset.sourcePath;
        const auto other=dir.filePath("second.jpg");QImage image(80,120,QImage::Format_RGB32);image.fill(QColor(90,70,30));QVERIFY(image.save(other,"JPEG"));w.addFiles({other});
        // Import prefetches the second file; it need not repaint the unchanged
        // current photo. The next gesture and activation each have their own gate.
        const auto start=point(c,{.25,.2}),end=point(c,{.65,.25});QTest::mousePress(c,Qt::LeftButton,Qt::NoModifier,start);move(c,end);QVERIFY(c->brushGestureActive());
        paint.clear();w.activate(1);QVERIFY(!c->brushGestureActive());QTest::mouseRelease(c,Qt::LeftButton,Qt::NoModifier,end);
        QVERIFY(waitFor([&]{return painted(w,c,paint)&&w.currentIndex()==1;}));QVERIFY(w.currentPhotoDocument()->layers.isEmpty());
        paint.clear();w.activate(0);QVERIFY(waitFor([&]{return painted(w,c,paint);}));
        QCOMPARE(w.currentPhotoDocument()->asset.sourcePath,a);QCOMPARE(w.currentPhotoDocument()->layers[0].mask.components.size(),3);
        const auto frozen=w.currentPhotoDocument();const auto frozenValue=*frozen;
        paint.clear();spin(w,"local-exposure",.8);QVERIFY(waitFor([&]{return painted(w,c,paint);}));QCOMPARE(*frozen,frozenValue);
        auto *radial=w.findChild<QPushButton *>("local-gradient-radial");linear->setFocus();QTest::keyClick(linear,Qt::Key_Tab);QVERIFY(radial->hasFocus());
        QTest::keyClick(radial,Qt::Key_Space);QVERIFY(radial->isChecked());QVERIFY(c->brushMode());
        auto *combo=w.findChild<QComboBox *>("local-mask-components");QTest::keyClick(radial,Qt::Key_Tab);QVERIFY(combo->hasFocus());
        w.resize(900,500);QCoreApplication::processEvents();
        QScrollArea *localArea=nullptr;for(auto *area:w.findChildren<QScrollArea *>())if(area->widget()==combo->parentWidget()->parentWidget())localArea=area;
        QVERIFY(localArea);auto *last=w.findChild<QDoubleSpinBox *>("local-saturation");localArea->ensureWidgetVisible(last);QCoreApplication::processEvents();
        QVERIFY(last->isVisible());QVERIFY(localArea->viewport()->rect().intersects(QRect(last->mapTo(localArea->viewport(),QPoint()),last->size())));
    }
};
QTEST_MAIN(LocalGradientWindowTests)
#include "local_gradient_window_tests.moc"
