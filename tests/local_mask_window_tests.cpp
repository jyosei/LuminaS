#include <QtTest>
#include <QTemporaryDir>
#include <QTabWidget>
#include <QSignalSpy>
#include <QCryptographicHash>
#include <QMouseEvent>
#include <QScrollArea>
#include <functional>
#include "app/window.h"
#include "app/local_mask_options.h"
#include "app/display_presenter.h"
#include "io/source_reader.h"
#include "render/local_renderer.h"
#include "render/geometry_transform.h"
#include <QFile>
using namespace LocalEdit;
namespace {
bool waitFor(const std::function<bool()> &test){QElapsedTimer t;t.start();while(!test()){if(t.elapsed()>90000)return false;QTest::qWait(10);}return true;}
bool painted(Window &w,Canvas *c,const QSignalSpy &paint){
    const auto doc=w.currentPhotoDocument();const auto r=qvariant_cast<EditResult>(w.property("mask-last-result"));
    return doc&&!paint.isEmpty()&&r.path==doc->asset.sourcePath&&r.error.isEmpty()&&!r.image.isNull()&&r.recipe==doc->global&&r.document&&r.document->layers==doc->layers&&
        paint.last()[0].toLongLong()==r.image.cacheKey()&&r.image.cacheKey()==c->presenter()->canonicalImage().cacheKey();
}
QPoint point(Canvas *c,QPointF uv){const auto s=c->presenter()->canonicalImage().size();return {int(std::lround(c->width()/2.+(uv.x()-.5)*s.width()*c->zoom())),int(std::lround(c->height()/2.+(uv.y()-.5)*s.height()*c->zoom()))};}
void move(Canvas *c,QPoint p){QMouseEvent e(QEvent::MouseMove,QPointF(p),c->mapToGlobal(p),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(c,&e);}
QByteArray pixels(const QImage &i){return QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char *>(i.constBits()),i.sizeInBytes()),QCryptographicHash::Sha256).toHex();}
QByteArray maskIndices(const QImage &i){
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for(int y=0;y<i.height();++y)hash.addData(QByteArrayView(reinterpret_cast<const char *>(i.constScanLine(y)),i.width()));
    return hash.result().toHex();
}
bool setup(Window &w,Canvas *c,QSignalSpy &paint,QTemporaryDir &dir){
    QObject::connect(w.findChild<EditLoader *>(),&EditLoader::ready,&w,[&w](const EditResult &r){w.setProperty("mask-last-result",QVariant::fromValue(r));});
    if(!dir.isValid())return false;auto path=qEnvironmentVariable("LUMINA_MASK_SAMPLE");
    if(path.isEmpty()){path=dir.filePath("mask.jpg");QImage image(96,64,QImage::Format_RGB32);image.fill(QColor(50,80,120));if(!image.save(path,"JPEG",98))return false;}
    c->setMonitorProfile({},"mask-native-test");w.show();if(!QTest::qWaitForWindowExposed(&w))return false;w.addFiles({path});
    if(!waitFor([&]{return w.neutralPickerAvailable();}))return false;
    w.findChild<QTabWidget *>("adjustment-scope-tabs")->setCurrentIndex(1);w.findChild<QPushButton *>("local-new-brush")->click();
    return waitFor([&]{return painted(w,c,paint)&&c->brushMode();});
}
void spin(QDoubleSpinBox *s,double value){s->setValue(value);QMetaObject::invokeMethod(s,"editingFinished",Qt::DirectConnection);}
}
class LocalMaskWindowTests:public QObject {
    Q_OBJECT
private slots:
    void globalCombinationsKeepLocalHistoryAndPreparedPixels(){
        QTemporaryDir dir;DocumentSnapshot frozenColor;QByteArray colorHash;
        {
            Window w(nullptr,QStringLiteral(""));auto *c=w.findChild<Canvas *>("photo-canvas");QVERIFY(c);
            w.setWindowTitle(QStringLiteral("LuminaS · C6 组合验证"));
            QSignalSpy paint(c,&Canvas::canonicalFramePainted);QVERIFY(setup(w,c,paint,dir));
            w.findChild<QPushButton *>("local-gradient-radial")->click();
            const auto from=point(c,{.5,.5}),to=point(c,{.7,.5});
            QTest::mousePress(c,Qt::LeftButton,Qt::NoModifier,from);move(c,to);QTest::mouseRelease(c,Qt::LeftButton,Qt::NoModifier,to);
            spin(w.findChild<QDoubleSpinBox *>("local-exposure"),.7);
            spin(w.findChild<QDoubleSpinBox *>("local-mask-feather"),2.3);
            w.findChild<QCheckBox *>("local-mask-invert")->setChecked(true);
            const auto settled=[&]{const auto r=qvariant_cast<EditResult>(w.property("mask-last-result"));
                return painted(w,c,paint)&&!r.tag.interactive&&!r.workFrame.linear.isNull();};
            paint.clear();QVERIFY(waitFor(settled));const auto locals=w.currentPhotoDocument()->layers;
            Recipe g;
            for(int i=0;i<4;++i)g.curves[i]={{0,0},{.25,.2+.01*i},{.7,.77-.01*i},{1,1}};
            g.luminanceCurve={{0,0},{.3,.25},{.7,.8},{1,1}};g.parametricCurve.regions={.1,-.1,.15,-.15};
            g.hsl[1]={.15,.2,-.1};g.hsl[5]={-.1,-.15,.1};g.primaries[0]={.1,.2};g.primaries[2]={-.15,.1};
            g.grading[0]={220,.1,-.05};g.grading[2]={40,.15,.05};g.gradingBlend=.6;g.gradingBalance=.1;
            PointColorAdjustment selection;selection.hue=30;selection.hueWidth=90;selection.saturationShift=.15;g.pointColors={selection};
            // Keep this visible composition inside the source. Out-of-source
            // rotation support is exercised separately by the boundary test.
            g.crop={.2,.22,.6,.56};g.angle=7;g.flipHorizontal=g.flipVertical=true;
            g.perspective={.08,-.06};g.distortion=.07;g.chromaticAberration={.3,-.2};g.lensShading=-.3;
            g.texture=.2;g.clarity=.15;g.dehaze=.1;g.sharpenAmount=.35;g.sharpenRadius=1.2;g.sharpenThreshold=.015;g.sharpenMask=.2;
            g.noiseLuminance=.12;g.noiseChroma=.08;g.defringe.purpleAmount=.4;g.defringe.greenAmount=.3;
            g.vignetteAmount=-.3;g.vignetteMidpoint=.55;g.vignetteFeather=.45;g.grainAmount=.15;g.grainSize=2.5;g.grainRoughness=.55;
            QVERIFY(validRecipe(g));QByteArray previous=pixels(c->presenter()->canonicalImage());
            for(int mode=0;mode<2;++mode){
                g.monochrome=mode==1;g.monochromeStrength=.4;g.monochromeMix[1]=.2;
                paint.clear();w.applyRecipe(g,"C6组合");QCOMPARE(w.currentPhotoDocument()->layers,locals);QVERIFY(waitFor(settled));
                const auto r=qvariant_cast<EditResult>(w.property("mask-last-result"));const auto doc=w.currentPhotoDocument();
                float minimumAlpha=1;
                for(int y=0;y<r.workFrame.linear.height();++y){
                    const auto *row=reinterpret_cast<const float *>(r.workFrame.linear.constScanLine(y));
                    for(int x=0;x<r.workFrame.linear.width();++x)minimumAlpha=std::min(minimumAlpha,row[x*4+3]);
                }
                QVERIFY2(minimumAlpha>.9999f,"The normal composition must not expose transparent source boundaries");
                auto applied=doc->global;clearPreparedGeometry(applied);
                const auto reference=renderDocumentPreview(r.workFrame,*doc,applied,3840,nullptr,r.view);
                QVERIFY2(reference.error.isEmpty(),qPrintable(reference.error));QVERIFY(!reference.image.isNull());
                QCOMPARE(reference.image,c->presenter()->canonicalImage());
                if(r.view.localMaskVisible){
                    QVERIFY(!reference.localMask.isNull());QCOMPARE(reference.localMask.size(),r.localMask.size());
                    QCOMPARE(reference.localMask.format(),r.localMask.format());
                    // Coverage indices intentionally have no display palette. QImage's
                    // indexed equality resolves palette entries; compare actual indices
                    // without the non-pixel alignment bytes at the end of each row.
                    QCOMPARE(maskIndices(reference.localMask),maskIndices(r.localMask));
                }
                const auto actual=pixels(reference.image);QVERIFY(actual!=previous);previous=actual;
                if(mode==0){frozenColor=doc;colorHash=actual;}
                qInfo().noquote()<<"C6-native-prepared"<<mode<<r.workFrame.raw<<reference.image.size()<<r.effectElapsedMs<<"minimumAlpha"<<minimumAlpha<<actual;
            }
            const auto mono=w.currentPhotoDocument();const auto monoHash=previous;
            paint.clear();w.undoEdit();QCOMPARE(w.currentPhotoDocument()->global,frozenColor->global);QCOMPARE(w.currentPhotoDocument()->layers,locals);
            QVERIFY(waitFor(settled));QCOMPARE(pixels(c->presenter()->canonicalImage()),colorHash);
            paint.clear();w.redoEdit();QCOMPARE(w.currentPhotoDocument()->global,mono->global);QCOMPARE(w.currentPhotoDocument()->layers,locals);
            QVERIFY(waitFor(settled));QCOMPARE(pixels(c->presenter()->canonicalImage()),monoHash);
            QVERIFY(!frozenColor->global.monochrome);QCOMPARE(frozenColor->layers,locals);
        } // Release the native/prepared caches before this numerical full-source replay.
        if(qEnvironmentVariableIsSet("LUMINA_C6_FULL_REPLAY")){
            QString error;const auto loaded=readSource(frozenColor->asset.sourcePath,frozenColor->global.warmth,frozenColor->global.tint);
            QVERIFY2(loaded.source,qPrintable(loaded.error));QCOMPARE(loaded.source->linear.size(),frozenColor->asset.normalizedSize);
            QCOMPARE(loaded.source->sha256,frozenColor->asset.sourceHash);
            QByteArray first;QSize outputSize;
            for(int i=0;i<2;++i){const auto full=renderDocumentFullLinear(*loaded.source,*frozenColor,error);
                QVERIFY2(!full.isNull(),qPrintable(error));QCOMPARE(full.size(),renderedSize(frozenColor->asset.normalizedSize,frozenColor->global));
                outputSize=full.size();if(i==0)first=pixels(full);else QCOMPARE(pixels(full),first);}
            qInfo().noquote()<<"C6-frozen-full-source"<<outputSize<<first;
        }
        if(!qEnvironmentVariable("LUMINA_MASK_SAMPLE").isEmpty()){
            QFile source(frozenColor->asset.sourcePath);QVERIFY(source.open(QIODevice::ReadOnly));QCryptographicHash digest(QCryptographicHash::Sha256);
            QVERIFY(digest.addData(&source));QCOMPARE(QString::fromLatin1(digest.result().toHex()),frozenColor->asset.sourceHash);
        }
    }
    void parameterDragReusesVisibleCoverageWhilePhotoStillUpdates(){
        QTemporaryDir dir;Window w(nullptr,QStringLiteral(""));auto *c=w.findChild<Canvas *>("photo-canvas");QSignalSpy paint(c,&Canvas::canonicalFramePainted);QVERIFY(setup(w,c,paint,dir));
        w.findChild<QPushButton *>("local-gradient-radial")->click();const auto from=point(c,{.5,.5}),to=point(c,{.7,.5});
        paint.clear();QTest::mousePress(c,Qt::LeftButton,Qt::NoModifier,from);move(c,to);QTest::mouseRelease(c,Qt::LeftButton,Qt::NoModifier,to);
        QVERIFY(waitFor([&]{return painted(w,c,paint);}));auto *slider=w.findChild<QSlider *>("slider-local-exposure");QVERIFY(slider);
        paint.clear();slider->setSliderDown(true);slider->setValue(40);QVERIFY(waitFor([&]{return painted(w,c,paint);}));
        const auto first=qvariant_cast<EditResult>(w.property("mask-last-result"));QVERIFY(!first.localMask.isNull());
        const auto oldPhoto=pixels(c->presenter()->canonicalImage());QSignalSpy masks(c,&Canvas::localMaskPainted);
        paint.clear();slider->setValue(70);QVERIFY(waitFor([&]{return painted(w,c,paint);}));
        const auto second=qvariant_cast<EditResult>(w.property("mask-last-result"));
        QCOMPARE(second.localMask.cacheKey(),first.localMask.cacheKey());QVERIFY(pixels(c->presenter()->canonicalImage())!=oldPhoto);QCOMPARE(masks.count(),0);
        slider->setSliderDown(false);
    }
    void featherInvertHistoryAndDisplayIsolation(){
        QTemporaryDir dir;Window w(nullptr,QStringLiteral(""));auto *c=w.findChild<Canvas *>("photo-canvas");QVERIFY(c);QSignalSpy paint(c,&Canvas::canonicalFramePainted);QVERIFY(setup(w,c,paint,dir));
        w.findChild<QPushButton *>("local-gradient-radial")->click();
        const auto from=point(c,{.5,.5}),to=point(c,{.7,.5});QTest::mousePress(c,Qt::LeftButton,Qt::NoModifier,from);move(c,to);QTest::mouseRelease(c,Qt::LeftButton,Qt::NoModifier,to);
        spin(w.findChild<QDoubleSpinBox *>("local-exposure"),.7);paint.clear();QVERIFY(waitFor([&]{return painted(w,c,paint);}));
        const auto sharp=w.currentPhotoDocument();const auto sharpHash=pixels(c->presenter()->canonicalImage());
        auto *feather=w.findChild<QDoubleSpinBox *>("local-mask-feather");auto *slider=w.findChild<QSlider *>("slider-local-mask-feather");auto *inverted=w.findChild<QCheckBox *>("local-mask-invert");QVERIFY(feather);QVERIFY(slider);QVERIFY(inverted);
        paint.clear();slider->setSliderDown(true);feather->setValue(2);feather->setValue(3.7);QMetaObject::invokeMethod(feather,"editingFinished",Qt::DirectConnection);QCoreApplication::processEvents();slider->setSliderDown(false);
        QTRY_COMPARE(w.currentPhotoDocument()->layers[0].mask.feather,3.7);QTest::qWait(10);QVERIFY(waitFor([&]{return painted(w,c,paint);}));
        const auto soft=w.currentPhotoDocument();QCOMPARE(soft->layers[0].mask.components,sharp->layers[0].mask.components);QVERIFY(pixels(c->presenter()->canonicalImage())!=sharpHash);
        paint.clear();w.undoEdit();QCOMPARE(w.currentPhotoDocument()->layers,sharp->layers);QVERIFY(waitFor([&]{return painted(w,c,paint);}));
        paint.clear();w.redoEdit();QCOMPARE(w.currentPhotoDocument()->layers,soft->layers);QVERIFY(waitFor([&]{return painted(w,c,paint);}));
        paint.clear();inverted->click();QVERIFY(w.currentPhotoDocument()->layers[0].mask.invert);QVERIFY(waitFor([&]{return painted(w,c,paint);}));
        const auto frozen=w.currentPhotoDocument();const auto frozenValue=*frozen;const auto image=c->presenter()->canonicalImage();const auto hash=pixels(image);
        auto *bw=w.findChild<QCheckBox *>("local-mask-blackwhite");auto *color=w.findChild<QComboBox *>("local-mask-color");QVERIFY(bw);QVERIFY(color);
        QSignalSpy frames(c,&Canvas::canonicalFramePainted),masks(c,&Canvas::localMaskPainted),requests(w.findChild<EditLoader *>(),&EditLoader::requestQueued);
        color->setCurrentIndex(1);bw->setChecked(true);c->repaint();QCoreApplication::processEvents();
        QCOMPARE(*w.currentPhotoDocument(),frozenValue);QCOMPARE(pixels(c->presenter()->canonicalImage()),hash);QCOMPARE(frames.count(),0);QCOMPARE(masks.count(),0);QCOMPARE(requests.count(),0);
        const auto shot=qEnvironmentVariable("LUMINA_MASK_SCREENSHOT");if(!shot.isEmpty())QVERIFY(w.grab().save(shot));
        auto *overlay=w.findChild<QCheckBox *>("local-overlay");QVERIFY(overlay);
        paint.clear();overlay->setChecked(false);QVERIFY(waitFor([&]{return painted(w,c,paint);}));
        requests.clear();bw->setChecked(false);QVERIFY(!overlay->isChecked());QCOMPARE(requests.count(),0);QCOMPARE(*w.currentPhotoDocument(),frozenValue);
        paint.clear();w.undoEdit();QVERIFY(!w.currentPhotoDocument()->layers[0].mask.invert);QCOMPARE(w.currentPhotoDocument()->layers[0].mask.feather,3.7);QVERIFY(waitFor([&]{return painted(w,c,paint);}));
        if(qEnvironmentVariableIsSet("LUMINA_MASK_FULL_REPLAY")){
            QString error;const auto source=readSource(frozen->asset.sourcePath,frozen->global.warmth,frozen->global.tint);QVERIFY2(source.source,qPrintable(source.error));
            auto full=renderDocumentFullLinear(*source.source,*frozen,error);QVERIFY2(!full.isNull(),qPrintable(error));QCOMPARE(full.size(),source.source->originalSize);const auto first=pixels(full);full={};
            full=renderDocumentFullLinear(*source.source,*frozen,error);QVERIFY2(!full.isNull(),qPrintable(error));QCOMPARE(pixels(full),first);
            QCOMPARE(*frozen,frozenValue);qInfo().noquote()<<"feather-full-source"<<full.size()<<first;
        }
    }
    void featherReturnToBaseAndPendingFinishDoesNotCloseNewGesture(){
        QTemporaryDir dir;Window w(nullptr,QStringLiteral(""));auto *c=w.findChild<Canvas *>("photo-canvas");QSignalSpy paint(c,&Canvas::canonicalFramePainted);QVERIFY(setup(w,c,paint,dir));
        auto *feather=w.findChild<QDoubleSpinBox *>("local-mask-feather");auto *slider=w.findChild<QSlider *>("slider-local-mask-feather");
        spin(w.findChild<QDoubleSpinBox *>("local-exposure"),.4);paint.clear();QVERIFY(waitFor([&]{return painted(w,c,paint);}));
        slider->setSliderDown(true);feather->setValue(5);feather->setValue(0);slider->setSliderDown(false);QTest::qWait(10);
        w.undoEdit();QCOMPARE(w.currentPhotoDocument()->layers[0].parameters.exposure,0.);QCOMPARE(w.currentPhotoDocument()->layers[0].mask.feather,0.);
        paint.clear();QVERIFY(waitFor([&]{return painted(w,c,paint);}));
        feather->setValue(2.5);QMetaObject::invokeMethod(feather,"editingFinished",Qt::DirectConnection);
        const auto from=point(c,{.3,.4}),to=point(c,{.7,.4});QTest::mousePress(c,Qt::LeftButton,Qt::NoModifier,from);QTest::qWait(20);QVERIFY(c->brushGestureActive());move(c,to);QTest::mouseRelease(c,Qt::LeftButton,Qt::NoModifier,to);
        QCOMPARE(w.currentPhotoDocument()->layers[0].mask.components.size(),1);QCOMPARE(w.currentPhotoDocument()->layers[0].mask.feather,2.5);
        w.undoEdit();QVERIFY(w.currentPhotoDocument()->layers[0].mask.components.isEmpty());QCOMPARE(w.currentPhotoDocument()->layers[0].mask.feather,2.5);
    }
};
QTEST_MAIN(LocalMaskWindowTests)
#include "local_mask_window_tests.moc"
