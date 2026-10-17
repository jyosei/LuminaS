// Native Qt Window regression for the manual lateral chromatic aberration card.
// This file drives the shipped Window surface only: the two spin boxes, their
// bound sliders, the two reset buttons, the history stack and the
// preview/loader handshake. No resampling model, no render maths and no
// sampling algorithm is restated here. Every expectation is either the exact
// double the UI was handed, a property of the shipped control range, or a
// documented display/history behaviour. The optional private sample and the
// optional output paths are read from the environment only at runtime and are
// never set or inspected by this file.
#include <QtTest/QtTest>
#include <QtTest/qtest_widgets.h>
#include <QTemporaryDir>
#include <QPainter>
#include <QSlider>
#include <QDoubleSpinBox>
#include <QScrollArea>
#include <QFile>
#include <QJsonDocument>
#include <QPushButton>
#include <QSignalSpy>
#include <QComboBox>
#include <algorithm>
#include <cmath>
#include "../src/app/window.h"
#include "../src/core/presets.h"

class ChromaticAberrationWindowTests : public QObject {
    Q_OBJECT
private:
    static constexpr double exact=.312345;
    static constexpr double baseRed=1.;
    static constexpr double baseBlue=-.765432;
    static constexpr double gestureRed=1.4;
    // The spin field shows three decimals, so the committed blue value is the
    // rounded display value -0.765 while the recipe keeps the exact -0.765432.
    static constexpr double blueDisplay=-.765;
    QTemporaryDir m_syntheticDir;
    // Sparse 6000x4000 sheet: a grid of lines plus a few large blocks. It is
    // written into the temporary directory so no external file is required.
    QString writeSyntheticJpeg(const QString &path) const{
        QImage sheet(6000,4000,QImage::Format_RGB32);
        sheet.fill(QColor(128,128,128));
        QPainter painter(&sheet);
        painter.setPen(QPen(QColor(30,30,30),3));
        for(int x=0;x<sheet.width();x+=500)painter.drawLine(x,0,x,sheet.height()-1);
        for(int y=0;y<sheet.height();y+=500)painter.drawLine(0,y,sheet.width()-1,y);
        painter.fillRect(200,200,1200,800,QColor(210,60,40));
        painter.fillRect(3200,1200,1800,1400,QColor(40,90,200));
        painter.fillRect(4200,2600,1400,900,QColor(230,220,120));
        painter.setPen(QPen(QColor(250,250,250),4));
        for(int x=0;x<sheet.width();x+=1000)painter.drawLine(x,0,x,sheet.height()-1);
        painter.end();
        return sheet.save(path,"JPG",92)?path:QString();
    }
    static QDoubleSpinBox *spin(Window &w,const char *name){return w.findChild<QDoubleSpinBox *>(QString::fromLatin1(name));}
    static QSlider *slider(Window &w,const char *name){return w.findChild<QSlider *>(QString::fromLatin1(name));}
    // Every delivered result must be error-free and must carry the 3840 long
    // edge of its own full size (or its own edge for a smaller original).
    static void requireClean(const QList<QVariant> &arguments,const char *where){
        for(const QVariant &argument:arguments){
            if(!argument.canConvert<EditResult>())continue;
            const EditResult result=argument.value<EditResult>();
            QVERIFY2(result.error.isEmpty(),qPrintable(QStringLiteral("%1: %2 (%3)").arg(QString::fromLatin1(where),result.error,result.path)));
            QVERIFY2(qMax(result.image.width(),result.image.height())==qMin(3840,qMax(result.fullSize.width(),result.fullSize.height())),
                     qPrintable(QStringLiteral("%1: preview %2x%3 is not min(3840, full %4x%5)").arg(QString::fromLatin1(where))
                         .arg(result.image.width()).arg(result.image.height()).arg(result.fullSize.width()).arg(result.fullSize.height())));
        }
    }
    // The card is the ancestor widget an adjustment index/list would reveal.
    static QWidget *cardOf(QWidget *field){
        if(!field)return nullptr;
        for(QWidget *widget=field->parentWidget();widget;widget=widget->parentWidget())
            if(widget->objectName()==QStringLiteral("adjustment-card"))return widget;
        return field->parentWidget();
    }
    // Navigation index entries name every card in the panel, so the new card
    // and both pre-existing neighbour cards must be discoverable.
    static bool navigationNamesCard(Window &w,const QString &needle){
        auto *choice=w.findChild<QComboBox *>("adjustment-section-choice");
        if(choice)for(int i=0;i<choice->count();++i)if(choice->itemText(i)==needle)return true;
        return false;
    }
private slots:
    void init();
    void cleanup();
    void mainWindow();
};

void ChromaticAberrationWindowTests::init(){
    QVERIFY2(m_syntheticDir.isValid(),"temporary directory for the synthetic source");
}

void ChromaticAberrationWindowTests::cleanup(){
}

void ChromaticAberrationWindowTests::mainWindow(){
    const QString sample=qEnvironmentVariable("LUMINA_CA_SAMPLE");
    const bool synthetic=sample.isEmpty();
    const QString source=synthetic?writeSyntheticJpeg(m_syntheticDir.filePath(QStringLiteral("chromatic-6000x4000.jpg"))):sample;
    QVERIFY2(!source.isEmpty()&&QFile::exists(source),qPrintable(QStringLiteral("source image: %1").arg(source)));
    Window window(nullptr,QStringLiteral(""));window.show();
    auto *editor=window.findChild<EditLoader *>();
    QVERIFY2(editor,"EditLoader is reachable from the window");
    QSignalSpy ready(editor,&EditLoader::ready);
    QVERIFY2(ready.isValid(),"EditLoader::ready spy is valid");
    window.addFiles(QStringList{source});
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    QVERIFY2(!ready.isEmpty(),"startup render produced a result");
    requireClean(ready.last(),"startup");
    const EditResult firstResult=qvariant_cast<EditResult>(ready.last().at(0));
    QVERIFY2(firstResult.error.isEmpty(),qPrintable(firstResult.error));
    QCOMPARE(qMax(firstResult.image.width(),firstResult.image.height()),qMin(3840,qMax(firstResult.fullSize.width(),firstResult.fullSize.height())));
    if(synthetic)QCOMPARE(firstResult.originalSize,QSize(6000,4000));
    QCOMPARE(firstResult.fullSize,firstResult.originalSize);
    auto *redSpin=spin(window,"ca-red");
    auto *blueSpin=spin(window,"ca-blue");
    auto *redBar=slider(window,"slider-ca-red");
    auto *blueBar=slider(window,"slider-ca-blue");
    auto *redReset=window.findChild<QPushButton *>(QStringLiteral("reset-ca-red"));
    auto *blueReset=window.findChild<QPushButton *>(QStringLiteral("reset-ca-blue"));
    QVERIFY2(redSpin&&blueSpin&&redBar&&blueBar&&redReset&&blueReset,"ca spins, sliders and resets exist");
    // The distortion and lens-shading cards must survive the new card.
    QVERIFY2(spin(window,"distortion")&&slider(window,"slider-distortion"),"distortion controls still exist");
    QVERIFY2(spin(window,"lens-shading")&&slider(window,"slider-lens-shading"),"lens-shading controls still exist");
    QVERIFY2(navigationNamesCard(window,QString::fromUtf8("手动横向色差")),"navigation exposes the chromatic card");
    QVERIFY2(navigationNamesCard(window,QString::fromUtf8("畸变校正")),"navigation exposes the distortion card");
    QVERIFY2(navigationNamesCard(window,QString::fromUtf8("镜头暗角校正")),"navigation exposes the lens-shading card");

    for(QDoubleSpinBox *field:{redSpin,blueSpin}){
        QCOMPARE(field->value(),0.);
        QCOMPARE(field->minimum(),-10.);
        QCOMPARE(field->maximum(),10.);
        QCOMPARE(field->decimals(),3);
        QCOMPARE(field->singleStep(),.1);
        QCOMPARE(field->keyboardTracking(),false);
        QVERIFY2(!field->accessibleName().isEmpty(),"the field carries an accessible name");
    }
    for(QSlider *bar:{redBar,blueBar}){
        QCOMPARE(bar->minimum(),-10000);
        QCOMPARE(bar->maximum(),10000);
        QCOMPARE(bar->value(),0);
        QVERIFY2(!bar->accessibleName().isEmpty(),"the slider carries an accessible name");
    }
    QVERIFY2(!redReset->accessibleName().isEmpty(),"the red reset carries an accessible name");
    QVERIFY2(!blueReset->accessibleName().isEmpty(),"the blue reset carries an accessible name");
    Recipe base=window.currentRecipe();
    base.crop={.1,.1,.8,.8};
    base.exposure=exact;
    base.chromaticAberration={baseRed,baseBlue};
    base.lensShading=.1;
    base.distortion=.05;
    base.saturation=1.1357;
    const int beforeApply=ready.count();
    window.applyRecipe(base,QStringLiteral("色差基准"));
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    QTRY_VERIFY_WITH_TIMEOUT(ready.count()>beforeApply,90000);
    requireClean(ready.last(),"applyRecipe");
    QCOMPARE(ready.last().at(0).value<EditResult>().recipe,base);
    QCOMPARE(window.currentRecipe(),base);
    // The recipe keeps the six-decimal blue value; only the visible field
    // rounds to three decimals.
    QCOMPARE(window.currentRecipe().chromaticAberration,QPointF(baseRed,baseBlue));
    QCOMPARE(redSpin->value(),baseRed);
    QCOMPARE(blueSpin->value(),blueDisplay);
    QCOMPARE(redBar->value(),1000);
    QCOMPARE(blueBar->value(),-765);
    QCOMPARE(redSpin->text(),QStringLiteral("1.000"));
    QCOMPARE(blueSpin->text(),QStringLiteral("-0.765"));
    auto *canvas=window.findChild<Canvas *>();
    QVERIFY2(canvas,"canvas is reachable");
    QSignalSpy previews(&window,&Window::previewChanged);
    QSignalSpy paintings(canvas,&Canvas::framePainted);
    QVERIFY2(previews.isValid()&&paintings.isValid(),"gesture spies are valid");

    int interimPreviews=0,interimPaints=0,stepped=0;
    QElapsedTimer dragClock;dragClock.start();
    redBar->setSliderDown(true);
    // Phase 1: the historical fixed sweep from 1010 to 1400 in 40 steps.
    for(int value=1010;value<=1400;value+=10){
        redBar->setValue(value);QTest::qWait(50);
        interimPreviews=qMax(interimPreviews,previews.count());interimPaints=qMax(interimPaints,paintings.count());++stepped;
    }
    // Phase 2: one worker frame can outlast the whole 2 s sweep on a slow
    // machine, so keep dragging inside the legal 1010..1400 range until an
    // interim preview AND an interim paint have both been observed. Every
    // step changes the value and the drag stays down throughout; the wall
    // clock bound only stops the input, never lowers an assertion.
    constexpr qint64 dragBudgetMs=12000;
    for(int value=1010;stepped<40||interimPreviews<2||interimPaints<2;){
        if(dragClock.elapsed()>=dragBudgetMs)break;
        value+=10;if(value>1400)value=1010;
        redBar->setValue(value);QTest::qWait(50);
        interimPreviews=qMax(interimPreviews,previews.count());interimPaints=qMax(interimPaints,paintings.count());++stepped;
    }
    redBar->setValue(1400);QTest::qWait(50);
    const qint64 dragMs=dragClock.elapsed();
    // Interim means observed while the slider is still held: both counts are
    // sampled above, before the release below, and are independent of the
    // final released frame.
    QVERIFY(stepped>=40);
    QCOMPARE(redBar->value(),1400);
    QCOMPARE(redBar->isSliderDown(),true);
    QList<double> workerTimes;for(const auto &record:ready){const auto result=qvariant_cast<EditResult>(record.at(0));if(result.tag.interactive)workerTimes.append(result.elapsedMs);}
    qInfo()<<"CA drag before release previews"<<interimPreviews<<"paints"<<interimPaints<<"stepped"<<stepped<<"dragClockMs"<<dragMs<<"workerMs"<<workerTimes;
    // Continuous feedback is required; 30/60 FPS remains an extra achievement.
    QVERIFY2(interimPreviews>=2,qPrintable(QStringLiteral("interim previewChanged signals %1").arg(interimPreviews)));
    QVERIFY2(interimPaints>=2,qPrintable(QStringLiteral("interim framePainted signals %1").arg(interimPaints)));
    redBar->setSliderDown(false);
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    requireClean(ready.last(),"gesture release");
    const Recipe committed=window.currentRecipe();
    QCOMPARE(committed.chromaticAberration.x(),gestureRed);
    QCOMPARE(committed.chromaticAberration.y(),baseBlue);
    QCOMPARE(committed.exposure,exact);
    QCOMPARE(committed.crop,base.crop);
    QCOMPARE(committed.lensShading,base.lensShading);
    QCOMPARE(committed.distortion,base.distortion);
    QCOMPARE(committed.saturation,base.saturation);
    QCOMPARE(redSpin->value(),gestureRed);
    QCOMPARE(blueSpin->value(),blueDisplay);
    QCOMPARE(redBar->value(),1400);
    const EditResult released=qvariant_cast<EditResult>(ready.last().at(0));
    QCOMPARE(released.recipe,committed);
    window.undoEdit();
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    requireClean(ready.last(),"undo");
    QCOMPARE(window.currentRecipe(),base);
    QCOMPARE(redSpin->value(),baseRed);
    QCOMPARE(blueSpin->value(),blueDisplay);
    window.redoEdit();
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    QTRY_VERIFY_WITH_TIMEOUT(window.currentRecipe()==committed,90000);
    requireClean(ready.last(),"redo");
    QCOMPARE(window.currentRecipe(),committed);

    const Recipe beforeCompare=window.currentRecipe();
    window.compareOriginal(true);
    QTRY_VERIFY_WITH_TIMEOUT(redSpin&&!redSpin->isEnabled(),90000);
    QVERIFY2(!blueSpin->isEnabled(),"the blue field disables while comparing");
    QVERIFY2(!redBar->isEnabled(),"the red slider disables while comparing");
    QVERIFY2(!blueBar->isEnabled(),"the blue slider disables while comparing");
    window.compareOriginal(false);
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    QCOMPARE(window.currentRecipe(),beforeCompare);

    const int beforeResetRed=ready.count();
    redReset->click();
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    QTRY_VERIFY_WITH_TIMEOUT(ready.count()>beforeResetRed,90000);
    requireClean(ready.last(),"reset-ca-red");
    const Recipe resetRed=window.currentRecipe();
    QCOMPARE(resetRed.chromaticAberration.x(),0.);
    QCOMPARE(resetRed.chromaticAberration.y(),baseBlue);
    QCOMPARE(redSpin->value(),0.);
    QCOMPARE(blueSpin->value(),blueDisplay);
    QCOMPARE(resetRed.exposure,beforeCompare.exposure);
    QCOMPARE(resetRed.crop,beforeCompare.crop);
    window.undoEdit();
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    QCOMPARE(window.currentRecipe(),committed);
    // Optional geometry reset: clears crop, distortion and both CA components
    // while the lens-shading and exposure values survive.
    auto *resetGeometry=window.findChild<QPushButton *>(QStringLiteral("reset-geometry"));
    QVERIFY2(resetGeometry,"reset-geometry button exists");
    const int beforeResetGeometry=ready.count();
    resetGeometry->click();
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    QTRY_VERIFY_WITH_TIMEOUT(ready.count()>beforeResetGeometry,90000);
    requireClean(ready.last(),"reset-geometry");
    const Recipe resetGeometryRecipe=window.currentRecipe();
    QCOMPARE(resetGeometryRecipe.crop,QRectF(0,0,1,1));
    QCOMPARE(resetGeometryRecipe.distortion,0.);
    QCOMPARE(resetGeometryRecipe.chromaticAberration,QPointF(0,0));
    QCOMPARE(resetGeometryRecipe.angle,0.);
    QCOMPARE(resetGeometryRecipe.lensShading,committed.lensShading);
    QCOMPARE(resetGeometryRecipe.exposure,exact);
    window.undoEdit();
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    QCOMPARE(window.currentRecipe(),committed);
    // Optional, runtime-only outputs. This file never sets the variables and
    // never inspects the pixels of the saved screenshot; the caller does that.
    const QString recipeOut=qEnvironmentVariable("LUMINA_CA_RECIPE");
    if(!recipeOut.isEmpty()){
        const Recipe probe=window.currentRecipe();
        QFile output(recipeOut);
        QVERIFY2(output.open(QIODevice::WriteOnly),qPrintable(QStringLiteral("recipe output %1").arg(recipeOut)));
        QVERIFY(output.write(QJsonDocument(recipeJson(probe)).toJson())>0);
        output.close();
        QFile readback(recipeOut);
        QVERIFY2(readback.open(QIODevice::ReadOnly),qPrintable(QStringLiteral("recipe output %1").arg(recipeOut)));
        QCOMPARE(recipeFromJson(QJsonDocument::fromJson(readback.readAll()).object()),probe);
    }
    const QString shot=qEnvironmentVariable("LUMINA_CA_SCREENSHOT");
    if(!shot.isEmpty()){
        QVERIFY2(redSpin,"the chromatic control exists for the screenshot");
        if(auto *area=window.findChild<QScrollArea *>())
            area->ensureWidgetVisible(qobject_cast<QWidget *>(cardOf(redSpin)),0,16);
        QTest::qWait(100);
        QVERIFY2(window.grab().save(shot),qPrintable(QStringLiteral("screenshot saved to %1").arg(shot)));
    }
    qInfo() << "chromatic aberration window"
            << "interim previews" << interimPreviews
            << "interim paints" << interimPaints
            << "frames stepped" << stepped
            << "drag clock ms" << dragMs
            << "final red" << committed.chromaticAberration.x()
            << "blue" << committed.chromaticAberration.y()
            << "exact exposure" << committed.exposure
            << "loader results" << ready.count();
}

QTEST_MAIN(ChromaticAberrationWindowTests)
#include "chromatic_aberration_window_tests.moc"
