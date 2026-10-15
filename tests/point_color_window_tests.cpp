#include "../src/app/window.h"
#include "../src/app/canvas.h"
#include "../src/app/edit_loader.h"
#include "../src/io/jpeg_loader.h"
#include <QtTest/QtTest>
#include <QtTest/qtest_widgets.h>
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QJsonDocument>
#include <QImage>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtMath>
#include <cmath>
#include <algorithm>

// Window-level point-color behaviour. Every expectation below is read off the
// public Window/PointColorPanel/EditLoader surface; no pixel maths or sampling
// model is reimplemented here. The optional freeform JPEG only feeds geometry.
class PointColorWindowTests : public QObject {
    Q_OBJECT
private:
    QTemporaryDir m_dir;
    QString writeJpeg(const QString &name,int width,int height,QColor left,QColor right){
        QImage sheet(width,height,QImage::Format_RGB888);
        const int half=width/2;
        for(int y=0;y<height;++y){
            unsigned char *line=sheet.scanLine(y);
            for(int x=0;x<width;++x){
                const QColor &c=x<half?left:right;
                line[x*3+0]=uchar(c.red());line[x*3+1]=uchar(c.green());line[x*3+2]=uchar(c.blue());
            }
        }
        const QString path=m_dir.filePath(name);
        if(!sheet.save(path,"JPG",95))return QString();
        return path;
    }
    QPushButton *picker(Window &window,const char *name){return window.findChild<QPushButton *>(QString::fromLatin1(name));}
    QSlider *slider(Window &window,const char *name){return window.findChild<QSlider *>(QString::fromLatin1(name));}
    static void dragSetSliderDown(QSlider *bar,int value){
        bar->setSliderDown(true);
        bar->setValue(value);
    }
    static int channelSpread(const QImage &image,QPoint at){
        if(image.isNull()||at.x()<0||at.y()<0||at.x()>=image.width()||at.y()>=image.height())return -1;
        const QRgb pixel=image.pixel(at);
        const int r=qRed(pixel),g=qGreen(pixel),b=qBlue(pixel);
        return qMax(r,qMax(g,b))-qMin(r,qMin(g,b));
    }
private slots:
    void sampleGestureHistoryAndRange();
    void pickerMutualExclusionAndEscape();
};

void PointColorWindowTests::sampleGestureHistoryAndRange(){
    QVERIFY2(m_dir.isValid(),"temporary directory for synthetic JPEGs");
    const QString freeform=qEnvironmentVariable("LUMINA_POINT_SAMPLE");
    const bool synthetic=freeform.isEmpty();
    const QString source=synthetic?writeJpeg(QStringLiteral("point-sheet.jpg"),3840,2160,QColor(200,30,20),QColor(30,200,20)):freeform;
    QVERIFY2(!source.isEmpty()&&QFile::exists(source),qPrintable(QStringLiteral("source image: %1").arg(source)));
    Window window(nullptr,QStringLiteral(""));window.show();
    QVERIFY2(window.findChild<QWidget *>(QStringLiteral("point-color-panel")),"point-color panel exists");
    QSignalSpy startup(window.findChild<EditLoader *>(),&EditLoader::ready);window.addFiles(QStringList{source});
    QTRY_COMPARE_WITH_TIMEOUT(window.currentIndex(),0,90000);
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    const Recipe base=window.currentRecipe();
    QCOMPARE(base.pointColors.size(),0);

    QPointF firstUv(.25,.5),secondUv(.75,.5);
    if(!synthetic){
        QVERIFY(!startup.isEmpty());const auto image=qvariant_cast<EditResult>(startup.last()[0]).image;QVERIFY(!image.isNull());
        auto choose=[&](bool second){int best=-1;QPointF uv;for(int gy=4;gy<60;++gy)for(int gx=4;gx<60;++gx){QPointF at((gx+.5)/64,(gy+.5)/64);if(second&&std::hypot(at.x()-firstUv.x(),at.y()-firstUv.y())<.25)continue;const auto color=image.pixelColor(int(at.x()*image.width()),int(at.y()*image.height()));const int high=std::max({color.red(),color.green(),color.blue()}),low=std::min({color.red(),color.green(),color.blue()}),score=high>30&&high<245?high-low:-1;if(score>best){best=score;uv=at;}}return std::pair<int,QPointF>{best,uv};};const auto a=choose(false);QVERIFY(a.first>15);firstUv=a.second;const auto b=choose(true);QVERIFY(b.first>15);secondUv=b.second;
    }
    QSignalSpy sampleResult(window.findChild<EditLoader *>(),&EditLoader::pointColorSampled);
    QString error;
    QVERIFY2(window.pickPointColor(firstUv,error),qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(!sampleResult.isEmpty(),90000);QVERIFY2(sampleResult.last()[3].toString().isEmpty(),qPrintable(sampleResult.last()[3].toString()));
    QTRY_COMPARE_WITH_TIMEOUT(window.currentRecipe().pointColors.size(),1,90000);
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    const Recipe first=window.currentRecipe();
    QCOMPARE(first.pointColors[0].hueShift,0.);

    const double wbWarmth=base.warmth,wbTint=base.tint;
    QCOMPARE(first.warmth,wbWarmth);
    QCOMPARE(first.tint,wbTint);
    QCOMPARE(first.crop,base.crop);
    QCOMPARE(first.flipHorizontal,base.flipHorizontal);
    QCOMPARE(first.flipVertical,base.flipVertical);
    QCOMPARE(first.angle,base.angle);
    QCOMPARE(first.exposure,base.exposure);
    QCOMPARE(first.saturation,base.saturation);

    QVERIFY2(window.pickPointColor(secondUv,error),qPrintable(error));
    QTRY_COMPARE_WITH_TIMEOUT(window.currentRecipe().pointColors.size(),2,90000);
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);

    auto *samples=window.findChild<QComboBox *>(QStringLiteral("point-color-samples"));
    QVERIFY2(samples&&samples->count()==2,qPrintable(QStringLiteral("sample combo count %1").arg(samples?samples->count():-1)));
    samples->setCurrentIndex(0);
    QCOMPARE(samples->currentIndex(),0);
    const Recipe saved=window.currentRecipe();

    QSignalSpy previews(&window,&Window::previewChanged);
    QSignalSpy paintings(window.findChild<Canvas *>(),&Canvas::framePainted);
    QSignalSpy editorReady(window.findChild<EditLoader *>(),&EditLoader::ready);
    QVERIFY(previews.isValid()&&paintings.isValid()&&editorReady.isValid());
    auto *hueSlider=slider(window,"slider-point-color-hue-shift");
    QVERIFY2(hueSlider&&hueSlider->isEnabled(),"hue-shift slider is enabled for the selected sample");
    int interimPreviews=0,interimPaintings=0;
    dragSetSliderDown(hueSlider,1);
    for(int value=1;value<=40;++value){
        hueSlider->setValue(value);
        QTest::qWait(50);
        interimPreviews=previews.count();
        interimPaintings=paintings.count();
    }
    QVERIFY2(interimPreviews>=4,qPrintable(QStringLiteral("interim previews %1").arg(interimPreviews)));
    QVERIFY2(interimPaintings>=4,qPrintable(QStringLiteral("interim paints %1").arg(interimPaintings)));
    hueSlider->setSliderDown(false);
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    const Recipe gesture=window.currentRecipe();
    QCOMPARE(gesture.pointColors.size(),2);
    QCOMPARE(gesture.pointColors[0].hueShift,.4);
    const PointColorAdjustment savedFirst=saved.pointColors[0],gestureFirst=gesture.pointColors[0];
    QCOMPARE(gestureFirst.hue,savedFirst.hue);
    QCOMPARE(gestureFirst.saturation,savedFirst.saturation);
    QCOMPARE(gestureFirst.luminance,savedFirst.luminance);
    QCOMPARE(gestureFirst.hueWidth,savedFirst.hueWidth);
    QCOMPARE(gestureFirst.saturationWidth,savedFirst.saturationWidth);
    QCOMPARE(gestureFirst.luminanceWidth,savedFirst.luminanceWidth);
    QCOMPARE(gestureFirst.feather,savedFirst.feather);
    QCOMPARE(gestureFirst.saturationShift,savedFirst.saturationShift);
    QCOMPARE(gestureFirst.luminanceShift,savedFirst.luminanceShift);
    QCOMPARE(gesture.pointColors[1],saved.pointColors[1]);

    window.undoEdit();
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    QCOMPARE(window.currentRecipe(),saved);
    window.redoEdit();
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    QCOMPARE(window.currentRecipe(),gesture);

    const Recipe beforeRange=window.currentRecipe();
    auto *range=window.findChild<QCheckBox *>(QStringLiteral("point-color-range-preview"));
    QVERIFY2(range&&range->isEnabled(),"range-preview checkbox is enabled");
    QSignalSpy rangeReady(window.findChild<EditLoader *>(),&EditLoader::ready);
    const int readyBefore=rangeReady.count();
    auto latestResult=[&rangeReady](int from,int &pointRange){
        bool found=false;
        for(int i=from;i<rangeReady.count();++i){
            const EditResult result=qvariant_cast<EditResult>(rangeReady.at(i).at(0));
            pointRange=result.view.pointRange;
            found=true;
        }
        return found;
    };
    range->setChecked(true);
    const int readyFrom=readyBefore;
    int viewRange=-2;
    QTRY_VERIFY_WITH_TIMEOUT(latestResult(readyFrom,viewRange)&&viewRange==0,90000);
    QCOMPARE(window.currentRecipe(),beforeRange);
    const auto presetPath=m_dir.filePath("range-safe.json");QVERIFY(window.saveAdjustmentPreset(presetPath,error));AdjustmentPreset preset;QVERIFY(loadPresetFile(presetPath,preset,error));QCOMPARE(preset.adjustments.pointColors,beforeRange.pointColors);
    const QImage selected=qvariant_cast<EditResult>(rangeReady.last()[0]).image;
    QVERIFY2(!selected.isNull(),"range preview frame is renderable");
    if(synthetic){
        const int greenSpread=channelSpread(selected,QPoint(3*selected.width()/4,selected.height()/2));
        const int redSpread=channelSpread(selected,QPoint(selected.width()/4,selected.height()/2));
        QVERIFY2(greenSpread>=0,qPrintable(QStringLiteral("green probe inside %1x%2").arg(selected.width()).arg(selected.height())));
        QVERIFY2(redSpread>=0,qPrintable(QStringLiteral("red probe inside %1x%2").arg(selected.width()).arg(selected.height())));
        QVERIFY2(greenSpread<=2,qPrintable(QStringLiteral("unselected green spread %1").arg(greenSpread)));
        QVERIFY2(redSpread>10,qPrintable(QStringLiteral("selected red spread %1").arg(redSpread)));
    }else{
        qInfo()<<"Real photo: synthetic red/green pixel assertions omitted; complete history/range checks continue.";
    }
    range->setChecked(false);
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    QCOMPARE(window.currentRecipe(),beforeRange);
    window.undoEdit();
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    QCOMPARE(window.currentRecipe(),saved);
    window.redoEdit();
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    QCOMPARE(window.currentRecipe(),gesture);
    window.compareOriginal(true);QVERIFY(!window.findChild<PointColorPanel *>()->isEnabled());window.compareOriginal(false);QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);QCOMPARE(window.currentRecipe(),gesture);
    const auto recipePath=qEnvironmentVariable("LUMINA_POINT_RECIPE");if(!recipePath.isEmpty()){QFile output(recipePath);QVERIFY(output.open(QIODevice::WriteOnly));QVERIFY(output.write(QJsonDocument(recipeJson(gesture)).toJson())>0);}
    const auto shot=qEnvironmentVariable("LUMINA_POINT_SCREENSHOT");if(!shot.isEmpty()){window.findChild<QScrollArea *>()->ensureWidgetVisible(window.findChild<PointColorPanel *>());QTest::qWait(100);QVERIFY(window.grab().save(shot));}
    qInfo() << "sample gestures" << window.currentRecipe().pointColors.size()
            << "interim previews" << interimPreviews << "interim paints" << interimPaintings
            << "loader results" << editorReady.count();
}

void PointColorWindowTests::pickerMutualExclusionAndEscape(){
    QVERIFY2(m_dir.isValid(),"temporary directory for synthetic JPEGs");
    const QString first=writeJpeg(QStringLiteral("picker-a.jpg"),96,64,QColor(200,30,20),QColor(30,200,20));
    const QString second=writeJpeg(QStringLiteral("picker-b.jpg"),96,64,QColor(20,30,220),QColor(220,210,40));
    QVERIFY2(!first.isEmpty()&&!second.isEmpty(),"synthetic picker JPEGs");
    Window window(nullptr,QStringLiteral(""));window.show();
    window.addFiles(QStringList{first});
    QTRY_COMPARE_WITH_TIMEOUT(window.currentIndex(),0,90000);
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    auto *wb=picker(window,"wb-picker");
    auto *point=picker(window,"point-color-picker");
    auto *samples=window.findChild<QComboBox *>(QStringLiteral("point-color-samples"));
    QVERIFY2(wb&&point&&samples,"picker controls exist");
    QTRY_VERIFY_WITH_TIMEOUT(point->isEnabled(),90000);

    wb->click();
    QVERIFY2(wb->isChecked(),"white-balance picker arms");
    point->click();
    QVERIFY2(point->isChecked(),"point picker arms");
    QVERIFY2(!wb->isChecked(),"white-balance picker releases when the point picker arms");

    wb->click();
    QVERIFY2(wb->isChecked(),"white-balance picker takes over");
    QVERIFY2(!point->isChecked(),"point picker releases when white balance arms");

    point->click();
    QVERIFY2(point->isChecked(),"point picker arms again");
    window.activateWindow();
    window.setFocus();
    QTest::keyClick(&window,Qt::Key_Escape);
    QTRY_VERIFY_WITH_TIMEOUT(!point->isChecked(),5000);
    QVERIFY2(!point->isChecked(),"Esc leaves the point picker disarmed");

    QString error;
    QVERIFY2(window.pickPointColor(QPointF(.5,.5),error),qPrintable(error));
    QTest::keyClick(&window,Qt::Key_Escape);
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable()&&point->isEnabled(),90000);
    QCOMPARE(samples->count(),0);
    QCOMPARE(window.currentRecipe().pointColors.size(),0);

    QVERIFY2(window.pickPointColor(QPointF(.5,.5),error),qPrintable(error));
    window.compareOriginal(true);
    QVERIFY2(!window.pickPointColor(QPointF(.5,.5),error),qPrintable(QStringLiteral("compare mode must refuse sampling: %1").arg(error)));
    QVERIFY2(!error.isEmpty(),"refusal carries a message");
    QVERIFY2(!point->isEnabled(),"point picker disabled while comparing originals");
    QCOMPARE(window.currentRecipe().pointColors.size(),0);
    window.compareOriginal(false);
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);

    window.addFiles(QStringList{second});
    QTRY_VERIFY_WITH_TIMEOUT(window.filmstrip()->count()==2,30000);
    QVERIFY2(window.pickPointColor(QPointF(.5,.5),error),qPrintable(error));
    window.activate(1);
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    QCOMPARE(window.currentIndex(),1);
    QCOMPARE(window.currentRecipe().pointColors.size(),0);
    window.activate(0);
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    QCOMPARE(window.currentIndex(),0);
    QCOMPARE(window.currentRecipe().pointColors.size(),0);
    QCOMPARE(samples->count(),0);

    const QString shot=qEnvironmentVariable("LUMINA_POINT_PICKER_SCREENSHOT");
    if(!shot.isEmpty()){
        if(auto *area=window.findChild<QScrollArea *>()){
            if(auto *panel=window.findChild<QWidget *>(QStringLiteral("point-color-panel")))area->ensureWidgetVisible(panel);
        }
        QVERIFY2(window.grab().save(shot),qPrintable(QStringLiteral("screenshot saved to %1").arg(shot)));
    }
    qInfo() << "picker exclusions verified; samples" << samples->count();
}

QTEST_MAIN(PointColorWindowTests)
#include "point_color_window_tests.moc"






