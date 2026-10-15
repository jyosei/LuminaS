// Native Qt Window regression for the distortion card. This file drives the
// shipped Window surface only: the spin box, its bound slider, the reset
// button, the history stack and the preview/loader handshake. No radial model,
// no render maths and no sampling algorithm is restated here. Every expectation
// is either the exact double the UI was handed, a property of the shipped
// control range, or a documented display/history behaviour. The optional
// private sample and the optional output paths are read from the environment
// only at runtime and are never set or inspected by this file.
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
#include <QSet>
#include <QComboBox>
#include <algorithm>
#include <cmath>
#include "../src/app/window.h"
#include "../src/core/presets.h"

class DistortionWindowTests : public QObject {
    Q_OBJECT
private:
    static constexpr double exact=.312345;
    static constexpr double gestureTarget=.14;
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
    // The distortion card is the parent of the form layout that owns the row;
    // the card is the widget an adjustment index/list would reveal.
    static QWidget *cardOf(QWidget *field){
        if(!field)return nullptr;
        for(QWidget *widget=field->parentWidget();widget;widget=widget->parentWidget())
            if(widget->objectName()==QStringLiteral("adjustment-card"))return widget;
        return field->parentWidget();
    }
    // Navigation index entries name every card in the panel. Both the
    // distortion and the lens-shading card must still be discoverable, so an
    // earlier task cannot have quietly dropped the lens card.
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

void DistortionWindowTests::init(){
    QVERIFY2(m_syntheticDir.isValid(),"temporary directory for the synthetic source");
}

void DistortionWindowTests::cleanup(){
}

void DistortionWindowTests::mainWindow(){
    const QString sample=qEnvironmentVariable("LUMINA_DISTORTION_SAMPLE");
    const bool synthetic=sample.isEmpty();
    const QString source=synthetic?writeSyntheticJpeg(m_syntheticDir.filePath(QStringLiteral("distortion-6000x4000.jpg"))):sample;
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

    auto *spinBox=spin(window,"distortion");
    auto *bar=slider(window,"slider-distortion");
    auto *reset=window.findChild<QPushButton *>(QStringLiteral("reset-distortion"));
    QVERIFY2(spinBox&&bar&&reset,"distortion spin, slider and reset controls exist");
    // The lens-shading card must survive the distortion card work.
    auto *lensSpin=spin(window,"lens-shading");
    auto *lensBar=slider(window,"slider-lens-shading");
    QVERIFY2(lensSpin&&lensBar,"lens-shading spin and slider still exist alongside the distortion card");
    QVERIFY2(navigationNamesCard(window,QString::fromUtf8("畸变校正")),"navigation exposes the distortion card");
    QVERIFY2(navigationNamesCard(window,QString::fromUtf8("镜头暗角校正")),"navigation exposes the lens-shading card");

    QCOMPARE(spinBox->value(),0.);
    QCOMPARE(spinBox->minimum(),-.4);
    QCOMPARE(spinBox->maximum(),.4);
    QCOMPARE(spinBox->decimals(),3);
    QCOMPARE(spinBox->singleStep(),.01);
    QCOMPARE(spinBox->keyboardTracking(),false);
    QCOMPARE(bar->minimum(),-400);
    QCOMPARE(bar->maximum(),400);
    QCOMPARE(bar->value(),0);
    QCOMPARE(reset->accessibleName().isEmpty(),false);

    Recipe base=window.currentRecipe();
    base.crop={.1,.1,.8,.8};
    base.distortion=.1;
    base.lensShading=.1;
    base.exposure=exact;
    base.contrast=.2468;
    base.saturation=1.1357;
    base.curves[0]=Curve{{0,0},{.25,.2},{1,.97}};
    const int beforeApply=ready.count();
    window.applyRecipe(base,QStringLiteral("畸变基准"));
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    QTRY_VERIFY_WITH_TIMEOUT(ready.count()>beforeApply,90000);
    requireClean(ready.last(),"applyRecipe");
    QCOMPARE(ready.last().at(0).value<EditResult>().recipe,base);
    QCOMPARE(window.currentRecipe(),base);
    QCOMPARE(spinBox->value(),.1);
    QCOMPARE(bar->value(),100);
    auto *canvas=window.findChild<Canvas *>();
    QVERIFY2(canvas,"canvas is reachable");
    QSignalSpy previews(&window,&Window::previewChanged);
    QSignalSpy paintings(canvas,&Canvas::framePainted);
    QVERIFY2(previews.isValid()&&paintings.isValid(),"gesture spies are valid");

    int interimPreviews=0,interimPaints=0,stepped=0;
    bar->setSliderDown(true);
    for(int value=101;value<=140;++value){
        bar->setValue(value);QTest::qWait(50);
        interimPreviews=previews.count();interimPaints=paintings.count();++stepped;
    }
    QCOMPARE(stepped,40);
    QCOMPARE(bar->value(),140);
    QCOMPARE(bar->isSliderDown(),true);
    // Continuous feedback is required; 30/60 FPS remains an extra achievement.
    QVERIFY2(interimPreviews>=2,qPrintable(QStringLiteral("interim previewChanged signals %1").arg(interimPreviews)));
    QVERIFY2(interimPaints>=2,qPrintable(QStringLiteral("interim framePainted signals %1").arg(interimPaints)));
    bar->setSliderDown(false);
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    requireClean(ready.last(),"gesture release");
    const Recipe committed=window.currentRecipe();
    QCOMPARE(committed.distortion,gestureTarget);
    QCOMPARE(committed.crop,base.crop);
    QCOMPARE(committed.exposure,exact);
    QCOMPARE(committed.lensShading,.1);
    QCOMPARE(committed.contrast,.2468);
    QCOMPARE(committed.saturation,1.1357);
    QCOMPARE(committed.curves,base.curves);
    QCOMPARE(committed.angle,base.angle);
    QCOMPARE(spinBox->value(),gestureTarget);
    QCOMPARE(bar->value(),140);
    const EditResult released=qvariant_cast<EditResult>(ready.last().at(0));
    QCOMPARE(released.recipe,committed);
    const QSize expectedFull(qRound(firstResult.originalSize.width()*.8),qRound(firstResult.originalSize.height()*.8));
    QCOMPARE(released.fullSize,expectedFull);
    QCOMPARE(qMax(released.image.width(),released.image.height()),qMin(3840,qMax(expectedFull.width(),expectedFull.height())));

    window.undoEdit();
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    QTRY_VERIFY_WITH_TIMEOUT(ready.count()>beforeApply+1,90000);
    requireClean(ready.last(),"undo");
    QCOMPARE(window.currentRecipe(),base);
    QCOMPARE(ready.last().at(0).value<EditResult>().recipe,base);
    QVERIFY2(ready.last().at(0).value<EditResult>().error.isEmpty(),"undo result is error-free");
    window.redoEdit();
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    QCOMPARE(window.currentRecipe(),committed);
    QTRY_VERIFY_WITH_TIMEOUT(ready.last().at(0).value<EditResult>().recipe==committed,90000);
    requireClean(ready.last(),"redo");

    const Recipe beforeCompare=window.currentRecipe();
    window.compareOriginal(true);
    QTRY_VERIFY_WITH_TIMEOUT(spinBox&&!spinBox->isEnabled(),90000);
    QVERIFY2(!bar->isEnabled(),"the distortion slider disables while comparing");
    window.compareOriginal(false);
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    QCOMPARE(window.currentRecipe(),beforeCompare);

    const int beforeReset=ready.count();
    reset->click();
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    QTRY_VERIFY_WITH_TIMEOUT(ready.count()>beforeReset,90000);
    requireClean(ready.last(),"reset-distortion");
    const Recipe resetRecipe=window.currentRecipe();
    QCOMPARE(resetRecipe.distortion,0.);
    QCOMPARE(resetRecipe.lensShading,beforeCompare.lensShading);
    QCOMPARE(resetRecipe.exposure,beforeCompare.exposure);
    QCOMPARE(resetRecipe.crop,beforeCompare.crop);
    QCOMPARE(resetRecipe.curves,beforeCompare.curves);
    window.undoEdit();
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    QCOMPARE(window.currentRecipe(),beforeCompare);
    window.redoEdit();
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    QCOMPARE(window.currentRecipe(),resetRecipe);

    // Optional, runtime-only outputs. This file never sets the variables and
    // never inspects the pixels of the saved screenshot; the caller does that.
    window.undoEdit();
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    QCOMPARE(window.currentRecipe(),beforeCompare);
    const QString recipeOut=qEnvironmentVariable("LUMINA_DISTORTION_RECIPE");
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
    const QString shot=qEnvironmentVariable("LUMINA_DISTORTION_SCREENSHOT");
    if(!shot.isEmpty()){
        QVERIFY2(spinBox,"the distortion control exists for the screenshot");
        if(auto *area=window.findChild<QScrollArea *>())
            area->ensureWidgetVisible(qobject_cast<QWidget *>(cardOf(spinBox)),0,16);
        QTest::qWait(100);
        QVERIFY2(window.grab().save(shot),qPrintable(QStringLiteral("screenshot saved to %1").arg(shot)));
    }
    qInfo() << "distortion window"
            << "interim previews" << interimPreviews
            << "interim paints" << interimPaints
            << "frames stepped" << stepped
            << "final distortion" << committed.distortion
            << "exact exposure" << committed.exposure
            << "loader results" << ready.count();
}

QTEST_MAIN(DistortionWindowTests)
#include "distortion_window_tests.moc"
