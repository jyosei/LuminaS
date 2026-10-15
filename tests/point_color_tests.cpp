#include <QtTest>
#include <QJsonObject>
#include <QJsonArray>
#include <algorithm>
#include <cmath>
#include <QTemporaryDir>
#include <limits>
#include "core/point_color_sampling.h"
#include "app/edit_loader.h"
#include "app/point_color_panel.h"
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QSlider>
#include <QCheckBox>
#include "core/document.h"
#include "core/point_color_json.h"
#include "render/point_color_operator.h"
#include "render/cpu_renderer.h"
#include "color/color_pipeline.h"
#include "core/presets.h"

// Expected properties only; the operator algorithm itself is owned elsewhere.
class PointColorTests:public QObject{
    Q_OBJECT
private slots:
    void neutralSamplesAndContinuousOverlap();
    void rangeViewAndCacheIsolation();
    void asynchronousSamplingAndStaleGate();
    void panelPrecisionAndSignals();
    void integratedRecipeAndPreset();
    void jsonRoundtripEightEntries();
    void jsonLegacyEmpty();
    void jsonRejectsInvalidInput();
    void jsonAtomicParseFailureKeepsSentinel();
    void renderAlphaAndNeutral();
    void renderOverlappingSymmetry();
    void renderUnselectedBlueUnchanged();
    void hueWrapAndFeather();
};

static PointColorAdjustment sample(int i){
    PointColorAdjustment p;
    p.hue=i*45.0;p.saturation=.5;p.luminance=.5;
    p.hueWidth=30;p.saturationWidth=.25;p.luminanceWidth=.25;p.feather=.5;
    p.hueShift=(i%2)?.2:-.2;p.saturationShift=.1;p.luminanceShift=-.1;
    return p;
}

void PointColorTests::jsonRoundtripEightEntries(){
    QVector<PointColorAdjustment> points;
    for(int i=0;i<maximumPointColors;++i)points.append(sample(i));
    QVERIFY(validPointColors(points));
    const QJsonArray array=pointColorsJson(points);
    QCOMPARE(array.size(),maximumPointColors);
    QCOMPARE(array.first().toObject().size(),10);
    QVector<PointColorAdjustment> parsed;
    QVERIFY(pointColorsFromJson(array,parsed));
    QCOMPARE(parsed,points);
    QCOMPARE(pointColorsJson(parsed),array);
}

void PointColorTests::jsonLegacyEmpty(){
    QVector<PointColorAdjustment> parsed{sample(1)};
    QVERIFY(pointColorsFromJson(QJsonArray(),parsed));
    QVERIFY(parsed.isEmpty());
    QVERIFY(validPointColors(QVector<PointColorAdjustment>()));
    QVERIFY(pointColorsJson(QVector<PointColorAdjustment>()).isEmpty());
}

// Field-by-field range rejection, one override per key.
static void checkRangeRejections(){
    const QStringList keys{"hue","saturation","luminance","hueWidth","saturationWidth",
        "luminanceWidth","feather","hueShift","saturationShift","luminanceShift"};
    const double bad[10]={360.0,-.01,1.01,0.0,.001,1.01,0.0,1.01,-1.01,1.01};
    for(int i=0;i<keys.size();++i){
        QJsonObject o=pointColorsJson({sample(0)}).first().toObject();
        o[keys[i]]=bad[i];
        QVector<PointColorAdjustment> out;
        QVERIFY2(!pointColorsFromJson(QJsonArray{o},out),qPrintable(keys[i]));
    }
}


void PointColorTests::jsonRejectsInvalidInput(){
    QVector<PointColorAdjustment> out;
    QVERIFY(!pointColorsFromJson(QJsonValue(3.0),out));
    QVERIFY(!pointColorsFromJson(QJsonObject(),out));
    QJsonObject base=pointColorsJson({sample(0)}).first().toObject();
    QJsonObject missing=base;missing.remove("feather");
    QVERIFY(!pointColorsFromJson(QJsonArray{missing},out));
    QJsonObject unknown=base;unknown["extra"]=1.0;
    QVERIFY(!pointColorsFromJson(QJsonArray{unknown},out));
    QJsonObject text=base;text["hue"]=QStringLiteral("10");
    QVERIFY(!pointColorsFromJson(QJsonArray{text},out));
    QJsonArray element{base,QJsonValue(2.0)};
    QVERIFY(!pointColorsFromJson(element,out));
    QJsonArray nonfinite{base};
    nonfinite[0]=QJsonValue(std::nan(""));
    QVERIFY(!pointColorsFromJson(nonfinite,out));
    QVector<PointColorAdjustment> tooMany;
    for(int i=0;i<maximumPointColors+1;++i)tooMany.append(sample(i));
    QVERIFY(!validPointColors(tooMany));
    QVERIFY(!pointColorsFromJson(pointColorsJson(tooMany),out));
    checkRangeRejections();
}

void PointColorTests::jsonAtomicParseFailureKeepsSentinel(){
    QVector<PointColorAdjustment> out{sample(3)};
    const QVector<PointColorAdjustment> sentinel=out;
    QJsonObject bad=pointColorsJson({sample(1)}).first().toObject();
    bad["hue"]=-1.0;
    QVERIFY(!pointColorsFromJson(QJsonArray{bad},out));
    QCOMPARE(out,sentinel);
    QJsonArray oversized;
    for(int i=0;i<maximumPointColors+1;++i)oversized.append(pointColorsJson({sample(i)}).first());
    QVERIFY(!pointColorsFromJson(oversized,out));
    QCOMPARE(out,sentinel);
}

static QImage floatFrame(int w,int h){
    QImage image(w,h,QImage::Format_RGBA32FPx4);
    image.fill(QColor(0,0,0,0));
    return image;
}

void PointColorTests::renderAlphaAndNeutral(){
    QImage frame=floatFrame(2,1);
    float *row=reinterpret_cast<float*>(frame.bits());
    row[0]=2.5f;row[1]=-.25f;row[2]=1.5f;row[3]=.4f;
    row[4]=0;row[5]=0;row[6]=0;row[7]=0;
    QVERIFY(std::abs(frame.pixelColor(0,0).alphaF()-.4f)<.01f);
    QCOMPARE(frame.pixelColor(1,0).alpha(),0);

    SourceFrame source;
    source.linear=frame;source.originalSize=QSize(2,1);
    QString error;
    const QImage plain=renderLinear(source,Recipe(),2,error);
    QVERIFY2(error.isEmpty(),qPrintable(error));

    Recipe neutral;
    PointColorAdjustment p=sample(0);
    p.hue=0;p.luminance=.5;p.saturation=.5;p.hueWidth=30;p.feather=.5;
    p.hueShift=0;p.saturationShift=0;p.luminanceShift=0;
    neutral.pointColors.append(p);
    const QImage unchanged=renderLinear(source,neutral,2,error);
    QVERIFY2(error.isEmpty(),qPrintable(error));
    QCOMPARE(unchanged,plain);
    QCOMPARE(unchanged.pixelColor(1,0).alpha(),0);
}

void PointColorTests::renderOverlappingSymmetry(){
    SourceFrame source;
    source.linear=floatFrame(4,1);
    float *row=reinterpret_cast<float*>(source.linear.bits());
    for(int x=0;x<4;++x){
        row[x*4+0]=.2f+x*.1f;row[x*4+1]=.12f;row[x*4+2]=.06f;row[x*4+3]=1.0f;
    }
    QVector<PointColorAdjustment> points;
    for(int i=0;i<3;++i){
        PointColorAdjustment p=sample(i*2);
        p.hueWidth=180;p.saturationWidth=1;p.luminanceWidth=1;
        points.append(p);
    }
    Recipe forward;forward.pointColors=points;
    Recipe reversed=forward;
    std::reverse(reversed.pointColors.begin(),reversed.pointColors.end());
    QString error;
    const QImage a=renderLinear(source,forward,4,error);
    QVERIFY2(error.isEmpty(),qPrintable(error));
    const QImage b=renderLinear(source,reversed,4,error);
    QVERIFY2(error.isEmpty(),qPrintable(error));
    QCOMPARE(a.size(),b.size());
    for(int y=0;y<a.height();++y)for(int x=0;x<a.width();++x){
        const QColor ca=a.pixelColor(x,y),cb=b.pixelColor(x,y);
        QVERIFY2(std::abs(ca.redF()-cb.redF())<=1e-6,"red");
        QVERIFY2(std::abs(ca.greenF()-cb.greenF())<=1e-6,"green");
        QVERIFY2(std::abs(ca.blueF()-cb.blueF())<=1e-6,"blue");
    }
    QVector<PointColorAdjustment> duplicated=points;
    duplicated.append(points);
    QVERIFY(validPointColors(duplicated));
    Recipe bounded=forward;bounded.pointColors=duplicated;
    const QImage c=renderLinear(source,bounded,4,error);
    QVERIFY2(error.isEmpty(),qPrintable(error));
    for(int x=0;x<c.width();++x){
        const QColor cc=c.pixelColor(x,0);
        QVERIFY(cc.redF()<=1.5f&&cc.greenF()<=1.5f&&cc.blueF()<=1.5f);
        QVERIFY(cc.redF()>=-.5f);
    }
}

void PointColorTests::renderUnselectedBlueUnchanged(){
    SourceFrame source;
    source.linear=floatFrame(2,1);
    float *row=reinterpret_cast<float*>(source.linear.bits());
    row[0]=0;row[1]=0;row[2]=1;row[3]=1;   // blue, outside the selection
    row[4]=1;row[5]=0;row[6]=0;row[7]=1;   // red, the selected colour
    const QImage before=source.linear.copy();
    Recipe selected;
    PointColorAdjustment p=sample(0);
    // Reference sits on the red descriptor so red is fully selected.
    p.hue=0;p.saturation=1;p.luminance=.5934;p.hueWidth=10;
    p.saturationWidth=.25;p.luminanceWidth=.25;p.feather=.5;
    p.hueShift=.2;p.saturationShift=0;p.luminanceShift=0;
    selected.pointColors.append(p);
    QString error;
    const QImage result=renderLinear(source,selected,2,error);
    QVERIFY2(error.isEmpty(),qPrintable(error));
    QCOMPARE(source.linear,before); // source frame is immutable
    const QColor blue=result.pixelColor(0,0);
    QVERIFY(blue.blueF()>.99f);
    QVERIFY(blue.redF()<.01f&&blue.greenF()<.01f);
    QCOMPARE(blue.alpha(),255);
    // Red is selected: a nonzero hue shift must move it away from pure red.
    const QColor red=result.pixelColor(1,0);
    QVERIFY(red.hue()!=0||std::abs(red.redF()-1.f)>.05f);
    QCOMPARE(red.alpha(),255);
}

void PointColorTests::hueWrapAndFeather(){
    // Gray and black are not selectable colours.
    PointColorAdjustment out;
    const float black[3]={0,0,0},gray[3]={.5f,.5f,.5f};
    QVERIFY(!PointColorOperator::descriptor(black,out));
    QVERIFY(!PointColorOperator::descriptor(gray,out));
    const float nan[3]={std::nanf(""),0,0};
    QVERIFY(!PointColorOperator::descriptor(nan,out));
    const float red[3]={1,0,0};
    QVERIFY(PointColorOperator::descriptor(red,out));
    QVERIFY(std::abs(out.hue)<1e-6||std::abs(out.hue-360)<1e-6);

    PointColorAdjustment p;
    p.hue=0;p.saturation=.5;p.luminance=.5;p.hueWidth=30;
    p.saturationWidth=1;p.luminanceWidth=1;p.feather=.5;p.hueShift=.5;
    Recipe recipe;recipe.pointColors.append(p);
    const PointColorOperator op(recipe);

    // Fully saturated probe colour at an exact work-RGB hue.
    auto hueProbe=[](float hueDegrees,float v[3]){
        const float hue=std::fmod(std::fmod(hueDegrees,360.f)+360.f,360.f);
        const int sector=int(hue/60.f);const float f=hue/60.f-sector;
        v[0]=v[1]=v[2]=0.f;
        switch(sector){
        case 0:v[0]=1;v[1]=f;break;case 1:v[0]=1-f;v[1]=1;break;
        case 2:v[1]=1;v[2]=f;break;case 3:v[1]=1-f;v[2]=1;break;
        case 4:v[0]=f;v[2]=1;break;default:v[0]=1;v[2]=1-f;}
    };
    auto weightAt=[&](float hueDegrees){
        float v[3];hueProbe(hueDegrees,v);return op.weight(v);
    };
    // Wrap continuity: hues just below and above 0 stay inside the window.
    const float belowWrap=weightAt(-.1f),atHue=weightAt(0.f),aboveWrap=weightAt(.1f);
    QVERIFY(atHue>0.f);
    QVERIFY(belowWrap>0.f);
    QVERIFY(aboveWrap>0.f);
    QVERIFY(std::abs(belowWrap-atHue)<1e-3f);
    QVERIFY(std::abs(aboveWrap-atHue)<1e-3f);
    QVERIFY(weightAt(180.f)<1e-6f);   // far outside the hue width
    QVERIFY(op.weight(gray)<1e-6f);   // achromatic is never selected
    // Feather continuity: a half-degree step moves the weight only slightly.
    const float mid=weightAt(20.f),midNext=weightAt(20.5f);
    QVERIFY(mid>0.f&&midNext>0.f);
    QVERIFY(std::abs(mid-midNext)<.05f);

    // A selected colour survives apply() finite and bounded.
    float moved[3];hueProbe(0.f,moved);
    QVERIFY(op.weight(moved)>0.f);
    PointColorAdjustment descriptor;
    QVERIFY(PointColorOperator::descriptor(moved,descriptor));
    QVERIFY(std::abs(descriptor.hue)<1e-3||std::abs(descriptor.hue-360)<1e-3);
    op.apply(moved);
    for(int c=0;c<3;++c)QVERIFY(std::isfinite(moved[c]));
}

void PointColorTests::integratedRecipeAndPreset(){
    Recipe r;QVERIFY(recipeFromJson(QJsonObject{}).pointColors.isEmpty());float input[]{2.f,-.2f,.4f,.4f};PointColorAdjustment p;QVERIFY(PointColorOperator::descriptor(input,p));p.hueShift=.3;p.saturationShift=-.2;p.luminanceShift=.25;r.pointColors={p};QVERIFY(validRecipe(r));QCOMPARE(recipeFromJson(recipeJson(r)),r);
    auto invalid=recipeJson(r);invalid["pointColors"]=false;QVERIFY(!validRecipe(recipeFromJson(invalid)));auto nonfinite=r;nonfinite.pointColors[0].feather=std::numeric_limits<double>::quiet_NaN();QVERIFY(!validRecipe(nonfinite));
    SourceFrame source;source.linear=QImage(31,17,QImage::Format_RGBA32FPx4);for(int y=0;y<17;++y){auto *v=reinterpret_cast<float *>(source.linear.scanLine(y));for(int x=0;x<31;++x)std::copy(input,input+4,v+x*4);}const auto original=source.linear;QString error;const auto linear=renderLinear(source,r,0,error);QVERIFY2(error.isEmpty(),qPrintable(error));QVERIFY(!linear.isNull());QCOMPARE(source.linear,original);const auto *first=reinterpret_cast<const float *>(linear.constScanLine(0));QCOMPARE(first[3],.4f);QVERIFY(std::abs(first[0]-input[0])>.001f);const auto reference=workToSrgbReference(linear,error),actual=renderFrame(source,r,0).image;QVERIFY(!actual.isNull());QCOMPARE(actual.size(),reference.size());int difference=0;for(qsizetype i=0;i<actual.sizeInBytes();++i)difference=std::max(difference,std::abs(int(actual.constBits()[i])-int(reference.constBits()[i])));QVERIFY(difference<=1);
    QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=dir.filePath("point.json");QVERIFY(savePresetFile(path,{"Point",r},error));AdjustmentPreset loaded;QVERIFY(loadPresetFile(path,loaded,error));QCOMPARE(loaded.adjustments,r);Recipe mixed;QVERIFY(blendPreset({},loaded,.5,mixed,error));QCOMPARE(mixed.pointColors.size(),1);QCOMPARE(mixed.pointColors[0].hue,p.hue);QCOMPARE(mixed.pointColors[0].hueShift,p.hueShift*.5);QVERIFY(blendPreset(r,loaded,.5,mixed,error));QCOMPARE(mixed,r);
    auto duplicate=r;duplicate.pointColors.append(p);QVERIFY(blendPreset(duplicate,{"Duplicate",duplicate},.5,mixed,error));QCOMPARE(mixed,duplicate);auto many=r;many.pointColors.clear();for(int i=0;i<8;++i){auto a=p;a.hue=i*40;many.pointColors.append(a);}auto other=many;for(auto &a:other.pointColors)a.hue+=1;const auto before=mixed;QVERIFY(!blendPreset(many,{"Other",other},.5,mixed,error));QCOMPARE(mixed,before);
}

void PointColorTests::asynchronousSamplingAndStaleGate(){
    QTemporaryDir dir;QVERIFY(dir.isValid());QImage image(96,64,QImage::Format_RGB32);for(int y=0;y<64;++y)for(int x=0;x<96;++x)image.setPixelColor(x,y,x<48?QColor(200,30,20):QColor(30,200,20));const auto path=dir.filePath("sample.jpg");QVERIFY(image.save(path));EditLoader loader;EditTag tag{1,1,1,1,1,false};Recipe r;QSignalSpy ready(&loader,&EditLoader::ready),sampled(&loader,&EditLoader::pointColorSampled);loader.request(path,r,tag);QTRY_VERIFY_WITH_TIMEOUT(loader.canPickNeutral(path,r,tag),10000);QString error;QVERIFY(!loader.requestPointColorSample(path,r,tag,{1,0},error));QVERIFY(loader.requestPointColorSample(path,r,tag,{.25,.5},error));QVERIFY(!loader.requestPointColorSample(path,r,tag,{.25,.5},error));QTRY_COMPARE_WITH_TIMEOUT(sampled.size(),1,10000);QVERIFY(sampled[0][3].toString().isEmpty());const auto red=qvariant_cast<PointColorAdjustment>(sampled[0][2]);QVERIFY(red.hue<40||red.hue>320);
    QVERIFY(loader.requestPointColorSample(path,r,tag,{.25,.5},error));loader.cancelPointColorSample();QTRY_VERIFY_WITH_TIMEOUT(loader.canSamplePointColor(path,r,tag),10000);QCOMPARE(sampled.size(),1);QVERIFY(loader.requestPointColorSample(path,r,tag,{.25,.5},error));auto newer=tag;++newer.job;++newer.revision;r.flipHorizontal=true;loader.request(path,r,newer);QTRY_VERIFY_WITH_TIMEOUT(loader.canPickNeutral(path,r,newer),10000);QVERIFY(!loader.requestPointColorSample(path,{},tag,{.25,.5},error));QTRY_VERIFY_WITH_TIMEOUT(loader.canSamplePointColor(path,r,newer),10000);QVERIFY(loader.requestPointColorSample(path,r,newer,{.25,.5},error));QTRY_COMPARE_WITH_TIMEOUT(sampled.size(),2,10000);QCOMPARE(qvariant_cast<EditTag>(sampled[1][1]),newer);const auto green=qvariant_cast<PointColorAdjustment>(sampled[1][2]);QVERIFY(green.hue>80&&green.hue<160);QVERIFY(sampled[1][3].toString().isEmpty());
}
void PointColorTests::panelPrecisionAndSignals(){
    PointColorPanel panel;panel.show();qRegisterMetaType<QVector<PointColorAdjustment>>();QSignalSpy edits(&panel,&PointColorPanel::pointsEdited),finished(&panel,&PointColorPanel::editFinished),ranges(&panel,&PointColorPanel::rangePreviewChanged);auto p=sample(0);p.hueShift=.123456;p.luminanceWidth=.234567;panel.setPoints({p});QCOMPARE(edits.size(),0);panel.findChild<QDoubleSpinBox *>("point-color-saturation-shift")->setValue(.4);QCOMPARE(edits.size(),1);const auto edited=qvariant_cast<QVector<PointColorAdjustment>>(edits.last()[0]);QCOMPARE(edited[0].hueShift,p.hueShift);QCOMPARE(edited[0].luminanceWidth,p.luminanceWidth);QCOMPARE(edited[0].saturationShift,.4);
    panel.setRangeActive(true);panel.setPoints(edited);QVERIFY(panel.findChild<QCheckBox *>("point-color-range-preview")->isChecked());QCOMPARE(ranges.size(),0);auto *bar=panel.findChild<QSlider *>("slider-point-color-saturation-shift");bar->setSliderDown(true);bar->setValue(45);QMetaObject::invokeMethod(panel.findChild<QDoubleSpinBox *>("point-color-saturation-shift"),"editingFinished");QCOMPARE(finished.size(),0);bar->setSliderDown(false);QCOMPARE(finished.size(),1);panel.findChild<QPushButton *>("point-color-delete")->click();QCOMPARE(finished.size(),3);QVERIFY(qvariant_cast<QVector<PointColorAdjustment>>(edits.last()[0]).isEmpty());QCOMPARE(panel.selectedIndex(),-1);QVERIFY(!bar->isEnabled());QVector<PointColorAdjustment> eight;for(int i=0;i<8;++i)eight.append(sample(i));panel.setPoints(eight);panel.setPickerAvailable(true);QVERIFY(!panel.findChild<QPushButton *>("point-color-picker")->isEnabled());
}

void PointColorTests::rangeViewAndCacheIsolation(){
    SourceFrame source;source.linear=QImage(19,13,QImage::Format_RGBA32FPx4);float rgb[]{.8f,.05f,.02f,.6f};for(int y=0;y<13;++y){auto *v=reinterpret_cast<float *>(source.linear.scanLine(y));for(int x=0;x<19;++x)std::copy(rgb,rgb+4,v+4*x);}Recipe r;PointColorAdjustment p;QVERIFY(PointColorOperator::descriptor(rgb,p));p.hueShift=.2;r.pointColors={p};QString error;const auto normal=renderFrame(source,r,0).image,linear=renderLinear(source,r,0,error,{0}),reference=workToSrgbReference(linear,error),range=renderFrame(source,r,0,nullptr,{0}).image;QVERIFY(error.isEmpty());QVERIFY(!range.isNull());int difference=0;for(qsizetype i=0;i<range.sizeInBytes();++i)difference=std::max(difference,std::abs(int(range.constBits()[i])-int(reference.constBits()[i])));QVERIFY(difference<=1);QCOMPARE(renderFrame(source,r,0).image,normal);QVERIFY(!renderFrame(source,r,0,nullptr,{1}).error.isEmpty());QVERIFY(!renderFrame(source,r,0,nullptr,{-2}).error.isEmpty());QCOMPARE(recipeFromJson(recipeJson(r)),r);
    QTemporaryDir dir;QVERIFY(dir.isValid());QImage image(96,64,QImage::Format_RGB32);image.fill(QColor(200,30,20));const auto path=dir.filePath("cache.jpg");QVERIFY(image.save(path));EditLoader loader;EditTag tag{1,1,1,1,1,false};QSignalSpy ready(&loader,&EditLoader::ready);loader.request(path,r,tag);QTRY_VERIFY_WITH_TIMEOUT(loader.canPickNeutral(path,r,tag),10000);const auto cached=loader.preview(path,r);QVERIFY(cached);++tag.job;loader.request(path,r,tag,{0});QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty()&&qvariant_cast<EditResult>(ready.last()[0]).tag==tag,10000);QCOMPARE(qvariant_cast<EditResult>(ready.last()[0]).view.pointRange,0);QVERIFY(!loader.canPickNeutral(path,r,tag));QVERIFY(loader.preview(path,r));QCOMPARE(loader.preview(path,r)->image,cached->image);++tag.job;loader.request(path,r,tag);QTRY_VERIFY_WITH_TIMEOUT(loader.canPickNeutral(path,r,tag),10000);QCOMPARE(qvariant_cast<EditResult>(ready.last()[0]).view.pointRange,-1);
}

void PointColorTests::neutralSamplesAndContinuousOverlap(){
    float input[]{.8f,.05f,.02f};PointColorAdjustment p;QVERIFY(PointColorOperator::descriptor(input,p));p.hueShift=.2;Recipe r;r.pointColors={p};float alone[]{input[0],input[1],input[2]};PointColorOperator(r).apply(alone);auto zero=p;zero.hueShift=0;r.pointColors.append(zero);float withZero[]{input[0],input[1],input[2]};PointColorOperator(r).apply(withZero);for(int c=0;c<3;++c)QCOMPARE(withZero[c],alone[c]);r.pointColors[1].hueShift=1e-6;float tiny[]{input[0],input[1],input[2]};PointColorOperator(r).apply(tiny);for(int c=0;c<3;++c)QVERIFY(std::abs(tiny[c]-alone[c])<2e-6f);for(auto &point:r.pointColors)point.hueShift=1;while(r.pointColors.size()<8)r.pointColors.append(p);float extreme[]{input[0],input[1],input[2]};PointColorOperator(r).apply(extreme);for(float v:extreme)QVERIFY(std::isfinite(v));
}

QTEST_MAIN(PointColorTests)
#include "point_color_tests.moc"





