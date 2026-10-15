// Lens-shading coverage: a parameter/source-domain suite plus a standalone
// QtTest driver that reaches the real Window surface. No radial model, no
// render maths and no sampling algorithm is restated here; every expectation
// is either the exact double the UI was handed, a property of the shipped
// operator, or a documented renderer/preset behaviour. The optional private
// sample is only handed to the window, never decoded in this file.
#include <QtTest/QtTest>
#include <QtTest/qtest_widgets.h>
#include <QTemporaryDir>
#include <QPainter>
#include <QSlider>
#include <QDoubleSpinBox>
#include <QScrollArea>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QPushButton>
#include <QSignalSpy>
#include <algorithm>
#include <cmath>
#include <limits>
#include "../src/app/window.h"
#include "../src/render/lens_shading_operator.h"
#include "../src/color/color_pipeline.h"
#include "../src/render/cpu_renderer.h"
#include "../src/render/geometry_transform.h"
#include "../src/core/presets.h"
#include "render/spatial_operator.h"

class LensShadingTests : public QObject {
    Q_OBJECT
private:
    static constexpr double floatTolerance=2e-6;
    QTemporaryDir m_sampleDir;
    // Uniform synthetic stand-in for the optional 4K source; no private photo
    // and no sample pixel assumption is made about either one.
    QString writeUniformJpeg(const QString &path) const{
        QImage sheet(6000,4000,QImage::Format_RGB32);
        sheet.fill(QColor(150,110,80));
        QPainter painter(&sheet);
        for(int x=0;x<sheet.width();x+=200)painter.drawLine(x,0,x,sheet.height()-1);
        for(int y=0;y<sheet.height();y+=200)painter.drawLine(0,y,sheet.width()-1,y);
        painter.end();
        return sheet.save(path,"JPG",95)?path:QString();
    }
    // The adjustment card is the parent of the form layout that owns the row,
    // and the card is the widget the scroll area can actually reveal.
    static QWidget *formCardOf(QWidget *field){
        if(!field)return nullptr;
        for(QWidget *widget=field->parentWidget();widget;widget=widget->parentWidget())
            if(widget->objectName()==QStringLiteral("adjustment-card"))return widget;
        return field->parentWidget();
    }
    static SourceFrame uniformFrame(int width=101,int height=101,float r=-.2f,float g=1.f,float b=2.f,float a=.4f){
        SourceFrame frame;
        frame.linear=QImage(width,height,QImage::Format_RGBA32FPx4);
        for(int y=0;y<height;++y){auto *p=reinterpret_cast<float *>(frame.linear.scanLine(y));
            for(int x=0;x<width;++x){p[x*4]=r;p[x*4+1]=g;p[x*4+2]=b;p[x*4+3]=a;}}
        return frame;
    }
    static QDoubleSpinBox *spin(Window &w,const char *name){return w.findChild<QDoubleSpinBox *>(QString::fromLatin1(name));}
    static QSlider *slider(Window &w,const char *name){return w.findChild<QSlider *>(QString::fromLatin1(name));}
    // Every delivered result must be error-free and must deliver the 3840 long
    // edge of its own full size. No sample-pixel assumption is made here.
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
private slots:
    void parameterAndSourceDomain();
    void windowControllerAndPersistence();
};

void LensShadingTests::parameterAndSourceDomain(){
    // ---- parameter domain: default, legacy JSON, roundtrip, finite gate ----
    QCOMPARE(Recipe{}.lensShading,0.);
    QCOMPARE(recipeFromJson(QJsonObject{}).lensShading,0.); // legacy payload without the key
    Recipe rounded;rounded.lensShading=.75;
    QCOMPARE(recipeFromJson(recipeJson(rounded)),rounded);
    QCOMPARE(recipeFromJson(recipeJson(rounded)).lensShading,.75);
    for(const QJsonValue &bad:{QJsonValue(QStringLiteral("0.5")),QJsonValue(QJsonArray{0}),QJsonValue(QJsonObject{}),QJsonValue(true)}){
        auto json=recipeJson(rounded);json["lensShading"]=bad;
        QVERIFY2(!validRecipe(recipeFromJson(json)),"a non-numeric lensShading must not become a recipe");
    }
    for(double outOfRange:{2.001,-2.001}){
        Recipe value;value.lensShading=outOfRange;
        QVERIFY2(!validRecipe(value),qPrintable(QStringLiteral("lensShading %1 must be rejected").arg(outOfRange)));
    }
    for(double notFinite:{std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()}){
        Recipe value;value.lensShading=notFinite;
        QVERIFY2(!validRecipe(value),"non-finite lensShading must be rejected");
        QVERIFY2(!validRecipe(recipeFromJson(recipeJson(value))),"non-finite lensShading must not round-trip");
    }

    // ---- source domain: the shipped radial compensation, queried not cloned ----
    const QSize source{101,101};
    const LensShading flat(source,0);
    QCOMPARE(flat.enabled(),false);
    QCOMPARE(flat.gain(50,50),1.f);
    const LensShading positive(source,1),negative(source,-1);
    QVERIFY(positive.enabled()&&negative.enabled());
    const QPoint corners[4]{{0,0},{100,0},{0,100},{100,100}};
    for(const QPoint &corner:corners)QVERIFY2(positive.gain(corner.x(),corner.y())>1.f,"corner gain must exceed one");
    for(const QPoint &edge:{QPoint(0,50),QPoint(100,50),QPoint(50,0),QPoint(50,100)})
        QVERIFY2(positive.gain(edge.x(),edge.y())<positive.gain(0,0),"mid-axis must stay below the corner");
    QVERIFY(positive.gain(50,50)<positive.gain(0,50));
    for(const QPoint &point:{QPoint(20,20),QPoint(80,20),QPoint(20,80),QPoint(80,80),QPoint(10,90),QPoint(90,10)})
        QCOMPARE(positive.gain(point.x(),point.y()),positive.gain(100-point.x(),100-point.y()));
    for(int x=0;x<101;x+=10)for(int y=0;y<101;y+=10)
        QVERIFY2(std::abs(positive.gain(x,y)*negative.gain(x,y)-1.f)<1e-6f,"opposite amounts must be reciprocal");

    SourceFrame frame=uniformFrame();
    const QImage original=frame.linear;
    Recipe shade;shade.lensShading=.75;
    QString error;
    const QImage linear=renderLinear(frame,shade,0,error);
    QVERIFY2(error.isEmpty(),qPrintable(error));
    QCOMPARE(frame.linear,original); // the source frame is immutable
    const LensShading model(source,.75);
    for(int y=0;y<linear.height();++y){
        const auto *p=reinterpret_cast<const float *>(linear.constScanLine(y));
        for(int x=0;x<linear.width();++x){
            const float gain=model.gain(x,y);
            QCOMPARE(p[x*4+3],.4f); // alpha passes through exactly
            QVERIFY2(std::abs(p[x*4]-(-.2f)*gain)<floatTolerance,"red keeps its sign and takes the gain");
            QVERIFY2(std::abs(p[x*4+1]-1.f*gain)<floatTolerance,"green follows the gain");
            QVERIFY2(std::abs(p[x*4+2]-2.f*gain)<floatTolerance,"HDR blue stays above one");
        }
    }
    Recipe zero;zero.lensShading=0;
    const auto baseline=renderLinear(frame,zero,0,error);
    QVERIFY(error.isEmpty()&&!baseline.isNull());QCOMPARE(baseline,renderLinear(frame,Recipe{},0,error));
    for(int y=0;y<baseline.height();++y){
        const auto *p=reinterpret_cast<const float *>(baseline.constScanLine(y));
        for(int x=0;x<baseline.width();++x){
            QVERIFY(std::abs(p[x*4]+.2f)<1e-7f);QCOMPARE(p[x*4+1],1.f);QCOMPARE(p[x*4+2],2.f);QCOMPARE(p[x*4+3],.4f);
        }
    }

    // ---- crop: the correction belongs to the source plane, not the frame ----
    // Integer correspondence only: odd 101 with 20/101 and 61/101 gives a 61px
    // output whose pixel centres land exactly on source centres 20..80, so the
    // bilinear neighbours collapse onto the same sample.
    Recipe cropped=shade;cropped.crop={20/101.,20/101.,61/101.,61/101.};
    const auto croppedLinear=renderLinear(frame,cropped,0,error);
    QVERIFY2(error.isEmpty(),qPrintable(error));
    QCOMPARE(croppedLinear.size(),QSize(61,61));
    for(int row=0;row<61;++row){
        const auto *p=reinterpret_cast<const float *>(croppedLinear.constScanLine(row));
        for(int column=0;column<61;++column){
            const float expected=model.gain(20+column,20+row); // source x follows the output column
            QVERIFY2(std::abs(p[column*4]-(-.2f)*expected)<floatTolerance,"cropped red must use its own source position");
            QVERIFY2(std::abs(p[column*4+1]-expected)<floatTolerance,"cropped green must use its own source position");
        }
    }
    const auto *croppedFirst=reinterpret_cast<const float *>(croppedLinear.constScanLine(0));
    QVERIFY2(std::abs(croppedFirst[1]-model.gain(20,20))<floatTolerance,"output 0,0 must carry the source 20,20 gain");
    QVERIFY2(std::abs(croppedFirst[1]-model.gain(0,0))>1e-3f,"output 0,0 must not carry the crop frame corner gain");


    // ---- combined pipeline: the delivered frame is the reference encoding ----
    // Exercise correction before spatial processing and the colour pipeline.
    Recipe combined;combined.lensShading=.6;combined.perspective={.1,-.08};combined.flipHorizontal=true;
    combined.angle=7;combined.texture=.1;combined.sharpenAmount=.2;combined.sharpenMask=.5;combined.exposure=.2;
    const auto referenceLinear=renderLinear(frame,combined,0,error);
    QVERIFY2(error.isEmpty(),qPrintable(error));
    const auto reference=workToSrgbReference(referenceLinear,error);
    QVERIFY(error.isEmpty());
    const auto rendered=renderFrame(frame,combined,0);
    QVERIFY2(rendered.error.isEmpty(),qPrintable(rendered.error));
    int difference=0;
    for(qsizetype i=0;i<reference.sizeInBytes();++i)
        difference=std::max(difference,std::abs(int(reference.constBits()[i])-int(rendered.image.constBits()[i])));
    QVERIFY2(difference<=1,qPrintable(QStringLiteral("combined render differs by %1 code values").arg(difference)));
    Recipe withoutLens=combined;withoutLens.lensShading=0;
    const auto bare=renderFrame(frame,withoutLens,0);
    QVERIFY2(bare.error.isEmpty(),qPrintable(bare.error));
    const auto bareLinear=renderLinear(frame,withoutLens,0,error);QVERIFY(error.isEmpty());float delta=0;for(int y=0;y<bareLinear.height();++y){const auto *a=reinterpret_cast<const float *>(bareLinear.constScanLine(y)),*b=reinterpret_cast<const float *>(referenceLinear.constScanLine(y));for(int x=0;x<bareLinear.width()*4;++x)delta=std::max(delta,std::abs(a[x]-b[x]));}QVERIFY2(delta>.01f,"correction must change the linear HDR frame before SDR clipping");
    const auto midrange=uniformFrame(101,101,.2f,.3f,.4f,1);QVERIFY(renderFrame(midrange,combined,0).image!=renderFrame(midrange,withoutLens,0).image);SpatialCache cache;QVERIFY(renderFrame(midrange,combined,0,&cache).error.isEmpty());QCOMPARE(renderFrame(midrange,withoutLens,0,&cache).image,renderFrame(midrange,withoutLens,0).image);
    // Second render from the same (untouched) source: the correction stays a
    // pure function of the recipe, so no cached spatial state can drift.
    const auto repeated=renderFrame(frame,combined,0);
    QVERIFY2(repeated.error.isEmpty(),qPrintable(repeated.error));
    QCOMPARE(repeated.image,rendered.image);

    // ---- loader surface: the correction reaches the delivered frame ----
    // SourceReader reads JPEG/CR3 only, so the loader fixture is a real JPEG.
    // The expected values below stay geometry/dimension only: the encoded
    // round trip is not a place to assert float HDR colour.
    QTemporaryDir dir;
    QVERIFY2(dir.isValid(),"temporary directory");
    const QString path=dir.filePath(QStringLiteral("lens-shading.jpg"));
    QImage sheet(5,5,QImage::Format_RGB32);
    sheet.fill(QColor(150,110,80));
    QVERIFY2(sheet.save(path,"JPG",95),"synthetic source saved");
    EditLoader loader;QSignalSpy ready(&loader,&EditLoader::ready);
    EditTag tag{7,3,1,1,1,false};
    loader.request(path,combined,tag);
    QTRY_VERIFY_WITH_TIMEOUT(loader.canPickNeutral(path,combined,tag),30000);
    requireClean(ready.last(),"EditLoader");
    const auto cached=loader.preview(path,combined);
    QVERIFY(cached&&cached->error.isEmpty());
    const EditResult delivered=qvariant_cast<EditResult>(ready.last().at(0));
    QCOMPARE(delivered.recipe.lensShading,.6);
    QCOMPARE(delivered.recipe.crop,combined.crop);
    QCOMPARE(delivered.image.size(),renderedSize(QSize(5,5),combined));
    auto full=recipeJson(combined);
    QVERIFY2(full.contains(QStringLiteral("lensShading")),"full recipe JSON carries lensShading");
    QCOMPARE(recipeFromJson(full),combined);
    for(const char *geometryKey:{"perspective","x","y","width","height","flipHorizontal","flipVertical"})
        QVERIFY2(full.contains(QString::fromLatin1(geometryKey)),
                 qPrintable(QStringLiteral("full recipe JSON keeps geometry key %1").arg(QString::fromLatin1(geometryKey))));

    // ---- presets: the correction is stored, geometry is still preserved ----
    const QString presetPath=dir.filePath(QStringLiteral("lens-shading-preset.json"));
    QVERIFY2(savePresetFile(presetPath,AdjustmentPreset{QStringLiteral("镜头"),combined},error),qPrintable(error));
    AdjustmentPreset stored;
    QVERIFY2(loadPresetFile(presetPath,stored,error),qPrintable(error));
    QCOMPARE(stored.adjustments.lensShading,.6); // unlike geometry, this one is in the payload
    QCOMPARE(stored.adjustments.crop,QRectF(0,0,1,1));
    QVERIFY2(stored.adjustments.perspective.isNull(),"the preset payload still drops perspective");
    Recipe current;current.crop={.2,.15,.5,.6};current.perspective={-.03,.04};current.angle=5;current.exposure=-.4;
    const auto applied=applyPreset(current,AdjustmentPreset{QStringLiteral("当前"),combined});
    QCOMPARE(applied.lensShading,.6); // copied from the preset
    QCOMPARE(applied.crop,current.crop); // geometry stays with the session
    QCOMPARE(applied.perspective,current.perspective);
    QCOMPARE(applied.angle,current.angle);
    QCOMPARE(applied.exposure,combined.exposure); // exposure is a preset value, not session geometry
    // Perspective-only recipes still clear the correction through the geometry
    // helper the renderer uses, so no stale correction survives a reset.
    Recipe prepared=combined;clearPreparedGeometry(prepared);
    QCOMPARE(prepared.lensShading,0.);
    QCOMPARE(hasPreparedAdjustment(combined),true);
}

void LensShadingTests::windowControllerAndPersistence(){
    QVERIFY2(m_sampleDir.isValid(),"temporary directory for the synthetic source");
    const QString sample=qEnvironmentVariable("LUMINA_LENS_SAMPLE");
    const bool synthetic=sample.isEmpty();
    const QString source=synthetic?writeUniformJpeg(m_sampleDir.filePath(QStringLiteral("lens-6000x4000.jpg"))):sample;
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

    auto *amount=spin(window,"lens-shading");
    auto *bar=slider(window,"slider-lens-shading");
    auto *reset=window.findChild<QPushButton *>(QStringLiteral("reset-lens-shading"));
    QVERIFY2(amount&&bar&&reset,"lens-shading spin, slider and reset controls exist");
    QCOMPARE(amount->value(),0.);
    QCOMPARE(amount->minimum(),-2.);
    QCOMPARE(amount->maximum(),2.);
    QCOMPARE(amount->decimals(),3);
    QCOMPARE(amount->singleStep(),.05);
    QCOMPARE(amount->keyboardTracking(),false);
    QCOMPARE(bar->minimum(),-2000);
    QCOMPARE(bar->maximum(),2000);
    QCOMPARE(bar->value(),0);
    QCOMPARE(reset->accessibleName().isEmpty(),false);

    Recipe base=window.currentRecipe();
    base.crop={.1,.1,.8,.8};
    base.lensShading=.2;
    base.exposure=.312345;
    const int beforeApply=ready.count();
    window.applyRecipe(base,QStringLiteral("镜头基准"));
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    QTRY_VERIFY_WITH_TIMEOUT(ready.count()>beforeApply,90000);
    requireClean(ready.last(),"applyRecipe");
    QCOMPARE(window.currentRecipe(),base);
    QCOMPARE(amount->value(),.2);
    QCOMPARE(bar->value(),200);
    if(synthetic)QCOMPARE(qvariant_cast<EditResult>(ready.last().at(0)).originalSize,QSize(6000,4000));

    auto *canvas=window.findChild<Canvas *>();
    QVERIFY2(canvas,"canvas is reachable");
    QSignalSpy previews(&window,&Window::previewChanged);
    QSignalSpy paintings(canvas,&Canvas::framePainted);
    QVERIFY2(previews.isValid()&&paintings.isValid(),"gesture spies are valid");
    int gesturePreviews=0,gesturePaints=0,frameCount=0;
    bar->setSliderDown(true);
    for(int value=201;value<=240;++value){
        bar->setValue(value);QTest::qWait(50);
        gesturePreviews=previews.count();gesturePaints=paintings.count();++frameCount;
    }
    QVERIFY2(gesturePreviews>=2,qPrintable(QStringLiteral("interim previewChanged signals %1").arg(gesturePreviews)));
    QVERIFY2(gesturePaints>=2,qPrintable(QStringLiteral("interim framePainted signals %1").arg(gesturePaints)));
    bar->setSliderDown(false);
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    requireClean(ready.last(),"gesture release");
    const Recipe committed=window.currentRecipe();
    QCOMPARE(committed.lensShading,.24);
    QCOMPARE(committed.crop,base.crop);
    QCOMPARE(committed.exposure,.312345);
    QCOMPARE(amount->value(),.24);
    QCOMPARE(committed.angle,base.angle);
    QCOMPARE(committed.saturation,base.saturation);
    QCOMPARE(committed.curves,base.curves);

    window.undoEdit();
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    QCOMPARE(window.currentRecipe(),base); // one gesture, one history entry
    window.redoEdit();
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    QCOMPARE(window.currentRecipe(),committed);

    const Recipe beforeCompare=window.currentRecipe();
    window.compareOriginal(true);
    QTRY_VERIFY_WITH_TIMEOUT(amount&&!amount->isEnabled(),90000);
    QVERIFY2(!bar->isEnabled(),"the lens slider disables while comparing");
    window.compareOriginal(false);
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    QCOMPARE(window.currentRecipe(),beforeCompare);

    const int beforeReset=ready.count();
    reset->click();
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    QTRY_VERIFY_WITH_TIMEOUT(ready.count()>beforeReset,90000);
    requireClean(ready.last(),"reset-lens-shading");
    const Recipe resetRecipe=window.currentRecipe();
    QCOMPARE(resetRecipe.lensShading,0.);
    QCOMPARE(resetRecipe.crop,beforeCompare.crop);
    QCOMPARE(resetRecipe.exposure,beforeCompare.exposure);
    QCOMPARE(resetRecipe.angle,beforeCompare.angle);
    QCOMPARE(resetRecipe.perspective,beforeCompare.perspective);
    window.undoEdit();
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    QCOMPARE(window.currentRecipe(),beforeCompare);
    window.redoEdit();
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    QCOMPARE(window.currentRecipe(),resetRecipe);

    window.undoEdit();QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);QCOMPARE(window.currentRecipe(),committed);
    const QString recipeOut=qEnvironmentVariable("LUMINA_LENS_RECIPE");
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
    const QString shot=qEnvironmentVariable("LUMINA_LENS_SCREENSHOT");
    if(!shot.isEmpty()){
        QVERIFY2(amount,"the lens control exists for the screenshot");
        if(auto *area=window.findChild<QScrollArea *>())
            area->ensureWidgetVisible(qobject_cast<QWidget *>(formCardOf(amount)),0,16);
        QTest::qWait(100);
        QVERIFY2(window.grab().save(shot),qPrintable(QStringLiteral("screenshot saved to %1").arg(shot)));
    }
    qInfo() << "lens shading" << window.currentRecipe().lensShading
            << "gesture previews" << gesturePreviews << "gesture paints" << gesturePaints
            << "frames stepped" << frameCount << "loader results" << ready.count();
}

QTEST_MAIN(LensShadingTests)
#include "lens_shading_tests.moc"


