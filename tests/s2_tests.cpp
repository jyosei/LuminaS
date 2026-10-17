#include "background_source.h"
#include "inspector_navigation_support.h"
#include "app/window.h"
#include "color/color_pipeline.h"
#include "io/source_reader.h"
#include "render/cpu_renderer.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QColorSpace>
#include <QSlider>
#include <QLineEdit>
#include <QInputMethodEvent>
#include <QDir>
#include <QStandardPaths>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QElapsedTimer>
#include <lcms2.h>
#include <cmath>
#include <limits>
#include <QDockWidget>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolButton>
#include <QToolBar>
#include <QCryptographicHash>
#include <QScreen>
#include <QFontInfo>
#include <QBuffer>
#include <QInputDialog>
#include <QMessageBox>
#include <QTimer>
#include "render/spatial_operator.h"
#include "render/primary_operator.h"
#include "core/curve_spline.h"
#include "core/neutral_balance.h"
#include "core/auto_balance.h"
#include "core/parametric_curve.h"
#include "render/luminance_curve_operator.h"
#include "render/tone_operator.h"
#include "render/vignette_operator.h"
#include "render/grain_operator.h"

class S2Tests : public QObject {
    Q_OBJECT
    SourceFrame frame(QSize size) {
        SourceFrame s;s.linear=QImage(size,QImage::Format_RGBA32FPx4);
        for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x){auto *p=reinterpret_cast<float *>(s.linear.scanLine(y))+x*4;p[0]=float(x)/size.width();p[1]=float(y)/size.height();p[2]=.2f;p[3]=1;}
        return s;
    }
private slots:
    void grainScaleMetadataRoundTrip(){
        auto s=frame({101,57});s.path="grain-scale";s.sha256="1";s.originalSize={2561,1447};const double sx=2561./101,sy=1447./57;s.sourcePixelsPerPixel=std::max(sx,sy);PreviewCache cache;QVERIFY(cache.put(s));const auto got=cache.get(s.path,s.sha256);QVERIFY(got);QCOMPARE(got->pixelScaleX(),sx);QCOMPARE(got->pixelScaleY(),sy);QCOMPARE(got->linear,s.linear);QCOMPARE(got->sha256,s.sha256);QVERIFY(sx!=sy);Recipe r;r.grainAmount=.5;for(double scale:{0.,-1.,129.,std::numeric_limits<double>::infinity()})QVERIFY(!ProceduralGrain(r,{11,7},scale,1).valid());
    }
    void grainSizeGestureAndGate(){
        QTemporaryDir dir;QVERIFY(dir.isValid());QImage image(3840,2160,QImage::Format_RGB32);image.fill(QColor(150,110,80));const auto path=dir.filePath("size.jpg");QVERIFY(image.save(path));Window w(nullptr,QStringLiteral(""));w.show();QVERIFY(openAllInspectorSections(w));w.addFiles({path});QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),90000);Recipe base;base.grainAmount=.5;w.applyRecipe(base);QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),90000);auto *bar=w.findChild<QSlider *>("slider-grain-size");auto *spin=w.findChild<QDoubleSpinBox *>("grain-size");QVERIFY(bar);QVERIFY(spin);QSignalSpy ready(&w,&Window::previewChanged),painted(w.findChild<Canvas *>(),&Canvas::framePainted);bar->setSliderDown(true);for(int i=1;i<=40;++i){const double value=3+i*.02;bar->setValue(qRound(bar->minimum()+(value-spin->minimum())/(spin->maximum()-spin->minimum())*(bar->maximum()-bar->minimum())));QTest::qWait(50);}const int frames=ready.size(),paints=painted.size();bar->setSliderDown(false);QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),90000);auto expected=base;expected.grainSize=3.8;QCOMPARE(w.currentRecipe(),expected);qInfo()<<"Grain size feedback"<<frames<<paints;QVERIFY(frames>=2);QVERIFY(paints>=2);w.undoEdit();QCOMPARE(w.currentRecipe(),base);QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),90000);w.redoEdit();QCOMPARE(w.currentRecipe(),expected);QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),90000);w.compareOriginal(true);QTRY_VERIFY_WITH_TIMEOUT(!bar->isEnabled(),90000);w.compareOriginal(false);QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),90000);QVERIFY(bar->isEnabled());
    }
    void grainFootprintAndPixels(){
        Recipe r;QVERIFY(validRecipe(r));QCOMPARE(recipeFromJson(QJsonObject{}).grainSize,3.);QCOMPARE(recipeFromJson(QJsonObject{}).grainRoughness,.5);r.grainAmount=.8;r.grainSize=2.7;r.grainRoughness=.6;QCOMPARE(recipeFromJson(recipeJson(r)),r);ProceduralGrain full(r,{63,45},1,1),small(r,{21,15},3,3);QVERIFY(full.valid());QVERIFY(small.valid());for(int y=0;y<15;++y)for(int x=0;x<21;++x){double sum=0;for(int j=0;j<3;++j)for(int i=0;i<3;++i)sum+=full.gain(x*3+i,y*3+j);QVERIFY(std::abs(sum/9-small.gain(x,y))<2e-5);}ProceduralGrain repeated(r,{21,15},3,3);for(int y=0;y<15;++y)for(int x=0;x<21;++x)QCOMPARE(repeated.gain(x,y),small.gain(x,y));ProceduralGrain invalid(r,{21,15},std::numeric_limits<double>::quiet_NaN(),1);QVERIFY(!invalid.valid());
        auto s=frame({63,45});for(int y=0;y<45;++y){auto *p=reinterpret_cast<float *>(s.linear.scanLine(y));for(int x=0;x<63;++x){p[x*4]=-.2f;p[x*4+1]=1.f;p[x*4+2]=2.f;p[x*4+3]=.4f;}}QString error;const auto input=s.linear;const auto linear=renderLinear(s,r,0,error);QVERIFY(error.isEmpty());for(int y=0;y<45;++y){const auto *p=reinterpret_cast<const float *>(linear.constScanLine(y));for(int x=0;x<63;++x){const float gain=full.gain(x,y);QVERIFY(std::abs(p[x*4]+.2f*gain)<1e-6);QVERIFY(std::abs(p[x*4+2]-2.f*gain)<1e-6);QCOMPARE(p[x*4+3],.4f);}}QCOMPARE(s.linear,input);const auto expected=workToSrgbReference(linear,error),actual=renderFrame(s,r,0).image;QVERIFY(!actual.isNull());int difference=0;for(qsizetype i=0;i<expected.sizeInBytes();++i)difference=std::max(difference,std::abs(int(expected.constBits()[i])-int(actual.constBits()[i])));QVERIFY(difference<=1);
        r.grainAmount=0;auto neutral=r;neutral.grainSize=12;neutral.grainRoughness=1;QCOMPARE(renderLinear(s,r,0,error),renderLinear(s,neutral,0,error));auto json=recipeJson(r);json["grainSize"]="bad";QVERIFY(!validRecipe(recipeFromJson(json)));r.grainSize=0;QVERIFY(!validRecipe(r));r.grainSize=3;r.grainRoughness=1.1;QVERIFY(!validRecipe(r));
    }
    void grainCachedControls(){
        QTemporaryDir dir;QVERIFY(dir.isValid());auto path=qEnvironmentVariable("LUMINA_GRAIN_SAMPLE");if(path.isEmpty()){QImage image(3840,2160,QImage::Format_RGB32);image.fill(QColor(150,110,80));path=dir.filePath("grain.jpg");QVERIFY(image.save(path));}Window w(nullptr,QStringLiteral(""));w.show();QVERIFY(openAllInspectorSections(w));w.addFiles({path});QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),90000);Recipe base;base.grainAmount=.5;base.grainSize=3;QElapsedTimer first;first.start();w.applyRecipe(base);QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),90000);qInfo()<<"Grain first preparation ms"<<first.elapsed();QSignalSpy ready(&w,&Window::previewChanged),painted(w.findChild<Canvas *>(),&Canvas::framePainted);
        for(const auto &name:{"grain-amount","grain-roughness"}){auto *bar=w.findChild<QSlider *>(QString("slider-")+name);QVERIFY(bar);const int before=ready.size(),paintBefore=painted.size();bar->setSliderDown(true);for(int i=1;i<=40;++i){bar->setValue(20+i);QTest::qWait(50);}const int frames=ready.size()-before,paints=painted.size()-paintBefore;bar->setSliderDown(false);QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),90000);QVERIFY(frames>=4);QVERIFY(paints>=4);qInfo()<<"Grain feedback"<<name<<frames<<paints;}
        auto expected=base;expected.grainAmount=expected.grainRoughness=.6;QCOMPARE(w.currentRecipe(),expected);w.undoEdit();auto before=expected;before.grainRoughness=.5;QCOMPARE(w.currentRecipe(),before);QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),90000);w.redoEdit();QCOMPARE(w.currentRecipe(),expected);QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),90000);QString error;const auto preset=dir.filePath("Grain.json");QVERIFY(w.saveAdjustmentPreset(preset,error));AdjustmentPreset loaded;QVERIFY(loadPresetFile(preset,loaded,error));QCOMPARE(loaded.adjustments.grainAmount,.6);QCOMPARE(loaded.adjustments.grainSize,3.);const auto shot=qEnvironmentVariable("LUMINA_GRAIN_SCREENSHOT");if(!shot.isEmpty()){w.findChild<QScrollArea *>()->ensureWidgetVisible(w.findChild<QDoubleSpinBox *>("grain-size"));QTest::qWait(100);QVERIFY(w.grab().save(shot));}
    }
    void presetStrengthCancellationAndSwitch(){
        QTemporaryDir dir;QVERIFY(dir.isValid());QImage image(96,64,QImage::Format_RGB32);image.fill(QColor(150,100,80));const auto a=dir.filePath("a.jpg"),b=dir.filePath("b.jpg");QVERIFY(image.save(a));QVERIFY(image.save(b));Window w(nullptr,QStringLiteral(""));w.show();QVERIFY(openAllInspectorSections(w));w.addFiles({a,b});QTRY_VERIFY(w.neutralPickerAvailable());auto *choice=w.findChild<QComboBox *>("preset-choice");auto *bar=w.findChild<QSlider *>("slider-preset-strength");choice->setCurrentIndex(2);w.findChild<QPushButton *>("preset-apply")->click();QTRY_VERIFY(w.neutralPickerAvailable());const auto target=w.currentRecipe();bar->setSliderDown(true);bar->setValue(30);QVERIFY(w.currentRecipe()!=target);bar->setFocus();QTest::keyClick(bar,Qt::Key_Escape);QCOMPARE(w.currentRecipe(),target);bar->setSliderDown(false);QTRY_VERIFY(w.neutralPickerAvailable());QVERIFY(!bar->isEnabled());w.undoEdit();QCOMPARE(w.currentRecipe(),Recipe{});QTRY_VERIFY(w.neutralPickerAvailable());w.findChild<QPushButton *>("preset-apply")->click();QTRY_VERIFY(w.neutralPickerAvailable());bar->setSliderDown(true);bar->setValue(30);const auto draft=w.currentRecipe();w.activate(1);bar->setSliderDown(false);QTRY_VERIFY(w.neutralPickerAvailable());QCOMPARE(w.currentRecipe(),Recipe{});QVERIFY(!bar->isEnabled());w.activate(0);QTRY_VERIFY(w.neutralPickerAvailable());QCOMPARE(w.currentRecipe(),draft);QVERIFY(!bar->isEnabled());
    }
    void presetStrengthMathematics(){
        Recipe base;base.exposure=.312345;base.warmth=.23;base.tint=-.2;base.crop={.1,.1,.8,.8};base.flipHorizontal=true;base.curves[0]={{0,0},{.2,.8},{.7,.1},{1,1}};base.luminanceCurve={{0,0},{.3,.6},{1,1}};base.grading[0]={350,.3,0};AdjustmentPreset preset{"Mixed",{}};preset.adjustments.exposure=-.2;preset.adjustments.curves[0]={{0,.03},{.23,.4},{.4,.5},{.8,.9},{1,.97}};preset.adjustments.luminanceCurve={{0,.02},{.6,.7},{1,.98}};preset.adjustments.monochrome=true;preset.adjustments.grading[0]={10,.5,.1};QString error;Recipe mixed;QVERIFY(blendPreset(base,preset,0,mixed,error));QCOMPARE(mixed,base);QVERIFY(blendPreset(base,preset,1,mixed,error));QCOMPARE(mixed,applyPreset(base,preset));
        for(double amount:{.0001,.25,.5,.9999}){QVERIFY2(blendPreset(base,preset,amount,mixed,error),qPrintable(error));QVERIFY(validRecipe(mixed));const CurveSpline a(base.curves[0]),b(preset.adjustments.curves[0]),m(mixed.curves[0],mixed.curveTangents[0]);for(int i=-80;i<=480;++i){const double x=i/400.;QVERIFY(std::abs(m.value(x)-(a.value(x)*(1-amount)+b.value(x)*amount))<3e-12);}QCOMPARE(mixed.warmth,base.warmth);QCOMPARE(mixed.tint,base.tint);QCOMPARE(mixed.crop,base.crop);QCOMPARE(mixed.flipHorizontal,base.flipHorizontal);QCOMPARE(mixed.monochromeStrength,amount);QCOMPARE(recipeFromJson(recipeJson(mixed)),mixed);}
        QVERIFY(blendPreset(base,preset,.5,mixed,error));QVERIFY(std::abs(mixed.grading[0].hue)<1e-12);const auto before=mixed;QVERIFY(!blendPreset(base,preset,-.1,mixed,error));QCOMPARE(mixed,before);auto tooClose=preset;tooClose.adjustments.curves[0]={{0,0},{.200000000001,.5},{1,1}};QVERIFY(!blendPreset(base,tooClose,.5,mixed,error));QCOMPARE(mixed,before);auto invalid=recipeJson(mixed);invalid["curveTangents"]=false;QVERIFY(!validRecipe(recipeFromJson(invalid)));auto legacy=recipeJson(base);legacy.remove("curveTangents");legacy.remove("luminanceTangents");QCOMPARE(recipeFromJson(legacy),base);
        auto s=frame({61,43});auto r=before;r.crop={0,0,1,1};r.flipHorizontal=false;const auto linear=renderLinear(s,r,0,error),reference=workToSrgbReference(linear,error),actual=renderFrame(s,r,0).image;QVERIFY(error.isEmpty());QVERIFY(!actual.isNull());int difference=0;for(qsizetype i=0;i<actual.sizeInBytes();++i)difference=std::max(difference,std::abs(int(actual.constBits()[i])-int(reference.constBits()[i])));QVERIFY(difference<=1);QTemporaryDir dir;QVERIFY(dir.isValid());QVERIFY(savePresetFile(dir.filePath("mixed.json"),{"Mixed",r},error));AdjustmentPreset loaded;QVERIFY(loadPresetFile(dir.filePath("mixed.json"),loaded,error));QCOMPARE(loaded.adjustments,applyPreset(Recipe{},AdjustmentPreset{"Mixed",r}));const auto recipePath=qEnvironmentVariable("LUMINA_PRESET_STRENGTH_RECIPE");if(!recipePath.isEmpty()){QFile output(recipePath);QVERIFY(output.open(QIODevice::WriteOnly));QVERIFY(output.write(QJsonDocument(recipeJson(r)).toJson())>0);}
    }
    void presetStrengthGesture(){
        QTemporaryDir dir;QVERIFY(dir.isValid());auto path=qEnvironmentVariable("LUMINA_PRESET_STRENGTH_SAMPLE");if(path.isEmpty()){QImage image(4800,3200,QImage::Format_RGB32);image.fill(QColor(160,100,70));path=dir.filePath("strength.jpg");QVERIFY(image.save(path));}Window w(nullptr,QStringLiteral(""));w.show();QVERIFY(openAllInspectorSections(w));auto *bar=w.findChild<QSlider *>("slider-preset-strength");QVERIFY(bar);QVERIFY(!bar->isEnabled());w.addFiles({path});QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),90000);QVERIFY(!bar->isEnabled());Recipe base;base.exposure=.312345;base.crop={.1,.1,.8,.8};base.flipHorizontal=true;base.curves[0]={{0,0},{.3,.4},{1,1}};w.applyRecipe(base);QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),90000);auto *choice=w.findChild<QComboBox *>("preset-choice");choice->setCurrentIndex(2);w.findChild<QPushButton *>("preset-apply")->click();QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),90000);QVERIFY(bar->isEnabled());const auto target=applyPreset(base,builtinPresets()[2]);QCOMPARE(w.currentRecipe(),target);QSignalSpy ready(&w,&Window::previewChanged),painted(w.findChild<Canvas *>(),&Canvas::framePainted);bar->setSliderDown(true);for(int i=1;i<=40;++i){bar->setValue(100-i*2);QMetaObject::invokeMethod(w.findChild<QDoubleSpinBox *>("exposure"),"editingFinished");QTest::qWait(50);}const auto frames=ready.size(),paints=painted.size();bar->setSliderDown(false);QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),90000);Recipe expected;QString error;QVERIFY(blendPreset(base,builtinPresets()[2],.2,expected,error));QCOMPARE(w.currentRecipe(),expected);QVERIFY(frames>=4);QVERIFY(paints>=4);qInfo()<<"Preset strength feedback"<<frames<<paints;w.undoEdit();QCOMPARE(w.currentRecipe(),target);QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),90000);QVERIFY(!bar->isEnabled());w.redoEdit();QCOMPARE(w.currentRecipe(),expected);QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),90000);
        w.applyRecipe(base);QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),90000);w.findChild<QPushButton *>("preset-apply")->click();QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),90000);bar->setValue(0);QCOMPARE(w.currentRecipe(),base);QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),90000);bar->setValue(100);QCOMPARE(w.currentRecipe(),target);QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),90000);bar->setValue(20);QCOMPARE(w.currentRecipe(),expected);QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),90000);const auto shot=qEnvironmentVariable("LUMINA_PRESET_STRENGTH_SCREENSHOT");if(!shot.isEmpty()){w.findChild<QScrollArea *>()->ensureWidgetVisible(bar);QTest::qWait(100);QVERIFY(w.grab().save(shot));}auto *exposure=w.findChild<QDoubleSpinBox *>("exposure");exposure->setValue(-.25);QMetaObject::invokeMethod(exposure,"editingFinished");QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),90000);QVERIFY(!bar->isEnabled());
    }
    void monochromeStrengthModel(){
        auto s=frame({43,31});auto *edge=reinterpret_cast<float *>(s.linear.scanLine(0));edge[0]=-.1f;edge[1]=.8f;edge[2]=1.7f;edge[3]=.4f;Recipe r;r.exposure=.2;r.saturation=1.6;r.vibrance=.3;r.monochromeMix[0]=.5;r.monochromeMix[5]=-.3;r.curves[1]={{0,0},{.4,.5},{1,1}};r.hsl[0]={.2,.1,.2};r.grading[1]={230,.2,.1};r.vignetteAmount=-.4;QString error;const auto color=renderLinear(s,r,0,error);QVERIFY(error.isEmpty());r.monochrome=true;const auto gray=renderLinear(s,r,0,error);QVERIFY(error.isEmpty());r.monochromeStrength=0;QCOMPARE(renderLinear(s,r,0,error),color);
        for(double strength:{.0001,.2,.5,.9999}){r.monochromeStrength=strength;const auto mixed=renderLinear(s,r,0,error);QVERIFY(error.isEmpty());for(int y=0;y<mixed.height();++y){const auto *a=reinterpret_cast<const float *>(color.constScanLine(y)),*b=reinterpret_cast<const float *>(gray.constScanLine(y)),*v=reinterpret_cast<const float *>(mixed.constScanLine(y));for(int x=0;x<mixed.width();++x){for(int c=0;c<3;++c)QVERIFY(std::abs(v[x*4+c]-(a[x*4+c]*(1-strength)+b[x*4+c]*strength))<2e-6);QCOMPARE(v[x*4+3],a[x*4+3]);}}const auto expected=workToSrgbReference(mixed,error),actual=renderFrame(s,r,0).image;QVERIFY(!actual.isNull());int difference=0;for(qsizetype i=0;i<expected.sizeInBytes();++i)difference=std::max(difference,std::abs(int(expected.constBits()[i])-int(actual.constBits()[i])));QVERIFY(difference<=1);}
        r.monochromeStrength=1;QCOMPARE(renderLinear(s,r,0,error),gray);QCOMPARE(recipeFromJson(recipeJson(r)),r);QTemporaryDir presetDir;QVERIFY(presetDir.isValid());r.monochromeStrength=.37;const auto presetPath=presetDir.filePath("partial.json");QVERIFY(savePresetFile(presetPath,{"Partial",r},error));AdjustmentPreset loadedPreset;QVERIFY(loadPresetFile(presetPath,loadedPreset,error));QCOMPARE(loadedPreset.adjustments,applyPreset(Recipe{},AdjustmentPreset{"Partial",r}));auto json=recipeJson(r);json.remove("monochromeStrength");QCOMPARE(recipeFromJson(json).monochromeStrength,1.);for(const auto &value:{QJsonValue("bad"),QJsonValue(true),QJsonValue(-.1),QJsonValue(1.1)}){json["monochromeStrength"]=value;QVERIFY(!validRecipe(recipeFromJson(json)));}r.monochromeStrength=std::numeric_limits<double>::quiet_NaN();QVERIFY(!validRecipe(r));
    }
    void continuousMonochromeStrength(){
        QTemporaryDir dir;QVERIFY(dir.isValid());auto path=qEnvironmentVariable("LUMINA_MONO_STRENGTH_SAMPLE");if(path.isEmpty()){QImage image(3840,2160,QImage::Format_RGB32);image.fill(QColor(160,100,70));path=dir.filePath("mono4k.jpg");QVERIFY(image.save(path));}Window w(nullptr,QStringLiteral(""));w.show();QVERIFY(openAllInspectorSections(w));connect(w.findChild<EditLoader *>(),&EditLoader::ready,&w,[](const EditResult &result){QVERIFY2(result.error.isEmpty(),qPrintable(result.error));QCOMPARE(qMax(result.image.width(),result.image.height()),3840);});auto *bar=w.findChild<QSlider *>("slider-monochrome-strength");QVERIFY(bar);w.addFiles({path});QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),90000);QVERIFY(!bar->isEnabled());Recipe base;base.monochrome=true;base.monochromeStrength=.8;base.saturation=1.4;base.curves[1]={{0,0},{.4,.5},{1,1}};base.grading[1]={230,.2,.1};w.applyRecipe(base);QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),90000);QVERIFY(bar->isEnabled());QSignalSpy ready(&w,&Window::previewChanged),painted(w.findChild<Canvas *>(),&Canvas::framePainted);bar->setSliderDown(true);for(int i=1;i<=40;++i){bar->setValue(80-i);QTest::qWait(50);}const int frames=ready.size(),paints=painted.size();bar->setSliderDown(false);QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),90000);auto expected=base;expected.monochromeStrength=.4;QCOMPARE(w.currentRecipe(),expected);QVERIFY(frames>=4);QVERIFY(paints>=4);qInfo()<<"Monochrome strength feedback"<<frames<<paints;w.undoEdit();QCOMPARE(w.currentRecipe(),base);QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),90000);w.redoEdit();QCOMPARE(w.currentRecipe(),expected);QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),90000);auto *toggle=w.findChild<QCheckBox *>("monochrome-enabled");toggle->click();QVERIFY(!bar->isEnabled());QCOMPARE(w.currentRecipe().monochromeStrength,.4);QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),90000);QVERIFY(!bar->isEnabled());toggle->click();QCOMPARE(w.currentRecipe(),expected);QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),90000);QVERIFY(bar->isEnabled());w.compareOriginal(true);QTRY_VERIFY_WITH_TIMEOUT(!bar->isEnabled(),90000);w.compareOriginal(false);QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),90000);QVERIFY(bar->isEnabled());const auto shot=qEnvironmentVariable("LUMINA_MONO_STRENGTH_SCREENSHOT");if(!shot.isEmpty()){w.findChild<QScrollArea *>()->ensureWidgetVisible(bar);QTest::qWait(100);QVERIFY(w.grab().save(shot));}
    }
    void managedPresetUiAndRestart(){
        QTemporaryDir dir;QVERIFY(dir.isValid());const auto root=dir.filePath("library"),photo=dir.filePath("photo.jpg"),external=dir.filePath("external.json");QString error;Recipe saved;saved.exposure=.7;QVERIFY(storePresetLibrary(root,{"Alpha",saved},error));QVERIFY(storePresetLibrary(root,{"Beta",{}},error));QImage image(96,64,QImage::Format_RGB32);image.fill(QColor(120,90,70));QVERIFY(image.save(photo));
        {Window w(nullptr,root);w.show();QVERIFY(openAllInspectorSections(w));auto *choice=w.findChild<QComboBox *>("preset-choice");auto *manage=w.findChild<QPushButton *>("preset-manage");QVERIFY(manage);QVERIFY(!manage->isEnabled());QVERIFY(!w.renameSelectedPreset("Oops",error));QVERIFY(!w.removeSelectedPreset(error));w.addFiles({photo});QTRY_VERIFY(w.neutralPickerAvailable());Recipe base;base.exposure=.312345;base.warmth=.2;base.crop={.1,.1,.8,.8};w.applyRecipe(base);QTRY_VERIFY(w.neutralPickerAvailable());QSignalSpy ready(&w,&Window::previewChanged);choice->setCurrentIndex(4);QVERIFY(manage->isEnabled());QVERIFY(!w.renameSelectedPreset("Beta",error));QVERIFY(!w.renameSelectedPreset(" ",error));QVERIFY(w.renameSelectedPreset("Gamma",error));QVERIFY(choice->currentText().contains("Gamma"));QCOMPARE(w.currentRecipe(),base);QCOMPARE(ready.size(),0);
            bool renameDialog=false,deleteDialog=false;QTimer::singleShot(0,[&]{auto *dialog=qobject_cast<QInputDialog *>(QApplication::activeModalWidget());if(dialog){renameDialog=true;dialog->reject();}});w.findChild<QAction *>("preset-rename")->trigger();QVERIFY(renameDialog);QVERIFY(choice->currentText().contains("Gamma"));QTimer::singleShot(0,[&]{auto *dialog=qobject_cast<QMessageBox *>(QApplication::activeModalWidget());if(dialog){deleteDialog=true;QCOMPARE(dialog->standardButton(dialog->defaultButton()),QMessageBox::No);dialog->done(QMessageBox::No);}});w.findChild<QAction *>("preset-remove")->trigger();QVERIFY(deleteDialog);QCOMPARE(choice->count(),6);
            const auto shot=qEnvironmentVariable("LUMINA_PRESET_MANAGE_SCREENSHOT");if(!shot.isEmpty()){w.findChild<QScrollArea *>()->ensureWidgetVisible(manage);QTest::qWait(100);QVERIFY(w.grab().save(shot));}
            QVERIFY(w.removeSelectedPreset(error));QCOMPARE(choice->count(),5);QCOMPARE(w.currentRecipe(),base);QCOMPARE(ready.size(),0);w.undoEdit();QCOMPARE(w.currentRecipe(),Recipe{});QTRY_VERIFY(w.neutralPickerAvailable());}
        {Window w(nullptr,root);auto *choice=w.findChild<QComboBox *>("preset-choice");QCOMPARE(choice->count(),5);QVERIFY(choice->itemText(4).contains("Beta"));choice->setCurrentIndex(4);QStringList warnings;QVERIFY(removePresetLibrary(root,"Beta",error));QVERIFY(!w.renameSelectedPreset("Missing",error));QVERIFY(!w.removeSelectedPreset(error));QCOMPARE(choice->count(),5);QVERIFY(choice->currentText().contains("Beta"));}
        QVERIFY(savePresetFile(external,{"Session",saved},error));Window session(nullptr,QStringLiteral(""));session.show();session.addFiles({photo});QTRY_VERIFY(session.neutralPickerAvailable());QVERIFY(session.loadAdjustmentPreset(external,error));QTRY_VERIFY(session.neutralPickerAvailable());const auto applied=session.currentRecipe();QVERIFY(session.renameSelectedPreset("Transient",error));QVERIFY(session.removeSelectedPreset(error));QCOMPARE(session.currentRecipe(),applied);QVERIFY(QFile::exists(external));session.undoEdit();QCOMPARE(session.currentRecipe(),Recipe{});
    }
    void managedPresetRenameAndDelete(){
        QTemporaryDir dir;QVERIFY(dir.isValid());const auto root=dir.filePath("library");QString error;QStringList warnings;AdjustmentPreset original{QStringLiteral("Old"),{}};original.adjustments.exposure=.312345;original.adjustments.sharpenMask=.73;original.adjustments.vignetteAmount=-.4;QVERIFY(storePresetLibrary(root,original,error));auto files=[&]{return QDir(root).entryList({"*.luminas-preset.json"},QDir::Files,QDir::Name);};const auto before=files();QCOMPARE(before.size(),1);QVERIFY(renamePresetLibrary(root,"Old","New",error));QCOMPARE(files(),before);auto library=loadPresetLibrary(root,warnings);QCOMPARE(library.size(),1);QCOMPARE(library[0].name,QStringLiteral("New"));QCOMPARE(library[0].adjustments,applyPreset(Recipe{},original));
        QFile stored(QDir(root).filePath(before[0]));QVERIFY(stored.open(QIODevice::ReadOnly));const auto bytes=stored.readAll();stored.close();QVERIFY(renamePresetLibrary(root,"New"," New ",error));for(const auto &bad:{QString("  "),QString(81,'x'),QString("Bad\nName")})QVERIFY(!renamePresetLibrary(root,"New",bad,error));QVERIFY(stored.open(QIODevice::ReadOnly));QCOMPARE(stored.readAll(),bytes);stored.close();QVERIFY(storePresetLibrary(root,original,error));QCOMPARE(files().size(),2);QVERIFY(!renamePresetLibrary(root,"New","Old",error));AdjustmentPreset updated{"New",original.adjustments};updated.adjustments.exposure=.9;QVERIFY(storePresetLibrary(root,updated,error));QCOMPARE(files().size(),2);library=loadPresetLibrary(root,warnings);QCOMPARE(library.size(),2);QCOMPARE(library[0].name,QStringLiteral("New"));QCOMPARE(library[0].adjustments,applyPreset(Recipe{},updated));QCOMPARE(library[1].name,QStringLiteral("Old"));QCOMPARE(library[1].adjustments,applyPreset(Recipe{},original));
        const auto external=dir.filePath("external.luminas-preset.json");QVERIFY(savePresetFile(external,original,error));QVERIFY(removePresetLibrary(root,"Old",error));QVERIFY(!removePresetLibrary(root,"Old",error));QVERIFY(removePresetLibrary(root,"New",error));QVERIFY(files().isEmpty());QVERIFY(QFile::exists(external));
        const auto duplicate=dir.filePath("duplicates");QVERIFY(QDir().mkpath(duplicate));QVERIFY(savePresetFile(duplicate+"/one.luminas-preset.json",original,error));QVERIFY(savePresetFile(duplicate+"/two.luminas-preset.json",original,error));QVERIFY(!renamePresetLibrary(duplicate,"Old","Other",error));QVERIFY(!removePresetLibrary(duplicate,"Old",error));QVERIFY(!storePresetLibrary(duplicate,original,error));QCOMPARE(QDir(duplicate).entryList({"*.luminas-preset.json"},QDir::Files).size(),2);
    }
    void automaticWhiteBalanceReal(){
        const auto path=qEnvironmentVariable("LUMINA_AUTO_WB_SAMPLE");if(path.isEmpty())QSKIP("Optional local photo verification");Window w(nullptr,QStringLiteral(""));w.show();QVERIFY(openAllInspectorSections(w));w.addFiles({path});QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),90000);Recipe base;base.exposure=.312345;base.crop={.1,.1,.8,.8};base.angle=2;base.flipHorizontal=true;base.vignetteAmount=-.4;w.applyRecipe(base);QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),90000);QString error;QElapsedTimer timer;timer.start();QVERIFY2(w.autoWhiteBalance(error),qPrintable(error));const auto estimateMs=timer.elapsed();const auto estimated=w.currentRecipe();auto preserved=estimated;preserved.warmth=base.warmth;preserved.tint=base.tint;QCOMPARE(preserved,base);QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),90000);qInfo()<<"Automatic WB"<<estimated.warmth<<estimated.tint<<"estimate ms"<<estimateMs;w.undoEdit();QCOMPARE(w.currentRecipe(),base);QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),90000);w.redoEdit();QCOMPARE(w.currentRecipe(),estimated);QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),90000);const auto report=qEnvironmentVariable("LUMINA_AUTO_WB_RECIPE");if(!report.isEmpty()){QFile file(report);QVERIFY(file.open(QIODevice::WriteOnly));QVERIFY(file.write(QJsonDocument(recipeJson(estimated)).toJson())>0);}
    }
    void automaticWhiteBalanceUi(){
        QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=dir.filePath("auto.jpg");QImage image(96,64,QImage::Format_RGB32);image.fill(QColor(170,130,95));QVERIFY(image.save(path,"JPEG"));
        Window w(nullptr,QStringLiteral(""));auto *button=w.findChild<QPushButton *>("wb-auto"),*picker=w.findChild<QPushButton *>("wb-picker");QVERIFY(button);QVERIFY(picker);QVERIFY(!button->isEnabled());QString error;QVERIFY(!w.autoWhiteBalance(error));w.show();QVERIFY(openAllInspectorSections(w));QSignalSpy ready(&w,&Window::previewChanged);w.addFiles({path});QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),30000);
        Recipe base;base.exposure=.312345;base.warmth=.23;base.tint=-.1;base.crop={.1,.1,.8,.8};base.angle=2;base.flipHorizontal=true;base.vignetteAmount=-.4;w.applyRecipe(base);QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),30000);w.compareOriginal(true);QVERIFY(!w.autoWhiteBalance(error));QVERIFY(!button->isEnabled());w.compareOriginal(false);QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),30000);
        picker->click();QVERIFY(picker->isChecked());button->click();QVERIFY(!picker->isChecked());const auto estimated=w.currentRecipe();QVERIFY(estimated.warmth!=base.warmth||estimated.tint!=base.tint);auto preserved=estimated;preserved.warmth=base.warmth;preserved.tint=base.tint;QCOMPARE(preserved,base);QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),30000);
        w.undoEdit();QCOMPARE(w.currentRecipe(),base);QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),30000);w.redoEdit();QCOMPARE(w.currentRecipe(),estimated);QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),30000);const int count=ready.size();QVERIFY2(w.autoWhiteBalance(error),qPrintable(error));QCOMPARE(w.currentRecipe(),estimated);QTest::qWait(100);QCOMPARE(ready.size(),count);w.undoEdit();QCOMPARE(w.currentRecipe(),base);
        const auto shot=qEnvironmentVariable("LUMINA_AUTO_WB_SCREENSHOT");if(!shot.isEmpty()){QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),30000);w.findChild<QScrollArea *>()->ensureWidgetVisible(button);QTest::qWait(100);QVERIFY(w.grab().save(shot));}
    }
    void automaticBalanceModel(){
        auto s=frame({64,64});for(int y=0;y<64;++y){auto *p=reinterpret_cast<float *>(s.linear.scanLine(y));for(int x=0;x<64;++x){p[x*4]=.3f;p[x*4+1]=.2f;p[x*4+2]=.15f;p[x*4+3]=.4f;}}
        Recipe original;original.exposure=.312345;original.crop={.1,.1,.8,.8};original.flipHorizontal=true;original.vignetteAmount=-.4;auto r=original;QString error;const auto inputHash=QCryptographicHash::hash(QByteArrayView(s.linear.constBits(),s.linear.sizeInBytes()),QCryptographicHash::Sha256);QVERIFY2(automaticBalance(s,r,error),qPrintable(error));auto expected=original;expected.warmth=-.5;expected.tint=std::log2(std::sqrt(.3*.15)/.2);QVERIFY(std::abs(r.warmth-expected.warmth)<1e-6);QVERIFY(std::abs(r.tint-expected.tint)<1e-6);auto preserved=r;preserved.warmth=original.warmth;preserved.tint=original.tint;QCOMPARE(preserved,original);QCOMPARE(QCryptographicHash::hash(QByteArrayView(s.linear.constBits(),s.linear.sizeInBytes()),QCryptographicHash::Sha256),inputHash);auto again=r;QVERIFY(automaticBalance(s,again,error));QCOMPARE(again,r);
        for(int y=0;y<6;++y){auto *p=reinterpret_cast<float *>(s.linear.scanLine(y));for(int x=0;x<64;++x){p[x*4]=p[x*4+1]=.02f;p[x*4+2]=.6f;}}r=original;QVERIFY(automaticBalance(s,r,error));QVERIFY(std::abs(r.warmth-expected.warmth)<1e-6);QVERIFY(std::abs(r.tint-expected.tint)<1e-6);
        SourceFrame raw;raw.raw=true;raw.cameraToWork={1.5f,0,0,0,1,0,0,0,.5f};const std::array<double,4> baseWB{2,1,1.5,1};raw.rawWarmth=.3;raw.rawTint=-.2;const double delta[]{std::exp2(.3),std::exp2(-.2),std::exp2(-.3),std::exp2(-.2)};for(int i=0;i<4;++i)raw.cameraWB[i]=baseWB[i]*delta[i];const double scale=*std::min_element(baseWB.begin(),baseWB.end())/ *std::min_element(raw.cameraWB.begin(),raw.cameraWB.end());raw.linear=QImage(64,64,QImage::Format_RGBA32FPx4);for(int y=0;y<64;++y){auto *p=reinterpret_cast<float *>(raw.linear.scanLine(y));for(int x=0;x<64;++x){p[x*4]=float(.3*delta[0]*scale);p[x*4+1]=float(.2*delta[1]*scale);p[x*4+2]=float(.15*delta[2]*scale);p[x*4+3]=1;}}r=original;r.warmth=.3;r.tint=-.2;QVERIFY2(automaticBalance(raw,r,error),qPrintable(error));QVERIFY(std::abs(r.warmth-expected.warmth)<2e-6);QVERIFY(std::abs(r.tint-expected.tint)<2e-6);raw.cameraWB[1]=0;const auto before=r;QVERIFY(!automaticBalance(raw,r,error));QCOMPARE(r,before);
        auto dark=frame({64,64});dark.linear.fill(Qt::black);r=original;QVERIFY(!automaticBalance(dark,r,error));QCOMPARE(r,original);auto tiny=frame({1,1});QVERIFY(!automaticBalance(tiny,r,error));QCOMPARE(r,original);auto *bad=reinterpret_cast<float *>(s.linear.scanLine(20));bad[0]=std::numeric_limits<float>::quiet_NaN();QVERIFY(!automaticBalance(s,r,error));QCOMPARE(r,original);
    }
    void pureFlipFusedAndHiddenAlpha(){
        auto s=frame({41,33});auto *hidden=reinterpret_cast<float *>(s.linear.scanLine(0));hidden[0]=hidden[1]=hidden[2]=3e38f;hidden[3]=0;reinterpret_cast<float *>(s.linear.scanLine(10))[3*4+3]=.4f;QString error;
        for(bool h:{false,true})for(bool v:{false,true})for(double amount:{0.,-1.,2.}){Recipe r;r.flipHorizontal=h;r.flipVertical=v;r.vignetteAmount=amount;const auto linear=renderLinear(s,r,0,error);QVERIFY2(error.isEmpty(),qPrintable(error));const auto reference=workToSrgbReference(linear,error),actual=renderFrame(s,r,0).image;QVERIFY(!actual.isNull());QCOMPARE(reference.size(),actual.size());int difference=0;for(qsizetype i=0;i<reference.sizeInBytes();++i)difference=std::max(difference,std::abs(int(reference.constBits()[i])-int(actual.constBits()[i])));QVERIFY(difference<=1);const auto pixel=actual.pixelColor(h?40:0,v?32:0);QCOMPARE(pixel.alpha(),0);QCOMPARE(pixel.red(),0);QCOMPARE(pixel.green(),0);QCOMPARE(pixel.blue(),0);}
        hidden[0]=std::numeric_limits<float>::quiet_NaN();Recipe r;r.vignetteAmount=2;QVERIFY(!renderFrame(s,r,0).error.isEmpty());
    }
    void continuousVignetteFeedback(){
        QTemporaryDir dir;const auto sample=qEnvironmentVariable("LUMINA_VIGNETTE_SAMPLE");QString path=sample;if(path.isEmpty()){QImage image(3840,2160,QImage::Format_RGB32);image.fill(QColor(130,100,80));path=dir.path()+"/vignette4k.jpg";QVERIFY(image.save(path));}Window w(nullptr,QStringLiteral(""));w.show();QVERIFY(openAllInspectorSections(w));QSignalSpy ready(&w,&Window::previewChanged),painted(w.findChild<Canvas *>(),&Canvas::framePainted);connect(w.findChild<EditLoader *>(),&EditLoader::ready,&w,[&](const EditResult &r){QVERIFY2(r.error.isEmpty(),qPrintable(r.error));QCOMPARE(qMax(r.image.width(),r.image.height()),3840);});w.addFiles({path});QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),90000);Recipe base;base.vignetteAmount=-.8;if(!sample.isEmpty()){base.crop={.1,.1,.8,.8};base.angle=2;base.flipHorizontal=true;}w.applyRecipe(base);QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),90000);QJsonArray results;
        for(int stage=0;stage<3;++stage){auto *bar=w.findChild<QSlider *>(stage==0?"slider-vignette-amount":stage==1?"slider-vignette-midpoint":"slider-vignette-feather");QVERIFY(bar);const auto before=ready.size(),paintBefore=painted.size();bar->setSliderDown(true);for(int i=1;i<=40;++i){bar->setValue(stage==0?-i*3:10+i*2);QTest::qWait(50);}const auto frames=ready.size()-before,paints=painted.size()-paintBefore;bar->setSliderDown(false);QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),90000);QVERIFY(frames>=4);QVERIFY(paints>=4);results.append(QJsonObject{{"control",stage==0?"amount":stage==1?"midpoint":"feather"},{"frames",frames},{"painted",paints}});qInfo()<<"Vignette feedback"<<stage<<frames<<paints;}
        auto expected=base;expected.vignetteAmount=-1.2;expected.vignetteMidpoint=expected.vignetteFeather=.9;QCOMPARE(w.currentRecipe(),expected);w.undoEdit();auto before=expected;before.vignetteFeather=.5;QCOMPARE(w.currentRecipe(),before);QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),90000);w.redoEdit();QCOMPARE(w.currentRecipe(),expected);QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),90000);const auto report=qEnvironmentVariable("LUMINA_VIGNETTE_REPORT");if(!report.isEmpty()){QFile file(report);QVERIFY(file.open(QIODevice::WriteOnly));const auto bytes=QJsonDocument(results).toJson();QCOMPARE(file.write(bytes),qint64(bytes.size()));}const auto shot=qEnvironmentVariable("LUMINA_VIGNETTE_SCREENSHOT");if(!shot.isEmpty()){w.findChild<QScrollArea *>()->ensureWidgetVisible(w.findChild<QDoubleSpinBox *>("vignette-feather"));QTest::qWait(100);QVERIFY(w.grab().save(shot));}
    }
    void vignettePixelsCoordinatesAndAlpha(){
        auto s=frame({801,603});s.linear.fill(QColor());for(int y=0;y<603;++y){auto *p=reinterpret_cast<float *>(s.linear.scanLine(y));for(int x=0;x<801;++x){p[x*4]=-.2f;p[x*4+1]=.8f;p[x*4+2]=2.f;p[x*4+3]=.4f;}}
        Recipe r;QString error;const auto neutral=renderLinear(s,r,0,error);r.vignetteMidpoint=0;r.vignetteFeather=0;QCOMPARE(renderLinear(s,r,0,error),neutral);r.vignetteAmount=-2;const auto full=renderLinear(s,r,0,error);QVERIFY(error.isEmpty());const auto preview=renderLinear(s,r,267,error);QCOMPARE(preview.size(),QSize(267,201));const auto *center=reinterpret_cast<const float *>(full.constScanLine(301))+400*4;QVERIFY(std::abs(center[0]+.2f)<1e-6);QCOMPARE(center[3],.4f);const auto *corner=reinterpret_cast<const float *>(full.constScanLine(0));QVERIFY(corner[0]<0);QVERIFY(std::abs(corner[1]-.2f)<1e-6);QVERIFY(std::abs(corner[2]-.5f)<1e-6);
        for(int y=0;y<201;++y){const auto *a=reinterpret_cast<const float *>(preview.constScanLine(y)),*b=reinterpret_cast<const float *>(full.constScanLine(3*y+1));for(int x=0;x<267;++x)for(int c=0;c<4;++c)QVERIFY(std::abs(a[x*4+c]-b[(3*x+1)*4+c])<2e-6);}
        r.vignetteAmount=2;const auto bright=renderLinear(s,r,0,error);QVERIFY(error.isEmpty());const auto *brightRow=reinterpret_cast<const float *>(bright.constScanLine(0));QVERIFY(brightRow[0]<-.2f);QVERIFY(brightRow[2]>2);QCOMPARE(brightRow[3],.4f);r.vignetteAmount=-2;r.vignetteMidpoint=.2;r.vignetteFeather=0;PostCropVignette hard(r,{801,603});r.vignetteFeather=1;PostCropVignette soft(r,{801,603});QVERIFY(soft.gain(400,0)>hard.gain(400,0));QCOMPARE(soft.gain(400,301),1.f);
        r.crop={.2,.1,.5,.6};r.angle=12;r.flipHorizontal=true;r.vignetteAmount=-.8;r.vignetteMidpoint=.3;r.vignetteFeather=.8;const auto crop=renderLinear(s,r,0,error);QVERIFY(error.isEmpty());SourceFrame constant=s;constant.linear=QImage(crop.size(),QImage::Format_RGBA32FPx4);for(int y=0;y<crop.height();++y){auto *p=reinterpret_cast<float *>(constant.linear.scanLine(y));for(int x=0;x<crop.width();++x){p[x*4]=-.2f;p[x*4+1]=.8f;p[x*4+2]=2.f;p[x*4+3]=.4f;}}auto flat=r;flat.crop={0,0,1,1};flat.angle=0;flat.flipHorizontal=false;const auto expected=renderLinear(constant,flat,0,error);for(int y=0;y<crop.height();++y){const auto *a=reinterpret_cast<const float *>(crop.constScanLine(y)),*b=reinterpret_cast<const float *>(expected.constScanLine(y));for(int x=0;x<crop.width()*4;++x)QVERIFY(std::abs(a[x]-b[x])<3e-6);}
        r={};r.vignetteAmount=-1;r.contrast=.1;r.exposure=.2;r.saturation=1.2;r.curves[0]={{0,0},{.4,.5},{1,1}};for(bool spatial:{false,true}){r.texture=spatial?.1:0;r.sharpenAmount=spatial?.3:0;r.sharpenMask=spatial?.7:0;const auto reference=workToSrgbReference(renderLinear(s,r,0,error),error);SpatialCache cache;const auto result=renderFrame(s,r,0,&cache);QVERIFY2(result.error.isEmpty(),qPrintable(result.error));int difference=0;for(qsizetype i=0;i<reference.sizeInBytes();++i)difference=std::max(difference,std::abs(int(reference.constBits()[i])-int(result.image.constBits()[i])));QVERIFY(difference<=1);}
        auto *p=reinterpret_cast<float *>(s.linear.scanLine(0));p[3]=0;r={};r.vignetteAmount=1;const auto alpha=renderLinear(s,r,0,error);const auto *out=reinterpret_cast<const float *>(alpha.constScanLine(0));QCOMPARE(out[0],0.f);QCOMPARE(out[1],0.f);QCOMPARE(out[2],0.f);QCOMPARE(out[3],0.f);
        for(double value:{-2.01,2.01}){r={};r.vignetteAmount=value;QVERIFY(!validRecipe(r));}for(double value:{-.01,1.01}){r={};r.vignetteMidpoint=value;QVERIFY(!validRecipe(r));r={};r.vignetteFeather=value;QVERIFY(!validRecipe(r));}
    }
void vignetteRecipeAndUi()
{
    // --- defaults ---
    Recipe defaults;
    QCOMPARE(defaults.vignetteAmount, 0.0);
    QCOMPARE(defaults.vignetteMidpoint, 0.5);
    QCOMPARE(defaults.vignetteFeather, 0.5);
    QVERIFY(validRecipe(Recipe{}));

    // --- JSON roundtrip ---
    Recipe r;
    r.vignetteAmount = -1.25;
    r.vignetteMidpoint = 0.23;
    r.vignetteFeather = 0.76;
    r.exposure = 0.312345;
    Recipe back = recipeFromJson(recipeJson(r));
    QCOMPARE(back.vignetteAmount, -1.25);
    QCOMPARE(back.vignetteMidpoint, 0.23);
    QCOMPARE(back.vignetteFeather, 0.76);
    QCOMPARE(back.exposure, 0.312345);

    // --- missing keys default ---
    QJsonObject empty;
    Recipe def = recipeFromJson(empty);
    QCOMPARE(def.vignetteAmount, 0.0);
    QCOMPARE(def.vignetteMidpoint, 0.5);
    QCOMPARE(def.vignetteFeather, 0.5);
    QVERIFY(validRecipe(def));

    // --- each key rejected when wrong type ---
    const QStringList keys{QStringLiteral("vignetteAmount"), QStringLiteral("vignetteMidpoint"), QStringLiteral("vignetteFeather")};
    const QJsonValue bad[] = {QJsonValue(), QJsonValue(true), QJsonValue(QStringLiteral("x"))};
    for (const QString &k : keys) {
        for (const QJsonValue &v : bad) {
            QJsonObject o; o.insert(k, v);
            QVERIFY(!validRecipe(recipeFromJson(o)));
        }
    }

    // --- invalid numeric fields ---
    {
        Recipe o; o.vignetteAmount = std::numeric_limits<double>::infinity();
        QVERIFY(!validRecipe(o));
    }
    {
        Recipe o; o.vignetteMidpoint = std::numeric_limits<double>::quiet_NaN();
        QVERIFY(!validRecipe(o));
    }
    {
        Recipe o; o.vignetteFeather = std::numeric_limits<double>::quiet_NaN();
        QVERIFY(!validRecipe(o));
    }

    // --- preset atomic save/load ---
    QTemporaryDir dir; QVERIFY(dir.isValid());
    AdjustmentPreset p{QStringLiteral("vignette"), r};
    const QString path = dir.filePath(QStringLiteral("v.luminas-preset.json"));
    QString err;
    QVERIFY(savePresetFile(path, p, err));
    AdjustmentPreset loaded;
    QVERIFY(loadPresetFile(path, loaded, err));
    QCOMPARE(loaded.adjustments.vignetteAmount, -1.25);
    QCOMPARE(loaded.adjustments.vignetteMidpoint, 0.23);
    QCOMPARE(loaded.adjustments.vignetteFeather, 0.76);
    Recipe want = applyPreset(Recipe{}, p);
    QCOMPARE(loaded.adjustments, want);

    // --- UI ---
    QImage img(96, 64, QImage::Format_RGB32);
    img.fill(Qt::gray);
    QTemporaryDir idir; QVERIFY(idir.isValid());
    const QString photo = idir.filePath(QStringLiteral("p.jpg"));
    QVERIFY(img.save(photo, "JPEG"));

    Window w(nullptr, QStringLiteral(""));
    w.show();QVERIFY(openAllInspectorSections(w));
    w.addFiles({photo});
    QTRY_VERIFY(w.neutralPickerAvailable());

    Recipe base; base.exposure = 0.312345;
    w.applyRecipe(base);
    QCOMPARE(w.currentRecipe(),base);QTRY_VERIFY(w.neutralPickerAvailable());

    const char *names[3] = {"slider-vignette-amount", "slider-vignette-midpoint", "slider-vignette-feather"};
    const int vals[3] = {-125, 23, 76};
    for (int i = 0; i < 3; ++i) {
        auto *s = w.findChild<QSlider *>(QLatin1String(names[i]));
        QVERIFY(s);
        s->setSliderDown(true);
        s->setValue(vals[i]);
        s->setSliderDown(false);
        QTRY_VERIFY(w.neutralPickerAvailable());
    }

    Recipe expected;
    expected.exposure = 0.312345;
    expected.vignetteAmount = -1.25;
    expected.vignetteMidpoint = 0.23;
    expected.vignetteFeather = 0.76;
    QTRY_VERIFY(w.currentRecipe() == expected);QTRY_VERIFY(w.neutralPickerAvailable());

    w.undoEdit();
    QTRY_VERIFY(w.currentRecipe().vignetteFeather == 0.5);QTRY_VERIFY(w.neutralPickerAvailable());
    {
        Recipe u = w.currentRecipe();
        QCOMPARE(u.vignetteAmount, -1.25);
        QCOMPARE(u.vignetteMidpoint, 0.23);
        QCOMPARE(u.exposure, 0.312345);
    }

    w.redoEdit();
    QTRY_VERIFY(w.currentRecipe() == expected);QTRY_VERIFY(w.neutralPickerAvailable());

    auto *reset = w.findChild<QPushButton *>(QStringLiteral("reset-vignette-amount"));
    QVERIFY(reset);
    reset->click();
    QTRY_VERIFY(w.currentRecipe().vignetteAmount == 0.0);
    {
        Recipe a = w.currentRecipe();
        QCOMPARE(a.vignetteMidpoint, 0.23);
        QCOMPARE(a.vignetteFeather, 0.76);
        QCOMPARE(a.exposure, 0.312345);
    }
}
    void presetLibraryRestartAndFailure(){
        QTemporaryDir dir;const auto library=dir.path()+"/library",photo=dir.path()+"/photo.jpg",external=dir.path()+"/My.luminas-preset.json";QImage image(96,64,QImage::Format_RGB32);image.fill(Qt::gray);QVERIFY(image.save(photo));Recipe saved;saved.exposure=.312345;saved.sharpenMask=.73;saved.warmth=.2;saved.crop={.1,.1,.8,.8};QString error;
        {Window w(nullptr,library);w.show();QVERIFY(openAllInspectorSections(w));w.addFiles({photo});QTRY_VERIFY(w.neutralPickerAvailable());w.applyRecipe(saved);QTRY_VERIFY(w.neutralPickerAvailable());QSignalSpy ready(&w,&Window::previewChanged);QVERIFY(w.saveAdjustmentPreset(external,error));QCOMPARE(w.currentRecipe(),saved);QCOMPARE(ready.size(),0);QCOMPARE(w.findChild<QComboBox *>("preset-choice")->count(),5);QVERIFY(w.saveAdjustmentPreset(external,error));QCOMPARE(w.findChild<QComboBox *>("preset-choice")->count(),5);w.undoEdit();QCOMPARE(w.currentRecipe(),Recipe{});}
        QVERIFY(QFile::remove(external));QFile broken(library+"/bad.luminas-preset.json");QVERIFY(broken.open(QIODevice::WriteOnly));broken.write("{bad");broken.close();
        {Window w(nullptr,library);w.show();QVERIFY(openAllInspectorSections(w));auto *choice=w.findChild<QComboBox *>("preset-choice");QCOMPARE(choice->count(),5);QVERIFY(w.findChild<QLabel *>("preset-library-hint")->text().contains(QStringLiteral("忽略")));w.addFiles({photo});QTRY_VERIFY(w.neutralPickerAvailable());Recipe destination;destination.warmth=-.3;destination.tint=.1;destination.crop={.2,.1,.7,.8};destination.angle=2;destination.flipHorizontal=true;w.applyRecipe(destination);QTRY_VERIFY(w.neutralPickerAvailable());choice->setCurrentIndex(4);w.findChild<QPushButton *>("preset-apply")->click();const auto expected=applyPreset(destination,{QStringLiteral("My"),saved});QCOMPARE(w.currentRecipe(),expected);w.undoEdit();QCOMPARE(w.currentRecipe(),destination);}
        const auto blocked=dir.path()+"/blocked";QFile obstruction(blocked);QVERIFY(obstruction.open(QIODevice::WriteOnly));obstruction.write("not a directory");obstruction.close();QVERIFY(savePresetFile(external,{QStringLiteral("My"),saved},error));Window w(nullptr,blocked);w.show();QVERIFY(openAllInspectorSections(w));w.addFiles({photo});QTRY_VERIFY(w.neutralPickerAvailable());const auto before=w.currentRecipe();QSignalSpy ready(&w,&Window::previewChanged);QVERIFY(!w.loadAdjustmentPreset(external,error));QCOMPARE(w.currentRecipe(),before);QCOMPARE(ready.size(),0);QCOMPARE(w.findChild<QComboBox *>("preset-choice")->count(),4);QVERIFY(!w.saveAdjustmentPreset(dir.path()+"/Other.json",error));QVERIFY(error.contains(QStringLiteral("外部 JSON 已保存")));QVERIFY(QFile::exists(dir.path()+"/Other.json"));
    }
    void presetLibraryStorage(){
        QTemporaryDir dir;QString error;QStringList warnings;Recipe recipe;recipe.exposure=.312345;recipe.sharpenMask=.73;recipe.warmth=.3;recipe.crop={.1,.1,.8,.8};AdjustmentPreset p{QStringLiteral("  My Preset  "),recipe};QVERIFY2(storePresetLibrary(dir.path(),p,error),qPrintable(error));auto loaded=loadPresetLibrary(dir.path(),warnings);QCOMPARE(loaded.size(),1);QCOMPARE(warnings.size(),0);QCOMPARE(loaded[0].name,p.name.trimmed());QCOMPARE(loaded[0].adjustments,applyPreset(Recipe{},p));p.adjustments.exposure=.7;QVERIFY(storePresetLibrary(dir.path(),p,error));loaded=loadPresetLibrary(dir.path(),warnings);QCOMPARE(loaded.size(),1);QCOMPARE(loaded[0].adjustments.exposure,.7);
        const auto target=dir.path()+"/"+QString::fromLatin1(QCryptographicHash::hash(p.name.trimmed().toUtf8(),QCryptographicHash::Sha256).toHex())+".luminas-preset.json";QFile file(target);QVERIFY(file.open(QIODevice::ReadOnly));const auto before=file.readAll();file.close();p.adjustments.exposure=99;QVERIFY(!storePresetLibrary(dir.path(),p,error));QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),before);file.close();for(const auto &name:{"bad","large"}){QFile invalid(dir.path()+"/"+name+".luminas-preset.json");QVERIFY(invalid.open(QIODevice::WriteOnly));const auto bytes=QString(name)=="large"?QByteArray(256*1024+1,'x'):QByteArray("{bad");QCOMPARE(invalid.write(bytes),qint64(bytes.size()));}loaded=loadPresetLibrary(dir.path(),warnings);QCOMPARE(loaded.size(),1);QCOMPARE(warnings.size(),2);QVERIFY(loadPresetLibrary(dir.path()+"/missing",warnings).isEmpty());
    }
    void presetLibraryCapacityAndNonFileTarget(){
        QTemporaryDir dir;QString error;QStringList warnings;for(int i=0;i<64;++i)QVERIFY(storePresetLibrary(dir.path(),{QStringLiteral("Item%1").arg(i,2,10,QLatin1Char('0')),{}},error));QVERIFY(!storePresetLibrary(dir.path(),{QStringLiteral("Overflow"),{}},error));Recipe updated;updated.exposure=.9;QVERIFY(storePresetLibrary(dir.path(),{QStringLiteral("Item00"),updated},error));const auto loaded=loadPresetLibrary(dir.path(),warnings);QCOMPARE(loaded.size(),64);QCOMPARE(loaded.first().name,QStringLiteral("Item00"));QCOMPARE(loaded.first().adjustments.exposure,.9);QCOMPARE(warnings.size(),0);
        QTemporaryDir second;const auto target=second.path()+"/"+QString::fromLatin1(QCryptographicHash::hash(QByteArray("Target"),QCryptographicHash::Sha256).toHex())+".luminas-preset.json";QVERIFY(QDir().mkpath(target));QVERIFY(!storePresetLibrary(second.path(),{QStringLiteral("Target"),{}},error));QVERIFY(QFileInfo(target).isDir());
    }
void sharpenMaskQuantizedReference()
{
    const QString baseDir = qEnvironmentVariable("LUMINA_MASK_REFERENCE_DIR");
    if (baseDir.isEmpty())
        QSKIP("LUMINA_MASK_REFERENCE_DIR is not set");

    const QStringList cases = { QStringLiteral("raw"),
                                QStringLiteral("jpeg"),
                                QStringLiteral("45mp") };
    const QStringList phases = { QStringLiteral("preview"),
                                 QStringLiteral("full") };

    for (const QString &c : cases) {
        for (const QString &p : phases) {
            const QString oldPath = QStringLiteral("%1/sharpen-mask-v17-%2-%3.png")
                                        .arg(baseDir, c, p);
            const QString newPath = QStringLiteral("%1/sharpen-mask-v17u16-%2-%3.png")
                                        .arg(baseDir, c, p);

            QImage oldImg;
            QImage newImg;
            if (!oldImg.load(oldPath))
                QFAIL(qPrintable(QStringLiteral("cannot load reference %1").arg(oldPath)));
            if (!newImg.load(newPath))
                QFAIL(qPrintable(QStringLiteral("cannot load reference %1").arg(newPath)));

            oldImg = oldImg.convertToFormat(QImage::Format_RGBA8888);
            newImg = newImg.convertToFormat(QImage::Format_RGBA8888);

            QVERIFY2(!oldImg.isNull(),
                     qPrintable(QStringLiteral("null after convert old %1").arg(oldPath)));
            QVERIFY2(!newImg.isNull(),
                     qPrintable(QStringLiteral("null after convert new %1").arg(newPath)));

            QCOMPARE(oldImg.size(), newImg.size());

            int maxDiff = 0;
            for (int y = 0; y < oldImg.height(); ++y) {
                const uchar *oldLine = oldImg.constScanLine(y);
                const uchar *newLine = newImg.constScanLine(y);
                const int bytes = oldImg.width() * 4;
                for (int x = 0; x < bytes; ++x) {
                    const int d = qAbs(int(oldLine[x]) - int(newLine[x]));
                    if (d > maxDiff)
                        maxDiff = d;
                }
            }

            QVERIFY2(maxDiff <= 1,
                     qPrintable(QStringLiteral("case=%1 phase=%2 maxAbsDiff=%3")
                                    .arg(c, p)
                                    .arg(maxDiff)));

            for (int y = 0; y < oldImg.height(); ++y) {
                const uchar *oldLine = oldImg.constScanLine(y);
                const uchar *newLine = newImg.constScanLine(y);
                for (int x = 0; x < oldImg.width(); ++x) {
                    QCOMPARE(oldLine[4 * x + 3], newLine[4 * x + 3]);
                }
            }

            qInfo("sharpen-mask reference case=%s phase=%s maxAbsDiff=%d",
                  qPrintable(c), qPrintable(p), maxDiff);
        }
    }
}


void sharpenMaskRecipeAndHistory()
{
    // 1. Recipe JSON round-trip with all sharpenMask fields populated
    Recipe r;
    r.sharpenAmount = 0.7; r.sharpenRadius = 1.75; r.sharpenThreshold = 0.02; r.sharpenMask = 0.55;
    Recipe rt = recipeFromJson(recipeJson(r));
    QVERIFY(validRecipe(rt));
    QCOMPARE(rt, r);

    // 2. Missing sharpenMask in old JSON decodes to default 0
    {
        QJsonObject o = recipeJson(r);
        QVERIFY(o.contains("sharpenMask"));
        o.remove("sharpenMask");
        Recipe old = recipeFromJson(o);
        QVERIFY(validRecipe(old));
        QCOMPARE(old.sharpenMask, 0.0);
    }

    // 3. String / bool / null are all rejected
    {
        QJsonObject o = recipeJson(r);
        o["sharpenMask"] = QStringLiteral("0.5");
        QVERIFY(!validRecipe(recipeFromJson(o)));
    }
    {
        QJsonObject o = recipeJson(r);
        o["sharpenMask"] = true;
        QVERIFY(!validRecipe(recipeFromJson(o)));
    }
    {
        QJsonObject o = recipeJson(r);
        o["sharpenMask"] = QJsonValue(QJsonValue::Null);
        QVERIFY(!validRecipe(recipeFromJson(o)));
    }

    // 4. Negative, >1, NaN all invalid
    {
        Recipe bad = r; bad.sharpenMask = -0.01;
        QVERIFY(!validRecipe(recipeFromJson(recipeJson(bad))));
    }
    {
        Recipe bad = r; bad.sharpenMask = 1.01;
        QVERIFY(!validRecipe(recipeFromJson(recipeJson(bad))));
    }
    {
        Recipe bad = r;
        bad.sharpenMask = std::numeric_limits<double>::quiet_NaN();
        QVERIFY(!validRecipe(bad));
    }

    // 5. Preset save/load preserves the sharpen fields
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("preset.json"));
        AdjustmentPreset in;
        in.name = QStringLiteral("sharp-preset");
        in.adjustments = r;
        QString err;
        QVERIFY(savePresetFile(path, in, err));
        AdjustmentPreset out;
        QVERIFY(loadPresetFile(path, out, err));
        QCOMPARE(out.name, in.name);
        QCOMPARE(out.adjustments.sharpenAmount, r.sharpenAmount);
        QCOMPARE(out.adjustments.sharpenRadius, r.sharpenRadius);
        QCOMPARE(out.adjustments.sharpenThreshold, r.sharpenThreshold);
        QCOMPARE(out.adjustments.sharpenMask, r.sharpenMask);
    }

    QTemporaryDir photos;QImage image(96,64,QImage::Format_RGB32);image.fill(QColor(128,128,128));const auto path=photos.path()+"/sample.jpg";QVERIFY(image.save(path));Window w(nullptr,QStringLiteral(""));w.show();QVERIFY(openAllInspectorSections(w));w.addFiles({path});QTRY_VERIFY(w.neutralPickerAvailable());Recipe base;base.sharpenAmount=.4;base.exposure=.312345;w.applyRecipe(base);QTRY_VERIFY(w.neutralPickerAvailable());auto *bar=w.findChild<QSlider *>("slider-sharpen-mask");QVERIFY(bar);bar->setSliderDown(true);bar->setValue(75);bar->setSliderDown(false);QTRY_VERIFY(w.neutralPickerAvailable());auto expected=base;expected.sharpenMask=.75;QCOMPARE(w.currentRecipe(),expected);w.undoEdit();QCOMPARE(w.currentRecipe(),base);QTRY_VERIFY(w.neutralPickerAvailable());w.redoEdit();QCOMPARE(w.currentRecipe(),expected);QTRY_VERIFY(w.neutralPickerAvailable());w.findChild<QPushButton *>("reset-sharpen-mask")->click();QTRY_VERIFY(w.neutralPickerAvailable());QCOMPARE(w.currentRecipe(),base);
}

    void sharpenMaskImageAndCache(){
        QString scaleError;std::array<float,2> edgeScores{};for(int n=0;n<2;++n){const double scale=n+1;auto ramp=frame({96/(n+1),48});for(int y=0;y<48;++y){auto *p=reinterpret_cast<float *>(ramp.linear.scanLine(y));for(int x=0;x<ramp.linear.width();++x){const float v=float(.2+.001*x*scale);p[x*4]=p[x*4+1]=p[x*4+2]=v;p[x*4+3]=.4f;}}auto b=prepareSpatial(ramp.linear,scale,{96,48},scaleError,false,false,1,false,false,true);QVERIFY2(b,qPrintable(scaleError));edgeScores[n]=b->sharpenEdges[size_t(24)*ramp.linear.width()+32/(n+1)];Recipe neutral;neutral.sharpenAmount=1;neutral.sharpenMask=1;const auto out=applySpatial(*b,neutral,scaleError);QCOMPARE(reinterpret_cast<const float *>(out.constScanLine(24))[16*4+3],.4f);}QVERIFY(std::abs(edgeScores[0]-edgeScores[1])<=1);
        auto s=frame({96,48});for(int y=0;y<48;++y){auto *p=reinterpret_cast<float *>(s.linear.scanLine(y));for(int x=0;x<96;++x){const float v=(x<48?.2f:.6f)+((x+y)%2?.002f:-.002f);p[x*4]=p[x*4+1]=p[x*4+2]=v;}}
        QString error;Recipe r;r.sharpenAmount=1;auto ordinary=renderLinear(s,r,0,error);QVERIFY(error.isEmpty());auto basis=prepareSpatial(s.linear,1,s.linear.size(),error,false,false,1,false,false,true);QVERIFY(basis);QCOMPARE(basis->sharpenEdges.size(),size_t(96*48));auto zero=applySpatial(*basis,r,error);QCOMPARE(zero,ordinary);const auto *input=reinterpret_cast<const float *>(s.linear.constScanLine(24));const auto *unmasked=reinterpret_cast<const float *>(ordinary.constScanLine(24));const double noiseGain=std::abs(unmasked[16*4]-input[16*4]);QVERIFY(noiseGain>.001);
        double previous=noiseGain;for(double strength:{.25,.5,.75,1.}){r.sharpenMask=strength;auto out=applySpatial(*basis,r,error);QVERIFY(error.isEmpty());const auto *p=reinterpret_cast<const float *>(out.constScanLine(24));const double gain=std::abs(p[16*4]-input[16*4]);QVERIFY(gain<=previous+1e-7);previous=gain;QVERIFY(std::abs(p[47*4]-input[47*4])>.04);QCOMPARE(p[47*4],p[47*4+1]);QCOMPARE(p[16*4+3],1.f);}QVERIFY(previous<noiseGain*.01);
        Recipe geometry;SpatialCache cache;int builds=0;auto build=[&]{++builds;return s.linear;};auto a=cache.get(s,geometry,0,1,build,error,false,false,1);QVERIFY(a->sharpenEdges.empty());auto b=cache.get(s,geometry,0,1,build,error,false,false,1,false,false,true);QVERIFY(a!=b);QCOMPARE(builds,1);geometry.sharpenMask=.9;auto c=cache.get(s,geometry,0,1,build,error,false,false,1,false,false,true);QCOMPARE(c,b);QCOMPARE(builds,1);
        r.sharpenMask=.7;r.exposure=.2;r.crop={.1,.1,.8,.8};r.angle=2;r.flipHorizontal=true;r.texture=.1;r.dehaze=.1;const auto linear=renderLinear(s,r,0,error),reference=workToSrgbReference(linear,error);const auto rendered=renderFrame(s,r,0,&cache);QVERIFY2(rendered.error.isEmpty(),qPrintable(rendered.error));int difference=0;for(qsizetype i=0;i<reference.sizeInBytes();++i)difference=std::max(difference,std::abs(int(reference.constBits()[i])-int(rendered.image.constBits()[i])));QVERIFY(difference<=1);
        for(int y=0;y<48;++y){auto *p=reinterpret_cast<float *>(s.linear.scanLine(y));for(int x=0;x<10;++x){p[x*4]=100;p[x*4+3]=0;}}r={};r.sharpenAmount=1;r.sharpenMask=1;auto alpha=renderLinear(s,r,0,error);QVERIFY(error.isEmpty());const auto *p=reinterpret_cast<const float *>(alpha.constScanLine(24));QCOMPARE(p[0],0.f);QCOMPARE(p[3],0.f);QVERIFY(std::abs(p[16*4]-input[16*4])<.005);
    }
    // DeepSeek draft reviewed: wait on exact readiness, not any completion;
    // store only the latest image and use inline member definitions.
    void originalWhiteBalanceHistory(){
        QTemporaryDir dir;QImage image(96,64,QImage::Format_RGB32);image.fill(QColor(155,140,125));const auto a=dir.path()+"/a.jpg",b=dir.path()+"/b.jpg";QVERIFY(image.save(a));QVERIFY(image.save(b));Window w(nullptr,QStringLiteral(""));w.show();QVERIFY(openAllInspectorSections(w));auto *button=w.findChild<QPushButton *>("wb-original");auto *picker=w.findChild<QPushButton *>("wb-picker");QSignalSpy ready(&w,&Window::previewChanged);QVERIFY(!button->isEnabled());QVERIFY(!w.restoreOriginalWhiteBalance());w.addFiles({a,b});QTRY_VERIFY(w.neutralPickerAvailable());QVERIFY(!button->isEnabled());
        Recipe recipe;recipe.warmth=.213456;recipe.tint=-.112345;recipe.exposure=.312345;recipe.crop={.1,.1,.8,.8};recipe.angle=2;recipe.flipHorizontal=true;w.applyRecipe(recipe);QVERIFY(!button->isEnabled());QVERIFY(!w.restoreOriginalWhiteBalance());QTRY_VERIFY(w.neutralPickerAvailable());QVERIFY(button->isEnabled());w.compareOriginal(true);QVERIFY(!button->isEnabled());QVERIFY(!w.restoreOriginalWhiteBalance());w.compareOriginal(false);QTRY_VERIFY(w.neutralPickerAvailable());picker->click();QVERIFY(picker->isChecked());button->click();auto expected=recipe;expected.warmth=expected.tint=0;QCOMPARE(w.currentRecipe(),expected);QVERIFY(!picker->isChecked());QCOMPARE(w.findChild<Canvas *>()->cursor().shape(),Qt::OpenHandCursor);QTRY_VERIFY(w.neutralPickerAvailable());const auto count=ready.size();QVERIFY(!w.restoreOriginalWhiteBalance());QCOMPARE(ready.size(),count);w.undoEdit();QCOMPARE(w.currentRecipe(),recipe);QTRY_VERIFY(w.neutralPickerAvailable());w.redoEdit();QCOMPARE(w.currentRecipe(),expected);QTRY_VERIFY(w.neutralPickerAvailable());w.activate(1);QTRY_VERIFY(w.neutralPickerAvailable());QCOMPARE(w.currentRecipe(),Recipe{});QVERIFY(!button->isEnabled());w.activate(0);QTRY_VERIFY(w.neutralPickerAvailable());QCOMPARE(w.currentRecipe(),expected);
    }
    void originalWhiteBalanceRealSamples(){
        const auto directory=qEnvironmentVariable("LUMINA_WB_RESET_SAMPLES");if(directory.isEmpty())QSKIP("Optional private RAW/JPEG WB reset verification");
        for(const auto &name:{"4X1A9579.CR3","4X1A0705.JPG"}){const auto path=directory+"/"+name;QFile source(path);QVERIFY(source.open(QIODevice::ReadOnly));const auto hash=QCryptographicHash::hash(source.readAll(),QCryptographicHash::Sha256);source.close();Window w(nullptr,QStringLiteral(""));w.show();QVERIFY(openAllInspectorSections(w));EditResult latest;connect(w.findChild<EditLoader *>(),&EditLoader::ready,&w,[&](const EditResult &result){latest=result;});w.addFiles({path});QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),90000);Recipe baseline;baseline.exposure=.2;baseline.flipHorizontal=true;w.applyRecipe(baseline);QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),90000);const auto image=latest.image;QCOMPARE(qMax(image.width(),image.height()),3840);auto changed=baseline;changed.warmth=.2;changed.tint=-.1;w.applyRecipe(changed);QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),90000);QVERIFY(latest.sourceRebuilt==latest.workFrame.raw);QVERIFY(w.restoreOriginalWhiteBalance());QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),90000);QCOMPARE(w.currentRecipe(),baseline);QCOMPARE(latest.image,image);w.undoEdit();QCOMPARE(w.currentRecipe(),changed);QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),90000);w.redoEdit();QCOMPARE(w.currentRecipe(),baseline);QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),90000);QCOMPARE(latest.image,image);QVERIFY(source.open(QIODevice::ReadOnly));QCOMPARE(QCryptographicHash::hash(source.readAll(),QCryptographicHash::Sha256),hash);}
    }
    void pickerReadinessAndStaleFrames(){
        QTemporaryDir dir;QImage image(96,64,QImage::Format_RGB32);image.fill(QColor(155,140,125));const auto a=dir.path()+"/a.jpg",b=dir.path()+"/b.jpg";QVERIFY(image.save(a));image.fill(QColor(125,145,160));QVERIFY(image.save(b));Window w(nullptr,QStringLiteral(""));w.show();QVERIFY(openAllInspectorSections(w));auto *loader=w.findChild<EditLoader *>();EditResult previous;connect(loader,&EditLoader::ready,&w,[&](const EditResult &r){previous=r;});QSignalSpy ready(&w,&Window::previewChanged);auto *button=w.findChild<QPushButton *>("wb-picker");auto *hint=w.findChild<QLabel *>("wb-picker-hint");QVERIFY(!button->isEnabled());w.addFiles({a,b});QVERIFY(!w.neutralPickerAvailable());QTRY_VERIFY(w.neutralPickerAvailable());QVERIFY(button->isEnabled());const auto stale=previous;Recipe r;r.exposure=.2;w.applyRecipe(r);QVERIFY(!w.neutralPickerAvailable());QVERIFY(!button->isEnabled());const auto count=ready.size();QVERIFY(QMetaObject::invokeMethod(loader,"ready",Qt::DirectConnection,Q_ARG(EditResult,stale)));QCOMPARE(ready.size(),count);QVERIFY(!button->isEnabled());QTRY_VERIFY(w.neutralPickerAvailable());
        button->click();QVERIFY(button->isChecked());QCOMPARE(w.findChild<Canvas *>()->cursor().shape(),Qt::CrossCursor);const auto beforeEscape=ready.size();QTest::keyClick(&w,Qt::Key_Escape);QVERIFY(!button->isChecked());QVERIFY(w.neutralPickerAvailable());QCOMPARE(ready.size(),beforeEscape);QCOMPARE(w.findChild<Canvas *>()->cursor().shape(),Qt::OpenHandCursor);
        auto *slider=w.findChild<QSlider *>("slider-exposure");slider->setSliderDown(true);slider->setValue(35);QVERIFY(!button->isEnabled());QTest::qWait(100);QVERIFY(!w.neutralPickerAvailable());slider->setSliderDown(false);QTRY_VERIFY(w.neutralPickerAvailable());w.activate(1);QVERIFY(!button->isEnabled());QTRY_VERIFY(w.neutralPickerAvailable());w.compareOriginal(true);QVERIFY(!button->isEnabled());QVERIFY(hint->text().contains(QStringLiteral("比较")));w.compareOriginal(false);QTRY_VERIFY(w.neutralPickerAvailable());const auto shot=qEnvironmentVariable("LUMINA_PICKER_STATE_SCREENSHOT");if(!shot.isEmpty()){w.findChild<QScrollArea *>()->ensureWidgetVisible(button);QTest::qWait(100);QVERIFY(w.grab().save(shot));}
        w.addFiles({dir.path()+"/missing.jpg"});w.activate(2);QTRY_VERIFY(hint->text().contains(QStringLiteral("失败")));QVERIFY(!button->isEnabled());QVERIFY(!w.neutralPickerAvailable());
    }
    void tonePaired4kBenchmark(){
        const auto path=qEnvironmentVariable("LUMINA_TONE_SAMPLE");if(path.isEmpty())QSKIP("Optional paired private 4K tone benchmark");SourceFrame base;QString error;{const auto read=readTestSource(path);QVERIFY2(read.error.isEmpty(),qPrintable(read.error));base=*read.source;base.linear=renderLinear(*read.source,{},3840,error);QVERIFY(error.isEmpty());base.sourcePixelsPerPixel=double(read.source->linear.width())/base.linear.width();}QCOMPARE(qMax(base.linear.width(),base.linear.height()),3840);
        const auto inputHash=QCryptographicHash::hash(QByteArrayView(base.linear.constBits(),base.linear.sizeInBytes()),QCryptographicHash::Sha256);
        std::array<Recipe,5> recipes;recipes[0].curves[0]={{0,0},{.4,.5},{1,1}};recipes[1].parametricCurve.regions={.3,-.2,.4,-.1};recipes[1].luminanceCurve={{0,0},{.4,.55},{1,1}};recipes[2].hsl[1]={.2,.15,-.1};recipes[3].primaries[0]={.3,.2};recipes[4].shadows=.2;recipes[4].saturation=1.15;recipes[4].vibrance=.25;
        QJsonArray results;for(int c=0;c<5;++c){auto reference=recipes[c];if(reference.vibrance==0)reference.vibrance=1e-8;QCOMPARE(float(1+float(1e-8)),1.f);QVector<double> a,b;int difference=0;auto run=[&](const Recipe &r){const auto result=renderFrame(base,r,0);if(!result.error.isEmpty())qFatal("Paired render failed");return result;};run(recipes[c]);run(reference);
            for(int i=0;i<12;++i){decltype(renderFrame(base,recipes[c],0)) optimized,forced;if(i%2){forced=run(reference);optimized=run(recipes[c]);}else{optimized=run(recipes[c]);forced=run(reference);}a.append(optimized.elapsedMs);b.append(forced.elapsedMs);if(i==0){for(qsizetype k=0;k<optimized.image.sizeInBytes();++k)difference=std::max(difference,std::abs(int(optimized.image.constBits()[k])-int(forced.image.constBits()[k])));QVERIFY(difference<=1);}}
            auto median=[](QVector<double> values){std::sort(values.begin(),values.end());return (values[5]+values[6])/2;};QJsonArray samplesA,samplesB;for(auto t:a)samplesA.append(t);for(auto t:b)samplesB.append(t);results.append(QJsonObject{{"case",c},{"optimizedMedianMs",median(a)},{"forcedNeutralMedianMs",median(b)},{"optimizedSamplesMs",samplesA},{"forcedSamplesMs",samplesB},{"maxByteDifference",difference},{"width",base.linear.width()},{"height",base.linear.height()}});
        }QCOMPARE(QCryptographicHash::hash(QByteArrayView(base.linear.constBits(),base.linear.sizeInBytes()),QCryptographicHash::Sha256),inputHash);const auto report=qEnvironmentVariable("LUMINA_TONE_REPORT");if(!report.isEmpty()){QFile file(report);QVERIFY(file.open(QIODevice::WriteOnly));file.write(QJsonDocument(QJsonObject{{"inputFloatSha256",QString::fromLatin1(inputHash.toHex())},{"results",results}}).toJson());}
    }
    void toneNeutralBypassPrecision(){
        ToneOperator identity(Recipe{});for(const auto &input:{std::array<float,3>{-.3f,.4f,2.f},std::array<float,3>{3e38f,-3e38f,3e38f},std::array<float,3>{1e-7f,2e-6f,.8f}}){auto v=input;identity.apply(v.data());QCOMPARE(v,input);}
        std::array<Recipe,5> recipes;recipes[0].curves[0]={{0,0},{.4,.5},{1,1}};recipes[1].parametricCurve.regions={.3,-.2,.4,-.1};recipes[1].luminanceCurve={{0,.02},{.4,.55},{1,1}};recipes[2].hsl[1]={.2,.15,-.1};recipes[3].primaries[0]={.3,.2};recipes[4].shadows=.2;recipes[4].grading[1]={45,.2,.1};
        for(const auto &recipe:recipes){auto forced=recipe;forced.vibrance=1e-300;QCOMPARE(float(forced.vibrance),0.f);ToneOperator fast(recipe),legacyMix(forced);for(int i=0;i<10000;++i){float a[]{float(i%101)/25-.5f,float((i*7)%103)/26-.4f,float((i*13)%107)/27-.3f},b[]{a[0],a[1],a[2]};fast.apply(a);legacyMix.apply(b);for(int c=0;c<3;++c){QVERIFY(std::isfinite(a[c]));QVERIFY(std::abs(a[c]-b[c])<=3e-6f*std::max({1.f,std::abs(a[c]),std::abs(b[c])}));}}
        }
        auto source=frame({137,91});for(int y=0;y<source.linear.height();++y)for(int x=0;x<source.linear.width();++x)reinterpret_cast<float *>(source.linear.scanLine(y))[4*x+3]=x%3==0?0.f:x%3==1?.3f:1.f;
        for(auto recipe:recipes){auto forced=recipe;forced.vibrance=1e-300;const auto a=renderFrame(source,recipe,0),b=renderFrame(source,forced,0);QVERIFY(a.error.isEmpty());QVERIFY(b.error.isEmpty());for(qsizetype i=0;i<a.image.sizeInBytes();++i){QVERIFY(std::abs(int(a.image.constBits()[i])-int(b.image.constBits()[i]))<=1);if(i%4==3)QCOMPARE(a.image.constBits()[i],b.image.constBits()[i]);}}
    }
    void tone4kBenchmark(){
        const auto path=qEnvironmentVariable("LUMINA_TONE_SAMPLE");if(path.isEmpty())QSKIP("Optional private 4K tone benchmark");SourceFrame base;QString error;{const auto read=readTestSource(path);QVERIFY2(read.error.isEmpty(),qPrintable(read.error));base=*read.source;base.linear=renderLinear(*read.source,{},3840,error);QVERIFY(error.isEmpty());}QCOMPARE(qMax(base.linear.width(),base.linear.height()),3840);
        std::array<Recipe,5> recipes;recipes[0].curves[0]={{0,0},{.4,.5},{1,1}};recipes[1].parametricCurve.regions={.3,-.2,.4,-.1};recipes[1].luminanceCurve={{0,0},{.4,.55},{1,1}};recipes[2].hsl[1]={.2,.15,-.1};recipes[3].primaries[0]={.3,.2};recipes[4].shadows=.2;recipes[4].saturation=1.15;recipes[4].vibrance=.25;
        const auto baseline=qEnvironmentVariable("LUMINA_TONE_BASELINE"),reference=qEnvironmentVariable("LUMINA_TONE_REFERENCE");if(!baseline.isEmpty())QVERIFY(QDir().mkpath(baseline));QJsonArray results;
        for(int c=0;c<5;++c){QVector<double> times;QByteArray digest;int maxDifference=0;for(int i=0;i<7;++i){const auto result=renderFrame(base,recipes[c],0);QVERIFY2(result.error.isEmpty(),qPrintable(result.error));const auto hash=QCryptographicHash::hash(QByteArrayView(result.image.constBits(),result.image.sizeInBytes()),QCryptographicHash::Sha256).toHex();if(i==0){digest=hash;if(!baseline.isEmpty()){const auto file=baseline+QString("/case-%1.png").arg(c);QVERIFY(!QFile::exists(file));QVERIFY(result.image.save(file));}if(!reference.isEmpty()){const QImage previous(reference+QString("/case-%1.png").arg(c));QCOMPARE(previous.size(),result.image.size());const auto normalized=previous.convertToFormat(result.image.format());for(qsizetype b=0;b<normalized.sizeInBytes();++b)maxDifference=std::max(maxDifference,std::abs(int(normalized.constBits()[b])-int(result.image.constBits()[b])));QVERIFY(maxDifference<=1);}}else{QCOMPARE(hash,digest);times.append(result.elapsedMs);}}
            const auto original=times;std::sort(times.begin(),times.end());QJsonArray samples;for(auto t:original)samples.append(t);results.append(QJsonObject{{"case",c},{"medianMs",(times[2]+times[3])/2},{"samplesMs",samples},{"width",base.linear.width()},{"height",base.linear.height()},{"pixelSha256",QString::fromLatin1(digest)},{"maxByteDifferenceFromBaseline",maxDifference}});
        }const auto report=qEnvironmentVariable("LUMINA_TONE_REPORT");if(!report.isEmpty()){QFile file(report);QVERIFY(file.open(QIODevice::WriteOnly));file.write(QJsonDocument(results).toJson());}
    }
    void lumaCurveRealContinuousFeedback(){
        const auto path=qEnvironmentVariable("LUMINA_LIVE_SAMPLE");if(path.isEmpty())QSKIP("Optional private luminance curve feedback");Window w(nullptr,QStringLiteral(""));w.show();QVERIFY(openAllInspectorSections(w));QSignalSpy ready(&w,&Window::previewChanged);w.addFiles({path});QTRY_VERIFY_WITH_TIMEOUT(ready.size()>0,90000);QSignalSpy painted(w.findChild<Canvas *>(),&Canvas::framePainted);QJsonArray report;
        const QString controls[]{"param-shadows","param-darks","param-lights","param-highlights","param-shadow-split","param-mid-split","param-highlight-split"};
        for(int c=0;c<7;++c){auto *bar=w.findChild<QSlider *>("slider-"+controls[c]);w.findChild<QScrollArea *>()->ensureWidgetVisible(bar);const auto before=ready.size(),paintBefore=painted.size();bar->setSliderDown(true);for(int i=1;i<=40;++i){bar->setValue(c<4?(c%2?-i:i):(c-3)*25+i/4);QTest::qWait(50);}const int frames=ready.size()-before,paints=painted.size()-paintBefore,release=ready.size();bar->setSliderDown(false);QTRY_VERIFY_WITH_TIMEOUT(ready.size()>release,90000);QVERIFY(ready.last().at(1).toBool());QVERIFY2(frames>=4,qPrintable(controls[c]));QVERIFY(paints>=4);report.append(QJsonObject{{"control",controls[c]},{"frames",frames},{"paints",paints}});}
        const auto beforePoint=w.currentRecipe();w.findChild<QComboBox *>("curve-channel")->setCurrentIndex(4);auto *graph=w.findChild<CurveEditor *>("curve-editor");w.findChild<QScrollArea *>()->ensureWidgetVisible(graph);const QPoint middle(graph->width()/2,graph->height()/2);const auto before=ready.size(),paintBefore=painted.size();QTest::mousePress(graph,Qt::LeftButton,Qt::NoModifier,middle);for(int i=1;i<=40;++i){QTest::mouseMove(graph,middle-QPoint(0,i));QTest::qWait(50);}const int frames=ready.size()-before,paints=painted.size()-paintBefore,release=ready.size();QTest::mouseRelease(graph,Qt::LeftButton,Qt::NoModifier,middle-QPoint(0,40));QTRY_VERIFY_WITH_TIMEOUT(ready.size()>release,90000);QVERIFY(ready.last().at(1).toBool());QVERIFY(frames>=4);QVERIFY(paints>=4);QVERIFY(w.currentRecipe().luminanceCurve!=beforePoint.luminanceCurve);report.append(QJsonObject{{"control","luminanceCurve"},{"frames",frames},{"paints",paints}});const auto final=w.currentRecipe();w.undoEdit();QCOMPARE(w.currentRecipe(),beforePoint);w.redoEdit();QCOMPARE(w.currentRecipe(),final);QTRY_VERIFY_WITH_TIMEOUT(w.findChild<QSlider *>("slider-param-lights")->isEnabled(),90000);
        const auto output=qEnvironmentVariable("LUMINA_LUMA_REPORT");if(!output.isEmpty()){QFile file(output);QVERIFY(file.open(QIODevice::WriteOnly));file.write(QJsonDocument(report).toJson());}const auto shot=qEnvironmentVariable("LUMINA_LUMA_SCREENSHOT");if(!shot.isEmpty()){w.findChild<QScrollArea *>()->ensureWidgetVisible(w.findChild<CurveEditor *>("parametric-curve-graph"));QTest::qWait(100);QVERIFY(w.grab().save(shot));}
    }
    void parametricCurveMonotonicAndLuminanceRatios(){
        Recipe r;for(const auto &splits:{std::array<double,3>{.25,.5,.75},std::array<double,3>{.05,.1,.15},std::array<double,3>{.85,.9,.95},std::array<double,3>{.05,.5,.95}})for(int bits=0;bits<16;++bits){r.parametricCurve.splits=splits;for(int c=0;c<4;++c)r.parametricCurve.regions[c]=(bits&(1<<c))?1:-1;QVERIFY(validRecipe(r));const auto p=parametricPoints(r.parametricCurve);CurveSpline spline(p);double previous=-1;for(int i=0;i<=1000;++i){const double y=spline.value(i/1000.);QVERIFY(y>=previous-1e-12);QVERIFY(y>=0&&y<=1);previous=y;}QCOMPARE(spline.value(0),0.);QCOMPARE(spline.value(1),1.);}
        r={};r.parametricCurve.regions={.3,-.2,.5,-.1};r.luminanceCurve={{0,.02},{.4,.5},{1,1}};LuminanceCurveOperator curve(r);
        for(float y:{.001f,.02f,.2f,1.f,2.f,9.f}){float v[]{3*y,y,.5f*y};const float oldY=.2627f*v[0]+.6780f*v[1]+.0593f*v[2],mapped=curve.apply(v,oldY);QVERIFY(std::isfinite(mapped));QVERIFY(std::abs(v[0]/v[1]-3)<1e-6);QVERIFY(std::abs(v[2]/v[1]-.5)<1e-6);QVERIFY(std::abs((.2627f*v[0]+.6780f*v[1]+.0593f*v[2])-mapped)<1e-5*std::max(1.f,mapped));}
        float black[]{0,0,0};QVERIFY(curve.apply(black,0)>0);QCOMPARE(black[0],black[1]);QCOMPARE(black[1],black[2]);float negative[]{-2,-1,-3};QCOMPARE(curve.apply(negative,-1.5f),-1.5f);QCOMPARE(negative[0],-2.f);
        float below[]{1.4e-6f,.9e-6f,.3743e-6f},above[]{1.4e-6f,.9e-6f,.3743e-6f};const float baseY=.2627f*below[0]+.6780f*below[1]+.0593f*below[2];for(int c=0;c<3;++c){below[c]*=.999e-6f/baseY;above[c]*=1.001e-6f/baseY;}curve.apply(below,.999e-6f);curve.apply(above,1.001e-6f);for(int c=0;c<3;++c)QVERIFY(std::abs(below[c]-above[c])<1e-5f);
        LuminanceCurveOperator identity(Recipe{});float untouched[]{-.3f,.4f,2.f};const float original[]{untouched[0],untouched[1],untouched[2]};identity.apply(untouched,.3f);for(int c=0;c<3;++c)QCOMPARE(untouched[c],original[c]);
    }
    void luminanceCurveJsonAndRenderReference(){
        Recipe r;r.parametricCurve.regions={.2,-.3,.4,-.1};r.parametricCurve.splits={.2,.55,.8};r.luminanceCurve={{0,0},{.4,.6},{1,1}};QVERIFY(hasColorEditing(r));QCOMPARE(recipeFromJson(recipeJson(r)),r);QCOMPARE(recipeFromJson({}),Recipe{});auto bad=recipeJson(r);bad["luminanceCurve"]="bad";QVERIFY(!validRecipe(recipeFromJson(bad)));bad=recipeJson(r);bad["parametricCurve"]=QJsonObject{{"regions",QJsonArray{0,0,0,"1"}},{"splits",QJsonArray{.25,.5,.75}}};QVERIFY(!validRecipe(recipeFromJson(bad)));r.parametricCurve.splits={.5,.51,.8};QVERIFY(!validRecipe(r));r.parametricCurve.splits={.2,.55,.8};auto source=frame({137,91});QString error;const auto linear=renderLinear(source,r,0,error);QVERIFY2(error.isEmpty(),qPrintable(error));const auto reference=workToSrgbReference(linear,error);const auto fast=renderFrame(source,r,0);QVERIFY(error.isEmpty());QVERIFY(fast.error.isEmpty());for(int y=0;y<linear.height();++y)for(int x=0;x<linear.width();++x){const auto a=reference.pixelColor(x,y),b=fast.image.pixelColor(x,y);for(int c=0;c<3;++c){const int va=c==0?a.red():c==1?a.green():a.blue(),vb=c==0?b.red():c==1?b.green():b.blue();QVERIFY(std::abs(va-vb)<=1);}}
        QTemporaryDir dir;AdjustmentPreset preset{"curve",r};QVERIFY(savePresetFile(dir.path()+"/curve.json",preset,error));AdjustmentPreset loaded;QVERIFY(loadPresetFile(dir.path()+"/curve.json",loaded,error));QCOMPARE(loaded.adjustments,r);
    }
    void parametricCurveWindowHistory(){
        QTemporaryDir dir;QImage jpeg(90,60,QImage::Format_RGB32);jpeg.fill(QColor(140,100,80));const auto path=dir.path()+"/curve.jpg";QVERIFY(jpeg.save(path));Window w(nullptr,QStringLiteral(""));w.show();QVERIFY(openAllInspectorSections(w));QSignalSpy ready(&w,&Window::previewChanged);w.addFiles({path});QTRY_VERIFY(ready.size()>0);auto *bar=w.findChild<QSlider *>("slider-param-lights");bar->setSliderDown(true);for(int value:{10,20,35})bar->setValue(value);bar->setSliderDown(false);QCOMPARE(w.currentRecipe().parametricCurve.regions[2],.35);w.undoEdit();QCOMPARE(w.currentRecipe(),Recipe{});w.redoEdit();QCOMPARE(w.currentRecipe().parametricCurve.regions[2],.35);
        Recipe r=w.currentRecipe();r.luminanceCurve={{0,0},{.4,.6},{1,1}};const auto pending=ready.size();w.applyRecipe(r);QTRY_VERIFY(ready.size()>pending);QTRY_VERIFY(w.findChild<QPushButton *>("reset-parametric-curve")->isEnabled());w.findChild<QComboBox *>("curve-channel")->setCurrentIndex(4);QCOMPARE(w.currentRecipe(),r);w.findChild<QDoubleSpinBox *>("param-shadow-split")->setValue(.9);QVERIFY(validRecipe(w.currentRecipe()));QVERIFY(w.currentRecipe().parametricCurve.splits[0]<=.45+1e-9);w.findChild<QPushButton *>("reset-parametric-curve")->click();QCOMPARE(w.currentRecipe().parametricCurve,ParametricCurve{});QCOMPARE(w.currentRecipe().luminanceCurve,r.luminanceCurve);
    }
    void neutralBalanceMathAndCoordinates(){
        auto s=frame({9,9});for(int y=0;y<9;++y)for(int x=0;x<9;++x){auto *p=reinterpret_cast<float *>(s.linear.scanLine(y))+4*x;p[0]=.3f;p[1]=.2f;p[2]=.15f;p[3]=1;}
        Recipe r;r.exposure=2;r.flipHorizontal=true;QString error;QVERIFY(neutralBalance(s,{.5,.5},r,error));QVERIFY(std::abs(r.warmth+.5)<1e-6);QVERIFY(std::abs(r.tint-std::log2(std::sqrt(.3*.15)/.2))<1e-6);QCOMPARE(r.exposure,2.);
        s.raw=true;s.rawWarmth=.2;s.rawTint=-.1;s.cameraToWork={.8f,.1f,.1f,.1f,.8f,.1f,.1f,.1f,.8f};const double c[]{.2*std::exp2(.3),.2*std::exp2(-.2),.2*std::exp2(-.3)};for(int y=0;y<9;++y)for(int x=0;x<9;++x){auto *p=reinterpret_cast<float *>(s.linear.scanLine(y))+4*x;for(int k=0;k<3;++k)p[k]=float(s.cameraToWork[k*3]*c[0]+s.cameraToWork[k*3+1]*c[1]+s.cameraToWork[k*3+2]*c[2]);}
        QVERIFY(neutralBalance(s,{.5,.5},r,error));QVERIFY(std::abs(r.warmth-(-.1))<1e-6);QVERIFY(std::abs(r.tint-.1)<1e-6);s.cameraToWork.fill(0);const auto before=r;QVERIFY(!neutralBalance(s,{.5,.5},r,error));QCOMPARE(r,before);QVERIFY(!neutralBalance(s,{-1,.5},r,error));
        s.raw=false;for(int y=0;y<9;++y)for(int x=0;x<9;++x){auto *p=reinterpret_cast<float *>(s.linear.scanLine(y))+4*x;p[0]=x<4?.4f:.2f;p[1]=.2f;p[2]=x<4?.2f:.4f;}
        r={};QVERIFY(neutralBalance(s,{.1,.5},r,error));QVERIFY(r.warmth<0);r.flipHorizontal=true;QVERIFY(neutralBalance(s,{.1,.5},r,error));QVERIFY(r.warmth>0);
        auto even=frame({8,7});Recipe mirrored,reference;mirrored.flipHorizontal=true;QVERIFY(neutralBalance(even,{.5,.5},mirrored,error));QVERIFY(neutralBalance(even,{3.5/8,.5},reference,error));QCOMPARE(mirrored.warmth,reference.warmth);QCOMPARE(mirrored.tint,reference.tint);
    }
    void neutralPickerCanvasAndHistory(){
        Canvas canvas;canvas.resize(400,300);QImage im(200,100,QImage::Format_RGB32);im.fill(Qt::gray);canvas.setImage(im);canvas.show();canvas.setPickerMode(true);QSignalSpy picked(&canvas,&Canvas::neutralPicked);QTest::mouseClick(&canvas,Qt::LeftButton,Qt::NoModifier,{200,150});QCOMPARE(picked.size(),1);const auto uv=picked.takeFirst().at(0).toPointF();QVERIFY(std::abs(uv.x()-.5)<.01);QVERIFY(std::abs(uv.y()-.5)<.01);QTest::mouseClick(&canvas,Qt::LeftButton,Qt::NoModifier,{1,1});QCOMPARE(picked.size(),0);
        QWheelEvent wheel({200,150},{200,150},{0,0},{0,120},Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);QApplication::sendEvent(&canvas,&wheel);QTest::mouseClick(&canvas,Qt::LeftButton,Qt::NoModifier,{200,150});QCOMPARE(picked.size(),1);QVERIFY(std::abs(picked.takeFirst().at(0).toPointF().x()-.5)<.01);canvas.setLiveAngle(2);QTest::mouseClick(&canvas,Qt::LeftButton,Qt::NoModifier,{200,150});QCOMPARE(picked.size(),0);canvas.setLiveAngle(0);
        QTemporaryDir dir;QImage jpeg(80,60,QImage::Format_RGB32);jpeg.fill(QColor(155,140,125));const auto path=dir.path()+"/neutral.jpg";QVERIFY(jpeg.save(path));Window w(nullptr,QStringLiteral(""));w.show();QVERIFY(openAllInspectorSections(w));QSignalSpy ready(&w,&Window::previewChanged);w.addFiles({path});QTRY_VERIFY(ready.size()>0);auto *button=w.findChild<QPushButton *>("wb-picker");button->click();QVERIFY(button->isChecked());w.findChild<QToolButton *>("tool-hand")->click();QVERIFY(!button->isChecked());button->click();w.findChild<QAction *>("workspace-browse")->trigger();QVERIFY(!button->isChecked());w.findChild<QAction *>("workspace-edit")->trigger();QString error;QVERIFY(w.pickNeutralBalance({.5,.5},error));const auto result=w.currentRecipe();QVERIFY(std::abs(result.warmth)>.01);w.undoEdit();QCOMPARE(w.currentRecipe(),Recipe{});w.redoEdit();QCOMPARE(w.currentRecipe(),result);w.compareOriginal(true);QVERIFY(!w.pickNeutralBalance({.5,.5},error));
    }
    void neutralPickerRealSamples(){
        const auto directory=qEnvironmentVariable("LUMINA_REAL_SAMPLES");if(directory.isEmpty())QSKIP("Optional private WB picker regression");
        for(const auto &name:{"4X1A9579.CR3","4X1A0705.JPG"}){Window w(nullptr,QStringLiteral(""));w.show();QVERIFY(openAllInspectorSections(w));QSignalSpy ready(&w,&Window::previewChanged);w.addFiles({directory+"/"+name});QTRY_VERIFY_WITH_TIMEOUT(ready.size()>0,90000);QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),90000);Recipe recipe;recipe.crop={.1,.1,.8,.8};recipe.angle=2;recipe.flipHorizontal=true;const auto count=ready.size();w.applyRecipe(recipe);QTRY_VERIFY_WITH_TIMEOUT(ready.size()>count,90000);QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),90000);QString error;bool found=false;for(double y:{.25,.5,.75})for(double x:{.25,.5,.75})if(!found&&w.pickNeutralBalance({x,y},error))found=true;QVERIFY2(found,qPrintable(error));const auto picked=w.currentRecipe();QCOMPARE(picked.crop,recipe.crop);QCOMPARE(picked.flipHorizontal,true);QCOMPARE(picked.angle,2.);const auto next=ready.size();QTRY_VERIFY_WITH_TIMEOUT(ready.size()>next,90000);QVERIFY(ready.last().at(1).toBool());w.undoEdit();QCOMPARE(w.currentRecipe(),recipe);w.redoEdit();QCOMPARE(w.currentRecipe(),picked);}
    }
    void primaryRealContinuousFeedback() {
        const auto path=qEnvironmentVariable("LUMINA_LIVE_SAMPLE");if(path.isEmpty())QSKIP("Optional private primary feedback");Window w(nullptr,QStringLiteral(""));w.show();QVERIFY(openAllInspectorSections(w));w.addFiles({path});QSignalSpy ready(&w,&Window::previewChanged);QTRY_VERIFY_WITH_TIMEOUT(ready.size()>0,90000);QSignalSpy painted(w.findChild<Canvas *>(),&Canvas::framePainted);QJsonArray report;auto *channel=w.findChild<QComboBox *>("primary-channel");
        for(int c=0;c<3;++c){channel->setCurrentIndex(c);for(const auto &name:{"primary-hue","primary-saturation"}){auto *bar=w.findChild<QSlider *>("slider-"+QString(name));const auto before=ready.size(),paintBefore=painted.size();bar->setSliderDown(true);for(int i=1;i<=40;++i){bar->setValue(QString(name)=="primary-hue"?i*25:-i);QTest::qWait(50);}const auto frames=ready.size()-before,paints=painted.size()-paintBefore,release=ready.size();bar->setSliderDown(false);QTRY_VERIFY_WITH_TIMEOUT(ready.size()>release,90000);QVERIFY(ready.last().at(1).toBool());QVERIFY(frames>=4);QVERIFY(paints>=4);report.append(QJsonObject{{"channel",c},{"control",name},{"frames",frames}});}}
        const auto final=w.currentRecipe();for(const auto &p:final.primaries){QVERIFY(std::abs(p.hue-1./3)<1e-12);QCOMPARE(p.saturation,-.4);}w.undoEdit();QCOMPARE(w.currentRecipe().primaries[2].saturation,0.);QVERIFY(std::abs(w.currentRecipe().primaries[2].hue-1./3)<1e-12);QTRY_COMPARE_WITH_TIMEOUT(w.displayedPath(),QFileInfo(path).canonicalFilePath(),90000);QTRY_VERIFY(w.findChild<QSlider *>("slider-primary-hue")->isEnabled());const auto output=qEnvironmentVariable("LUMINA_PRIMARY_REPORT");if(!output.isEmpty()){QFile file(output);QVERIFY(file.open(QIODevice::WriteOnly));file.write(QJsonDocument(report).toJson());}const auto shot=qEnvironmentVariable("LUMINA_PRIMARY_SCREENSHOT");if(!shot.isEmpty()){w.findChild<QScrollArea *>()->ensureWidgetVisible(channel);QTest::qWait(100);QVERIFY(w.grab().save(shot));}
    }
    void primaryModelAndSerialization() {
        Recipe r;r.primaries[0]={.5,.4};r.primaries[1]={-.2,-.3};r.primaries[2]={.3,.2};PrimaryOperator operation(r);for(float y:{-.5f,0.f,.18f,2.f}){float grey[]{y,y,y};operation.apply(grey);QCOMPARE(grey[0],y);QCOMPARE(grey[1],y);QCOMPARE(grey[2],y);}float v[]{-.2f,1.4f,.6f};const float before=.2627f*v[0]+.6780f*v[1]+.0593f*v[2];operation.apply(v);QVERIFY(std::abs(.2627f*v[0]+.6780f*v[1]+.0593f*v[2]-before)<1e-6);QCOMPARE(recipeFromJson(recipeJson(r)),r);QCOMPARE(recipeFromJson({}).primaries,Recipe{}.primaries);auto json=recipeJson(r);auto values=json["primaries"].toArray();values[0]=QJsonObject{{"hue","0.5"},{"saturation",0}};json["primaries"]=values;QVERIFY(!validRecipe(recipeFromJson(json)));r.primaries[0].hue=1.1;QVERIFY(!validRecipe(r));
        auto s=frame({32,24});r={};r.primaries[2]={.2,-.3};r.exposure=.2;r.curves[0]={{0,0},{.5,.6},{1,1}};QString error;auto linear=renderLinear(s,r,0,error),reference=workToSrgbReference(linear,error);const auto fast=renderFrame(s,r,0);QVERIFY2(fast.error.isEmpty(),qPrintable(fast.error));int difference=0;for(qsizetype i=0;i<reference.sizeInBytes();++i)difference=std::max(difference,std::abs(int(reference.constBits()[i])-int(fast.image.constBits()[i])));QVERIFY(difference<=1);
    }
    void primaryControlsAndPresetRoundTrip() {
        QTemporaryDir dir;QImage image(96,64,QImage::Format_RGB32);image.fill(QColor(150,90,40));const auto path=dir.path()+"/primary.jpg";QVERIFY(image.save(path));Window w(nullptr,QStringLiteral(""));w.show();QVERIFY(openAllInspectorSections(w));w.addFiles({path});QSignalSpy ready(&w,&Window::previewChanged);QTRY_VERIFY(ready.size()>0);auto *channel=w.findChild<QComboBox *>("primary-channel");auto *hue=w.findChild<QSlider *>("slider-primary-hue");hue->setSliderDown(true);hue->setValue(900);hue->setValue(1500);hue->setSliderDown(false);QCOMPARE(w.currentRecipe().primaries[0].hue,.5);channel->setCurrentIndex(2);auto *sat=w.findChild<QSlider *>("slider-primary-saturation");sat->setValue(-30);QMetaObject::invokeMethod(sat,"sliderReleased");QCOMPARE(w.currentRecipe().primaries[2].saturation,-.3);const auto recipe=w.currentRecipe();QString error;const auto file=dir.path()+"/primary.json";QVERIFY(w.saveAdjustmentPreset(file,error));w.applyRecipe({});QVERIFY(w.loadAdjustmentPreset(file,error));QCOMPARE(w.currentRecipe(),recipe);w.undoEdit();QCOMPARE(w.currentRecipe(),Recipe{});w.redoEdit();QCOMPARE(w.currentRecipe(),recipe);
    }
    void presetsRealRawAndJpeg() {
        const auto directory=qEnvironmentVariable("LUMINA_REAL_SAMPLES");if(directory.isEmpty())QSKIP("Optional private preset regression");QTemporaryDir temporary;
        for(const auto &name:{"4X1A9579.CR3","4X1A0705.JPG"}){const auto path=QFileInfo(directory+"/"+name).canonicalFilePath();Window w(nullptr,QStringLiteral(""));w.show();QVERIFY(openAllInspectorSections(w));w.addFiles({path});auto *loader=w.findChild<EditLoader *>();EditResult result;int count=0;connect(loader,&EditLoader::ready,&w,[&](const EditResult &r){result=r;++count;});QTRY_VERIFY_WITH_TIMEOUT(count>0,90000);Recipe original;original.crop={.1,.1,.8,.8};original.angle=2;original.flipHorizontal=true;count=0;w.applyRecipe(original);QTRY_VERIFY_WITH_TIMEOUT(count>0&&result.recipe==original,90000);QString error;const auto presetFile=temporary.path()+"/custom.json";QVERIFY(w.saveAdjustmentPreset(presetFile,error));auto *choice=w.findChild<QComboBox *>("preset-choice");
            for(int i=0;i<builtinPresets().size();++i){const auto expected=applyPreset(original,builtinPresets()[i]);count=0;choice->setCurrentIndex(i);w.findChild<QPushButton *>("preset-apply")->click();QTRY_VERIFY_WITH_TIMEOUT(count>0&&result.recipe==expected,90000);QVERIFY2(result.error.isEmpty(),qPrintable(result.error));QCOMPARE(qMax(result.image.width(),result.image.height()),3840);QCOMPARE(w.currentRecipe(),expected);QCOMPARE(result.recipe.crop,original.crop);QCOMPARE(result.recipe.flipHorizontal,true);}
            count=0;QVERIFY(w.loadAdjustmentPreset(presetFile,error));QTRY_VERIFY_WITH_TIMEOUT(count>0&&result.recipe==original,90000);QVERIFY(result.error.isEmpty());const auto shot=qEnvironmentVariable("LUMINA_PRESET_REAL_DIR");if(!shot.isEmpty()){QVERIFY(QDir().mkpath(shot));w.findChild<QScrollArea *>()->verticalScrollBar()->setValue(0);QTest::qWait(100);QVERIFY(w.grab().save(shot+"/"+name+".png"));}
        }
    }
    void presetsFormatAndSourceSafety() {
        QTemporaryDir dir;const auto path=dir.path()+"/custom.json";Recipe recipe;recipe.exposure=.3;recipe.noiseLuminance=.2;recipe.warmth=.4;recipe.tint=-.2;recipe.angle=3;recipe.crop={.1,.1,.8,.8};recipe.flipHorizontal=true;AdjustmentPreset preset{QStringLiteral("我的预设"),recipe};QString error;QVERIFY(savePresetFile(path,preset,error));AdjustmentPreset loaded;QVERIFY(loadPresetFile(path,loaded,error));QCOMPARE(loaded.name,preset.name);Recipe expected=recipe;expected.warmth=expected.tint=expected.angle=0;expected.crop={0,0,1,1};expected.flipHorizontal=false;QCOMPARE(loaded.adjustments,expected);Recipe destination;destination.warmth=-.5;destination.tint=.1;destination.angle=7;destination.crop={.2,.2,.5,.6};destination.flipVertical=true;const auto applied=applyPreset(destination,loaded);QCOMPARE(applied.exposure,.3);QCOMPARE(applied.warmth,destination.warmth);QCOMPARE(applied.crop,destination.crop);QCOMPARE(applied.angle,destination.angle);QCOMPARE(applied.flipVertical,destination.flipVertical);
        QFile file(path);QVERIFY(file.open(QIODevice::ReadOnly));const auto valid=file.readAll();file.close();QVERIFY(!valid.contains("warmth"));QVERIFY(!valid.contains("crop"));QVERIFY(!valid.contains("sourcePath"));preset.adjustments.exposure=99;QVERIFY(!savePresetFile(path,preset,error));QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),valid);file.close();QVERIFY(!savePresetFile(dir.path()+"/original.JPG",loaded,error));QVERIFY(!QFile::exists(dir.path()+"/original.JPG"));
        const auto json=QJsonDocument::fromJson(valid).object();auto write=[&](QJsonObject changed){QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::Truncate));file.write(QJsonDocument(changed).toJson());file.close();};auto bad=json;bad["version"]=1.1;write(bad);QVERIFY(!loadPresetFile(path,loaded,error));QCOMPARE(loaded.name,QStringLiteral("我的预设"));bad=json;auto settings=bad["adjustments"].toObject();settings["warmth"]=.5;bad["adjustments"]=settings;write(bad);QVERIFY(!loadPresetFile(path,loaded,error));bad=json;settings=bad["adjustments"].toObject();settings["exposure"]="0.5";bad["adjustments"]=settings;write(bad);QVERIFY(!loadPresetFile(path,loaded,error));for(const auto &p:builtinPresets())QVERIFY(validRecipe(p.adjustments));
    }
    void presetsWindowHistoryAndRoundTrip() {
        QTemporaryDir dir;QImage image(96,64,QImage::Format_RGB32);image.fill(QColor(160,90,60));const auto photo=dir.path()+"/preset.jpg",file=dir.path()+"/custom.luminas-preset.json";QVERIFY(image.save(photo));Window w(nullptr,QStringLiteral(""));w.show();QVERIFY(openAllInspectorSections(w));w.addFiles({photo});QSignalSpy ready(&w,&Window::previewChanged);QTRY_VERIFY(ready.size()>0);Recipe original;original.warmth=.2;original.tint=-.1;original.crop={.1,.1,.8,.8};original.angle=2;original.flipHorizontal=true;original.exposure=.25;w.applyRecipe(original);QTRY_COMPARE(w.displayedPath(),QFileInfo(photo).canonicalFilePath());QString error;QVERIFY(w.saveAdjustmentPreset(file,error));auto *choice=w.findChild<QComboBox *>("preset-choice");choice->setCurrentIndex(2);w.findChild<QPushButton *>("preset-apply")->click();const auto expected=applyPreset(original,builtinPresets()[2]);QCOMPARE(w.currentRecipe(),expected);w.undoEdit();QCOMPARE(w.currentRecipe(),original);w.redoEdit();QCOMPARE(w.currentRecipe(),expected);QVERIFY(w.loadAdjustmentPreset(file,error));QCOMPARE(w.currentRecipe(),original);w.undoEdit();QCOMPARE(w.currentRecipe(),expected);const auto before=w.currentRecipe();QFile bad(dir.path()+"/bad.json");QVERIFY(bad.open(QIODevice::WriteOnly));bad.write("{bad");bad.close();QVERIFY(!w.loadAdjustmentPreset(bad.fileName(),error));QCOMPARE(w.currentRecipe(),before);
        w.compareOriginal(true);QTRY_VERIFY(!w.findChild<QPushButton *>("preset-apply")->isEnabled());QVERIFY(!w.saveAdjustmentPreset(file,error));w.compareOriginal(false);QTRY_VERIFY(w.findChild<QPushButton *>("preset-apply")->isEnabled());const auto shot=qEnvironmentVariable("LUMINA_PRESET_SCREENSHOT");if(!shot.isEmpty()){w.findChild<QScrollArea *>()->ensureWidgetVisible(choice);QTest::qWait(100);QVERIFY(w.grab().save(shot));}
    }
    void spinFinishDuringSliderDoesNotSplitHistory() {
        QTemporaryDir dir;QImage image(96,64,QImage::Format_RGB32);image.fill(Qt::gray);const auto path=dir.path()+"/drag-history.jpg";QVERIFY(image.save(path));Window w(nullptr,QStringLiteral(""));w.show();QVERIFY(openAllInspectorSections(w));w.addFiles({path});QSignalSpy ready(&w,&Window::previewChanged);QTRY_VERIFY(ready.size()>0);auto *bar=w.findChild<QSlider *>("slider-exposure");bar->setSliderDown(true);bar->setValue(18);QMetaObject::invokeMethod(w.findChild<QDoubleSpinBox *>("exposure"),"editingFinished");QMetaObject::invokeMethod(w.findChild<QDoubleSpinBox *>("noise-chroma"),"editingFinished");bar->setValue(40);bar->setSliderDown(false);QCOMPARE(w.currentRecipe().exposure,.4);w.undoEdit();QCOMPARE(w.currentRecipe().exposure,0.);w.redoEdit();QCOMPARE(w.currentRecipe().exposure,.4);
    }
    void denoiseMatchesPriorReference() {
        auto s=frame({32,40});for(int y=0;y<40;++y){auto *p=reinterpret_cast<float *>(s.linear.scanLine(y));for(int x=0;x<32;++x){p[x*4]=-.1f+float((x*13+y*17)%29)/20;p[x*4+1]=.2f+float((x+y)%11)/10;p[x*4+2]=1.4f;p[x*4+3]=x<3?0:x<9?.4f:1;}}QString error;const auto optimized=prepareNoise(s.linear,2,true,true,error,16);QVERIFY2(optimized,qPrintable(error));const size_t n=32*40;std::vector<float> alpha(n);for(size_t i=0;i<n;++i)alpha[i]=reinterpret_cast<const float *>(s.linear.constBits())[i*4+3];
        for(int channel=0;channel<3;++channel){std::vector<float> input(n);for(size_t i=0;i<n;++i){const auto *p=reinterpret_cast<const float *>(s.linear.constBits())+i*4;const float y=.2627f*p[0]+.6780f*p[1]+.0593f*p[2];input[i]=channel==0?y:p[channel==1?0:2]-y;}const double sigma=channel==0?1:2;const float epsilon=channel==0?.0016f:.0025f;auto mean=[&](std::vector<float> value){for(size_t i=0;i<n;++i)value[i]*=alpha[i];return spatialGaussianMean(value,alpha,32,40,sigma);};const auto average=mean(input);auto a=input;for(size_t i=0;i<n;++i)a[i]*=a[i];a=mean(a);std::vector<float> b(n);for(size_t i=0;i<n;++i){const float variance=std::max(0.f,a[i]-average[i]*average[i]);a[i]=variance/(variance+epsilon);b[i]=average[i]*(1-a[i]);}a=mean(a);b=mean(b);const auto &delta=channel==0?optimized->luminance:channel==1?optimized->redChroma:optimized->blueChroma;for(size_t i=0;i<n;++i){const float expected=alpha[i]>1e-8f?a[i]*input[i]+b[i]-input[i]:0;QVERIFY(std::abs(expected-delta[i])<1e-6);}}
    }
    void denoiseModelAndRecipe() {
        auto s=frame({64,48});for(int y=0;y<48;++y){auto *p=reinterpret_cast<float *>(s.linear.scanLine(y));for(int x=0;x<64;++x){const float value=(x<32?.2f:.7f)+((x+y)%2?.02f:-.02f);p[x*4]=p[x*4+1]=p[x*4+2]=value;}}
        Recipe r;r.noiseLuminance=1;QString error;const auto out=renderLinear(s,r,0,error);QVERIFY2(error.isEmpty(),qPrintable(error));double before=0,after=0;for(int y=12;y<36;++y)for(int x=8;x<24;++x){const auto *p=reinterpret_cast<const float *>(out.constScanLine(y));before+=.02*.02;after+=std::pow(p[x*4]-.2f,2);}QVERIFY(after<before*.5);const auto *row=reinterpret_cast<const float *>(out.constScanLine(24));QVERIFY(row[31*4]<.3f);QVERIFY(row[32*4]>.6f);QCOMPARE(recipeFromJson(recipeJson(r)),r);auto j=recipeJson(r);j["noiseChroma"]="0.5";QVERIFY(!validRecipe(recipeFromJson(j)));r.noiseLuminance=1.1;QVERIFY(!validRecipe(r));
        r={};r.noiseChroma=1;for(int y=0;y<48;++y){auto *p=reinterpret_cast<float *>(s.linear.scanLine(y));for(int x=0;x<64;++x){const float d=(x+y)%2?.03f:-.03f;p[x*4]=.3f+d;p[x*4+1]=.3f-.2627f*d/.6780f;p[x*4+2]=.3f;}}const auto color=renderLinear(s,r,0,error);QVERIFY(error.isEmpty());double variation=0;for(int y=8;y<40;++y)for(int x=8;x<56;++x){const auto *p=reinterpret_cast<const float *>(color.constScanLine(y))+4*x;QVERIFY(std::abs(.2627f*p[0]+.6780f*p[1]+.0593f*p[2]-.3f)<1e-6);variation+=std::pow(p[0]-.3f,2);}QVERIFY(variation<32*48*.03*.03*.5);
        r.noiseLuminance=.5;r.sharpenAmount=.4;r.dehaze=.1;r.texture=.2;r.exposure=.2;const auto linear=renderLinear(s,r,0,error),reference=workToSrgbReference(linear,error);SpatialCache cache;const auto rendered=renderFrame(s,r,0,&cache);QVERIFY2(rendered.error.isEmpty(),qPrintable(rendered.error));int difference=0;for(qsizetype i=0;i<reference.sizeInBytes();++i)difference=std::max(difference,std::abs(int(reference.constBits()[i])-int(rendered.image.constBits()[i])));QVERIFY(difference<=1);
    }
    void denoiseStripsAlphaAndHeadroom() {
        auto s=frame({40,303});for(int y=0;y<303;++y){auto *p=reinterpret_cast<float *>(s.linear.scanLine(y));for(int x=0;x<40;++x){p[x*4]=-.1f+float((x*17+y*11)%13)/100;p[x*4+1]=1.5f+float((x+y)%7)/50;p[x*4+2]=.3f;p[x*4+3]=x<3?0:x<6?.4f:1;}}QString error;auto tiled=prepareNoise(s.linear,1,true,true,error,32),whole=prepareNoise(s.linear,1,true,true,error,303);QVERIFY2(tiled&&whole,qPrintable(error));for(size_t i=0;i<tiled->luminance.size();++i){QVERIFY(std::abs(tiled->luminance[i]-whole->luminance[i])<1e-5);QVERIFY(std::abs(tiled->redChroma[i]-whole->redChroma[i])<1e-5);QVERIFY(std::abs(tiled->blueChroma[i]-whole->blueChroma[i])<1e-5);}
        Recipe r;r.noiseLuminance=.6;r.noiseChroma=.7;auto inplace=s.linear.copy();QVERIFY(applyNoiseInPlace(inplace,1,r,error,32));auto basis=prepareSpatial(s.linear,1,{40,303},error,false,false,0,true,true);const auto cached=applySpatial(*basis,r,error);QVERIFY(error.isEmpty());for(int y=0;y<303;++y)for(int x=0;x<40;++x){const auto *a=reinterpret_cast<const float *>(inplace.constScanLine(y))+x*4,*b=reinterpret_cast<const float *>(cached.constScanLine(y))+x*4;for(int c=0;c<4;++c)QVERIFY(std::abs(a[c]-b[c])<1e-5);QCOMPARE(a[3],x<3?0.f:x<6?.4f:1.f);}const auto *p=reinterpret_cast<const float *>(inplace.constScanLine(100));QCOMPARE(p[0],0.f);QVERIFY(p[20*4]<0);QVERIFY(p[20*4+1]>1);
    }
    void denoiseCacheAndFailures() {
        auto s=frame({24,18});s.originalSize=s.linear.size();QString error;SpatialCache cache;int builds=0;Recipe geometry;auto build=[&]{++builds;return s.linear;};auto a=cache.get(s,geometry,0,1,build,error,false,false,0,true,false);QVERIFY(a&&a->noise);QVERIFY(a->noise->redChroma.empty());geometry.noiseLuminance=.8;auto b=cache.get(s,geometry,0,1,build,error,false,false,0,true,false);QCOMPARE(a,b);QCOMPARE(builds,1);auto c=cache.get(s,geometry,0,1,build,error,false,false,0,true,true);QVERIFY(c&&c!=a);QVERIFY(!c->noise->redChroma.empty());QCOMPARE(builds,1);geometry.flipVertical=true;QVERIFY(cache.get(s,geometry,0,1,build,error,false,false,0,true,true));QCOMPARE(builds,2);
        QVERIFY(!prepareNoise(s.linear,0,true,true,error));QVERIFY(!error.isEmpty());auto *p=reinterpret_cast<float *>(s.linear.bits());p[0]=std::numeric_limits<float>::quiet_NaN();QVERIFY(!prepareNoise(s.linear,1,true,true,error));QVERIFY(!error.isEmpty());Recipe r;r.noiseLuminance=1;QVERIFY(!applyNoiseInPlace(s.linear,1,r,error));
    }
    void continuousDenoiseFeedback() {
        QTemporaryDir dir;QString path=qEnvironmentVariable("LUMINA_LIVE_SAMPLE");if(path.isEmpty()){QImage image(3840,2160,QImage::Format_RGB32);image.fill(QColor(130,100,80));path=dir.path()+"/noise4k.jpg";QVERIFY(image.save(path));}Window w(nullptr,QStringLiteral(""));w.show();QVERIFY(openAllInspectorSections(w));w.addFiles({path});QSignalSpy ready(&w,&Window::previewChanged);QTRY_VERIFY_WITH_TIMEOUT(ready.size()>0,90000);Recipe r;r.noiseLuminance=r.noiseChroma=.01;const auto prepared=ready.size();QElapsedTimer timer;timer.start();w.applyRecipe(r);QTRY_VERIFY_WITH_TIMEOUT(ready.size()>prepared,90000);const auto firstPrepare=timer.elapsed();QSignalSpy painted(w.findChild<Canvas *>(),&Canvas::framePainted);QJsonArray results;
        for(const auto &name:{"noise-luminance","noise-chroma","exposure"}){auto *bar=w.findChild<QSlider *>("slider-"+QString(name));const auto before=ready.size(),paintBefore=painted.size();bar->setSliderDown(true);for(int i=1;i<=40;++i){bar->setValue(i);QTest::qWait(50);}const auto frames=ready.size()-before,paints=painted.size()-paintBefore,release=ready.size();bar->setSliderDown(false);QTRY_VERIFY_WITH_TIMEOUT(ready.size()>release,90000);QVERIFY(ready.last().at(1).toBool());QVERIFY(frames>=4);QVERIFY(paints>=4);results.append(QJsonObject{{"control",name},{"frames",frames},{"painted",paints}});qInfo()<<"Noise feedback"<<name<<frames;}
        w.undoEdit();QCOMPARE(w.currentRecipe().exposure,0.);QCOMPARE(w.currentRecipe().noiseChroma,.4);QTRY_COMPARE_WITH_TIMEOUT(w.displayedPath(),QFileInfo(path).canonicalFilePath(),90000);QTRY_VERIFY(w.findChild<QSlider *>("slider-noise-chroma")->isEnabled());const auto report=qEnvironmentVariable("LUMINA_NOISE_REPORT");if(!report.isEmpty()){QFile file(report);QVERIFY(file.open(QIODevice::WriteOnly));file.write(QJsonDocument(QJsonObject{{"firstNoisePrepareMs",double(firstPrepare)},{"controls",results}}).toJson());}const auto shot=qEnvironmentVariable("LUMINA_NOISE_SCREENSHOT");if(!shot.isEmpty()){w.findChild<QScrollArea *>()->ensureWidgetVisible(w.findChild<QDoubleSpinBox *>("noise-chroma"));QTest::qWait(100);QVERIFY(w.grab().save(shot));}
    }
    void sharpeningModelAndRecipe() {
        auto s=frame({64,32});for(int y=0;y<32;++y){auto *p=reinterpret_cast<float *>(s.linear.scanLine(y));for(int x=0;x<64;++x){p[x*4]=p[x*4+1]=p[x*4+2]=x<32?.2f:.6f;}}
        Recipe r;r.sharpenAmount=1;r.sharpenRadius=1;QString error;const auto sharp=renderLinear(s,r,0,error);QVERIFY2(error.isEmpty(),qPrintable(error));const auto *row=reinterpret_cast<const float *>(sharp.constScanLine(16));QVERIFY(row[31*4]<.2f);QVERIFY(row[32*4]>.6f);QVERIFY(std::abs(row[8*4]-.2f)<1e-6);QCOMPARE(row[32*4],row[32*4+1]);
        r.sharpenThreshold=.1;const auto threshold=renderLinear(s,r,0,error);QVERIFY(error.isEmpty());QVERIFY(reinterpret_cast<const float *>(threshold.constScanLine(16))[32*4]<row[32*4]);QCOMPARE(recipeFromJson(recipeJson(r)),r);QCOMPARE(recipeFromJson({}).sharpenRadius,1.);auto json=recipeJson(r);json["sharpenRadius"]="1";QVERIFY(!validRecipe(recipeFromJson(json)));r.sharpenAmount=2.1;QVERIFY(!validRecipe(r));r.sharpenAmount=1;r.sharpenRadius=0;QVERIFY(!validRecipe(r));
        r={};r.sharpenAmount=.6;r.sharpenRadius=2;r.sharpenThreshold=.01;r.texture=.2;r.dehaze=.15;r.exposure=.2;r.flipHorizontal=true;const auto linear=renderLinear(s,r,0,error),reference=workToSrgbReference(linear,error);SpatialCache cache;const auto rendered=renderFrame(s,r,0,&cache);QVERIFY2(rendered.error.isEmpty(),qPrintable(rendered.error));int difference=0;for(qsizetype i=0;i<reference.sizeInBytes();++i)difference=std::max(difference,std::abs(int(reference.constBits()[i])-int(rendered.image.constBits()[i])));QVERIFY(difference<=1);
    }
    void sharpeningAlphaScaleAndCache() {
        auto s=frame({33,33});s.originalSize={66,66};auto *center=reinterpret_cast<float *>(s.linear.scanLine(16))+16*4;for(int y=0;y<33;++y){auto *p=reinterpret_cast<float *>(s.linear.scanLine(y));for(int x=0;x<33;++x)p[x*4]=p[x*4+1]=p[x*4+2]=.2f;}center[0]=center[1]=center[2]=.4f;QString error;
        for(double scale:{1.,2.}){const auto basis=prepareSpatial(s.linear,scale,s.originalSize,error,false,false,2);QVERIFY2(basis,qPrintable(error));QVERIFY(basis->texture.empty());double mass=0,variance=0;for(int y=0;y<33;++y)for(int x=0;x<33;++x){const double difference=(x==16&&y==16?.2:0)-basis->sharpen[size_t(y)*33+x];const double weight=difference/.2;mass+=weight;variance+=(x-16)*(x-16)*weight;}QVERIFY(std::abs(mass-1)<.001);QVERIFY(std::abs(variance*scale*scale-4)<.01);}
        for(int y=0;y<33;++y){auto *p=reinterpret_cast<float *>(s.linear.scanLine(y));for(int x=0;x<10;++x){p[x*4]=100;p[x*4+3]=0;}}Recipe r;r.sharpenAmount=1;const auto alpha=renderLinear(s,r,0,error);QVERIFY(error.isEmpty());const auto *p=reinterpret_cast<const float *>(alpha.constScanLine(16));QCOMPARE(p[0],0.f);QCOMPARE(p[3],0.f);QVERIFY(std::abs(p[10*4]-.2f)<1e-5);
        SpatialCache cache;int builds=0;Recipe geometry;auto build=[&]{++builds;return s.linear;};auto a=cache.get(s,geometry,0,1,build,error,false,false,1);auto b=cache.get(s,geometry,0,1,build,error,false,false,1);QCOMPARE(a,b);QCOMPARE(builds,1);auto c=cache.get(s,geometry,0,1,build,error,false,false,2);QVERIFY(c!=a);QCOMPARE(builds,1);QCOMPARE(c->sharpenRadius,2.);geometry.flipHorizontal=true;auto d=cache.get(s,geometry,0,1,build,error,false,false,2);QVERIFY(d!=c);QCOMPARE(builds,2);
    }
    void continuousSharpeningFeedback() {
        QTemporaryDir dir;QString path=qEnvironmentVariable("LUMINA_LIVE_SAMPLE");if(path.isEmpty()){QImage image(3840,2160,QImage::Format_RGB32);image.fill(QColor(130,100,80));path=dir.path()+"/sharp4k.jpg";QVERIFY(image.save(path));}Window w(nullptr,QStringLiteral(""));w.show();QVERIFY(openAllInspectorSections(w));w.addFiles({path});QSignalSpy ready(&w,&Window::previewChanged);QTRY_VERIFY_WITH_TIMEOUT(ready.size()>0,90000);auto *loader=w.findChild<EditLoader *>();QJsonArray results;connect(loader,&EditLoader::ready,&w,[&](const EditResult &r){QVERIFY2(r.error.isEmpty(),qPrintable(r.error));QCOMPARE(qMax(r.image.width(),r.image.height()),3840);});QSignalSpy painted(w.findChild<Canvas *>(),&Canvas::framePainted);
        for(int stage=0;stage<4;++stage){auto *bar=w.findChild<QSlider *>(stage==0?"slider-sharpen-amount":stage==1?"slider-sharpen-radius":stage==2?"slider-sharpen-threshold":"slider-sharpen-mask");const auto before=ready.size(),paintBefore=painted.size();bar->setSliderDown(true);for(int i=1;i<=40;++i){bar->setValue(stage==0?i*3:stage==1?100+i*5:stage==2?i/4:i*2);QTest::qWait(50);}const auto frames=ready.size()-before,paints=painted.size()-paintBefore,release=ready.size();bar->setSliderDown(false);QTRY_VERIFY_WITH_TIMEOUT(ready.size()>release,90000);QVERIFY(frames>=4);QVERIFY(paints>=4);results.append(QJsonObject{{"control",stage==0?"amount":stage==1?"radius":stage==2?"threshold":"mask"},{"frames",frames},{"painted",paints}});qInfo()<<"Sharpen feedback"<<stage<<frames;}
        QCOMPARE(w.currentRecipe().sharpenMask,.8);w.undoEdit();QCOMPARE(w.currentRecipe().sharpenMask,0.);QCOMPARE(w.currentRecipe().sharpenAmount,1.2);QCOMPARE(w.currentRecipe().sharpenRadius,3.);QCOMPARE(w.currentRecipe().sharpenThreshold,.1);w.undoEdit();QCOMPARE(w.currentRecipe().sharpenThreshold,0.);QCOMPARE(w.currentRecipe().sharpenRadius,3.);w.undoEdit();QCOMPARE(w.currentRecipe().sharpenRadius,1.);QCOMPARE(w.currentRecipe().sharpenAmount,1.2);QTRY_COMPARE_WITH_TIMEOUT(w.displayedPath(),QFileInfo(path).canonicalFilePath(),90000);QTRY_VERIFY(w.findChild<QSlider *>("slider-sharpen-radius")->isEnabled());
        const auto report=qEnvironmentVariable("LUMINA_SHARPEN_REPORT");if(!report.isEmpty()){QFile file(report);QVERIFY(file.open(QIODevice::WriteOnly));file.write(QJsonDocument(results).toJson());}const auto shot=qEnvironmentVariable("LUMINA_SHARPEN_SCREENSHOT");if(!shot.isEmpty()){w.findChild<QScrollArea *>()->ensureWidgetVisible(w.findChild<QDoubleSpinBox *>("sharpen-mask"));QTest::qWait(100);QVERIFY(w.grab().save(shot));}
    }
    void flipsAndSourceCoordinates() {
        auto s=frame({8,6});QString error;Recipe r;r.flipHorizontal=true;auto out=renderLinear(s,r,0,error);QVERIFY(error.isEmpty());const auto *pixel=reinterpret_cast<const float *>(out.constBits());QVERIFY(std::abs(pixel[0]-7.f/8)<1e-6);r.flipVertical=true;out=renderLinear(s,r,0,error);pixel=reinterpret_cast<const float *>(out.constBits());QVERIFY(std::abs(pixel[0]-7.f/8)<1e-6);QVERIFY(std::abs(pixel[1]-5.f/6)<1e-6);QCOMPARE(outputToSource({0,0},{8,6},r),QPointF(8,6));QCOMPARE(recipeFromJson(recipeJson(r)),r);
        r.angle=10;r.crop={.1,.1,.8,.8};auto mirror=r;mirror.flipHorizontal=mirror.flipVertical=false;QCOMPARE(outputToSource({.2,.3},{8,6},r),outputToSource({.8,.7},{8,6},mirror));auto j=recipeJson(r);j["flipHorizontal"]=1;QVERIFY(!validRecipe(recipeFromJson(j)));
    }
    void constrainedCropGesture() {
        Canvas canvas;canvas.resize(640,480);canvas.show();QImage image(400,300,QImage::Format_RGB32);image.fill(Qt::gray);canvas.setImage(image);canvas.setCropAspect(3./2);canvas.setCropMode(true);QSignalSpy picked(&canvas,&Canvas::cropPicked);QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,QPoint(180,150));QTest::mouseMove(&canvas,QPoint(450,360));QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::NoModifier,QPoint(450,360));QCOMPARE(picked.size(),1);const auto crop=picked.last().at(0).toRectF();QVERIFY(std::abs(crop.width()*400/(crop.height()*300)-1.5)<1e-6);QVERIFY(crop.x()>=0&&crop.y()>=0&&crop.right()<=1&&crop.bottom()<=1);
    }
    void geometryControlsAndHistory() {
        QTemporaryDir dir;QImage image(400,300,QImage::Format_RGB32);image.fill(Qt::gray);const auto path=dir.path()+"/geometry.jpg";QVERIFY(image.save(path));Window w(nullptr,QStringLiteral(""));w.show();QVERIFY(openAllInspectorSections(w));w.addFiles({path});QSignalSpy ready(&w,&Window::previewChanged);QTRY_VERIFY(ready.size()>0);
        auto *horizontal=w.findChild<QPushButton *>("flip-horizontal"),*vertical=w.findChild<QPushButton *>("flip-vertical"),*reset=w.findChild<QPushButton *>("reset-geometry");auto *ratio=w.findChild<QComboBox *>("crop-aspect");QVERIFY(horizontal&&vertical&&reset&&ratio);horizontal->click();QVERIFY(w.currentRecipe().flipHorizontal);vertical->click();QVERIFY(w.currentRecipe().flipVertical);w.undoEdit();QVERIFY(w.currentRecipe().flipHorizontal);QVERIFY(!w.currentRecipe().flipVertical);w.redoEdit();QVERIFY(w.currentRecipe().flipVertical);
        ratio->setCurrentIndex(4);const auto before=ready.size();w.findChild<QToolButton *>("tool-crop")->click();QTRY_VERIFY(ready.size()>before);QVERIFY(!horizontal->isEnabled());auto *canvas=w.findChild<Canvas *>();const auto center=canvas->rect().center();QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,center-QPoint(100,75));QTest::mouseMove(canvas,center+QPoint(70,45));QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,center+QPoint(70,45));auto cropped=w.currentRecipe();QVERIFY(std::abs(cropped.crop.width()*400/(cropped.crop.height()*300)-1.5)<1e-6);QVERIFY(cropped.flipHorizontal&&cropped.flipVertical);
        cropped.exposure=.25;cropped.angle=3;w.applyRecipe(cropped);QTRY_VERIFY(horizontal->isEnabled());reset->click();auto restored=cropped;restored.crop={0,0,1,1};restored.angle=0;restored.flipHorizontal=restored.flipVertical=false;QCOMPARE(w.currentRecipe(),restored);w.undoEdit();QCOMPARE(w.currentRecipe(),cropped);
        const auto shot=qEnvironmentVariable("LUMINA_GEOMETRY_SCREENSHOT");if(!shot.isEmpty()){w.findChild<QScrollArea *>()->ensureWidgetVisible(reset);QTest::qWait(100);QVERIFY(w.grab().save(shot));}
    }
    void dehazeModelAndRecipe() {
        auto s=frame({128,96});s.originalSize={3840,2880};for(int y=0;y<96;++y){auto *p=reinterpret_cast<float *>(s.linear.scanLine(y));for(int x=0;x<128;++x){const bool white=x<32&&y<32;const float red=x<80?.8f:0,blue=x<80?0:.8f;p[x*4]=white?1:.4f+.6f*red;p[x*4+1]=white?1:.52f;p[x*4+2]=white?1:.4f+.6f*blue;p[x*4+3]=1;}}
        QString error;Recipe r;r.dehaze=1;auto restored=renderLinear(s,r,0,error);QVERIFY2(error.isEmpty(),qPrintable(error));r.dehaze=-1;auto hazed=renderLinear(s,r,0,error);QVERIFY(error.isEmpty());const auto *a=reinterpret_cast<const float *>(restored.constScanLine(64))+64*4,*b=reinterpret_cast<const float *>(hazed.constScanLine(64))+64*4;QVERIFY(a[0]-a[2]>.48f);QVERIFY(b[0]-b[2]<.48f);QCOMPARE(a[3],1.f);QVERIFY(std::isfinite(a[0]));
        r.dehaze=.4;r.texture=.2;r.exposure=.2;r.curves[0]={{0,0},{.5,.6},{1,1}};SpatialCache cache;const auto linear=renderLinear(s,r,0,error),reference=workToSrgbReference(linear,error);const auto fast=renderFrame(s,r,0,&cache);QVERIFY2(fast.error.isEmpty(),qPrintable(fast.error));int difference=0;for(qsizetype i=0;i<reference.sizeInBytes();++i)difference=std::max(difference,std::abs(int(reference.constBits()[i])-int(fast.image.constBits()[i])));QVERIFY(difference<=1);QCOMPARE(recipeFromJson(recipeJson(r)),r);r.dehaze=1.01;QVERIFY(!validRecipe(r));
        auto basis=prepareHaze(s.linear,1,s.originalSize,error);QVERIFY(basis);HazeSampler sampler(*basis,s.linear.size(),.4);float direct[]{.7f,.5f,.4f},optimized[]{.7f,.5f,.4f};applyHaze(*basis,64,64,s.linear.size(),direct,.4);sampler.apply(64,64,optimized);for(int c=0;c<3;++c)QVERIFY(std::abs(direct[c]-optimized[c])<1e-5);
    }
    void dehazeAlphaAndCacheReuse() {
        auto s=frame({32,16});s.originalSize={3840,1920};for(int y=0;y<16;++y){auto *p=reinterpret_cast<float *>(s.linear.scanLine(y));for(int x=0;x<32;++x){p[x*4]=-.1f;p[x*4+1]=2;p[x*4+2]=.5f;p[x*4+3]=x<8?0:1;}}QString error;Recipe r;r.dehaze=.5;auto out=renderLinear(s,r,0,error);QVERIFY(error.isEmpty());const auto *v=reinterpret_cast<const float *>(out.constScanLine(8));QCOMPARE(v[3],0.f);QCOMPARE(v[0],0.f);QVERIFY(v[20*4]<0);QVERIFY(v[20*4+1]>1);
        SpatialCache cache;int builds=0;Recipe geometry;auto build=[&]{++builds;return renderLinear(s,geometry,0,error);};auto first=cache.get(s,geometry,0,1,build,error,true,false);QVERIFY(first&&first->haze);QVERIFY(first->texture.empty());auto second=cache.get(s,geometry,0,1,build,error,true,false);QCOMPARE(first,second);QCOMPARE(builds,1);
    }
    void continuousDehazeFeedback() {
        QTemporaryDir dir;QString path=qEnvironmentVariable("LUMINA_LIVE_SAMPLE");if(path.isEmpty()){QImage image(3840,2160,QImage::Format_RGB32);image.fill(QColor(150,160,170));path=dir.path()+"/haze4k.jpg";QVERIFY(image.save(path));}Window w(nullptr,QStringLiteral(""));w.show();QVERIFY(openAllInspectorSections(w));w.addFiles({path});QSignalSpy ready(&w,&Window::previewChanged);QTRY_VERIFY_WITH_TIMEOUT(ready.size()>0,90000);QSignalSpy painted(w.findChild<Canvas *>(),&Canvas::framePainted);QJsonArray results;
        for(int stage=0;stage<3;++stage){auto *bar=w.findChild<QSlider *>(stage==2?"slider-exposure":"slider-dehaze");const auto before=ready.size(),paintBefore=painted.size();bar->setSliderDown(true);for(int i=1;i<=40;++i){bar->setValue(stage==1?-i:i);QTest::qWait(50);}const auto frames=ready.size()-before,paints=painted.size()-paintBefore;const auto release=ready.size();bar->setSliderDown(false);QTRY_VERIFY_WITH_TIMEOUT(ready.size()>release,90000);QVERIFY(ready.last().at(1).toBool());QVERIFY(frames>=4);QVERIFY(paints>=4);results.append(QJsonObject{{"stage",stage==0?"positive":stage==1?"negative":"exposure_with_dehaze"},{"frames",frames},{"painted",paints}});qInfo()<<"Dehaze feedback"<<stage<<frames;}
        const auto report=qEnvironmentVariable("LUMINA_DEHAZE_REPORT");if(!report.isEmpty()){QFile file(report);QVERIFY(file.open(QIODevice::WriteOnly));file.write(QJsonDocument(results).toJson());}w.undoEdit();QCOMPARE(w.currentRecipe().exposure,0.);QCOMPARE(w.currentRecipe().dehaze,-.4);
    }
    void realDehazeR7() {
        const auto directory=QDir::fromNativeSeparators(qEnvironmentVariable("LUMINA_REAL_SAMPLES"));if(directory.isEmpty())QSKIP("Optional private dehaze test");QStringList paths;for(const auto &name:{"4X1A0705","4X1A9413","4X1A9579","4X1A9817","4X1A9827","4X1A9954"})paths<<directory+"/"+name+".CR3"<<directory+"/"+name+".JPG";Window w(nullptr,QStringLiteral(""));w.show();QVERIFY(openAllInspectorSections(w));auto *loader=w.findChild<EditLoader *>();int count=0;EditResult result;connect(loader,&EditLoader::ready,&w,[&](const EditResult &r){result=r;++count;});w.addFiles(paths);
        for(int i=0;i<paths.size();++i){count=0;w.activate(i);QTRY_VERIFY_WITH_TIMEOUT(count>0&&result.path==QFileInfo(paths[i]).canonicalFilePath(),90000);QVERIFY(result.error.isEmpty());Recipe r;r.dehaze=i%2?.2:-.2;r.exposure=.1;count=0;w.applyRecipe(r);QTRY_VERIFY_WITH_TIMEOUT(count>0&&result.recipe==r,90000);QVERIFY2(result.error.isEmpty(),qPrintable(result.error));QVERIFY(!result.sourceRebuilt);QCOMPARE(qMax(result.image.width(),result.image.height()),3840);QCOMPARE(w.currentRecipe(),r);}
        const auto shot=qEnvironmentVariable("LUMINA_DEHAZE_SCREENSHOT");if(!shot.isEmpty()){w.findChild<QScrollArea *>()->ensureWidgetVisible(w.findChild<QDoubleSpinBox *>("dehaze"));QTest::qWait(100);QVERIFY(w.grab().save(shot));}
    }
    void allExifOrientationsAndMirrors() {
        QTemporaryDir dir;QImage image(240,160,QImage::Format_RGB32);for(int y=0;y<160;++y)for(int x=0;x<240;++x)image.setPixelColor(x,y,y<80?(x<120?QColor(220,20,20):QColor(20,220,20)):(x<120?QColor(20,20,220):QColor(220,220,20)));
        QByteArray encoded;QBuffer buffer(&encoded);QVERIFY(buffer.open(QIODevice::WriteOnly));QVERIFY(image.save(&buffer,"JPEG",95));QString error;const auto baseline=inputToWork(QImage::fromData(encoded,"JPEG"),{},error);QVERIFY(error.isEmpty());
        for(int orientation=1;orientation<=8;++orientation){auto payload=QByteArray::fromHex("45786966000049492a0008000000010012010300010000000600000000000000");payload[24]=char(orientation);QByteArray marker=QByteArray::fromHex("ffe1");const int length=int(payload.size())+2;marker.append(char(length>>8));marker.append(char(length));marker.append(payload);auto data=encoded;data.insert(2,marker);const auto path=dir.path()+QStringLiteral("/方向%1.jpg").arg(orientation);QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));QVERIFY(file.write(data)==data.size());file.close();const auto result=readTestSource(path);QVERIFY2(result.error.isEmpty(),qPrintable(result.error));const auto size=orientation<5?QSize(240,160):QSize(160,240);QCOMPARE(result.source->linear.size(),size);
            for(const auto point:{QPoint(10,10),QPoint(size.width()-11,10),QPoint(10,size.height()-11),QPoint(size.width()-11,size.height()-11)}){const int x=point.x(),y=point.y();QPoint source;switch(orientation){case 1:source={x,y};break;case 2:source={239-x,y};break;case 3:source={239-x,159-y};break;case 4:source={x,159-y};break;case 5:source={y,x};break;case 6:source={y,159-x};break;case 7:source={239-y,159-x};break;default:source={239-y,x};}
                const auto *actual=reinterpret_cast<const float *>(result.source->linear.constScanLine(y))+4*x,*expected=reinterpret_cast<const float *>(baseline.constScanLine(source.y()))+4*source.x();for(int c=0;c<4;++c)QVERIFY(std::abs(actual[c]-expected[c])<1e-6);}
        }
    }
    void iccFragmentationAndRejection() {
        QTemporaryDir dir;QImage image(64,48,QImage::Format_RGB32);image.fill(QColor(100,140,190));QByteArray encoded;QBuffer buffer(&encoded);QVERIFY(buffer.open(QIODevice::WriteOnly));QVERIFY(image.save(&buffer,"JPEG",95));const auto profile=QColorSpace(QColorSpace::AdobeRgb).iccProfile();QVERIFY(!profile.isEmpty());
        auto segment=[](int index,int count,const QByteArray &bytes){QByteArray payload("ICC_PROFILE\0",12);payload.append(char(index));payload.append(char(count));payload.append(bytes);QByteArray marker=QByteArray::fromHex("ffe2");const int length=int(payload.size())+2;marker.append(char(length>>8));marker.append(char(length));marker.append(payload);return marker;};
        int number=0;auto load=[&](const QByteArray &markers){auto bytes=encoded;bytes.insert(2,markers);const auto path=dir.path()+QStringLiteral("/icc-%1.jpg").arg(++number);QFile file(path);if(!file.open(QIODevice::WriteOnly))return FrameResult{{},"fixture write"};file.write(bytes);file.close();return readTestSource(path);};
        const auto first=profile.left(profile.size()/2),second=profile.mid(profile.size()/2);auto valid=load(segment(2,2,second)+segment(1,2,first));QVERIFY2(valid.error.isEmpty(),qPrintable(valid.error));QCOMPARE(valid.source->facts.value("embeddedIccBytes").toInt(),profile.size());QString error;const auto expected=inputToWork(QImage::fromData(encoded,"JPEG"),profile,error);QVERIFY(error.isEmpty());const auto *a=reinterpret_cast<const float *>(valid.source->linear.constBits()),*b=reinterpret_cast<const float *>(expected.constBits());for(int c=0;c<4;++c)QVERIFY(std::abs(a[c]-b[c])<1e-6);
        QVERIFY(!load(segment(1,2,first)).error.isEmpty());QVERIFY(!load(segment(1,2,first)+segment(1,2,first)).error.isEmpty());QVERIFY(!load(segment(1,2,first)+segment(2,3,second)).error.isEmpty());QVERIFY(!load(segment(0,1,profile)).error.isEmpty());QVERIFY(!load(segment(1,1,"invalid ICC")).error.isEmpty());
        auto curve=cmsBuildGamma(nullptr,2.2);auto gray=cmsCreateGrayProfile(cmsD50_xyY(),curve);cmsFreeToneCurve(curve);QVERIFY(gray);cmsUInt32Number bytes=0;QVERIFY(cmsSaveProfileToMem(gray,nullptr,&bytes));QByteArray grayBytes(int(bytes),Qt::Uninitialized);QVERIFY(cmsSaveProfileToMem(gray,grayBytes.data(),&bytes));cmsCloseProfile(gray);QVERIFY(!load(segment(1,1,grayBytes)).error.isEmpty());
        QByteArray oversized(4*1024*1024+1,'x'),markers;const int count=int((oversized.size()+64999)/65000);for(int i=0;i<count;++i)markers+=segment(i+1,count,oversized.mid(i*65000,65000));QVERIFY(load(markers).error.contains("4MiB"));
    }
    void spatialContrastAndCache() {
        auto s=frame({384,64});s.originalSize={3840,640};for(int y=0;y<s.linear.height();++y){auto *p=reinterpret_cast<float *>(s.linear.scanLine(y));for(int x=0;x<s.linear.width();++x){const float value=.25f+.06f*float(std::sin(x*3.141592653589793/6))+.03f*float(std::sin(x*3.141592653589793/60));p[x*4]=p[x*4+1]=p[x*4+2]=value;}}
        QString error;Recipe positive;positive.texture=1;auto enhanced=renderLinear(s,positive,0,error);QVERIFY2(error.isEmpty(),qPrintable(error));Recipe negative;negative.texture=-1;auto softened=renderLinear(s,negative,0,error);QVERIFY(error.isEmpty());const int x=123,y=32;const auto *a=reinterpret_cast<const float *>(enhanced.constScanLine(y))+x*4,*b=reinterpret_cast<const float *>(softened.constScanLine(y))+x*4;QVERIFY(a[0]>b[0]);QCOMPARE(a[0],a[1]);QCOMPARE(a[1],a[2]);
        SpatialCache cache;int builds=0;Recipe r=positive;auto builder=[&]{++builds;auto prefix=r;prefix.texture=prefix.clarity=0;return renderLinear(s,prefix,0,error);};auto first=cache.get(s,r,0,1,builder,error);QVERIFY(first);r.texture=.4;r.clarity=.2;auto second=cache.get(s,r,0,1,builder,error);QCOMPARE(first,second);QCOMPARE(builds,1);r.exposure=.2;QVERIFY(cache.get(s,r,0,1,builder,error));QCOMPARE(builds,2);
        r={};r.texture=.4;r.clarity=-.2;r.exposure=.2;r.contrast=.15;r.saturation=.9;r.curves[0]={{0,0},{.4,.5},{1,1}};r.hsl[1].hue=.15;r.grading[0]={210,.2,0};auto linear=renderLinear(s,r,0,error);auto reference=workToSrgbReference(linear,error);auto fast=renderFrame(s,r,0,&cache);QVERIFY2(fast.error.isEmpty(),qPrintable(fast.error));int maxError=0;for(qsizetype i=0;i<reference.sizeInBytes();++i)maxError=std::max(maxError,std::abs(int(reference.constBits()[i])-int(fast.image.constBits()[i])));QVERIFY(maxError<=1);r.texture=1.01;QVERIFY(!validRecipe(r));
    }
    void spatialAlphaAndHeadroom() {
        auto s=frame({64,16});s.originalSize={3840,960};for(int y=0;y<16;++y){auto *p=reinterpret_cast<float *>(s.linear.scanLine(y));for(int x=0;x<64;++x){p[x*4]=-.1f;p[x*4+1]=2;p[x*4+2]=.5f;p[x*4+3]=x<10?0:x<20?.5f:1;}}
        Recipe r;r.texture=.8;r.clarity=.7;QString error;auto out=renderLinear(s,r,0,error);QVERIFY2(error.isEmpty(),qPrintable(error));for(int x=0;x<64;++x){const auto *v=reinterpret_cast<const float *>(out.constScanLine(8))+x*4;if(x<10){QCOMPARE(v[0],0.f);QCOMPARE(v[3],0.f);}else{QVERIFY(std::abs(v[0]+.1f)<1e-5);QVERIFY(std::abs(v[1]-2)<1e-5);QCOMPARE(v[3],x<20?.5f:1.f);}}
        s.sourcePixelsPerPixel=0;QVERIFY(renderLinear(s,r,0,error).isNull());
    }
    void spatialSupportMatchesSourceScale() {
        QImage image(33,33,QImage::Format_RGBA32FPx4);for(int y=0;y<33;++y){auto *p=reinterpret_cast<float *>(image.scanLine(y));for(int x=0;x<33;++x){p[x*4]=p[x*4+1]=p[x*4+2]=x==16&&y==16?.4f:.2f;p[x*4+3]=1;}}
        QString error;const float base=std::log2(.2f+.018f),peak=std::log2(.4f+.018f),difference=peak-base;
        for(double scale:{1.,2.}){const auto basis=prepareSpatial(image,scale,{3840,3840},error);QVERIFY2(error.isEmpty(),qPrintable(error));QVERIFY(basis);double mass=0,variance=0;for(int y=10;y<=22;++y)for(int x=10;x<=22;++x){const double original=x==16&&y==16?peak:base;const double low=original-basis->texture[size_t(y)*33+x]/.7;const double weight=(low-base)/difference;mass+=weight;variance+=(x-16)*(x-16)*weight;}QVERIFY(std::abs(mass-1)<.001);QVERIFY(std::abs(variance*scale*scale-2.25)<.002);}
    }
    void cacheMetadataIntegrityAndScale() {
        PreviewCache cache(10000);auto s=frame({8,6});s.path="scale";s.sha256="1";s.originalSize={80,60};s.sourcePixelsPerPixel=10;QVERIFY(cache.put(s));auto read=cache.get(s.path,s.sha256);QVERIFY(read);QCOMPARE(read->sourcePixelsPerPixel,10.);const auto files=QDir(cache.storageDirectory()).entryList({"*.work"},QDir::Files);QCOMPARE(files.size(),1);QFile f(cache.storageDirectory()+"/"+files[0]);QVERIFY(f.open(QIODevice::ReadWrite));QVERIFY(f.seek(44));QVERIFY(f.write("[",1)==1);f.close();QVERIFY(!cache.get(s.path,s.sha256));QVERIFY(cache.put(s));s.sourcePixelsPerPixel=9;QVERIFY(!cache.put(s));
    }
    void truncatedCacheDoesNotBreakBudget() {
        PreviewCache cache(2000);auto s=frame({8,6});s.path="truncate";s.sha256="1";s.originalSize={80,60};s.sourcePixelsPerPixel=10;QVERIFY(cache.put(s));const auto files=QDir(cache.storageDirectory()).entryList({"*.work"},QDir::Files);QCOMPARE(files.size(),1);QFile f(cache.storageDirectory()+"/"+files[0]);QVERIFY(f.open(QIODevice::ReadWrite));QVERIFY(f.resize(20));f.close();QVERIFY(!cache.get(s.path,s.sha256));QVERIFY(cache.put(s));QVERIFY(cache.usedBytes()<=2000);QVERIFY(cache.get(s.path,s.sha256));
    }
    void continuousSpatialFeedback() {
        QTemporaryDir dir;QString path=qEnvironmentVariable("LUMINA_LIVE_SAMPLE");if(path.isEmpty()){QImage image(3840,2160,QImage::Format_RGB32);image.fill(QColor(130,100,80));path=dir.path()+"/spatial4k.jpg";QVERIFY(image.save(path));}Window w(nullptr,QStringLiteral(""));w.show();QVERIFY(openAllInspectorSections(w));w.addFiles({path});QSignalSpy ready(&w,&Window::previewChanged);QTRY_VERIFY_WITH_TIMEOUT(ready.size()>0,90000);QSignalSpy painted(w.findChild<Canvas *>(),&Canvas::framePainted);QJsonArray controls;
        for(const auto &name:{"texture","clarity","exposure","contrast","hsl-hue","warmth"}){auto *bar=w.findChild<QSlider *>("slider-"+QString(name));QVERIFY(bar->isEnabled());const auto before=ready.size(),paintBefore=painted.size();bar->setSliderDown(true);for(int i=1;i<=40;++i){bar->setValue(i);QTest::qWait(50);}const auto frames=ready.size()-before,paints=painted.size()-paintBefore;const auto release=ready.size();bar->setSliderDown(false);QTRY_VERIFY_WITH_TIMEOUT(ready.size()>release,90000);QVERIFY(ready.last().at(1).toBool());QVERIFY(frames>=4);QVERIFY(paints>=4);controls.append(QJsonObject{{"control",name},{"duringDrag",frames},{"painted",paints}});qInfo()<<"Spatial 4K drag"<<name<<frames;}
        const auto report=qEnvironmentVariable("LUMINA_SPATIAL_REPORT");if(!report.isEmpty()){QFile file(report);QVERIFY(file.open(QIODevice::WriteOnly));file.write(QJsonDocument(controls).toJson());}w.undoEdit();QCOMPARE(w.currentRecipe().warmth,0.);QCOMPARE(w.currentRecipe().texture,.4);
    }
    void realSpatialEdits() {
        const auto directory=QDir::fromNativeSeparators(qEnvironmentVariable("LUMINA_REAL_SAMPLES"));if(directory.isEmpty())QSKIP("Optional private spatial editing");QStringList paths;for(const auto &name:{"4X1A0705","4X1A9413","4X1A9579","4X1A9817","4X1A9827","4X1A9954"})paths<<directory+"/"+name+".CR3"<<directory+"/"+name+".JPG";Window w(nullptr,QStringLiteral(""));w.show();QVERIFY(openAllInspectorSections(w));auto *loader=w.findChild<EditLoader *>();EditResult result;int count=0;connect(loader,&EditLoader::ready,&w,[&](const EditResult &r){result=r;++count;});w.addFiles(paths);
        for(int i=0;i<paths.size();++i){count=0;w.activate(i);QTRY_VERIFY_WITH_TIMEOUT(count>0&&result.path==QFileInfo(paths[i]).canonicalFilePath(),90000);QVERIFY(result.error.isEmpty());Recipe r;r.texture=i%2?.3:-.3;r.clarity=.2;count=0;w.applyRecipe(r);QTRY_VERIFY_WITH_TIMEOUT(count>0&&result.recipe==r,90000);QVERIFY2(result.error.isEmpty(),qPrintable(result.error));QVERIFY(!result.sourceRebuilt);QCOMPARE(qMax(result.image.width(),result.image.height()),3840);QCOMPARE(w.currentRecipe(),r);}
        const auto shot=qEnvironmentVariable("LUMINA_SPATIAL_SCREENSHOT");if(!shot.isEmpty()){w.findChild<QScrollArea *>()->ensureWidgetVisible(w.findChild<QDoubleSpinBox *>("clarity"));QTest::qWait(100);QVERIFY(w.grab().save(shot));}
    }
    void spatialSteadyResources() {
        // Optional sampler control, before importing. Keep this out of normal
        // test runs and source identity; it contains paths/phase only, no pixels.
        const auto control=qEnvironmentVariable("LUMINA_RESOURCE_CONTROL");
        if(!control.isEmpty()){
            const auto rawRoot=QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+"/raw-worker";QVERIFY(QDir().mkpath(rawRoot));
            QFile file(control);QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::NewOnly));
            file.write(QJsonDocument(QJsonObject{{"pid",double(QCoreApplication::applicationPid())},{"tempRoot",QDir::tempPath()},{"rawCacheRoot",rawRoot}}).toJson());file.close();
            // Let the sampler attach before the RAW worker starts. This is a
            // resource run, not an uninstrumented latency benchmark.
            QTest::qWait(1500);
        }
        const auto marker=qEnvironmentVariable("LUMINA_SPATIAL_STEADY_MARKER"),directory=QDir::fromNativeSeparators(qEnvironmentVariable("LUMINA_REAL_SAMPLES"));if(marker.isEmpty()||directory.isEmpty())QSKIP("Explicit private steady resource probe");QStringList paths;for(const auto &name:{"4X1A0705","4X1A9413","4X1A9579","4X1A9817","4X1A9827","4X1A9954"})paths<<directory+"/"+name+".CR3"<<directory+"/"+name+".JPG";Window w(nullptr,QStringLiteral(""));w.show();QVERIFY(openAllInspectorSections(w));auto *loader=w.findChild<EditLoader *>();int prepared=0,count=0;EditResult result;connect(loader,&EditLoader::prefetched,&w,[&](const EditResult &r){QVERIFY2(r.error.isEmpty(),qPrintable(r.error));++prepared;});connect(loader,&EditLoader::ready,&w,[&](const EditResult &r){result=r;++count;});w.addFiles(paths);QTRY_COMPARE_WITH_TIMEOUT(prepared,12,180000);
        count=0;w.activate(4);QTRY_VERIFY_WITH_TIMEOUT(count>0&&result.path==QFileInfo(paths[4]).canonicalFilePath(),90000);Recipe r;r.texture=.4;r.clarity=.3;count=0;w.applyRecipe(r);QTRY_VERIFY_WITH_TIMEOUT(count>0&&result.recipe==r,90000);QVERIFY(result.error.isEmpty());QVERIFY(!result.sourceRebuilt);QFile output(marker);QVERIFY(output.open(QIODevice::WriteOnly));output.write(QJsonDocument(QJsonObject{{"phase","steady_12_cached_one_spatial"},{"pid",double(QCoreApplication::applicationPid())},{"width",result.image.width()},{"height",result.image.height()},{"sourceScale",result.workFrame.sourcePixelsPerPixel}}).toJson());output.close();QTest::qWait(5000);
    }
    void gradingAndMonochromeMath() {
        auto s=frame({1,1});auto *p=reinterpret_cast<float *>(s.linear.bits());p[0]=p[1]=p[2]=.18f;QString error;Recipe r;r.grading[1]={240,.5,0};auto out=renderLinear(s,r,0,error);QVERIFY(error.isEmpty());auto *v=reinterpret_cast<const float *>(out.constBits());QVERIFY(v[2]>v[0]);QVERIFY(std::abs(.2627f*v[0]+.6780f*v[1]+.0593f*v[2]-.18f)<1e-6);
        r={};for(auto &g:r.grading)g.luminance=1;p[0]=-.3f;p[1]=2;p[2]=.5f;out=renderLinear(s,r,0,error);v=reinterpret_cast<const float *>(out.constBits());QVERIFY(std::abs(v[0]+.6f)<1e-6);QVERIFY(std::abs(v[1]-4)<1e-6);
        r={};r.monochrome=true;r.monochromeMix[0]=1;p[0]=1;p[1]=p[2]=0;out=renderLinear(s,r,0,error);v=reinterpret_cast<const float *>(out.constBits());QVERIFY(std::abs(v[0]-.5254f)<1e-6);QCOMPARE(v[0],v[1]);QCOMPARE(v[1],v[2]);QCOMPARE(v[3],1.f);
        p[0]=p[1]=p[2]=.4f;out=renderLinear(s,r,0,error);v=reinterpret_cast<const float *>(out.constBits());QVERIFY(std::abs(v[0]-.4f)<1e-6);p[0]=p[1]=p[2]=-.3f;out=renderLinear(s,r,0,error);v=reinterpret_cast<const float *>(out.constBits());QVERIFY(std::abs(v[0]+.3f)<1e-6);
        r.grading[0]={30,.2,.1};r.gradingBlend=.7;r.gradingBalance=-.3;QVERIFY(validRecipe(r));QCOMPARE(recipeFromJson(recipeJson(r)),r);QCOMPARE(recipeFromJson({}),Recipe{});
        auto j=recipeJson(r);j["monochrome"]=1;QVERIFY(!validRecipe(recipeFromJson(j)));j=recipeJson(r);j["monochromeMix"]=QJsonArray{0};QVERIFY(!validRecipe(recipeFromJson(j)));j=recipeJson(r);j["gradingBlend"]="invalid";QVERIFY(!validRecipe(recipeFromJson(j)));j=recipeJson(r);j["exposure"]="invalid";QVERIFY(!validRecipe(recipeFromJson(j)));r.grading[0].hue=361;QVERIFY(!validRecipe(r));
    }
    void realGradingAndMonochrome() {
        const auto directory=QDir::fromNativeSeparators(qEnvironmentVariable("LUMINA_REAL_SAMPLES"));if(directory.isEmpty())QSKIP("Optional private R7 styles");QStringList paths;for(const auto &name:{"4X1A0705","4X1A9413","4X1A9579","4X1A9817","4X1A9827","4X1A9954"})paths<<directory+"/"+name+".CR3"<<directory+"/"+name+".JPG";
        Window w(nullptr,QStringLiteral(""));w.show();QVERIFY(openAllInspectorSections(w));auto *loader=w.findChild<EditLoader *>();EditResult result;int count=0;connect(loader,&EditLoader::ready,&w,[&](const EditResult &r){result=r;++count;});w.addFiles(paths);
        for(int i=0;i<paths.size();++i){count=0;w.activate(i);QTRY_VERIFY_WITH_TIMEOUT(count>0&&result.path==QFileInfo(paths[i]).canonicalFilePath(),90000);QVERIFY(result.error.isEmpty());Recipe r;r.grading[i%3]={i%2?30.:210.,.3,.1};r.gradingBlend=.65;r.gradingBalance=.2;r.monochrome=i%2==0;r.monochromeMix[1]=.3;r.monochromeMix[5]=-.2;count=0;w.applyRecipe(r);QTRY_VERIFY_WITH_TIMEOUT(count>0&&result.recipe==r,90000);QVERIFY2(result.error.isEmpty(),qPrintable(result.error));QVERIFY(!result.sourceRebuilt);QCOMPARE(w.currentRecipe(),r);QCOMPARE(qMax(result.image.width(),result.image.height()),3840);}
        w.undoEdit();QCOMPARE(w.currentIndex(),11);QCOMPARE(w.currentRecipe(),Recipe{});w.redoEdit();QCOMPARE(w.currentRecipe().grading[2].saturation,.3);
    }
    void continuousStyleFeedback() {
        QTemporaryDir dir;QString path=qEnvironmentVariable("LUMINA_LIVE_SAMPLE");if(path.isEmpty()){QImage image(3840,2160,QImage::Format_RGB32);image.fill(QColor(140,100,80));path=dir.path()+"/style4k.jpg";QVERIFY(image.save(path));}Window w(nullptr,QStringLiteral(""));w.show();QVERIFY(openAllInspectorSections(w));w.addFiles({path});QSignalSpy ready(&w,&Window::previewChanged);QTRY_VERIFY_WITH_TIMEOUT(ready.size()>0,90000);Recipe r;r.grading[0]={240,.3,0};const auto prepared=ready.size();w.applyRecipe(r);QTRY_VERIFY_WITH_TIMEOUT(ready.size()>prepared,90000);QSignalSpy painted(w.findChild<Canvas *>(),&Canvas::framePainted);QJsonArray controls;
        for(const auto &name:{"grade-hue","grade-saturation","grade-luminance","grade-blend","grade-balance","monochrome-mix"}){if(QString(name)=="monochrome-mix"){auto *enabled=w.findChild<QCheckBox *>("monochrome-enabled");const auto previous=ready.size();enabled->click();QTRY_VERIFY_WITH_TIMEOUT(ready.size()>previous,90000);QVERIFY(w.currentRecipe().monochrome);}auto *bar=w.findChild<QSlider *>("slider-"+QString(name));QVERIFY(bar->isEnabled());const auto before=ready.size(),paintBefore=painted.size();const auto initial=bar->value();bar->setSliderDown(true);for(int i=1;i<=40;++i){bar->setValue(initial+i*(QString(name)=="grade-hue"?100:1));QTest::qWait(50);}const auto frames=ready.size()-before,paints=painted.size()-paintBefore;const auto release=ready.size();bar->setSliderDown(false);QTRY_VERIFY_WITH_TIMEOUT(ready.size()>release,90000);QVERIFY(ready.last().at(1).toBool());QVERIFY(frames>=4);QVERIFY(paints>=4);controls.append(QJsonObject{{"control",name},{"duringDrag",frames},{"painted",paints}});qInfo()<<"Style 4K drag"<<name<<frames;}
        const auto report=qEnvironmentVariable("LUMINA_STYLE_REPORT");if(!report.isEmpty()){QFile file(report);QVERIFY(file.open(QIODevice::WriteOnly));file.write(QJsonDocument(controls).toJson());}
    }
    void gradingAndMonochromeGestures() {
        QTemporaryDir dir;QImage image(96,64,QImage::Format_RGB32);image.fill(QColor(160,100,70));const auto path=dir.path()+"/style.jpg";QVERIFY(image.save(path));Window w(nullptr,QStringLiteral(""));w.show();QVERIFY(openAllInspectorSections(w));w.addFiles({path});QSignalSpy ready(&w,&Window::previewChanged);QTRY_VERIFY(ready.size()>0);
        auto *range=w.findChild<QComboBox *>("grade-range");auto *sat=w.findChild<QSlider *>("slider-grade-saturation");range->setCurrentIndex(1);sat->setValue(30);sat->setValue(60);QMetaObject::invokeMethod(sat,"sliderReleased");QCOMPARE(w.currentRecipe().grading[1].saturation,.6);range->setCurrentIndex(2);QCOMPARE(w.currentRecipe().grading[2].saturation,0.);w.undoEdit();QCOMPARE(w.currentRecipe().grading[1].saturation,0.);w.redoEdit();QCOMPARE(w.currentRecipe().grading[1].saturation,.6);
        auto *enabled=w.findChild<QCheckBox *>("monochrome-enabled");auto *mix=w.findChild<QSlider *>("slider-monochrome-mix");QTRY_VERIFY(enabled->isEnabled());QVERIFY(!mix->isEnabled());enabled->click();QVERIFY(w.currentRecipe().monochrome);QVERIFY(mix->isEnabled());mix->setValue(40);QMetaObject::invokeMethod(mix,"sliderReleased");QCOMPARE(w.currentRecipe().monochromeMix[0],.4);w.findChild<QComboBox *>("monochrome-band")->setCurrentIndex(5);QCOMPARE(w.currentRecipe().monochromeMix[5],0.);w.undoEdit();QCOMPARE(w.currentRecipe().monochromeMix[0],0.);w.undoEdit();QVERIFY(!w.currentRecipe().monochrome);w.redoEdit();QVERIFY(w.currentRecipe().monochrome);QTRY_VERIFY(mix->isEnabled());
        const auto shot=qEnvironmentVariable("LUMINA_STYLE_SCREENSHOT");if(!shot.isEmpty()){auto *scroll=w.findChild<QScrollArea *>();scroll->ensureWidgetVisible(w.findChild<QDoubleSpinBox *>("grade-balance"));QTest::qWait(100);QVERIFY(w.grab().save(shot));scroll->ensureWidgetVisible(mix);QTest::qWait(50);QVERIFY(w.grab().save(shot+"-bw.png"));}
    }
    void productionWorkspaceLayout() {
        auto fileSha256=[](const QString &path){QFile file(path);if(!file.open(QIODevice::ReadOnly))return QByteArray();QCryptographicHash hash(QCryptographicHash::Sha256);if(!hash.addData(&file))return QByteArray();return hash.result();};
        QTemporaryDir dir;QString path=qEnvironmentVariable("LUMINA_UI_SAMPLE");if(path.isEmpty()){QImage image(960,640,QImage::Format_RGB32);image.fill(QColor(110,130,150));path=dir.path()+"/workspace.jpg";QVERIFY(image.save(path));}path=QFileInfo(path).canonicalFilePath();const auto before=fileSha256(path);
        Window w(nullptr,QStringLiteral(""));QCOMPARE(w.windowTitle(),QString("LuminaS"));auto *brand=w.findChild<QLabel *>("brand");QVERIFY(brand);QCOMPARE(brand->text(),QString("LuminaS"));QCOMPARE(QFontInfo(brand->font()).family(),QString("Source Serif 4"));QVERIFY(!w.windowIcon().isNull());w.show();QVERIFY(openAllInspectorSections(w));w.addFiles({path});QSignalSpy ready(&w,&Window::previewChanged);QTRY_VERIFY_WITH_TIMEOUT(ready.size()>0,90000);Recipe r;r.shadows=.2;r.hsl[1].saturation=.1;r.curves[0]={{0,0},{.5,.65},{1,1}};w.applyRecipe(r);const auto previous=ready.size();QTRY_VERIFY_WITH_TIMEOUT(ready.size()>previous,90000);
        auto *strip=w.filmstrip();auto *toggle=w.findChild<QToolButton *>("filmstrip-toggle");auto *crop=w.findChild<QToolButton *>("tool-crop");auto *hand=w.findChild<QToolButton *>("tool-hand");QVERIFY(toggle&&crop&&hand);const auto screen=QGuiApplication::primaryScreen()->geometry().size();w.resize(qMin(1000,screen.width()-16),qMin(700,screen.height()-32));QTest::qWait(30);
        toggle->click();QVERIFY(!strip->isVisible());toggle->click();QVERIFY(strip->isVisible());QCOMPARE(w.currentRecipe(),r);
        crop->click();QVERIFY(crop->isChecked());QVERIFY(!hand->isChecked());hand->click();QVERIFY(hand->isChecked());QVERIFY(!crop->isChecked());hand->click();QVERIFY(hand->isChecked());QCOMPARE(w.currentRecipe(),r);
        auto *card=w.findChild<QToolButton *>("card-header");QVERIFY(card);card->click();QVERIFY(!card->isChecked());card->click();QVERIFY(card->isChecked());QCOMPARE(w.currentRecipe(),r);
        auto *dock=w.findChild<QDockWidget *>("adjustment-dock");w.findChild<QAction *>("workspace-browse")->trigger();QVERIFY(!dock->isVisible());w.findChild<QAction *>("workspace-edit")->trigger();QVERIFY(dock->isVisible());QCOMPARE(w.currentRecipe(),r);
        const auto output=qEnvironmentVariable("LUMINA_UI_REPORT_DIR");if(!output.isEmpty())QVERIFY(QDir().mkpath(output));QJsonArray reports;
        for(const auto physical:{QSize(1366,768),QSize(1920,1080)}){const double dpr=w.devicePixelRatioF();const QSize logical(int(physical.width()/dpr)-16,int(physical.height()/dpr)-48);w.resize(logical);QTest::qWait(100);w.resize(logical);QTest::qWait(50);QCOMPARE(w.size(),logical);auto *bar=w.findChild<QToolBar *>("stage-toolbar");auto *canvas=w.findChild<Canvas *>();QVERIFY(bar&&canvas);QVERIFY(canvas->width()>=280);QVERIFY(canvas->height()>=120);QVERIFY(w.centralWidget()->rect().contains(canvas->geometry()));QVERIFY(!w.findChild<QToolButton *>("qt_toolbar_ext_button")->isVisible());
            reports.append(QJsonObject{{"physicalWidth",physical.width()},{"physicalHeight",physical.height()},{"logicalWidth",w.width()},{"logicalHeight",w.height()},{"dpr",dpr},{"canvasWidth",canvas->width()},{"canvasHeight",canvas->height()},{"filmstripVisible",strip->isVisible()}});
            if(!output.isEmpty()){const QString prefix=output+QStringLiteral("/workspace-%1x%2").arg(physical.width()).arg(physical.height());QVERIFY(w.grab().save(prefix+"-dark.png"));w.findChild<QAction *>("theme-toggle")->trigger();QTest::qWait(50);QVERIFY(w.grab().save(prefix+"-light.png"));w.findChild<QAction *>("theme-toggle")->trigger();w.findChild<QAction *>("workspace-browse")->trigger();QTest::qWait(50);QVERIFY(w.grab().save(prefix+"-browse.png"));w.findChild<QAction *>("workspace-edit")->trigger();}
        }
        if(!output.isEmpty()){QFile file(output+"/layout.json");QVERIFY(file.open(QIODevice::WriteOnly));file.write(QJsonDocument(reports).toJson());}QCOMPARE(fileSha256(path),before);
    }
    void curvesHslAndRecipeValidation() {
        const CurveSpline spline(Curve{{0,0},{.5,.75},{1,1}});QVERIFY(std::abs(spline.value(.25)-.375)>.01);
        for(int i=0;i<=1000;++i){const double x=i/1000.;QVERIFY(spline.value(x)>=0&&spline.value(x)<=1);if(i)QVERIFY(spline.value(x)>=spline.value((i-1)/1000.));}
        const double left=(spline.value(.5)-spline.value(.5-1e-5))/1e-5,right=(spline.value(.5+1e-5)-spline.value(.5))/1e-5;QVERIFY(std::abs(left-right)<.001);
        auto s=frame({1,1});auto *p=reinterpret_cast<float *>(s.linear.bits());p[0]=p[1]=p[2]=.214041f;QString error;Recipe r;r.curves[0]={{0,0},{.5,.75},{1,1}};
        auto out=renderLinear(s,r,0,error);QVERIFY(error.isEmpty());auto *v=reinterpret_cast<const float *>(out.constBits());QVERIFY(std::abs(v[0]-.522522f)<.001);QVERIFY(std::abs(v[0]-v[1])<1e-6);
        r={};r.curves[1]={{0,0},{.5,.75},{1,1}};out=renderLinear(s,r,0,error);v=reinterpret_cast<const float *>(out.constBits());QVERIFY(v[0]>.5);QVERIFY(std::abs(v[1]-.214041f)<1e-6);
        p[0]=1;p[1]=p[2]=0;r={};r.hsl[0].hue=1;out=renderLinear(s,r,0,error);v=reinterpret_cast<const float *>(out.constBits());QVERIFY(std::abs(v[0]-1)<1e-6);QVERIFY(std::abs(v[1]-.5)<1e-6);QVERIFY(std::abs(v[2])<1e-6);
        r={};r.hsl[0].saturation=-1;out=renderLinear(s,r,0,error);v=reinterpret_cast<const float *>(out.constBits());QVERIFY(std::abs(v[0]-.5)<1e-6);QVERIFY(std::abs(v[0]-v[1])<1e-6);
        p[0]=p[1]=0;p[2]=1;r={};r.hsl[5].luminance=1;out=renderLinear(s,r,0,error);v=reinterpret_cast<const float *>(out.constBits());QVERIFY(std::abs(v[2]-2)<1e-6);
        p[0]=p[1]=.2f;p[2]=.2001f;out=renderLinear(s,r,0,error);v=reinterpret_cast<const float *>(out.constBits());QVERIFY(std::abs(v[2]-.2001f)<.001);
        r.curves[0]={{0,0},{.5,.7},{1,1}};QCOMPARE(recipeFromJson(recipeJson(r)),r);QVERIFY(validRecipe(r));QVERIFY(hasColorEditing(r));r.curves[0]={{0,0},{.5,.7},{.5,.8},{1,1}};QVERIFY(!validRecipe(r));
        auto json=recipeJson(Recipe{});json.insert("hsl",QJsonArray{});QVERIFY(!validRecipe(recipeFromJson(json)));json=recipeJson(Recipe{});json.insert("curves",QJsonArray{});QVERIFY(!validRecipe(recipeFromJson(json)));
        p[0]=-.2f;p[1]=2;p[2]=.5;r={};r.curves[0]={{0,0},{.5,.7},{1,1}};out=renderLinear(s,r,0,error);v=reinterpret_cast<const float *>(out.constBits());QVERIFY(v[0]<0);QVERIFY(v[1]>1);
    }
    void selectiveToneAndVibrance() {
        auto s=frame({6,1});float levels[]{-.1f,0,.01f,.18f,.8f,2.f};auto *pixels=reinterpret_cast<float *>(s.linear.bits());
        for(int x=0;x<6;++x){for(int c=0;c<3;++c)pixels[x*4+c]=levels[x];pixels[x*4+3]=1;}
        QString error;Recipe r;r.shadows=1;auto out=renderLinear(s,r,0,error);QVERIFY(error.isEmpty());const auto *p=reinterpret_cast<const float *>(out.constBits());
        QVERIFY(p[8]>.02f);QVERIFY(p[16]/.8f<p[8]/.01f);QVERIFY(p[0]<0);QCOMPARE(p[4],0.f);
        r={};r.highlights=-1;out=renderLinear(s,r,0,error);p=reinterpret_cast<const float *>(out.constBits());QVERIFY(p[20]<2);QVERIFY(p[20]/2<p[8]/.01f);
        r={};r.whites=1;out=renderLinear(s,r,0,error);p=reinterpret_cast<const float *>(out.constBits());QVERIFY(p[20]>2);QVERIFY(p[20]/2>p[8]/.01f);
        r={};r.blacks=-1;out=renderLinear(s,r,0,error);p=reinterpret_cast<const float *>(out.constBits());QVERIFY(p[8]<.01f);QVERIFY(p[8]/.01f<p[16]/.8f);
        s=frame({3,1});pixels=reinterpret_cast<float *>(s.linear.bits());const float colors[]{.3f,.25f,.25f,1,1,0,0,1,.3f,.3f,.3f,1};std::copy(std::begin(colors),std::end(colors),pixels);
        r={};r.vibrance=1;out=renderLinear(s,r,0,error);p=reinterpret_cast<const float *>(out.constBits());QVERIFY(p[0]-p[1]>.05f);QCOMPARE(p[4],1.f);QCOMPARE(p[5],0.f);QVERIFY(std::abs(p[8]-.3f)<1e-6);
        r.highlights=.2;r.shadows=-.3;r.whites=.1;r.blacks=-.2;QCOMPARE(recipeFromJson(recipeJson(r)),r);QVERIFY(validRecipe(r));r.whites=1.01;QVERIFY(!validRecipe(r));r.whites=std::numeric_limits<double>::quiet_NaN();QVERIFY(!validRecipe(r));
    }
    void iccGammaAndRoundTrip() {
        QCOMPARE(LCMS_VERSION,2190);QCOMPARE(cmsGetEncodedCMMversion(),LCMS_VERSION);QImage encoded(1,1,QImage::Format_RGBA8888);encoded.fill(QColor(128,128,128));QString error;
        auto work=inputToWork(encoded,{},error);QVERIFY2(error.isEmpty(),qPrintable(error));QVERIFY(!work.isNull());
        const auto *p=reinterpret_cast<const float *>(work.constBits());QVERIFY(std::abs(p[0]-.21586f)<.0005f);QVERIFY(std::abs(p[1]-.21586f)<.0005f);QCOMPARE(p[3],1.f);
        const auto back=workToSrgb(work,error);QVERIFY(error.isEmpty());QVERIFY(std::abs(back.pixelColor(0,0).red()-128)<=1);
        QVERIFY(inputToWork(encoded,"invalid ICC",error).isNull());QVERIFY(!error.isEmpty());
    }
    void floatHeadroomAndInvalidValues() {
        auto s=frame({1,1});auto *p=reinterpret_cast<float *>(s.linear.bits());p[0]=-.25f;p[1]=2;p[2]=.5f;
        Recipe r;r.exposure=1;QString error;const auto out=renderLinear(s,r,0,error);QVERIFY(error.isEmpty());
        auto *v=reinterpret_cast<const float *>(out.constBits());QVERIFY(std::abs(v[0]+.5f)<1e-6);QVERIFY(std::abs(v[1]-4)<1e-6);QCOMPARE(p[1],2.f);
        r.exposure=std::numeric_limits<double>::infinity();QVERIFY(renderLinear(s,r,0,error).isNull());
        r={};p[0]=std::numeric_limits<float>::quiet_NaN();QVERIFY(renderLinear(s,r,0,error).isNull());
    }
    void fastSdrMatchesLcmsReference() {
        QImage image(65536,1,QImage::Format_RGBA32FPx4);auto *p=reinterpret_cast<float *>(image.bits());quint32 state=12345;
        for(int i=0;i<65536;++i){for(int c=0;c<3;++c){state=state*1664525+1013904223;p[i*4+c]=float(state&65535)/32767.5f-.5f;}p[i*4+3]=float(i)/65535;}
        p[0]=p[1]=p[2]=0;QString error;const auto fast=workToSrgb(image,error),reference=workToSrgbReference(image,error);QVERIFY2(error.isEmpty(),qPrintable(error));QVERIFY(!fast.isNull());
        int maxError=0;for(qsizetype i=0;i<fast.sizeInBytes();++i)maxError=std::max(maxError,std::abs(int(fast.constBits()[i])-int(reference.constBits()[i])));
        qInfo()<<"Fast SDR max byte error versus Little CMS"<<maxError;QVERIFY(maxError<=1);
    }
    void fusedPreviewMatchesFloatReference() {
        auto s=frame({100,80});s.draftBalance={1.05f,.02f,0,0,.95f,0,0,-.01f,1.1f};
        for(double ev:{-2.,0.,1.}){Recipe r;r.exposure=ev;r.contrast=.2;r.saturation=.6;r.warmth=.2;r.tint=-.1;QString error;
            r.highlights=-.4;r.shadows=.3;r.whites=.1;r.blacks=-.2;r.vibrance=.5;
            r.curves[0]={{0,0},{.4,.5},{1,1}};r.curves[2]={{0,0},{.5,.45},{1,1}};r.hsl[1]={.2,-.2,.2};r.hsl[5]={-.1,.2,-.1};
            r.grading[0]={210,.2,.1};r.grading[2]={40,.1,-.1};r.gradingBalance=.2;r.gradingBlend=.6;r.monochrome=ev==0;r.monochromeMix[0]=.4;r.monochromeMix[5]=-.3;
            const auto linear=renderLinear(s,r,0,error),reference=workToSrgbReference(linear,error);const auto fast=renderFrame(s,r,0);QVERIFY2(fast.error.isEmpty(),qPrintable(fast.error));
            int maxError=0;for(qsizetype i=0;i<fast.image.sizeInBytes();++i)maxError=std::max(maxError,std::abs(int(fast.image.constBits()[i])-int(reference.constBits()[i])));QVERIFY(maxError<=1);
        }
    }
    void sourceGeometryAndAlpha() {
        const auto s=frame({8,6});QString error;Recipe r;r.crop={.25,0,.5,1};auto out=renderLinear(s,r,0,error);QVERIFY(error.isEmpty());QCOMPARE(out.size(),QSize(4,6));
        QVERIFY(std::abs(reinterpret_cast<const float *>(out.constBits())[0]-.25f)<1e-6);
        r={};out=renderLinear(s,r,4,error);QCOMPARE(out.size(),QSize(4,3));auto *p=reinterpret_cast<const float *>(out.constBits());QVERIFY(std::abs(p[0]-.0625f)<1e-6);QVERIFY(std::abs(p[1]-1.f/12)<1e-6);
        r.angle=10;QCOMPARE(outputToSource({.5,.5},s.linear.size(),r),QPointF(4,3));
        auto alpha=frame({2,1});auto *a=reinterpret_cast<float *>(alpha.linear.bits());a[0]=100;a[1]=0;a[2]=0;a[3]=0;a[4]=0;a[5]=1;a[6]=0;a[7]=1;
        out=renderLinear(alpha,{},1,error);p=reinterpret_cast<const float *>(out.constBits());QVERIFY(std::abs(p[0])<1e-6);QVERIFY(std::abs(p[1]-1)<1e-6);QCOMPARE(p[3],.5f);
    }
    void sharedHistoryAndIndependentPhotos() {
        EditHistory h;Recipe a;a.exposure=1;Recipe b;b.saturation=.5;QVERIFY(h.commit("a",a,"曝光"));QVERIFY(h.commit("b",b,"饱和度"));
        QCOMPARE(h.undo(),QString("b"));QCOMPARE(h.value("b"),Recipe{});QCOMPARE(h.value("a"),a);QCOMPARE(h.undo(),QString("a"));QCOMPARE(h.redo(),QString("a"));
        QVERIFY(h.commit("b",b,"新调整"));QVERIFY(h.redo().isEmpty());
    }
    void previewAndFullUseSameCoordinates() {
        auto s=frame({100,80});Recipe r;r.crop={.1,.1,.8,.8};r.angle=13;r.exposure=.5;r.contrast=.2;r.saturation=.8;
        QString error;const auto full=renderLinear(s,r,0,error),preview=renderLinear(s,r,40,error);QVERIFY2(error.isEmpty(),qPrintable(error));QCOMPARE(full.size(),QSize(80,64));QCOMPARE(preview.size(),QSize(40,32));
        for(int y:{10,16,22})for(int x:{10,20,30}){const auto *p=reinterpret_cast<const float *>(preview.constScanLine(y))+4*x;
            for(int c=0;c<3;++c){float average=0;for(int j=0;j<2;++j)for(int i=0;i<2;++i)average+=reinterpret_cast<const float *>(full.constScanLine(y*2+j))[(x*2+i)*4+c]/4;
                QVERIFY(std::abs(p[c]-average)<1e-5);}
        }
    }
    void realR7Edits() {
        const auto dir=QDir::fromNativeSeparators(qEnvironmentVariable("LUMINA_REAL_SAMPLES"));if(dir.isEmpty())QSKIP("Optional private R7 samples not enabled");
        QStringList paths;for(const auto &name:{"4X1A0705","4X1A9413","4X1A9579","4X1A9817","4X1A9827","4X1A9954"})paths<<dir+"/"+name+".CR3"<<dir+"/"+name+".JPG";
        Window w(nullptr,QStringLiteral(""));w.show();QVERIFY(openAllInspectorSections(w));w.addFiles(paths);QSignalSpy ready(&w,&Window::previewChanged);
        for(int i=0;i<paths.size();++i){w.activate(i);QTRY_COMPARE_WITH_TIMEOUT(w.displayedPath(),paths[i],90000);
            qInfo()<<"R7 source ready"<<i<<QFileInfo(paths[i]).fileName();
            Recipe r;r.exposure=.35+i*.02;r.contrast=.1;r.saturation=.9;r.crop={.1,.1,.8,.8};r.angle=2;
            r.highlights=-.25;r.shadows=.3;r.whites=.1;r.blacks=-.1;r.vibrance=.3;
            ready.clear();w.applyRecipe(r);QTRY_VERIFY_WITH_TIMEOUT(ready.count()>0,90000);QVERIFY(ready.last().at(1).toBool());
            if(w.currentRecipe()!=r){const auto actual=recipeJson(w.currentRecipe()),expected=recipeJson(r);for(auto it=expected.begin();it!=expected.end();++it)if(actual.value(it.key())!=it.value())qInfo().noquote()<<"Recipe mismatch"<<it.key()<<QByteArray::number(actual.value(it.key()).toDouble(),'g',17)<<QByteArray::number(it.value().toDouble(),'g',17);}
            QCOMPARE(w.currentRecipe(),r);
        }
        w.activate(0);QCOMPARE(w.currentRecipe().exposure,.35);ready.clear();auto r=w.currentRecipe();r.warmth=.15;r.tint=-.1;w.applyRecipe(r);
        QTRY_VERIFY_WITH_TIMEOUT(ready.count()>0,90000);QVERIFY(ready.last().at(1).toBool());w.undoEdit();QCOMPARE(w.currentRecipe().warmth,0.);w.redoEdit();QCOMPARE(w.currentRecipe().warmth,.15);
        for(int i=0;i<30;++i)w.activate(i%paths.size());w.activate(4);QTRY_COMPARE_WITH_TIMEOUT(w.displayedPath(),paths[4],90000);
        const auto shot=qEnvironmentVariable("LUMINA_S2_SCREENSHOT");if(!shot.isEmpty())QVERIFY(w.grab().save(shot));
    }
    void realAdvancedColor() {
        const auto dir=QDir::fromNativeSeparators(qEnvironmentVariable("LUMINA_REAL_SAMPLES"));if(dir.isEmpty())QSKIP("Optional private R7 curve/HSL test");QStringList paths;for(const auto &name:{"4X1A0705","4X1A9413","4X1A9579","4X1A9817","4X1A9827","4X1A9954"})paths<<dir+"/"+name+".CR3"<<dir+"/"+name+".JPG";
        Window w(nullptr,QStringLiteral(""));w.show();QVERIFY(openAllInspectorSections(w));auto *loader=w.findChild<EditLoader *>();int count=0;EditResult result;connect(loader,&EditLoader::ready,&w,[&](const EditResult &r){++count;result=r;});w.addFiles(paths);
        for(int i=0;i<paths.size();++i){count=0;w.activate(i);QTRY_VERIFY_WITH_TIMEOUT(count>0&&result.path==QFileInfo(paths[i]).canonicalFilePath(),90000);QVERIFY2(result.error.isEmpty(),qPrintable(result.error));count=0;Recipe r;r.curves[i%4]={{0,0},{.3,.35},{.7,.75},{1,1}};r.hsl[1]={.2,-.2,.1};r.hsl[5]={-.1,.2,-.1};w.applyRecipe(r);QTRY_VERIFY_WITH_TIMEOUT(count>0&&result.recipe==r,90000);QVERIFY2(result.error.isEmpty(),qPrintable(result.error));QVERIFY(!result.sourceRebuilt);if(w.currentRecipe()!=r)qInfo().noquote()<<"actual"<<QJsonDocument(recipeJson(w.currentRecipe())).toJson(QJsonDocument::Compact)<<"expected"<<QJsonDocument(recipeJson(r)).toJson(QJsonDocument::Compact);QCOMPARE(w.currentRecipe(),r);QCOMPARE(qMax(result.image.width(),result.image.height()),3840);}
        count=0;w.activate(0);QTRY_VERIFY_WITH_TIMEOUT(count>0,90000);QCOMPARE(w.currentRecipe().curves[0].size(),4);w.undoEdit();QCOMPARE(w.currentIndex(),11);QCOMPARE(w.currentRecipe(),Recipe{});w.redoEdit();QCOMPARE(w.currentRecipe().hsl[1].saturation,-.2);
    }
    void embeddedProfileAndUnicodePath() {
        QTemporaryDir dir;QImage image(64,48,QImage::Format_RGB32);image.fill(QColor(100,80,60));image.setColorSpace(QColorSpace::AdobeRgb);
        const auto path=dir.path()+QStringLiteral("/中文色彩.jpg");QVERIFY(image.save(path,"JPEG",95));const auto result=readTestSource(path);
        QVERIFY2(result.error.isEmpty(),qPrintable(result.error));QCOMPARE(result.source->linear.size(),image.size());QVERIFY(result.source->facts.value("embeddedIccBytes").toInt()>0);
    }
    void windowGesturesAndSwitching() {
        QTemporaryDir dir;QImage image(64,48,QImage::Format_RGB32);image.fill(QColor(120,80,60));const auto a=dir.path()+"/a.jpg",b=dir.path()+"/b.jpg";QVERIFY(image.save(a));QVERIFY(image.save(b));
        Window w(nullptr,QStringLiteral(""));w.show();QVERIFY(openAllInspectorSections(w));w.addFiles({a,b});w.activate(0);QTRY_COMPARE(w.displayedPath(),a);auto *slider=w.findChild<QSlider *>("slider-exposure");QVERIFY(slider);
        slider->setValue(20);slider->setValue(60);slider->setValue(100);QMetaObject::invokeMethod(slider,"sliderReleased");QCOMPARE(w.currentRecipe().exposure,1.);
        w.undoEdit();QCOMPARE(w.currentRecipe().exposure,0.);w.redoEdit();QCOMPARE(w.currentRecipe().exposure,1.);
        w.activate(1);QCOMPARE(w.currentRecipe().exposure,0.);w.activate(0);QCOMPARE(w.currentRecipe().exposure,1.);
        QSignalSpy ready(&w,&Window::previewChanged);w.compareOriginal(true);QTRY_VERIFY(ready.count()>0);QVERIFY(!slider->isEnabled());QCOMPARE(w.currentRecipe().exposure,1.);
        ready.clear();w.compareOriginal(false);QTRY_VERIFY(ready.count()>0);QVERIFY(slider->isEnabled());QCOMPARE(w.currentRecipe().exposure,1.);
        slider->setValue(150);w.activate(1);w.activate(0);QCOMPARE(w.currentRecipe().exposure,1.5);w.undoEdit();QCOMPARE(w.currentRecipe().exposure,1.);
        w.applyRecipe({});QCOMPARE(w.currentRecipe(),Recipe{});QTRY_COMPARE(w.displayedPath(),a);
        ready.clear();auto *shadow=w.findChild<QSlider *>("slider-shadows");shadow->setValue(50);QMetaObject::invokeMethod(shadow,"sliderReleased");QCOMPARE(w.currentRecipe().shadows,.5);QTRY_VERIFY(ready.count()>0);
        auto *dock=w.findChild<QDockWidget *>("adjustment-dock");QVERIFY(dock);w.findChild<QAction *>("workspace-browse")->trigger();QVERIFY(!dock->isVisible());QCOMPARE(w.currentRecipe().shadows,.5);
        w.findChild<QAction *>("workspace-edit")->trigger();QVERIFY(dock->isVisible());QCOMPARE(w.currentRecipe().shadows,.5);
        w.findChild<QPushButton *>("reset-shadows")->click();QCOMPARE(w.currentRecipe().shadows,0.);w.undoEdit();QCOMPARE(w.currentRecipe().shadows,.5);
        QTRY_VERIFY_WITH_TIMEOUT(shadow->isEnabled(),30000);QTest::qWait(50);
        const auto screenshot=qEnvironmentVariable("LUMINA_S2_SCREENSHOT");if(!screenshot.isEmpty()){
            w.resize(1366,768);QTest::qWait(100);QVERIFY(w.grab().save(screenshot));
            w.findChild<QAction *>("theme-toggle")->trigger();QTest::qWait(50);QVERIFY(w.grab().save(screenshot+"-light.png"));
            w.findChild<QAction *>("workspace-browse")->trigger();QTest::qWait(50);QVERIFY(w.grab().save(screenshot+"-browse.png"));
        }
    }
    void textFocusImeAndEscape() {
        QTemporaryDir dir;QImage image(32,24,QImage::Format_RGB32);image.fill(Qt::gray);const auto path=dir.path()+"/text.jpg";QVERIFY(image.save(path));
        Window w(nullptr,QStringLiteral(""));w.show();QVERIFY(openAllInspectorSections(w));w.addFiles({path});w.activate(0);QTRY_COMPARE(w.displayedPath(),path);
        Recipe r;r.exposure=1;w.applyRecipe(r);QSignalSpy ready(&w,&Window::previewChanged);QTRY_VERIFY(ready.count()>0);
        auto *spin=w.findChild<QDoubleSpinBox *>("exposure");auto *text=spin->findChild<QLineEdit *>();QVERIFY(text);text->setFocus();text->selectAll();QTest::keyClicks(text,"-1.25");QTest::keyClick(text,Qt::Key_Return);
        QCOMPARE(w.currentRecipe().exposure,-1.25);QTest::keyClick(text,Qt::Key_Z,Qt::ControlModifier);QCOMPARE(w.currentRecipe().exposure,-1.25);
        QInputMethodEvent composition(QStringLiteral("测试"),{});QApplication::sendEvent(text,&composition);QCOMPARE(w.currentRecipe().exposure,-1.25);
        QInputMethodEvent cancel;QApplication::sendEvent(text,&cancel);w.findChild<Canvas *>()->setFocus();
        spin->setValue(2);QCOMPARE(w.currentRecipe().exposure,2.);QTest::keyClick(&w,Qt::Key_Escape);QTRY_COMPARE(w.currentRecipe().exposure,-1.25);
        w.undoEdit();QCOMPARE(w.currentRecipe().exposure,1.);
    }
    void editingOneControlPreservesOtherPrecision() {
        QTemporaryDir dir;QImage image(32,24,QImage::Format_RGB32);image.fill(Qt::gray);const auto path=dir.path()+"/precision.jpg";QVERIFY(image.save(path));
        Window w(nullptr,QStringLiteral(""));w.show();QVERIFY(openAllInspectorSections(w));w.addFiles({path});QSignalSpy ready(&w,&Window::previewChanged);QTRY_VERIFY(ready.count()>0);
        Recipe r;r.exposure=.35+6*.02;r.contrast=.12345;r.highlights=-.25;r.shadows=.3;r.vibrance=.3;ready.clear();w.applyRecipe(r);QTRY_VERIFY(ready.count()>0);QCOMPARE(w.currentRecipe(),r);
        auto *bar=w.findChild<QSlider *>("slider-shadows");bar->setValue(50);QMetaObject::invokeMethod(bar,"sliderReleased");QCOMPARE(w.currentRecipe().exposure,r.exposure);QCOMPARE(w.currentRecipe().contrast,r.contrast);QCOMPARE(w.currentRecipe().shadows,.5);
        w.undoEdit();QCOMPARE(w.currentRecipe(),r);
    }
    void curveAndHslGestures() {
        QTemporaryDir dir;QImage image(96,64,QImage::Format_RGB32);image.fill(QColor(180,80,40));auto path=dir.path()+"/curves.jpg";QVERIFY(image.save(path));Window w(nullptr,QStringLiteral(""));w.show();QVERIFY(openAllInspectorSections(w));w.addFiles({path});QSignalSpy ready(&w,&Window::previewChanged);QTRY_VERIFY(ready.size()>0);
        auto *curve=w.findChild<CurveEditor *>();QVERIFY(curve);auto *scroll=w.findChild<QScrollArea *>();scroll->ensureWidgetVisible(curve);QTest::qWait(50);
        const QPoint middle(curve->width()/2,curve->height()/2);QTest::mousePress(curve,Qt::LeftButton,Qt::NoModifier,middle);QTest::mouseMove(curve,middle-QPoint(0,30));QTest::mouseRelease(curve,Qt::LeftButton,Qt::NoModifier,middle-QPoint(0,30));QCOMPARE(w.currentRecipe().curves[0].size(),3);QVERIFY(w.currentRecipe().curves[0][1].y()>.5);w.undoEdit();QCOMPARE(w.currentRecipe().curves[0].size(),2);w.redoEdit();QCOMPARE(w.currentRecipe().curves[0].size(),3);
        auto *band=w.findChild<QComboBox *>("hsl-band");band->setCurrentIndex(1);auto *bar=w.findChild<QSlider *>("slider-hsl-saturation");bar->setValue(-30);QMetaObject::invokeMethod(bar,"sliderReleased");QCOMPARE(w.currentRecipe().hsl[1].saturation,-.3);band->setCurrentIndex(5);QCOMPARE(w.currentRecipe().hsl[5].saturation,0.);w.undoEdit();QCOMPARE(w.currentRecipe().hsl[1].saturation,0.);QCOMPARE(w.currentRecipe().curves[0].size(),3);
        const auto shot=qEnvironmentVariable("LUMINA_COLOR_SCREENSHOT");if(!shot.isEmpty()){scroll->ensureWidgetVisible(curve);QTest::qWait(100);QVERIFY(w.grab().save(shot));scroll->ensureWidgetVisible(w.findChild<QDoubleSpinBox *>("hsl-luminance"));QTest::qWait(50);QVERIFY(w.grab().save(shot+"-hsl.png"));}
    }
    void continuousCurveFeedback() {
        QTemporaryDir dir;QString path=qEnvironmentVariable("LUMINA_LIVE_SAMPLE");if(path.isEmpty()){QImage image(3840,2160,QImage::Format_RGB32);image.fill(QColor(130,80,60));path=dir.path()+"/curve4k.jpg";QVERIFY(image.save(path));}
        Window w(nullptr,QStringLiteral(""));w.show();QVERIFY(openAllInspectorSections(w));w.addFiles({path});QSignalSpy ready(&w,&Window::previewChanged);QTRY_VERIFY_WITH_TIMEOUT(ready.size()>0,90000);auto *curve=w.findChild<CurveEditor *>();w.findChild<QScrollArea *>()->ensureWidgetVisible(curve);QTest::qWait(50);
        auto *loader=w.findChild<EditLoader *>();QJsonArray times;connect(loader,&EditLoader::ready,&w,[&](const EditResult &r){if(r.tag.interactive){QCOMPARE(qMax(r.image.width(),r.image.height()),3840);times.append(r.elapsedMs);}});QSignalSpy painted(w.findChild<Canvas *>(),&Canvas::framePainted);
        const QPoint middle(curve->width()/2,curve->height()/2);const auto before=ready.size();QTest::mousePress(curve,Qt::LeftButton,Qt::NoModifier,middle);for(int i=1;i<=40;++i){QTest::mouseMove(curve,middle-QPoint(0,i));QTest::qWait(50);}const auto count=ready.size()-before,draws=painted.size();const auto releaseBefore=ready.size();QTest::mouseRelease(curve,Qt::LeftButton,Qt::NoModifier,middle-QPoint(0,40));QVERIFY(count>=4);QVERIFY(draws>=4);QTRY_VERIFY_WITH_TIMEOUT(ready.size()>releaseBefore,90000);QVERIFY(ready.last().at(1).toBool());QCOMPARE(w.currentRecipe().curves[0].size(),3);w.undoEdit();QCOMPARE(w.currentRecipe().curves[0].size(),2);
        const auto report=qEnvironmentVariable("LUMINA_CURVE_REPORT");if(!report.isEmpty()){QFile file(report);QVERIFY(file.open(QIODevice::WriteOnly));file.write(QJsonDocument(QJsonObject{{"dragFrames",count},{"painted",draws},{"workerMs",times}}).toJson());}
    }
    void deleteEditingWindowWhileFocused() {
        QTemporaryDir dir;QImage image(96,64,QImage::Format_RGB32);image.fill(Qt::gray);auto path=dir.path()+"/close-edit.jpg";QVERIFY(image.save(path));auto *w=new Window;w->show();w->addFiles({path});QTRY_COMPARE(w->displayedPath(),path);Recipe r;r.curves[0]={{0,0},{.5,.7},{1,1}};r.hsl[0].hue=.2;w->applyRecipe(r);w->findChild<QDoubleSpinBox *>("hsl-hue")->setFocus();delete w;QTest::qWait(50);
    }
    void continuous4kDragFeedback() {
        QTemporaryDir dir;const auto supplied=qEnvironmentVariable("LUMINA_LIVE_SAMPLE");QString path=supplied;
        if(path.isEmpty()){QImage image(3840,2160,QImage::Format_RGB32);image.fill(QColor(110,80,60));path=dir.path()+"/4k.jpg";QVERIFY(image.save(path));}
        path=QFileInfo(path).canonicalFilePath();Window w(nullptr,QStringLiteral(""));w.show();QVERIFY(openAllInspectorSections(w));w.addFiles({path});w.activate(0);QTRY_COMPARE_WITH_TIMEOUT(w.displayedPath(),path,90000);
        auto *slider=w.findChild<QSlider *>("slider-exposure");auto *loader=w.findChild<EditLoader *>();QVERIFY(slider);QVERIFY(loader);
        QJsonArray frames;QElapsedTimer clock;clock.start();QSignalSpy ready(&w,&Window::previewChanged);
        QSignalSpy painted(w.findChild<Canvas *>(),&Canvas::framePainted);
        connect(loader,&EditLoader::ready,&w,[&](const EditResult &r){if(r.tag.interactive){QCOMPARE(qMax(r.image.width(),r.image.height()),3840);frames.append(QJsonObject{{"atMs",double(clock.elapsed())},{"workerMs",r.elapsedMs}});}});
        QJsonArray controls;int during=0;
        for(const auto &name:{"exposure","contrast","saturation","warmth","tint","highlights","shadows","whites","blacks","vibrance","hsl-hue","hsl-saturation","hsl-luminance"}){
            slider=w.findChild<QSlider *>("slider-"+QString(name));QVERIFY(slider);const auto before=ready.size();const auto paintBefore=painted.size();const auto started=clock.elapsed();const int initial=slider->value();
            slider->setSliderDown(true);for(int i=1;i<=40;++i){slider->setValue(initial+i);QTest::qWait(50);}
            const auto count=ready.size()-before,paintCount=painted.size()-paintBefore;during+=count;const auto atRelease=ready.size();slider->setSliderDown(false);
            QTRY_VERIFY_WITH_TIMEOUT(ready.size()>atRelease,90000);QVERIFY(ready.last().at(1).toBool());
            controls.append(QJsonObject{{"control",name},{"duringDrag",count},{"paintedDuringDrag",paintCount},{"durationIncludingReleaseMs",double(clock.elapsed()-started)}});QVERIFY(paintCount>=4);
            qInfo()<<"Continuous 4K drag"<<name<<"frames"<<count;QVERIFY2(count>=4,"4K drag must produce multiple updates before release");
        }
        QCOMPARE(w.currentRecipe().exposure,.4);
        auto *angle=w.findChild<QSlider *>("slider-angle");const auto paintBefore=painted.size();const auto releaseBefore=ready.size();
        angle->setSliderDown(true);for(int i=1;i<=40;++i){angle->setValue(i*10);QTest::qWait(25);}const auto anglePaints=painted.size()-paintBefore;angle->setSliderDown(false);
        QTRY_VERIFY_WITH_TIMEOUT(ready.size()>releaseBefore,90000);QCOMPARE(w.currentRecipe().angle,4.);QVERIFY(anglePaints>=20);
        controls.append(QJsonObject{{"control","angle"},{"paintedDuringDrag",anglePaints},{"mode","cached canvas transform; source resample on release"}});
        const auto report=qEnvironmentVariable("LUMINA_LIVE_REPORT");if(!report.isEmpty()){QFile file(report);QVERIFY(file.open(QIODevice::WriteOnly));file.write(QJsonDocument(QJsonObject{{"frames",frames},{"controls",controls},{"duringDrag",during},{"durationMs",double(clock.elapsed())},{"source",QFileInfo(path).fileName()}}).toJson());}
    }
    void importPreloadsAndClickUsesCache() {
        QTemporaryDir dir;QImage image(96,64,QImage::Format_RGB32);image.fill(Qt::cyan);const auto a=dir.path()+"/a.jpg",b=dir.path()+"/b.jpg",bad=dir.path()+"/bad.jpg";
        QVERIFY(image.save(a));image.fill(Qt::yellow);QVERIFY(image.save(b));QFile file(bad);QVERIFY(file.open(QIODevice::WriteOnly));file.write("invalid");file.close();
        Window w(nullptr,QStringLiteral(""));w.show();QVERIFY(openAllInspectorSections(w));auto *loader=w.findChild<EditLoader *>();QSignalSpy prepared(loader,&EditLoader::prefetched),ready(loader,&EditLoader::ready);w.addFiles({a,b,bad});
        QTRY_COMPARE_WITH_TIMEOUT(prepared.size(),3,30000);QCOMPARE(w.currentIndex(),0);QVERIFY(loader->preview(b,{}).has_value());
        ready.clear();QElapsedTimer timer;timer.start();w.activate(1);QCOMPARE(w.displayedPath(),b);qInfo()<<"Preparsed click immediate presentationMs"<<timer.elapsed();
        QTRY_VERIFY(ready.size()>0);const auto result=qvariant_cast<EditResult>(ready.last().at(0));QVERIFY(!result.sourceRebuilt);QVERIFY(result.error.isEmpty());
        ready.clear();Recipe r;r.exposure=.5;w.applyRecipe(r);QTRY_VERIFY(ready.size()>0);QVERIFY(!qvariant_cast<EditResult>(ready.last().at(0)).sourceRebuilt);
        QVERIFY(!loader->preview(b,{}).has_value());QVERIFY(loader->preview(b,r).has_value());
    }
    void floatPreviewCacheBudgetAndIdentity() {
        PreviewCache cache(2500);auto a=frame({8,6});a.originalSize={80,60};a.sourcePixelsPerPixel=10;a.path="a";a.sha256="1";QVERIFY(cache.put(a));auto got=cache.get("a","1");QVERIFY(got.has_value());QCOMPARE(got->linear,a.linear);QCOMPARE(got->originalSize,a.originalSize);
        QVERIFY(!cache.get("a","changed").has_value());auto b=a;b.path="b";b.sha256="2";QVERIFY(cache.put(b));auto c=a;c.path="c";c.sha256="3";QVERIFY(cache.put(c));QVERIFY(cache.usedBytes()<=2500);QVERIFY(!cache.get("a","1").has_value());
    }
    void realImportPreparesAllBeforeClicks() {
        const auto dir=QDir::fromNativeSeparators(qEnvironmentVariable("LUMINA_REAL_SAMPLES"));if(dir.isEmpty())QSKIP("Optional private R7 import cache test");
        QStringList paths;for(const auto &name:{"4X1A0705","4X1A9413","4X1A9579","4X1A9817","4X1A9827","4X1A9954"})paths<<dir+"/"+name+".CR3"<<dir+"/"+name+".JPG";
        Window w(nullptr,QStringLiteral(""));w.show();QVERIFY(openAllInspectorSections(w));auto *loader=w.findChild<EditLoader *>();QSignalSpy prepared(loader,&EditLoader::prefetched);int readyCount=0;EditResult lastResult;
        connect(loader,&EditLoader::ready,&w,[&](const EditResult &r){++readyCount;lastResult=r;});
        QElapsedTimer clock;clock.start();w.addFiles(paths);QTRY_COMPARE_WITH_TIMEOUT(w.displayedPath(),paths[0],90000);
        QJsonArray concurrentTimes;connect(loader,&EditLoader::ready,&w,[&](const EditResult &r){if(r.tag.interactive)concurrentTimes.append(r.elapsedMs);});
        auto *bar=w.findChild<QSlider *>("slider-exposure");const auto before=readyCount;bar->setSliderDown(true);
        for(int i=1;i<=40;++i){bar->setValue(i);QTest::qWait(50);}const auto concurrentFrames=readyCount-before;bar->setSliderDown(false);QVERIFY(concurrentFrames>=4);
        QTRY_VERIFY_WITH_TIMEOUT(readyCount>before+concurrentFrames,30000);readyCount=0;w.applyRecipe({});QTRY_VERIFY_WITH_TIMEOUT(readyCount>0,30000);
        QTRY_COMPARE_WITH_TIMEOUT(prepared.size(),12,240000);const auto warmMs=clock.elapsed();QCOMPARE(w.currentIndex(),0);
        for(const auto &path:paths)QVERIFY(loader->preview(path,{}).has_value());QJsonArray clicks;
        for(int click=0;click<paths.size();++click){const int i=(click+1)%paths.size();readyCount=0;qint64 firstNativePaintMs=-1,paintedKey=0;QObject paintObserver;
            // Every activation switches photos. Count only the canonical key of
            // this photo, rather than an old queued repaint or a loading frame.
            const auto cached=loader->preview(paths[i],{});QVERIFY(cached);QSet<qint64> photoKeys{cached->image.cacheKey()};
            connect(loader,&EditLoader::ready,&paintObserver,[&](const EditResult &r){if(r.path==paths[i]&&r.recipe==Recipe{}&&r.error.isEmpty())photoKeys.insert(r.image.cacheKey());});
            connect(w.findChild<Canvas *>(),&Canvas::canonicalFramePainted,&paintObserver,[&](qint64 key){if(firstNativePaintMs<0&&photoKeys.contains(key)){paintedKey=key;firstNativePaintMs=clock.elapsed();}});
            clock.restart();w.activate(i);QCOMPARE(w.displayedPath(),paths[i]);const auto immediateMs=clock.elapsed();QApplication::processEvents();const auto paintedMs=clock.elapsed();
            QTRY_VERIFY_WITH_TIMEOUT(firstNativePaintMs>=0,5000);
            QTRY_VERIFY_WITH_TIMEOUT(readyCount>0,30000);const auto result=lastResult;QVERIFY2(result.error.isEmpty(),qPrintable(result.error));QVERIFY(!result.sourceRebuilt);QCOMPARE(qMax(result.image.width(),result.image.height()),3840);
            clicks.append(QJsonObject{{"file",QFileInfo(paths[i]).fileName()},{"immediateMs",double(immediateMs)},{"paintDispatchMs",double(paintedMs)},{"firstNativePaintMs",double(firstNativePaintMs)},{"paintedCanonicalKey",QString::number(paintedKey)},{"canonicalPaintVerified",photoKeys.contains(paintedKey)},{"verifiedWorkerMs",result.elapsedMs},{"sourceRebuilt",result.sourceRebuilt}});
        }
        const auto report=qEnvironmentVariable("LUMINA_PREFETCH_REPORT");if(!report.isEmpty()){QFile file(report);QVERIFY(file.open(QIODevice::WriteOnly));file.write(QJsonDocument(QJsonObject{{"importCount",12},{"preparseMs",double(warmMs)},{"dragFramesWhilePrefetch",concurrentFrames},{"concurrentWorkerMs",concurrentTimes},{"clicks",clicks}}).toJson());}
        const auto shot=qEnvironmentVariable("LUMINA_S2_SCREENSHOT");if(!shot.isEmpty())QVERIFY(w.grab().save(shot));
    }
    void liveWhileRawPrecisionIsRunning() {
        const auto path=QFileInfo(qEnvironmentVariable("LUMINA_LIVE_SAMPLE")).canonicalFilePath();if(!path.endsWith(".CR3",Qt::CaseInsensitive))QSKIP("Optional real RAW precision overlap test");
        Window w(nullptr,QStringLiteral(""));w.show();QVERIFY(openAllInspectorSections(w));auto *loader=w.findChild<EditLoader *>();int readyCount=0;EditResult lastResult;
        connect(loader,&EditLoader::ready,&w,[&](const EditResult &r){++readyCount;lastResult=r;});w.addFiles({path});QTRY_VERIFY_WITH_TIMEOUT(readyCount>0,90000);
        auto *warm=w.findChild<QSlider *>("slider-warmth"),*exposure=w.findChild<QSlider *>("slider-exposure");warm->setSliderDown(true);warm->setValue(40);QTest::qWait(250);warm->setSliderDown(false);
        const auto before=readyCount;QJsonArray times;connect(loader,&EditLoader::ready,&w,[&](const EditResult &r){if(r.tag.interactive)times.append(r.elapsedMs);});
        exposure->setSliderDown(true);for(int i=1;i<=40;++i){exposure->setValue(i);QTest::qWait(50);}const auto frames=readyCount-before;exposure->setSliderDown(false);
        QVERIFY2(frames>=4,"RAW precision must not stop cached slider feedback");QTRY_VERIFY_WITH_TIMEOUT(readyCount>before+frames,90000);
        QVERIFY(lastResult.error.isEmpty());QCOMPARE(w.currentRecipe().warmth,.4);QCOMPARE(w.currentRecipe().exposure,.4);
        qInfo()<<"Frames while RAW precision running"<<frames<<"worker times"<<times;
    }
};
QTEST_MAIN(S2Tests)
#include "s2_tests.moc"
