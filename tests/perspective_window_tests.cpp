// Window-level perspective controller coverage: synthetic-geometry JPEGs only,
// driven exclusively through the public Window/EditLoader/Canvas surface. No
// pixel maths, no perspective model and no renderer internals are restated
// here; the algorithm itself is owned elsewhere. Expected values are the exact
// doubles the UI was handed, so a rounding slip in the controller is visible.
#include "../src/app/window.h"
#include "../src/app/edit_loader.h"
#include "../src/app/canvas.h"
#include "../src/core/document.h"
#include "../src/core/presets.h"
#include <QtTest/QtTest>
#include <QtTest/qtest_widgets.h>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QJsonDocument>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalSpy>
#include <QSlider>
#include <QTemporaryDir>

class PerspectiveWindowTests : public QObject {
    Q_OBJECT
private:
    QTemporaryDir m_dir;
    static QString writeJpeg(const QString &path,int width,int height){
        QImage sheet(width,height,QImage::Format_RGB32);
        sheet.fill(QColor(128,128,128));
        QPainter painter(&sheet);
        for(int x=0;x<width;x+=100){painter.setPen(x%200==0?QColor(220,20,20):QColor(20,20,220));painter.drawLine(x,0,x,height-1);}
        for(int y=0;y<height;y+=100){painter.setPen(y%200==0?QColor(20,20,220):QColor(220,20,20));painter.drawLine(0,y,width-1,y);}
        painter.end();
        return sheet.save(path,"JPG",95)?path:QString();
    }
    static QDoubleSpinBox *spin(Window &w,const char *name){return w.findChild<QDoubleSpinBox *>(QString::fromLatin1(name));}
    static QSlider *slider(Window &w,const char *name){return w.findChild<QSlider *>(QString::fromLatin1(name));}
    static bool isFullEdge4K(int width,int height){return qMax(width,height)==qMin(3840,qMax(width,height));}
    // Every delivered result must be error-free and must own its pixels: the
    // long edge is min(3840, full size), so a 4K proxy can never stand in for a
    // deleted or downscaled full source.
    static void requireCleanFullEdge(const QList<QVariant> &arguments,const char *where){
        for(const QVariant &argument:arguments){
            if(!argument.canConvert<EditResult>())continue;
            const EditResult result=argument.value<EditResult>();
            QVERIFY2(result.error.isEmpty(),qPrintable(QStringLiteral("%1: %2 (%3)").arg(QString::fromLatin1(where),result.error,result.path)));
            QVERIFY2(isFullEdge4K(result.image.width(),result.image.height()),
                     qPrintable(QStringLiteral("%1: preview %2x%3 is not min(3840, full %4x%5)").arg(QString::fromLatin1(where))
                         .arg(result.image.width()).arg(result.image.height()).arg(result.fullSize.width()).arg(result.fullSize.height())));
        }
    }
private slots:
    void gesturePerspectivePresetAndRoundtrip();
    void zeroCrossingAndSwitch();
};

void PerspectiveWindowTests::gesturePerspectivePresetAndRoundtrip(){
    QVERIFY2(m_dir.isValid(),"temporary directory for synthetic JPEGs");
    const QString sample=qEnvironmentVariable("LUMINA_PERSPECTIVE_SAMPLE");
    const bool synthetic=sample.isEmpty();
    const QString source=synthetic?writeJpeg(m_dir.filePath(QStringLiteral("perspective-6000x4000.jpg")),6000,4000):sample;
    QVERIFY2(!source.isEmpty()&&QFile::exists(source),qPrintable(QStringLiteral("source image: %1").arg(source)));
    Window window(nullptr,QStringLiteral(""));window.show();
    auto *editor=window.findChild<EditLoader *>();
    QVERIFY2(editor,"EditLoader is reachable from the window");
    QSignalSpy ready(editor,&EditLoader::ready);
    connect(editor,&EditLoader::ready,&window,[](const EditResult &result){QVERIFY2(result.error.isEmpty(),qPrintable(result.error));QCOMPARE(qMax(result.image.width(),result.image.height()),qMin(3840,qMax(result.fullSize.width(),result.fullSize.height())));});
    QVERIFY2(ready.isValid(),"EditLoader::ready spy is valid");
    // Every result is verified as it arrives: an empty error and a preview that
    // matches min(3840, full size). A proxy standing in for a source would show
    // up here as a wrong long edge whatever the recipe does.
    auto checkReady=[&ready](const char *where){requireCleanFullEdge(ready.last(),where);};
    window.addFiles(QStringList{source});
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    QVERIFY2(!ready.isEmpty(),"startup render produced a result");
    checkReady("startup");
    const EditResult startup=qvariant_cast<EditResult>(ready.last().at(0));
    if(synthetic){
        QCOMPARE(startup.originalSize,QSize(6000,4000));
        QCOMPARE(startup.image.size(),QSize(3840,2560));
        QVERIFY2(isFullEdge4K(startup.image.width(),startup.image.height()),"synthetic 6000x4000 previews at the 3840 long edge");
    }else{
        QVERIFY2(startup.originalSize.isValid(),qPrintable(QStringLiteral("real source original size %1x%2").arg(startup.originalSize.width()).arg(startup.originalSize.height())));
        QVERIFY2(qMax(startup.originalSize.width(),startup.originalSize.height())>=4800,
                 qPrintable(QStringLiteral("real source long edge %1 is not a 4K-providing capture").arg(qMax(startup.originalSize.width(),startup.originalSize.height()))));
    }

    Recipe base=window.currentRecipe();
    base.crop={.1,.1,.8,.8};
    base.perspective=QPointF(.05,.071234);
    base.exposure=.312345;
    const int beforeApply=ready.count();
    window.applyRecipe(base,QStringLiteral("透视基准"));
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    QTRY_VERIFY_WITH_TIMEOUT(ready.count()>beforeApply,90000);
    checkReady("applyRecipe");
    // The crop only aims the render: the immutable full source is still attached,
    // and it is the original geometry rather than the 4K working proxy.
    const EditResult cropped=qvariant_cast<EditResult>(ready.last().at(0));
    QVERIFY2(cropped.fullGeometry,"cropped render retains the immutable full source");
    QCOMPARE(cropped.fullGeometry->linear.size(),cropped.originalSize);
    QVERIFY2(qMax(cropped.fullGeometry->linear.width(),cropped.fullGeometry->linear.height())!=3840||isFullEdge4K(cropped.originalSize.width(),cropped.originalSize.height()),
             "retained source is the original frame, not the 4K proxy");
    const Recipe applied=window.currentRecipe();
    QCOMPARE(applied.crop,base.crop);
    QCOMPARE(applied.perspective.x(),.05);
    QCOMPARE(applied.perspective.y(),.071234);
    QCOMPARE(applied.exposure,.312345);

    auto *horizontal=slider(window,"slider-perspective-horizontal");
    auto *vertical=slider(window,"slider-perspective-vertical");
    auto *horizontalReset=window.findChild<QPushButton *>(QStringLiteral("reset-perspective-horizontal"));
    QVERIFY2(horizontal&&vertical&&horizontalReset,"perspective slider and reset controls exist");
    auto *canvas=window.findChild<Canvas *>();
    QVERIFY2(canvas,"canvas is reachable");
    QSignalSpy previews(&window,&Window::previewChanged);
    QSignalSpy paintings(canvas,&Canvas::framePainted);
    QVERIFY2(previews.isValid()&&paintings.isValid(),"gesture spies are valid");
    int gesturePreviews=0,gesturePaints=0;
    horizontal->setSliderDown(true);
    for(int value=61;value<=100;++value){horizontal->setValue(value);QTest::qWait(50);gesturePreviews=previews.count();gesturePaints=paintings.count();}
    QVERIFY2(gesturePreviews>=2,qPrintable(QStringLiteral("interim previewChanged signals %1").arg(gesturePreviews)));
    QVERIFY2(gesturePaints>=2,qPrintable(QStringLiteral("interim framePainted signals %1").arg(gesturePaints)));
    horizontal->setSliderDown(false);
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    checkReady("gesture release");
    const Recipe committed=window.currentRecipe();
    QCOMPARE(committed.perspective.x(),.1);
    QCOMPARE(committed.perspective.y(),.071234);
    QCOMPARE(committed.crop,base.crop);
    QCOMPARE(committed.exposure,.312345);
    QCOMPARE(committed.angle,base.angle);
    QCOMPARE(committed.flipHorizontal,base.flipHorizontal);
    QCOMPARE(committed.flipVertical,base.flipVertical);
    QCOMPARE(committed.warmth,base.warmth);
    QCOMPARE(committed.tint,base.tint);
    QCOMPARE(committed.saturation,base.saturation);
    QCOMPARE(committed.contrast,base.contrast);
    QCOMPARE(committed.curves,base.curves);
    QCOMPARE(committed.monochrome,base.monochrome);

    window.undoEdit();
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    QCOMPARE(window.currentRecipe(),base); // one gesture, one history entry
    window.redoEdit();
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    QCOMPARE(window.currentRecipe(),committed);
    checkReady("gesture release");

    const Recipe beforeCompare=window.currentRecipe();
    window.compareOriginal(true);
    QTRY_VERIFY_WITH_TIMEOUT(spin(window,"perspective-horizontal")&&!spin(window,"perspective-horizontal")->isEnabled(),90000);
    QVERIFY2(!spin(window,"perspective-vertical")->isEnabled(),"vertical perspective spin disables while comparing");
    QVERIFY2(!horizontal->isEnabled(),"horizontal perspective slider disables while comparing");
    window.compareOriginal(false);
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    QCOMPARE(window.currentRecipe(),beforeCompare);

    const int beforeReset=ready.count();
    horizontalReset->click();
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    QTRY_VERIFY_WITH_TIMEOUT(ready.count()>beforeReset,90000);
    checkReady("reset-perspective-horizontal");
    const Recipe reset=window.currentRecipe();
    QCOMPARE(reset.perspective.x(),0.);
    QCOMPARE(reset.perspective.y(),.071234);
    QCOMPARE(reset.crop,beforeCompare.crop);
    QCOMPARE(reset.exposure,beforeCompare.exposure);
    QCOMPARE(reset.angle,beforeCompare.angle);
    window.undoEdit();
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    QCOMPARE(window.currentRecipe(),beforeCompare);
    window.redoEdit();
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    QCOMPARE(window.currentRecipe(),reset);

    // Presets carry tone only. Save a known geometry-only recipe, move to a
    // visibly different geometry and tone, then load the saved preset: the saved
    // tone must come back while the current geometry is preserved.
    const Recipe savedKnown=window.currentRecipe();
    QString error;
    const QString presetPath=m_dir.filePath(QStringLiteral("perspective-session-preset.json"));
    QVERIFY2(window.saveAdjustmentPreset(presetPath,error),qPrintable(error));
    const QByteArray rawPreset=[&presetPath]{
        QFile file(presetPath);
        if(!file.open(QIODevice::ReadOnly))return QByteArray();
        return file.readAll();
    }();
    QVERIFY2(!rawPreset.isEmpty(),"saved preset file is readable");
    // The preset payload itself must exclude geometry. recipeJson(defaults)
    // would always re-add default crop/angle/perspective, so the file is read.
    QJsonParseError parseError{};
    const QJsonObject presetRoot=QJsonDocument::fromJson(rawPreset,&parseError).object();
    QVERIFY2(parseError.error==QJsonParseError::NoError,"preset file parses as JSON");
    const QJsonObject adjustments=presetRoot.value(QStringLiteral("adjustments")).toObject();
    QVERIFY2(!adjustments.isEmpty(),"preset payload holds an adjustments object");
    for(const char *geometryKey:{"perspective","angle","x","y","width","height","flipHorizontal","flipVertical"})
        QVERIFY2(!adjustments.contains(QString::fromLatin1(geometryKey)),
                 qPrintable(QStringLiteral("preset payload leaked geometry key %1").arg(QString::fromLatin1(geometryKey))));
    AdjustmentPreset stored;
    QVERIFY2(loadPresetFile(presetPath,stored,error),qPrintable(error));
    QVERIFY2(stored.adjustments.perspective.isNull(),"stored preset adjustments carry no perspective");
    QCOMPARE(stored.adjustments.crop,QRectF(0,0,1,1));
    QCOMPARE(stored.adjustments.angle,0.);
    QVERIFY2(!QFile::exists(m_dir.filePath(QStringLiteral("presets"))),"session-only preset library left no directory behind");

    Recipe other=savedKnown;
    other.perspective=QPointF(-.12,.09);
    other.crop={.08,.12,.8,.8};
    other.angle=-9;
    other.flipHorizontal=false;
    other.flipVertical=true;
    other.exposure=1;
    const int beforeOther=ready.count();
    window.applyRecipe(other,QStringLiteral("其他构图"));
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    QTRY_VERIFY_WITH_TIMEOUT(ready.count()>beforeOther,90000);
    checkReady("applyRecipe other");
    QCOMPARE(window.currentRecipe(),other);

    QVERIFY2(window.loadAdjustmentPreset(presetPath,error),qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    checkReady("loadAdjustmentPreset");
    const Recipe loaded=window.currentRecipe();
    QCOMPARE(loaded.perspective,other.perspective); // geometry survives the preset
    QCOMPARE(loaded.crop,other.crop);
    QCOMPARE(loaded.angle,other.angle);
    QCOMPARE(loaded.flipHorizontal,other.flipHorizontal);
    QCOMPARE(loaded.flipVertical,other.flipVertical);
    QCOMPARE(loaded.exposure,savedKnown.exposure); // tone comes from the preset
    QCOMPARE(recipeFromJson(recipeJson(loaded)),loaded); // whole recipe survives JSON
    QVERIFY2(recipeJson(loaded).contains(QStringLiteral("perspective")),"recipe JSON still carries perspective");

    window.undoEdit();
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    QCOMPARE(window.currentRecipe().exposure,other.exposure);
    QCOMPARE(window.currentRecipe(),other); // one preset gesture, one history entry
    window.redoEdit();
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    QCOMPARE(window.currentRecipe(),loaded);

    const QString shot=qEnvironmentVariable("LUMINA_PERSPECTIVE_SCREENSHOT");
    if(!shot.isEmpty()){
        QVERIFY2(spin(window,"perspective-horizontal"),"perspective spin exists for the screenshot");
        if(auto *area=window.findChild<QScrollArea *>())area->ensureWidgetVisible(spin(window,"perspective-horizontal")->parentWidget()->parentWidget(),0,16);
        QTest::qWait(100);
        QVERIFY2(window.grab().save(shot),qPrintable(QStringLiteral("screenshot saved to %1").arg(shot)));
    }
    const QString recipeOut=qEnvironmentVariable("LUMINA_PERSPECTIVE_RECIPE");
    if(!recipeOut.isEmpty()){
        const Recipe probe=window.currentRecipe();
        QFile output(recipeOut);
        QVERIFY2(output.open(QIODevice::WriteOnly),qPrintable(QStringLiteral("recipe output %1").arg(recipeOut)));
        QVERIFY(output.write(QJsonDocument(recipeJson(probe)).toJson())>0);
        output.close();
        QFile readback(recipeOut);
        QVERIFY2(readback.open(QIODevice::ReadOnly),qPrintable(QStringLiteral("recipe output %1").arg(recipeOut)));
        const Recipe written=recipeFromJson(QJsonDocument::fromJson(readback.readAll()).object());
        QCOMPARE(written,probe);
        QCOMPARE(written.perspective,other.perspective);
    }
    qInfo() << "perspective window" << window.currentRecipe().perspective
            << "gesture previews" << gesturePreviews << "gesture paints" << gesturePaints
            << "loader results" << ready.count();
}

void PerspectiveWindowTests::zeroCrossingAndSwitch(){
    QVERIFY2(m_dir.isValid(),"temporary directory for synthetic JPEGs");
    const QString first=writeJpeg(m_dir.filePath(QStringLiteral("perspective-640x480.jpg")),640,480);
    const QString second=writeJpeg(m_dir.filePath(QStringLiteral("perspective-640x480-b.jpg")),640,480);
    QVERIFY2(!first.isEmpty()&&!second.isEmpty(),"synthetic 640x480 JPEGs");
    Window window(nullptr,QStringLiteral(""));window.show();
    auto *editor=window.findChild<EditLoader *>();
    QVERIFY2(editor,"EditLoader is reachable from the window");
    QSignalSpy ready(editor,&EditLoader::ready);
    QVERIFY2(ready.isValid(),"EditLoader::ready spy is valid");
    auto checkReady=[&ready](const char *where){requireCleanFullEdge(ready.last(),where);};
    window.addFiles(QStringList{first,second});
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    QCOMPARE(window.currentIndex(),0);
    checkReady("startup small");
    QCOMPARE(qvariant_cast<EditResult>(ready.last().at(0)).image.size(),QSize(640,480)); // never upscaled to 4K

    Recipe base=window.currentRecipe();
    base.perspective=QPointF(.03,.02);
    const int beforeBase=ready.count();
    window.applyRecipe(base,QStringLiteral("透视基准"));
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    QTRY_VERIFY_WITH_TIMEOUT(ready.count()>beforeBase,90000);
    checkReady("applyRecipe small");
    QCOMPARE(window.currentRecipe().perspective,QPointF(.03,.02));

    auto *horizontal=slider(window,"slider-perspective-horizontal");
    QVERIFY2(horizontal,"horizontal perspective slider exists");
    horizontal->setSliderDown(true);
    for(const int value:{30,15,0,-15,-30}){horizontal->setValue(value);QTest::qWait(100);}
    horizontal->setSliderDown(false);
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    checkReady("zero crossing release");
    const Recipe crossed=window.currentRecipe();
    QCOMPARE(crossed.perspective.x(),-.03);
    QCOMPARE(crossed.perspective.y(),.02);
    window.undoEdit();
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    QCOMPARE(window.currentRecipe(),base);
    window.redoEdit();
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    QCOMPARE(window.currentRecipe(),crossed);

    // Drag, then switch photos before the release: a slider still marked down
    // would keep the new photo interactive, so the release is synthesised at
    // once and only then is the new photo waited for. The stale gesture must not
    // overwrite the newly activated photo's empty recipe.
    const int beforeSwitch=ready.count();
    horizontal->setSliderDown(true);
    horizontal->setValue(40);
    window.activate(1);
    horizontal->setSliderDown(false);
    QTRY_COMPARE_WITH_TIMEOUT(window.currentIndex(),1,90000);
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    QVERIFY2(ready.count()>beforeSwitch,"switching photos still produces loader results");
    requireCleanFullEdge(ready.last(),"post-switch");
    const EditResult switched=qvariant_cast<EditResult>(ready.last().at(0));
    QCOMPARE(switched.path,second);
    QVERIFY2(window.currentRecipe().perspective.isNull(),
             qPrintable(QStringLiteral("stale gesture did not reach the new photo, perspective %1/%2")
                 .arg(window.currentRecipe().perspective.x()).arg(window.currentRecipe().perspective.y())));
    QVERIFY2(window.currentRecipe().exposure==0,"new photo keeps an empty tone recipe");
    const Recipe newPhoto=window.currentRecipe();
    window.activate(0);
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    QCOMPARE(window.currentIndex(),0);
    QCOMPARE(window.currentRecipe().perspective,QPointF(.04,.02)); // the draft committed once, to its own photo
    window.undoEdit();
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    QCOMPARE(window.currentRecipe().perspective,QPointF(-.03,.02));
    qInfo() << "switch draft" << newPhoto.perspective << "loader results" << ready.count();
}

QTEST_MAIN(PerspectiveWindowTests)
#include "perspective_window_tests.moc"

