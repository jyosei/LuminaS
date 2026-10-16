#include <QtTest>
#include <QCryptographicHash>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QSignalSpy>
#include <QSlider>
#include <QScrollArea>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QMouseEvent>
#include <cmath>
#include "app/window.h"
#include "app/display_presenter.h"

namespace {
bool waitFor(const std::function<bool()> &condition,int timeout=15000){
    QElapsedTimer timer;timer.start();
    while(!condition()){if(timer.elapsed()>timeout)return false;QTest::qWait(10);}return true;
}
bool ready(Window &window,Canvas *canvas,const QSignalSpy &paint){
    return window.currentPhotoDocument()&&!window.displayedPath().isEmpty()&&!paint.isEmpty()&&
        paint.last()[0].toLongLong()==canvas->presenter()->canonicalImage().cacheKey();
}
QPoint point(Canvas *canvas,double u,double v,QPoint pan={}){
    const auto size=canvas->presenter()->canonicalImage().size();
    return QPoint(int(std::lround(canvas->width()/2.+(u-.5)*size.width()*canvas->zoom())),
        int(std::lround(canvas->height()/2.+(v-.5)*size.height()*canvas->zoom())))+pan;
}
void move(Canvas *canvas,QPoint position){
    QMouseEvent event(QEvent::MouseMove,QPointF(position),canvas->mapToGlobal(position),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(canvas,&event);
}
QByteArray hash(const QImage &image){return QCryptographicHash::hash(QByteArrayView(
    reinterpret_cast<const char *>(image.constBits()),image.sizeInBytes()),QCryptographicHash::Sha256);}
QString sample(QTemporaryDir &directory,QString name){
    const auto path=directory.filePath(name);QImage image(96,64,QImage::Format_RGB32);image.fill(QColor(70,90,110));
    return image.save(path,"JPEG",98)?path:QString{};
}
}
class LocalWindowTests:public QObject {
    Q_OBJECT
private slots:
    void allNineControlsReachWindowHistory(){
        QTemporaryDir directory;const auto path=sample(directory,"nine-parameters.jpg");QVERIFY(!path.isEmpty());
        Window window(nullptr,QStringLiteral(""));auto *canvas=window.findChild<Canvas *>("photo-canvas");QVERIFY(canvas);
        canvas->setMonitorProfile({},"nine-parameter-test");QSignalSpy painted(canvas,&Canvas::canonicalFramePainted);
        window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));window.addFiles({path});
        QVERIFY(waitFor([&]{return ready(window,canvas,painted);}));
        auto *tabs=window.findChild<QTabWidget *>("adjustment-scope-tabs");QVERIFY(tabs);tabs->setCurrentIndex(1);
        painted.clear();QTest::mouseClick(window.findChild<QPushButton *>("local-new-brush"),Qt::LeftButton);
        QVERIFY(waitFor([&]{return ready(window,canvas,painted)&&canvas->brushMode();}));
        QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,point(canvas,.4,.5));
        QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,point(canvas,.6,.5));
        const auto mask=window.currentPhotoDocument()->layers[0].mask;
        QVERIFY(!mask.components.isEmpty());
        struct Field {const char *name;double LocalEdit::LocalParameters::*member;};
        const Field fields[]{{"exposure",&LocalEdit::LocalParameters::exposure},{"contrast",&LocalEdit::LocalParameters::contrast},
            {"highlights",&LocalEdit::LocalParameters::highlights},{"shadows",&LocalEdit::LocalParameters::shadows},
            {"whites",&LocalEdit::LocalParameters::whites},{"blacks",&LocalEdit::LocalParameters::blacks},
            {"warmth",&LocalEdit::LocalParameters::warmth},{"tint",&LocalEdit::LocalParameters::tint},{"saturation",&LocalEdit::LocalParameters::saturation}};
        auto *scroll=qobject_cast<QScrollArea *>(tabs->widget(1));QVERIFY(scroll);
        LocalEdit::LocalParameters expected;
        for(const auto &field:fields){
            auto *spin=window.findChild<QDoubleSpinBox *>(QString("local-%1").arg(field.name));QVERIFY(spin);
            scroll->ensureWidgetVisible(spin);QTest::qWait(20);
            QVERIFY2(spin->visibleRegion().contains(spin->rect()),field.name);
            expected.*(field.member)+=.2;painted.clear();
            spin->setValue(expected.*(field.member));QMetaObject::invokeMethod(spin,"editingFinished",Qt::DirectConnection);
            QVERIFY(waitFor([&]{return ready(window,canvas,painted);}));
            QCOMPARE(window.currentPhotoDocument()->layers[0].parameters,expected);
            QCOMPARE(window.currentPhotoDocument()->layers[0].mask,mask);
        }
        for(int i=8;i>=0;--i){
            painted.clear();window.undoEdit();expected.*(fields[i].member)=i==8?1:0;
            QCOMPARE(window.currentPhotoDocument()->layers[0].parameters,expected);
            QCOMPARE(window.currentPhotoDocument()->layers[0].mask,mask);
            QVERIFY(waitFor([&]{return ready(window,canvas,painted);}));
        }
    }
    void pendingParameterCommitDoesNotEndNewBrushPress(){
        QTemporaryDir directory;const auto path=sample(directory,"pending-parameter.jpg");QVERIFY(!path.isEmpty());
        Window window(nullptr,QStringLiteral(""));auto *canvas=window.findChild<Canvas *>("photo-canvas");QVERIFY(canvas);
        canvas->setMonitorProfile({},"pending-parameter-test");QSignalSpy painted(canvas,&Canvas::canonicalFramePainted);
        window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));window.addFiles({path});
        QVERIFY(waitFor([&]{return ready(window,canvas,painted);}));
        window.findChild<QTabWidget *>("adjustment-scope-tabs")->setCurrentIndex(1);
        painted.clear();QTest::mouseClick(window.findChild<QPushButton *>("local-new-brush"),Qt::LeftButton);
        QVERIFY(waitFor([&]{return ready(window,canvas,painted)&&canvas->brushMode();}));
        window.findChild<QDoubleSpinBox *>("local-radius")->setValue(8);
        // No editingFinished yet: beginning the brush must flush the parameter
        // draft, without closing the canvas gesture that just emitted press.
        window.findChild<QSlider *>("slider-local-exposure")->setValue(40);
        QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,point(canvas,.3,.5));
        QVERIFY(canvas->brushGestureActive());
        move(canvas,point(canvas,.7,.5));QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,point(canvas,.7,.5));
        auto document=window.currentPhotoDocument();QVERIFY(document);QCOMPARE(document->layers[0].parameters.exposure,.4);
        QCOMPARE(document->layers[0].mask.components.size(),1);
        QVERIFY(std::get<LocalEdit::BrushGeometry>(document->layers[0].mask.components[0].geometry).points.size()>2);
        window.undoEdit();document=window.currentPhotoDocument();
        QVERIFY(document->layers[0].mask.components.isEmpty());QCOMPARE(document->layers[0].parameters.exposure,.4);
        window.undoEdit();QCOMPARE(window.currentPhotoDocument()->layers[0].parameters.exposure,0.);
    }
    void brushHistoryParametersAndCancel(){
        QTemporaryDir directory;QVERIFY(directory.isValid());const auto path=sample(directory,"brush.jpg");QVERIFY(!path.isEmpty());
        Window window(nullptr,QStringLiteral(""));auto *canvas=window.findChild<Canvas *>("photo-canvas");QVERIFY(canvas);
        canvas->setMonitorProfile({},"native-local-test");QSignalSpy painted(canvas,&Canvas::canonicalFramePainted),preview(&window,&Window::previewChanged);
        window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));window.addFiles({path});
        QVERIFY(waitFor([&]{return ready(window,canvas,painted);}));
        auto *tabs=window.findChild<QTabWidget *>("adjustment-scope-tabs");QVERIFY(tabs);tabs->setCurrentIndex(1);
        auto *create=window.findChild<QPushButton *>("local-new-brush");QVERIFY(create);QVERIFY(create->isEnabled());
        painted.clear();QTest::mouseClick(create,Qt::LeftButton);
        QVERIFY(waitFor([&]{return ready(window,canvas,painted)&&canvas->brushMode();}));
        QCOMPARE(window.currentPhotoDocument()->layers.size(),1);
        window.findChild<QDoubleSpinBox *>("local-radius")->setValue(8);
        QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,point(canvas,.3,.5));
        move(canvas,point(canvas,.7,.5));QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,point(canvas,.7,.5));
        QCOMPARE(window.currentPhotoDocument()->layers[0].mask.components.size(),1);
        QVERIFY(std::get<LocalEdit::BrushGeometry>(window.currentPhotoDocument()->layers[0].mask.components[0].geometry).points.size()>2);
        painted.clear();preview.clear();window.undoEdit();
        QCOMPARE(window.currentPhotoDocument()->layers.size(),1);QVERIFY(window.currentPhotoDocument()->layers[0].mask.components.isEmpty());
        QVERIFY(waitFor([&]{return !preview.isEmpty()&&ready(window,canvas,painted);}));
        painted.clear();preview.clear();window.redoEdit();
        QCOMPARE(window.currentPhotoDocument()->layers[0].mask.components.size(),1);
        QVERIFY(waitFor([&]{return !preview.isEmpty()&&ready(window,canvas,painted);}));
        QSignalSpy rendered(window.findChild<EditLoader *>(),&EditLoader::ready);
        preview.clear();painted.clear();
        QTest::mousePress(canvas,Qt::LeftButton,Qt::AltModifier,point(canvas,.5,.5));
        QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,point(canvas,.5,.5));
        QCOMPARE(window.currentPhotoDocument()->layers[0].mask.components.size(),2);
        QCOMPARE(window.currentPhotoDocument()->layers[0].mask.components.last().operation,LocalEdit::MaskOperation::Subtract);
        QVERIFY(waitFor([&]{return !preview.isEmpty()&&!rendered.isEmpty()&&ready(window,canvas,painted);}));
        const auto erased=qvariant_cast<EditResult>(rendered.last()[0]);QVERIFY(!erased.localMask.isNull());
        QCOMPARE(erased.localMask.constScanLine(32)[48],uchar(0));
        preview.clear();painted.clear();window.undoEdit();
        QCOMPARE(window.currentPhotoDocument()->layers[0].mask.components.size(),1);
        QVERIFY(waitFor([&]{return !preview.isEmpty()&&ready(window,canvas,painted);}));
        const auto before=window.currentPhotoDocument()->layers;
        QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,point(canvas,.2,.25));
        move(canvas,point(canvas,.8,.25));QTest::keyClick(canvas,Qt::Key_Escape);
        QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,point(canvas,.8,.25));
        QCOMPARE(window.currentPhotoDocument()->layers,before);
        QVERIFY(waitFor([&]{return ready(window,canvas,painted);}));
        const auto baseImage=canvas->presenter()->canonicalImage();
        auto *exposure=window.findChild<QSlider *>("slider-local-exposure");QVERIFY(exposure);
        exposure->setSliderDown(true);exposure->setValue(20);exposure->setValue(50);
        preview.clear();painted.clear();exposure->setSliderDown(false);
        QCOMPARE(window.currentPhotoDocument()->layers[0].parameters.exposure,.5);
        QVERIFY(waitFor([&]{return !preview.isEmpty()&&ready(window,canvas,painted);}));
        QVERIFY(hash(baseImage)!=hash(canvas->presenter()->canonicalImage()));
        const auto screenshot=qEnvironmentVariable("LUMINA_LOCAL_UI_SCREENSHOT");
        if(!screenshot.isEmpty())QVERIFY(window.grab().save(screenshot));
        window.undoEdit();QCOMPARE(window.currentPhotoDocument()->layers[0].parameters.exposure,0.);
        QCOMPARE(window.currentPhotoDocument()->layers[0].mask.components.size(),1);
    }
    void spaceSeparatesStrokesAndTextFocusDoesNotPaint(){
        QTemporaryDir directory;const auto path=sample(directory,"space.jpg");QVERIFY(!path.isEmpty());
        Window window(nullptr,QStringLiteral(""));auto *canvas=window.findChild<Canvas *>("photo-canvas");QVERIFY(canvas);
        canvas->setMonitorProfile({},"native-local-test");QSignalSpy painted(canvas,&Canvas::canonicalFramePainted);
        window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));window.addFiles({path});
        QVERIFY(waitFor([&]{return ready(window,canvas,painted);}));
        window.findChild<QTabWidget *>("adjustment-scope-tabs")->setCurrentIndex(1);
        painted.clear();QTest::mouseClick(window.findChild<QPushButton *>("local-new-brush"),Qt::LeftButton);
        QVERIFY(waitFor([&]{return ready(window,canvas,painted)&&canvas->brushMode();}));
        auto *radius=window.findChild<QDoubleSpinBox *>("local-radius");radius->setValue(8);
        const auto start=point(canvas,.3,.4);QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,start);
        move(canvas,start+QPoint(20,0));QTest::keyPress(canvas,Qt::Key_Space);
        QCOMPARE(window.currentPhotoDocument()->layers[0].mask.components.size(),1);
        QVERIFY(!canvas->brushGestureActive());move(canvas,start+QPoint(45,20));QTest::keyRelease(canvas,Qt::Key_Space);
        move(canvas,start+QPoint(60,20));QVERIFY(!canvas->brushGestureActive());
        QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,start+QPoint(60,20));
        QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,start+QPoint(60,20));
        move(canvas,start+QPoint(75,20));QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,start+QPoint(75,20));
        QCOMPARE(window.currentPhotoDocument()->layers[0].mask.components.size(),2);
        QSignalSpy radiusChanges(canvas,&Canvas::brushRadiusStepRequested),overlayChanges(canvas,&Canvas::overlayToggleRequested);
        auto *line=radius->findChild<QLineEdit *>();QVERIFY(line);line->setFocus();
        QTest::keyClick(line,Qt::Key_O);QTest::keyClick(line,Qt::Key_BracketRight);
        QCOMPARE(radiusChanges.size(),0);QCOMPARE(overlayChanges.size(),0);
    }
    void geometryAndComparisonPreserveSourceAnchors(){
        QTemporaryDir directory;const auto path=sample(directory,"geometry.jpg");QVERIFY(!path.isEmpty());
        Window window(nullptr,QStringLiteral(""));auto *canvas=window.findChild<Canvas *>("photo-canvas");
        canvas->setMonitorProfile({},"native-local-test");QSignalSpy painted(canvas,&Canvas::canonicalFramePainted),preview(&window,&Window::previewChanged);
        window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));window.addFiles({path});
        QVERIFY(waitFor([&]{return ready(window,canvas,painted);}));
        window.findChild<QTabWidget *>("adjustment-scope-tabs")->setCurrentIndex(1);
        painted.clear();QTest::mouseClick(window.findChild<QPushButton *>("local-new-brush"),Qt::LeftButton);
        QVERIFY(waitFor([&]{return ready(window,canvas,painted)&&canvas->brushMode();}));
        auto recipe=window.currentRecipe();recipe.crop={.1,.1,.8,.8};recipe.angle=8;
        recipe.flipHorizontal=true;recipe.perspective={.03,-.02};recipe.distortion=.04;
        preview.clear();painted.clear();window.applyRecipe(recipe,"geometry-test");
        QVERIFY(waitFor([&]{return !preview.isEmpty()&&ready(window,canvas,painted)&&canvas->brushMode();}));
        const auto base=window.currentPhotoDocument();QSignalSpy pressed(canvas,&Canvas::brushPressed);
        QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,point(canvas,.35,.4));
        QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,point(canvas,.55,.6));
        QCOMPARE(pressed.size(),1);const auto edited=window.currentPhotoDocument();
        QCOMPARE(edited->layers[0].mask.components.size(),1);
        const auto actual=std::get<LocalEdit::BrushGeometry>(edited->layers[0].mask.components[0].geometry).points.first();
        const auto expected=outputToSource(pressed.first()[0].toPointF(),base->asset.normalizedSize,recipe);
        QVERIFY(QLineF(actual,expected).length()<1e-8);
        const auto layers=edited->layers;
        preview.clear();painted.clear();window.compareOriginal(true);
        QVERIFY(waitFor([&]{return !preview.isEmpty()&&ready(window,canvas,painted);}));
        QCOMPARE(window.currentPhotoDocument()->layers,layers);
        QCOMPARE(canvas->presenter()->canonicalImage().size(),QSize(96,64));
        preview.clear();painted.clear();window.compareOriginal(false);
        QVERIFY(waitFor([&]{return !preview.isEmpty()&&ready(window,canvas,painted);}));
        QCOMPARE(window.currentPhotoDocument()->layers,layers);QCOMPARE(window.currentRecipe(),recipe);
    }
    void switchingPhotoCommitsAndUndoReturnsToThatPhoto(){
        QTemporaryDir directory;const auto a=sample(directory,"first.jpg"),b=sample(directory,"second.jpg");
        QVERIFY(!a.isEmpty()&&!b.isEmpty());Window window(nullptr,QStringLiteral(""));
        auto *canvas=window.findChild<Canvas *>("photo-canvas");canvas->setMonitorProfile({},"native-local-test");
        QSignalSpy painted(canvas,&Canvas::canonicalFramePainted),preview(&window,&Window::previewChanged);
        window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));window.addFiles({a,b});
        QVERIFY(waitFor([&]{return ready(window,canvas,painted);}));
        window.findChild<QTabWidget *>("adjustment-scope-tabs")->setCurrentIndex(1);
        painted.clear();QTest::mouseClick(window.findChild<QPushButton *>("local-new-brush"),Qt::LeftButton);
        QVERIFY(waitFor([&]{return ready(window,canvas,painted)&&canvas->brushMode();}));
        QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,point(canvas,.3,.4));move(canvas,point(canvas,.6,.5));
        preview.clear();painted.clear();window.activate(1);
        QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,point(canvas,.6,.5));
        QVERIFY(waitFor([&]{return !preview.isEmpty()&&ready(window,canvas,painted);}));
        QCOMPARE(window.currentIndex(),1);QVERIFY(window.currentPhotoDocument()->layers.isEmpty());QVERIFY(!canvas->brushMode());
        preview.clear();painted.clear();window.undoEdit();
        QCOMPARE(window.currentIndex(),0);QCOMPARE(window.currentPhotoDocument()->layers.size(),1);
        QVERIFY(window.currentPhotoDocument()->layers[0].mask.components.isEmpty());
        QVERIFY(waitFor([&]{return !preview.isEmpty()&&ready(window,canvas,painted);}));
        window.filter(3); // no rejected photos: both inspector pages must become unavailable
        QCOMPARE(window.currentIndex(),-1);QVERIFY(!window.findChild<QPushButton *>("local-new-brush")->isEnabled());
    }
};
QTEST_MAIN(LocalWindowTests)
#include "local_window_tests.moc"
