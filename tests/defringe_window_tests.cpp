// Native Qt Window regression for the purple/green defringe card. This file
// drives the shipped Window surface only: the seven spin boxes, their bound
// sliders, the seven reset buttons, the history stack and the
// preview/loader handshake. No fringe operator, no hue-window maths and no
// render algorithm is restated here. Every expectation is either the exact
// double the UI was handed, a property of the shipped control range, or a
// documented display/history behaviour. The synthetic source is generated into
// a temporary directory; no private media is read by this file.
#include <QtTest/QtTest>
#include <QtTest/qtest_widgets.h>
#include <QTemporaryDir>
#include <QPainter>
#include <QSlider>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QComboBox>
#include <QFile>
#include <cmath>
#include "../src/app/window.h"
#include "../src/core/presets.h"

class DefringeWindowTests : public QObject {
    Q_OBJECT
private:
    static constexpr double exact=.312345;
    // A three-decimal field that shows 0.765 keeps 0.765432 in the recipe.
    static constexpr double purpleExact=.765432;
    static constexpr double purpleDisplay=.765;
    static constexpr double greenExact=.412345;
    static constexpr double greenDisplay=.412;
    static constexpr double radiusExact=7.5;
    static constexpr double hueExact=222.5;
    static constexpr double widthExact=61.5;
    QTemporaryDir m_syntheticDir;
    // Sparse 6000x4000 sheet: a grid of lines plus a few large blocks, written
    // into the temporary directory so no external file is required.
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
        // Known thin purple fringe spanning a neutral black/white edge. This
        // makes feedback checks observe changed pixels, not only paint signals.
        painter.fillRect(1400,3000,800,600,QColor(20,20,20));
        painter.fillRect(2200,3000,800,600,QColor(240,240,240));
        painter.fillRect(2197,3000,6,600,QColor(200,40,200));
        painter.end();
        return sheet.save(path,"JPG",92)?path:QString();
    }
    static QDoubleSpinBox *spin(Window &w,const char *name){return w.findChild<QDoubleSpinBox *>(QString::fromLatin1(name));}
    static QSlider *slider(Window &w,const char *name){auto key=QString::fromLatin1(name);if(!key.startsWith("slider-"))key.prepend("slider-");return w.findChild<QSlider *>(key);}
    static QPushButton *reset(Window &w,const char *name){auto key=QString::fromLatin1(name);if(!key.startsWith("reset-"))key.prepend("reset-");return w.findChild<QPushButton *>(key);}
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
    // The card is the ancestor widget the adjustment index/list would reveal.
    static QWidget *cardOf(QWidget *field){
        if(!field)return nullptr;
        for(QWidget *widget=field->parentWidget();widget;widget=widget->parentWidget())
            if(widget->objectName()==QStringLiteral("adjustment-card"))return widget;
        return field->parentWidget();
    }
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

void DefringeWindowTests::init(){
    QVERIFY2(m_syntheticDir.isValid(),"temporary directory for the synthetic source");
}

void DefringeWindowTests::cleanup(){
}

void DefringeWindowTests::mainWindow(){
    const auto external=qEnvironmentVariable("LUMINA_DEFRINGE_TEST_PHOTO");
    const QString source=external.isEmpty()?writeSyntheticJpeg(m_syntheticDir.filePath(QStringLiteral("defringe-6000x4000.jpg"))):external;
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
    QSize expectedSize(6000,4000);
    if(!external.isEmpty()){
        const auto dimensions=qEnvironmentVariable("LUMINA_DEFRINGE_EXPECT_SIZE").split('x');
        QVERIFY(dimensions.size()==2);expectedSize={dimensions[0].toInt(),dimensions[1].toInt()};QVERIFY(expectedSize.isValid());
    }
    QCOMPARE(firstResult.originalSize,expectedSize);

    auto *purpleAmount=spin(window,"defringe-purple-amount");
    auto *purpleHue=spin(window,"defringe-purple-hue");
    auto *purpleWidth=spin(window,"defringe-purple-width");
    auto *greenAmount=spin(window,"defringe-green-amount");
    auto *greenHue=spin(window,"defringe-green-hue");
    auto *greenWidth=spin(window,"defringe-green-width");
    auto *radius=spin(window,"defringe-radius");
    QVERIFY2(purpleAmount&&purpleHue&&purpleWidth&&greenAmount&&greenHue&&greenWidth&&radius,"all seven defringe spins exist");
    QDoubleSpinBox *fields[]{purpleAmount,purpleHue,purpleWidth,greenAmount,greenHue,greenWidth,radius};
    const char *names[]{"defringe-purple-amount","defringe-purple-hue","defringe-purple-width","defringe-green-amount","defringe-green-hue","defringe-green-width","defringe-radius"};

    // The neighbouring cards must survive the new card.
    QVERIFY2(spin(window,"ca-red")&&spin(window,"ca-blue"),"manual CA controls still exist");
    QVERIFY2(spin(window,"distortion")&&slider(window,"slider-distortion"),"distortion controls still exist");
    QVERIFY2(spin(window,"lens-shading")&&slider(window,"slider-lens-shading"),"lens-shading controls still exist");
    QVERIFY2(navigationNamesCard(window,QString::fromUtf8("紫绿边去色")),"navigation exposes the defringe card");
    QVERIFY2(navigationNamesCard(window,QString::fromUtf8("手动横向色差")),"navigation exposes the chromatic card");
    QVERIFY2(navigationNamesCard(window,QString::fromUtf8("畸变校正")),"navigation exposes the distortion card");
    QVERIFY2(navigationNamesCard(window,QString::fromUtf8("镜头暗角校正")),"navigation exposes the lens-shading card");

    // Card order: the defringe card sits after manual CA and before distortion.
    const QStringList order{QString::fromUtf8("手动横向色差"),QString::fromUtf8("紫绿边去色"),QString::fromUtf8("畸变校正")};
    auto *choice=window.findChild<QComboBox *>("adjustment-section-choice");
    QVERIFY2(choice,"the adjustment navigation selector exists");
    int previous=-1;
    for(const QString &needle:order){
        int found=-1;
        for(int i=0;i<choice->count();++i)if(choice->itemText(i)==needle)found=i;
        QVERIFY2(found>=0,qPrintable(QStringLiteral("navigation names %1").arg(needle)));
        QVERIFY2(found>previous,qPrintable(QStringLiteral("%1 is out of card order").arg(needle)));
        previous=found;
    }

    // Declared defaults and ranges, straight from DefringeSettings.
    QCOMPARE(purpleAmount->value(),0.);QCOMPARE(purpleHue->value(),300.);QCOMPARE(purpleWidth->value(),50.);
    QCOMPARE(greenAmount->value(),0.);QCOMPARE(greenHue->value(),115.);QCOMPARE(greenWidth->value(),45.);
    QCOMPARE(radius->value(),3.);
    for(QDoubleSpinBox *field:fields){
        QCOMPARE(field->decimals(),3);
        QCOMPARE(field->keyboardTracking(),false);
        QVERIFY2(!field->accessibleName().isEmpty(),"the field carries an accessible name");
    }
    QCOMPARE(purpleAmount->minimum(),0.);QCOMPARE(purpleAmount->maximum(),1.);
    QCOMPARE(purpleHue->minimum(),0.);QCOMPARE(purpleHue->maximum(),360.);
    QCOMPARE(purpleWidth->minimum(),5.);QCOMPARE(purpleWidth->maximum(),90.);
    QCOMPARE(greenAmount->minimum(),0.);QCOMPARE(greenAmount->maximum(),1.);
    QCOMPARE(greenHue->minimum(),0.);QCOMPARE(greenHue->maximum(),360.);
    QCOMPARE(greenWidth->minimum(),5.);QCOMPARE(greenWidth->maximum(),90.);
    QCOMPARE(radius->minimum(),1.);QCOMPARE(radius->maximum(),20.);
    // Sliders: x1000 for amounts and radius, x100 for hue and half-widths.
    const int sliderMin[]{0,0,500,0,0,500,1000};
    const int sliderMax[]{1000,36000,9000,1000,36000,9000,20000};
    for(int i=0;i<7;++i){
        auto *bar=slider(window,names[i]);
        QVERIFY2(bar,qPrintable(QStringLiteral("slider-%1 exists").arg(QString::fromLatin1(names[i]))));
        QCOMPARE(bar->minimum(),sliderMin[i]);
        QCOMPARE(bar->maximum(),sliderMax[i]);
        QVERIFY2(!bar->accessibleName().isEmpty(),"the slider carries an accessible name");
        auto *button=reset(window,names[i]);
        QVERIFY2(button,qPrintable(QStringLiteral("reset-%1 exists").arg(QString::fromLatin1(names[i]))));
        QVERIFY2(!button->accessibleName().isEmpty(),"the reset carries an accessible name");
    }

    Recipe base=window.currentRecipe();
    base.crop={.1,.1,.8,.8};
    base.exposure=exact;
    base.contrast=.2468;
    base.saturation=1.1357;
    base.defringe={.25,300.5,44.5,.35,101.5,33.5,5.5};
    const int beforeApply=ready.count();
    window.applyRecipe(base,QStringLiteral("去色基准"));
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    QTRY_VERIFY_WITH_TIMEOUT(ready.count()>beforeApply,90000);
    requireClean(ready.last(),"applyRecipe");
    QCOMPARE(ready.last().at(0).value<EditResult>().recipe,base);
    QCOMPARE(window.currentRecipe(),base);
    QCOMPARE(purpleAmount->value(),.25);QCOMPARE(purpleHue->value(),300.5);QCOMPARE(purpleWidth->value(),44.5);
    QCOMPARE(greenAmount->value(),.35);QCOMPARE(greenHue->value(),101.5);QCOMPARE(greenWidth->value(),33.5);
    QCOMPARE(radius->value(),5.5);
    QCOMPARE(slider(window,"slider-defringe-purple-amount")->value(),250);
    QCOMPARE(slider(window,"slider-defringe-purple-hue")->value(),30050);
    const auto beforeGestureImage=ready.last().at(0).value<EditResult>().image;
    QCOMPARE(slider(window,"slider-defringe-radius")->value(),5500);

    auto *canvas=window.findChild<Canvas *>();
    QVERIFY2(canvas,"canvas is reachable");
    QSignalSpy previews(&window,&Window::previewChanged);
    QSignalSpy paintings(canvas,&Canvas::framePainted);
    QVERIFY2(previews.isValid()&&paintings.isValid(),"gesture spies are valid");

    // A live drag on the purple amount slider: continuous feedback while the
    // slider is held, one history commit on release.
    auto *purpleBar=slider(window,"slider-defringe-purple-amount");
    int interimPreviews=0,interimPaints=0,stepped=0;
    purpleBar->setSliderDown(true);
    for(int value=256;value<=490;value+=6){
        purpleBar->setValue(value);QTest::qWait(50);
        interimPreviews=previews.count();interimPaints=paintings.count();++stepped;
    }
    QCOMPARE(purpleBar->value(),490);
    QCOMPARE(purpleBar->isSliderDown(),true);
    QCOMPARE(stepped,40);
    QVERIFY2(interimPreviews>=2,qPrintable(QStringLiteral("interim previewChanged signals %1").arg(interimPreviews)));
    QVERIFY2(interimPaints>=2,qPrintable(QStringLiteral("interim framePainted signals %1").arg(interimPaints)));
    purpleBar->setSliderDown(false);
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    requireClean(ready.last(),"gesture release");
    const Recipe committed=window.currentRecipe();
    QCOMPARE(committed.defringe.purpleAmount,.49);
    // Every other defringe parameter keeps the exact base value.
    QCOMPARE(committed.defringe.purpleHue,base.defringe.purpleHue);
    QCOMPARE(committed.defringe.purpleWidth,base.defringe.purpleWidth);
    QCOMPARE(committed.defringe.greenAmount,base.defringe.greenAmount);
    QCOMPARE(committed.defringe.greenHue,base.defringe.greenHue);
    QCOMPARE(committed.defringe.greenWidth,base.defringe.greenWidth);
    QCOMPARE(committed.defringe.radius,base.defringe.radius);
    QCOMPARE(committed.exposure,exact);
    QCOMPARE(committed.crop,base.crop);
    QCOMPARE(committed.saturation,base.saturation);
    QCOMPARE(ready.last().at(0).value<EditResult>().recipe,committed);
    if(external.isEmpty())QVERIFY(ready.last().at(0).value<EditResult>().image!=beforeGestureImage);

    window.undoEdit();
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    requireClean(ready.last(),"undo");
    QCOMPARE(window.currentRecipe(),base);
    window.redoEdit();
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    QTRY_VERIFY_WITH_TIMEOUT(window.currentRecipe()==committed,90000);
    requireClean(ready.last(),"redo");
    QCOMPARE(window.currentRecipe(),committed);

    // Three-decimal display versus an exact recipe value: the field rounds, the
    // recipe keeps every digit.
    Recipe precise=committed;
    precise.defringe.purpleAmount=purpleExact;
    precise.defringe.greenAmount=greenExact;
    precise.defringe.radius=radiusExact;
    precise.defringe.greenHue=hueExact;
    precise.defringe.greenWidth=widthExact;
    const int beforePrecise=ready.count();
    window.applyRecipe(precise,QStringLiteral("精确去色"));
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    QTRY_VERIFY_WITH_TIMEOUT(ready.count()>beforePrecise,90000);
    requireClean(ready.last(),"precise applyRecipe");
    QCOMPARE(window.currentRecipe().defringe.purpleAmount,purpleExact);
    QCOMPARE(window.currentRecipe().defringe.greenAmount,greenExact);
    QCOMPARE(window.currentRecipe().defringe.radius,radiusExact);
    QCOMPARE(purpleAmount->value(),purpleDisplay);
    QCOMPARE(greenAmount->value(),greenDisplay);
    QCOMPARE(radius->value(),radiusExact);
    QCOMPARE(purpleAmount->text(),QStringLiteral("0.765"));
    QCOMPARE(greenAmount->text(),QStringLiteral("0.412"));

    // Compare mode disables every control and leaves the recipe untouched.
    const Recipe beforeCompare=window.currentRecipe();
    window.compareOriginal(true);
    QTRY_VERIFY_WITH_TIMEOUT(purpleAmount&&!purpleAmount->isEnabled(),90000);
    for(QDoubleSpinBox *field:fields)QVERIFY2(!field->isEnabled(),"the field disables while comparing");
    for(const char *name:names)QVERIFY2(!slider(window,name)->isEnabled(),"the slider disables while comparing");
    window.compareOriginal(false);
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    QCOMPARE(window.currentRecipe(),beforeCompare);

    // Reset writes only its own default; the six siblings keep their values.
    const int beforeReset=ready.count();
    reset(window,"reset-defringe-purple-amount")->click();
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    QTRY_VERIFY_WITH_TIMEOUT(ready.count()>beforeReset,90000);
    requireClean(ready.last(),"reset-defringe-purple-amount");
    const Recipe resetPurple=window.currentRecipe();
    QCOMPARE(resetPurple.defringe.purpleAmount,0.);
    QCOMPARE(resetPurple.defringe.purpleHue,beforeCompare.defringe.purpleHue);
    QCOMPARE(resetPurple.defringe.purpleWidth,beforeCompare.defringe.purpleWidth);
    QCOMPARE(resetPurple.defringe.greenAmount,beforeCompare.defringe.greenAmount);
    QCOMPARE(resetPurple.defringe.greenHue,beforeCompare.defringe.greenHue);
    QCOMPARE(resetPurple.defringe.greenWidth,beforeCompare.defringe.greenWidth);
    QCOMPARE(resetPurple.defringe.radius,beforeCompare.defringe.radius);
    QCOMPARE(resetPurple.exposure,beforeCompare.exposure);
    QCOMPARE(resetPurple.crop,beforeCompare.crop);
    QCOMPARE(purpleAmount->value(),0.);
    QCOMPARE(greenAmount->value(),greenDisplay);
    window.undoEdit();
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    QCOMPARE(window.currentRecipe(),beforeCompare);

    // Reset a hue field: it returns to the declared DefringeSettings default
    // and leaves siblings alone.
    const int beforeHueReset=ready.count();
    window.applyRecipe(precise,QStringLiteral("精确去色"));
    QTRY_VERIFY_WITH_TIMEOUT(ready.count()>beforeHueReset,90000);
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    const int beforeGreenHueReset=ready.count();
    reset(window,"reset-defringe-green-hue")->click();
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    QTRY_VERIFY_WITH_TIMEOUT(ready.count()>beforeGreenHueReset,90000);
    requireClean(ready.last(),"reset-defringe-green-hue");
    QCOMPARE(window.currentRecipe().defringe.greenHue,115.);
    QCOMPARE(window.currentRecipe().defringe.greenWidth,widthExact);
    QCOMPARE(window.currentRecipe().defringe.radius,radiusExact);
    window.undoEdit();
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);

    // Geometry reset is a colour-only-preserving action: crop, angle,
    // perspective, distortion and CA clear, while defringe and exposure stay.
    auto *resetGeometry=window.findChild<QPushButton *>(QStringLiteral("reset-geometry"));
    QVERIFY2(resetGeometry,"reset-geometry button exists");
    window.applyRecipe(precise,QStringLiteral("几何前基准"));
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    const int beforeGeometry=ready.count();
    resetGeometry->click();
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    QTRY_VERIFY_WITH_TIMEOUT(ready.count()>beforeGeometry,90000);
    requireClean(ready.last(),"reset-geometry");
    const Recipe resetGeometryRecipe=window.currentRecipe();
    QCOMPARE(resetGeometryRecipe.crop,QRectF(0,0,1,1));
    QCOMPARE(resetGeometryRecipe.angle,0.);
    QCOMPARE(resetGeometryRecipe.distortion,0.);
    QCOMPARE(resetGeometryRecipe.chromaticAberration,QPointF(0,0));
    QCOMPARE(resetGeometryRecipe.perspective,QPointF(0,0));
    // Defringe is a colour adjustment: the geometry reset must preserve it.
    QCOMPARE(resetGeometryRecipe.defringe,precise.defringe);
    QCOMPARE(resetGeometryRecipe.exposure,precise.exposure);
    QCOMPARE(purpleAmount->value(),purpleDisplay);
    QCOMPARE(radius->value(),radiusExact);
    window.undoEdit();
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    QCOMPARE(window.currentRecipe(),precise);

    // Switching photos must resync the defringe controls, not keep stale ones.
    const QString second=writeSyntheticJpeg(m_syntheticDir.filePath(QStringLiteral("defringe-second.jpg")));
    QVERIFY2(!second.isEmpty(),"second synthetic source");
    window.addFiles(QStringList{second});
    QTRY_VERIFY_WITH_TIMEOUT(window.currentIndex()>=0,90000);
    window.activate(1);
    QTRY_VERIFY_WITH_TIMEOUT(window.currentIndex()==1,90000);
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    requireClean(ready.last(),"switch to second photo");
    // The second photo carries no recipe, so the controls show the defaults.
    QCOMPARE(window.currentRecipe().defringe,DefringeSettings{});
    QCOMPARE(purpleAmount->value(),0.);
    QCOMPARE(purpleHue->value(),300.);
    QCOMPARE(purpleWidth->value(),50.);
    QCOMPARE(greenAmount->value(),0.);
    QCOMPARE(greenHue->value(),115.);
    QCOMPARE(greenWidth->value(),45.);
    QCOMPARE(radius->value(),3.);
    window.activate(0);
    QTRY_VERIFY_WITH_TIMEOUT(window.currentIndex()==0,90000);
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    // Returning restores the precise recipe into the display again.
    QCOMPARE(window.currentRecipe(),precise);
    QCOMPARE(purpleAmount->value(),purpleDisplay);
    QCOMPARE(radius->value(),radiusExact);
    qInfo() << "defringe window"
            << "interim previews" << interimPreviews
            << "interim paints" << interimPaints
            << "final purple" << committed.defringe.purpleAmount
            << "loader results" << ready.count();
}

QTEST_MAIN(DefringeWindowTests)
#include "defringe_window_tests.moc"
