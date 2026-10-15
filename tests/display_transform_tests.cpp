#include "color/display_transform.h"
#include "color/color_pipeline.h"
#include <QtTest>
#include <QColorSpace>
#include <lcms2.h>
#include <algorithm>

static QByteArray profileBytes(cmsHPROFILE profile){
    cmsUInt32Number length=0;cmsSaveProfileToMem(profile,nullptr,&length);QByteArray bytes(length,Qt::Uninitialized);
    cmsSaveProfileToMem(profile,bytes.data(),&length);cmsCloseProfile(profile);return bytes;
}
static QByteArray monitor(){
    cmsCIExyY white{.3127,.3290,1};cmsCIExyYTRIPLE primaries{{.64,.33,1},{.21,.71,1},{.15,.06,1}};
    auto *gamma=cmsBuildGamma(nullptr,2.2);cmsToneCurve *curves[]{gamma,gamma,gamma};
    auto profile=cmsCreateRGBProfile(&white,&primaries,curves);cmsFreeToneCurve(gamma);return profileBytes(profile);
}
static QImage swatches(){
    QImage image(125,1,QImage::Format_RGBA8888);int x=0;
    for(int r:{0,64,128,192,255})for(int g:{0,64,128,192,255})for(int b:{0,64,128,192,255}){image.setPixelColor(x,0,QColor(r,g,b,x%256));++x;}
    image.setColorSpace(QColorSpace::SRgb);return image;
}
class DisplayTransformTests:public QObject {
    Q_OBJECT
private slots:
    void profileConversionAndSourceSafety();void fallbackInvalidAndCancel();void numericLabReference();
};
void DisplayTransformTests::profileConversionAndSourceSafety(){
    const auto input=swatches(),unchanged=input;const auto target=monitor();const auto actual=transformDisplay(input,target);
    QVERIFY2(actual.error.isEmpty(),qPrintable(actual.error));QCOMPARE(input,unchanged);QCOMPARE(actual.image.size(),input.size());
    QVERIFY(!actual.image.colorSpace().isValid());QVERIFY(actual.image!=input);
    auto sourceProfile=cmsCreate_sRGBProfile();auto destination=cmsOpenProfileFromMem(target.constData(),cmsUInt32Number(target.size()));
    auto reference=cmsCreateTransform(sourceProfile,TYPE_RGBA_8,destination,TYPE_RGBA_8,INTENT_RELATIVE_COLORIMETRIC,cmsFLAGS_COPY_ALPHA|cmsFLAGS_NOOPTIMIZE|cmsFLAGS_NOCACHE);
    QVERIFY(reference);QImage expected(input.size(),QImage::Format_RGBA8888);cmsDoTransform(reference,input.constBits(),expected.bits(),125);
    int maximum=0;for(int x=0;x<125;++x){const auto a=actual.image.pixelColor(x,0),e=expected.pixelColor(x,0);
        maximum=std::max({maximum,std::abs(a.red()-e.red()),std::abs(a.green()-e.green()),std::abs(a.blue()-e.blue())});QCOMPARE(a.alpha(),input.pixelColor(x,0).alpha());}
    QVERIFY(maximum<=1);cmsDeleteTransform(reference);cmsCloseProfile(sourceProfile);cmsCloseProfile(destination);
    auto tagged=input;tagged.setColorSpace(QColorSpace::AdobeRgb);const auto normalized=transformDisplay(tagged,{});
    QVERIFY(normalized.error.isEmpty());QCOMPARE(normalized.image.colorSpace(),QColorSpace(QColorSpace::SRgb));QVERIFY(normalized.image!=tagged);
}
void DisplayTransformTests::fallbackInvalidAndCancel(){
    const auto input=swatches();QCOMPARE(transformDisplay(input,{}).image,input);
    QVERIFY(!transformDisplay(input,"invalid ICC").error.isEmpty());QVERIFY(!transformDisplay({},{}).error.isEmpty());
    const auto cancelled=std::make_shared<std::atomic<bool>>(true);QVERIFY(transformDisplay(input,monitor(),cancelled).image.isNull());
    auto invalid=monitor();invalid[12]='p';invalid[13]='r';invalid[14]='t';invalid[15]='r';
    QVERIFY(!transformDisplay(input,invalid).error.isEmpty());
    QString validation;QVERIFY(validDisplayProfile({},validation));QVERIFY(validDisplayProfile(monitor(),validation));
    QVERIFY(!validDisplayProfile(invalid,validation));auto version=monitor();version[8]=3;QVERIFY(!validDisplayProfile(version,validation));
    QVERIFY(!transformDisplay(input,QByteArray(4*1024*1024+1,'x')).error.isEmpty());
}
void DisplayTransformTests::numericLabReference(){
    const auto input=swatches();QString error;const auto linear=inputToWork(input,{},error);QVERIFY(error.isEmpty());
    const auto fast=workToSrgb(linear,error),reference=workToSrgbReference(linear,error);QVERIFY(error.isEmpty());
    auto srgb=cmsCreate_sRGBProfile();auto lab=cmsCreateLab4Profile(nullptr);
    auto transform=cmsCreateTransform(srgb,TYPE_RGBA_8,lab,TYPE_Lab_DBL,INTENT_RELATIVE_COLORIMETRIC,cmsFLAGS_NOOPTIMIZE|cmsFLAGS_NOCACHE);
    QVERIFY(transform);cmsCIELab a[125],b[125];cmsDoTransform(transform,fast.constBits(),a,125);cmsDoTransform(transform,reference.constBits(),b,125);
    double maximum=0;for(int i=0;i<125;++i)maximum=std::max(maximum,cmsCIE2000DeltaE(&a[i],&b[i],1,1,1));
    qInfo()<<"synthetic125swatches fast-vs-LCMS maxDeltaE2000"<<maximum;QVERIFY(maximum<=1);
    cmsDeleteTransform(transform);cmsCloseProfile(srgb);cmsCloseProfile(lab);
}
QTEST_MAIN(DisplayTransformTests)
#include "display_transform_tests.moc"
