#include <QtTest>
#include <QtGui/qtestsupport_gui.h>
#include <QApplication>
#include <QLineEdit>
#include <QSignalSpy>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QFocusEvent>
#include <QEvent>
#include <cmath>
#include <limits>
#include "app/canvas.h"
#include "app/display_presenter.h"

namespace {
const QSize kImageSize(256,160);
QImage syntheticImage(){QImage i(kImageSize,QImage::Format_RGBA8888);i.fill(QColor(90,110,130));return i;}
struct Fit{double scale=1;QPointF offset;};
Fit fitOf(const Canvas &canvas){
    const double s=std::min(double(canvas.width())/kImageSize.width(),
                            double(canvas.height())/kImageSize.height())*.95;
    return {s,QPointF((canvas.width()-kImageSize.width()*s)/2.,
                      (canvas.height()-kImageSize.height()*s)/2.)};
}
QPoint widgetAt(const Fit &fit,double u,double v){
    return QPoint(int(std::lround(fit.offset.x()+u*kImageSize.width()*fit.scale)),
                  int(std::lround(fit.offset.y()+v*kImageSize.height()*fit.scale)));
}
void showCanvas(Canvas &canvas){
    canvas.resize(400,300);
    canvas.setMonitorProfile(QByteArray(),QStringLiteral("canvas-input-test"));
    canvas.show();
    QVERIFY(QTest::qWaitForWindowExposed(&canvas));
}
void presentImage(Canvas &canvas,QSignalSpy &painted,const QString &context=QStringLiteral("ctx")){
    canvas.setImage(syntheticImage(),context);
    QTRY_VERIFY(painted.count()>0);
}
void sendMove(Canvas &canvas,const QPoint &position,Qt::MouseButtons buttons=Qt::LeftButton){
    QMouseEvent event(QEvent::MouseMove,QPointF(position),canvas.mapToGlobal(position),
                      Qt::NoButton,buttons,Qt::NoModifier);
    QApplication::sendEvent(&canvas,&event);
}
bool near(double value,double expected,double tolerance=0.01){return std::abs(value-expected)<=tolerance;}
}

class LocalCanvasInputTests : public QObject {
    Q_OBJECT
private slots:
    void maskStyleOnlyRepaintsWithoutChangingCanonicalOrCoverage(){
        Canvas canvas;showCanvas(canvas);QSignalSpy photo(&canvas,&Canvas::framePainted),coverage(&canvas,&Canvas::localMaskPainted);
        presentImage(canvas,photo);const auto source=canvas.presenter()->canonicalImage();const auto key=source.cacheKey();
        QImage mask(kImageSize,QImage::Format_Indexed8);mask.fill(127);canvas.setLocalMask(mask);
        canvas.repaint();QCoreApplication::processEvents();QVERIFY(!coverage.isEmpty());
        const int photos=photo.count(),masks=coverage.count();
        canvas.setLocalMaskStyle(QColor(255,0,0),false);canvas.repaint();const auto red=canvas.grab();
        canvas.setLocalMaskStyle(QColor(255,0,0),true);canvas.repaint();const auto bw=canvas.grab();
        QCoreApplication::processEvents();QCOMPARE(photo.count(),photos);QCOMPARE(coverage.count(),masks);
        QCOMPARE(canvas.presenter()->canonicalImage().cacheKey(),key);QCOMPARE(canvas.presenter()->canonicalImage(),source);
        QCOMPARE(mask.colorCount(),0);QCOMPARE(mask.pixelIndex(10,10),127);
        const QPoint p(int(canvas.width()/2.*bw.devicePixelRatioF()),int(canvas.height()/2.*bw.devicePixelRatioF()));
        const auto a=red.toImage().pixelColor(p),b=bw.toImage().pixelColor(p);
        QVERIFY(a!=b);QCOMPARE(b,QColor(127,127,127));
    }
    void gradientHandlesHitInDipsOutsidePhotoWithoutPhotoPaint(){
        Canvas canvas;showCanvas(canvas);QSignalSpy painted(&canvas,&Canvas::framePainted);
        presentImage(canvas,painted);const auto fit=fitOf(canvas);
        canvas.setBrushMode(true);canvas.setGradientMode(true);
        const double nan=std::numeric_limits<double>::quiet_NaN();
        canvas.setGradientHandles({QPointF(.5,.5),QPointF(-.015,.5),QPointF(nan,nan)});
        canvas.repaint();QCoreApplication::processEvents();const int baseline=painted.count();
        canvas.setGradientHandles({QPointF(.5,.5),QPointF(-.015,.5)});
        canvas.setGradientGuide({QPointF(.2,.2),QPointF(.7,.8),QPointF(nan,nan),QPointF(.8,.2),QPointF(.2,.8)});
        canvas.repaint();QCoreApplication::processEvents();
        QCOMPARE(painted.count(),baseline);
        QSignalSpy pressed(&canvas,&Canvas::brushPressed),finished(&canvas,&Canvas::brushFinished);
        const auto centre=widgetAt(fit,.5,.5);
        QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,centre+QPoint(8,0));
        QCOMPARE(canvas.pressedGradientHandle(),0);QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::NoModifier,centre+QPoint(8,0));
        QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,centre+QPoint(12,0));
        QCOMPARE(canvas.pressedGradientHandle(),-1);QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::NoModifier,centre+QPoint(12,0));
        const auto outside=widgetAt(fit,-.015,.5);QVERIFY(outside.x()>=0);
        QTest::mousePress(&canvas,Qt::LeftButton,Qt::AltModifier,outside);
        QCOMPARE(canvas.pressedGradientHandle(),1);QVERIFY(pressed.last()[0].toPointF().x()<0);QVERIFY(pressed.last()[1].toBool());
        canvas.setGradientMode(false);QVERIFY(!canvas.brushGestureActive());QCOMPARE(finished.count(),3);
        QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::NoModifier,outside);QCOMPARE(finished.count(),3);
        canvas.setGradientMode(true);canvas.actualPixels();
        const QPoint actualCentre(canvas.width()/2,canvas.height()/2);
        QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,actualCentre+QPoint(8,0));
        QCOMPARE(canvas.pressedGradientHandle(),0);canvas.finishBrushGesture(true);
        QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::NoModifier,actualCentre+QPoint(8,0));
        painted.clear();presentImage(canvas,painted,QStringLiteral("another-photo"));
        QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,actualCentre);
        QCOMPARE(canvas.pressedGradientHandle(),-1);canvas.finishBrushGesture(true);
    }
    // 1. Normalised press/move/final-release, exactly one finish, release ends it.
    void pressMoveReleaseNormalizedSingleFinish(){
        Canvas canvas;showCanvas(canvas);
        QSignalSpy painted(&canvas,&Canvas::framePainted);
        presentImage(canvas,painted);
        const Fit fit=fitOf(canvas);
        canvas.setBrushMode(true);
        QVERIFY(canvas.brushMode());QVERIFY(!canvas.brushGestureActive());
        QSignalSpy pressed(&canvas,&Canvas::brushPressed);
        QSignalSpy moved(&canvas,&Canvas::brushMoved);
        QSignalSpy finished(&canvas,&Canvas::brushFinished);
        const QPoint start=widgetAt(fit,.25,.5);
        QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,start);
        QCOMPARE(pressed.count(),1);QVERIFY(canvas.brushGestureActive());
        const auto startUv=pressed.at(0).at(0).toPointF();
        QVERIFY(near(startUv.x(),.25));QVERIFY(near(startUv.y(),.5));
        QCOMPARE(pressed.at(0).at(1).toBool(),false);
        const QPoint middle=widgetAt(fit,.75,.25);
        sendMove(canvas,middle);
        QCOMPARE(moved.count(),1);
        QVERIFY(near(moved.at(0).at(0).toPointF().x(),.75));
        QVERIFY(near(moved.at(0).at(0).toPointF().y(),.25));
        QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::NoModifier,middle);
        QCOMPARE(moved.count(),2); // final release position appended once
        QVERIFY(near(moved.at(1).at(0).toPointF().x(),.75));
        QCOMPARE(finished.count(),1);QCOMPARE(finished.at(0).at(0).toBool(),false);
        QVERIFY(!canvas.brushGestureActive());
        // Idempotent: an extra finish emits nothing more.
        canvas.finishBrushGesture(false);QCOMPARE(finished.count(),1);
        sendMove(canvas,middle);QCOMPARE(moved.count(),2); // no stroke without a new press
        // A move outside the image is still emitted while the stroke is open.
        QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,start);
        sendMove(canvas,QPoint(widgetAt(fit,.0,.5).x()-40,middle.y()));
        QVERIFY(canvas.brushGestureActive());
        QVERIFY(moved.last().at(0).toPointF().x()<0.0);
        QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::NoModifier,start);
        QCOMPARE(finished.count(),2);
    }
    // 2. Alt subtract is frozen at press; mid-stroke modifier/middle changes do nothing.
    void altIntentFrozenAndModifiersIgnored(){
        Canvas canvas;showCanvas(canvas);
        QSignalSpy painted(&canvas,&Canvas::framePainted);presentImage(canvas,painted);
        const Fit fit=fitOf(canvas);canvas.setBrushMode(true);
        QSignalSpy pressed(&canvas,&Canvas::brushPressed);
        QSignalSpy finished(&canvas,&Canvas::brushFinished);
        const QPoint point=widgetAt(fit,.5,.5);
        QTest::mousePress(&canvas,Qt::LeftButton,Qt::AltModifier,point);
        QCOMPARE(pressed.count(),1);QCOMPARE(pressed.at(0).at(1).toBool(),true);
        QSignalSpy moved(&canvas,&Canvas::brushMoved);
        sendMove(canvas,widgetAt(fit,.6,.5));
        QTest::mousePress(&canvas,Qt::MiddleButton,Qt::NoModifier,point);
        QTest::mouseRelease(&canvas,Qt::MiddleButton,Qt::NoModifier,point);
        QCOMPARE(pressed.count(),1);QVERIFY(canvas.brushGestureActive());
        QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::NoModifier,widgetAt(fit,.6,.5));
        QCOMPARE(finished.count(),1);QCOMPARE(finished.at(0).at(0).toBool(),false);
        QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,point);
        QCOMPARE(pressed.count(),2);QCOMPARE(pressed.at(1).at(1).toBool(),false);
        QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::NoModifier,point);
    }
    // 3. Space commits then pans while left is held; no restart until release+new press.
    void spaceCommitsThenPansAndNeedsNewPress(){
        Canvas canvas;showCanvas(canvas);
        QSignalSpy painted(&canvas,&Canvas::framePainted);presentImage(canvas,painted);
        const Fit fit=fitOf(canvas);canvas.setBrushMode(true);
        QSignalSpy pressed(&canvas,&Canvas::brushPressed);
        QSignalSpy moved(&canvas,&Canvas::brushMoved);
        QSignalSpy finished(&canvas,&Canvas::brushFinished);
        const QPoint start=widgetAt(fit,.4,.4);
        QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,start);
        sendMove(canvas,widgetAt(fit,.5,.45));
        QCOMPARE(moved.count(),1);
        QTest::keyPress(&canvas,Qt::Key_Space);
        QVERIFY(!canvas.brushGestureActive());QCOMPARE(finished.count(),1);
        QCOMPARE(finished.at(0).at(0).toBool(),false);
        QCOMPARE(canvas.cursor().shape(),Qt::ClosedHandCursor);
        const QImage beforePan=canvas.grab().toImage();
        sendMove(canvas,start+QPoint(30,20));
        QCOMPARE(moved.count(),1); // panning never paints
        QVERIFY(canvas.grab().toImage()!=beforePan);
        QTest::keyRelease(&canvas,Qt::Key_Space);
        sendMove(canvas,start+QPoint(60,40));
        QCOMPARE(moved.count(),1);QCOMPARE(pressed.count(),1); // still no automatic new stroke
        QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::NoModifier,start+QPoint(60,40));
        QCOMPARE(pressed.count(),1);
        const Fit panned=fitOf(canvas); // geometry is stale after pan: press where the image now is
        QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,panned.offset.toPoint()+QPoint(40,40));
        QCOMPARE(pressed.count(),2);
        QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::NoModifier,panned.offset.toPoint()+QPoint(40,40));
        // Autorepeat Space must not commit or start anything.
        QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,start);
        QVERIFY(canvas.brushGestureActive());
        const int finishedBefore=finished.count();
        QKeyEvent repeat(QEvent::KeyPress,Qt::Key_Space,Qt::NoModifier,QString(),true);
        QApplication::sendEvent(&canvas,&repeat);
        QVERIFY(canvas.brushGestureActive());QCOMPARE(finished.count(),finishedBefore);
        QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::NoModifier,start);
        QCOMPARE(finished.count(),finishedBefore+1);
    }
    // 4. Wheel and Esc: scroll commits an open stroke, Esc cancels, idle Esc goes to host.
    void wheelCommitsEscapeCancels(){
        Canvas canvas;showCanvas(canvas);
        QSignalSpy painted(&canvas,&Canvas::framePainted);presentImage(canvas,painted);
        const Fit fit=fitOf(canvas);canvas.setBrushMode(true);
        QSignalSpy moved(&canvas,&Canvas::brushMoved);
        QSignalSpy finished(&canvas,&Canvas::brushFinished);
        const QPoint point=widgetAt(fit,.5,.5);
        QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,point);
        QVERIFY(canvas.brushGestureActive());
        const double fitScale=canvas.zoom();
        QWheelEvent wheel(QPointF(point),canvas.mapToGlobal(point),QPoint(),QPoint(0,120),
                          Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
        QApplication::sendEvent(&canvas,&wheel);
        QVERIFY(canvas.zoom()>fitScale);
        QCOMPARE(finished.count(),1);QCOMPARE(finished.at(0).at(0).toBool(),false);
        const int moveCount=moved.count();
        sendMove(canvas,point);QCOMPARE(moved.count(),moveCount); // zoom needs a new press
        QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::NoModifier,point);
        QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,point);
        QVERIFY(canvas.brushGestureActive());
        QTest::keyClick(&canvas,Qt::Key_Escape);
        QCOMPARE(finished.count(),2);QCOMPARE(finished.at(1).at(0).toBool(),true);
        QVERIFY(!canvas.brushGestureActive());
        QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::NoModifier,point);
        QKeyEvent idle(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);
        QApplication::sendEvent(&canvas,&idle);
        QVERIFY(!idle.isAccepted());
        QCOMPARE(finished.count(),2); // release after cancellation must not finish twice
    }
    // 5. Same-size feedback keeps the stroke; a geometry change and actualPixels close it.
    void sameSizeKeepsGestureGeometryChangeCloses(){
        Canvas canvas;showCanvas(canvas);
        QSignalSpy painted(&canvas,&Canvas::framePainted);presentImage(canvas,painted);
        const Fit fit=fitOf(canvas);canvas.setBrushMode(true);
        QSignalSpy moved(&canvas,&Canvas::brushMoved);
        QSignalSpy finished(&canvas,&Canvas::brushFinished);
        const QPoint point=widgetAt(fit,.5,.5);
        QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,point);
        canvas.setImage(syntheticImage(),QStringLiteral("ctx")); // async RGB feedback, same geometry
        QVERIFY(canvas.brushGestureActive());QCOMPARE(finished.count(),0);
        sendMove(canvas,widgetAt(fit,.6,.5));QCOMPARE(moved.count(),1);
        canvas.actualPixels();
        QVERIFY(!canvas.brushGestureActive());QCOMPARE(finished.count(),1);
        QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::NoModifier,point);
        canvas.fit();
        QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,point);
        QVERIFY(canvas.brushGestureActive());
        QImage bigger(320,200,QImage::Format_RGBA8888);bigger.fill(QColor(20,40,60));
        canvas.setImage(bigger,QStringLiteral("ctx-b"));
        QCOMPARE(finished.count(),2);QVERIFY(!canvas.brushGestureActive());
        QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::NoModifier,point);
    }
    // 6. Focus loss and window deactivation commit once; text focus keeps shortcuts local.
    void focusLossCommitsAndTextFocusIsIsolated(){
        Canvas canvas;showCanvas(canvas);
        QSignalSpy painted(&canvas,&Canvas::framePainted);presentImage(canvas,painted);
        const Fit fit=fitOf(canvas);canvas.setBrushMode(true);
        QSignalSpy moved(&canvas,&Canvas::brushMoved);
        QSignalSpy finished(&canvas,&Canvas::brushFinished);
        const QPoint point=widgetAt(fit,.5,.5);
        QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,point);
        QFocusEvent out(QEvent::FocusOut,Qt::OtherFocusReason);
        QApplication::sendEvent(&canvas,&out);
        QCOMPARE(finished.count(),1);QCOMPARE(finished.at(0).at(0).toBool(),false);
        QVERIFY(!canvas.brushGestureActive());
        QApplication::sendEvent(&canvas,&out);QCOMPARE(finished.count(),1); // idempotent
        QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::NoModifier,point);
        const int moveCount=moved.count();
        sendMove(canvas,point);QCOMPARE(moved.count(),moveCount); // leftDown was reset
        QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,point);
        QEvent deactivate(QEvent::WindowDeactivate);
        QApplication::sendEvent(&canvas,&deactivate);
        QCOMPARE(finished.count(),2);QVERIFY(!canvas.brushGestureActive());
        QApplication::sendEvent(&canvas,&deactivate);QCOMPARE(finished.count(),2);
        QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::NoModifier,point);
        // A focused sibling text field consumes the keys; the canvas stays silent.
        QWidget host;Canvas nested;QLineEdit edit(&host);nested.setParent(&host);
        host.resize(500,400);edit.setGeometry(0,0,200,30);host.show();
        QVERIFY(QTest::qWaitForWindowExposed(&host));
        edit.setFocus();QVERIFY(edit.hasFocus());
        QSignalSpy radius(&nested,&Canvas::brushRadiusStepRequested);
        QSignalSpy overlay(&nested,&Canvas::overlayToggleRequested);
        QTest::keyClick(&edit,Qt::Key_BracketRight);
        QTest::keyClick(&edit,Qt::Key_O);
        QCOMPARE(radius.count(),0);QCOMPARE(overlay.count(),0);
        QVERIFY(!edit.text().isEmpty());
    }
    // 7. Bracket/overlay shortcuts finish first; crop and picker stay exclusive with brush.
    void shortcutsAndModeExclusivity(){
        Canvas canvas;showCanvas(canvas);
        QSignalSpy painted(&canvas,&Canvas::framePainted);presentImage(canvas,painted);
        const Fit fit=fitOf(canvas);canvas.setBrushMode(true);
        QSignalSpy finished(&canvas,&Canvas::brushFinished);
        QSignalSpy radius(&canvas,&Canvas::brushRadiusStepRequested);
        QSignalSpy overlay(&canvas,&Canvas::overlayToggleRequested);
        const QPoint point=widgetAt(fit,.5,.5);
        QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,point);
        QTest::keyClick(&canvas,Qt::Key_BracketLeft);
        QCOMPARE(finished.count(),1);QCOMPARE(radius.count(),1);QCOMPARE(radius.at(0).at(0).toInt(),-1);
        QVERIFY(!canvas.brushGestureActive());
        QTest::keyClick(&canvas,Qt::Key_BracketRight);
        QCOMPARE(radius.count(),2);QCOMPARE(radius.at(1).at(0).toInt(),1);QCOMPARE(finished.count(),1);
        QTest::keyClick(&canvas,Qt::Key_O);
        QCOMPARE(overlay.count(),1);QCOMPARE(finished.count(),1);
        QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::NoModifier,point);
        // Enabling crop/picker leaves brush mode.
        canvas.setCropMode(true);QVERIFY(!canvas.brushMode());
        canvas.setPickerMode(true);QVERIFY(!canvas.brushMode());
        QSignalSpy picked(&canvas,&Canvas::cropPicked);
        QSignalSpy neutral(&canvas,&Canvas::neutralPicked);
        QSignalSpy pressed(&canvas,&Canvas::brushPressed);
        canvas.setBrushMode(true);QVERIFY(canvas.brushMode());
        QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,point);
        QCOMPARE(pressed.count(),1);QCOMPARE(neutral.count(),0);
        QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::NoModifier,point);
        QCOMPARE(picked.count(),0);
        canvas.setMessage(QStringLiteral("no image")); // clears the canonical frame
        QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,point);
        QCOMPARE(pressed.count(),1);QCOMPARE(finished.count(),2);
        canvas.setMessage(QString());
        const int frames=painted.count();
        canvas.setImage(syntheticImage(),QStringLiteral("late"));
        QTRY_VERIFY(painted.count()>frames);
        canvas.setBrushMode(true);
        QSignalSpy pressed2(&canvas,&Canvas::brushPressed);
        QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,point);
        QCOMPARE(pressed2.count(),1);
        QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::NoModifier,point);
    }
    // 8. Legacy hand pan/zoom/double-click and the view-only mask stay unchanged.
    void legacyHandAndMaskBehaviour(){
        Canvas canvas;showCanvas(canvas);
        QSignalSpy painted(&canvas,&Canvas::framePainted);presentImage(canvas,painted);
        const Fit fit=fitOf(canvas);
        const double fitScale=canvas.zoom();
        QVERIFY(!canvas.brushMode());
        QWheelEvent wheel(QPointF(widgetAt(fit,.5,.5)),canvas.mapToGlobal(widgetAt(fit,.5,.5)),
                          QPoint(),QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
        QApplication::sendEvent(&canvas,&wheel);
        QVERIFY(canvas.zoom()>fitScale);
        const QPoint panFrom=widgetAt(fit,.5,.5);
        QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,panFrom);
        QCOMPARE(canvas.cursor().shape(),Qt::ClosedHandCursor);
        const QImage beforePan=canvas.grab().toImage();
        sendMove(canvas,panFrom+QPoint(20,15));
        QVERIFY(canvas.grab().toImage()!=beforePan);
        QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::NoModifier,panFrom+QPoint(20,15));
        QTest::mouseDClick(&canvas,Qt::LeftButton,Qt::NoModifier,widgetAt(fit,.5,.5));
        QCOMPARE(canvas.zoom(),fitScale);
        canvas.setCropMode(true);QCOMPARE(canvas.cursor().shape(),Qt::CrossCursor);
        canvas.setCropMode(false);QCOMPARE(canvas.cursor().shape(),Qt::OpenHandCursor);
        // View-only mask: palette-driven paint, no extra framePainted, canonical untouched.
        const QImage canonicalBefore=canvas.presenter()->canonicalImage();
        const Fit fresh=fitOf(canvas);
        const QPoint left=widgetAt(fresh,.25,.5),right=widgetAt(fresh,.75,.5);
        const QImage baseline=canvas.grab().toImage();
        const int frames=painted.count();
        QImage mask(kImageSize,QImage::Format_Indexed8);mask.fill(0);
        for(int y=0;y<kImageSize.height();++y)
            for(int x=0;x<kImageSize.width()/2;++x)mask.scanLine(y)[x]=255;
        canvas.setLocalMask(mask);
        const QImage tinted=canvas.grab().toImage();
        QCOMPARE(painted.count(),frames); // mask-only repaint is not a new frame
        QVERIFY(tinted.pixelColor(left).blue()>baseline.pixelColor(left).blue()+20);
        QVERIFY(std::abs(tinted.pixelColor(right).blue()-baseline.pixelColor(right).blue())<=1);
        QCOMPARE(canvas.presenter()->canonicalImage(),canonicalBefore);
        QImage rgbMask(kImageSize,QImage::Format_RGB32);rgbMask.fill(Qt::white);
        canvas.setLocalMask(rgbMask);
        QCOMPARE(canvas.grab().toImage().pixelColor(left),baseline.pixelColor(left));
        QImage wrongSize(kImageSize.width()+8,kImageSize.height(),QImage::Format_Indexed8);
        wrongSize.fill(255);
        canvas.setLocalMask(wrongSize);
        QCOMPARE(canvas.grab().toImage().pixelColor(left),baseline.pixelColor(left));
        canvas.setLocalMask(mask);
        QVERIFY(canvas.grab().toImage().pixelColor(left).blue()>baseline.pixelColor(left).blue()+20);
        canvas.setImage(syntheticImage(),QStringLiteral("ctx-2")); // context change clears the mask
        QSignalSpy repainted(&canvas,&Canvas::framePainted);
        QTRY_VERIFY(repainted.count()>0);
        QCOMPARE(canvas.grab().toImage().pixelColor(left),baseline.pixelColor(left));
    }
};

QTEST_MAIN(LocalCanvasInputTests)
#include "local_canvas_input_tests.moc"

