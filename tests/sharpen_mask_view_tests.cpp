// Sharpen edge-mask view tests: the diagnostic grayscale mask and its temporary
// RenderView flag. Synthetic frames only; expected coefficients are computed here
// from the documented formula, never by calling the production helper.
#include <QtTest>
#include <limits>
#include <cmath>
#include <algorithm>
#include "render/spatial_operator.h"
#include "render/cpu_renderer.h"
#include "render/geometry_transform.h"
#include "color/color_pipeline.h"
#include "io/preview_size.h"
#include "app/window.h"
#include "inspector_navigation_support.h"
#include "app/edit_loader.h"
#include "app/canvas.h"
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QListWidget>
#include <QSlider>
#include <QFileInfo>

class SharpenMaskViewTests:public QObject {
    Q_OBJECT
private slots:
    void basisWeightEdgesAndPartialStrength();
    void basisWeightRejectsInvalidStrengthAndMissingEdges();
    void rendererFixtureGeometryAndView();
    void renderFrameViewMatchesLinearCachedAndUncached();
    void renderLinearMaskIgnoresToneAndGrain();
    void defaultRenderUnaffectedAndViewNotSerialized();
    void longEdgeAndSmallSourceGeometry();
    void defringeBranchMaskAndNeutral();
    void nativeWindowModesAndCacheIsolation();
};

static const double coefficientNan=std::numeric_limits<double>::quiet_NaN();
static const double coefficientInf=std::numeric_limits<double>::infinity();

static const float *pixelAt(const QImage &image,int x,int y){return reinterpret_cast<const float *>(image.constScanLine(y))+4*x;}

// Independent transcription of the documented coefficient: a smoothstep ramp in
// the normalized edge over the [.8*strength, 1.0*strength] band, blended with the
// floor (1-strength) so that zero strength is a constant one.
static double expectedCoefficient(std::uint16_t edge,double strength){
    const double s=double(float(strength)),t=std::clamp((double(edge)/65535.0-.8*s)/.2,0.0,1.0);
    return 1-s+s*t*t*(3-2*t);
}
static double expectedCoefficient(std::uint16_t edge,double strength,bool transparent){
    return transparent?0.0:strength==0?1.0:expectedCoefficient(edge,strength);
}

// One fixed 96x64 basis. Column 0 is a flat opaque dark ramp step, column 32 a flat
// light one. Edge rows probe the coefficient band at full alpha; column 63 carries
// 32768 at the tested alpha values.
static SpatialBasis fixedBasis(){
    SpatialBasis basis;
    basis.before=QImage(96,64,QImage::Format_RGBA32FPx4);
    basis.sharpenEdges.assign(96*64,0);
    basis.sharpenRadius=1;
    for(int y=0;y<64;++y){auto *p=reinterpret_cast<float *>(basis.before.scanLine(y));for(int x=0;x<96;++x){
        const float gray=x==0?0.f:x==32?1.f:x==63?.5f:.25f;p[4*x]=gray;p[4*x+1]=gray;p[4*x+2]=gray;p[4*x+3]=1.f;}}
    const auto set=[&](int x,int y,std::uint16_t edge,float alpha){
        basis.sharpenEdges[size_t(y)*96+x]=edge;auto *p=reinterpret_cast<float *>(basis.before.scanLine(y))+4*x;p[3]=alpha;};
    set(0,0,0,1.f);set(0,1,32768,1.f);set(0,2,65535,1.f);set(0,3,0,1.f);
    set(63,0,32768,.5f);set(63,1,32768,0.f);
    // A transparent pixel keeps its own (zeroed) RGB in the source frame.
    auto *transparent=reinterpret_cast<float *>(basis.before.scanLine(1))+4*63;
    transparent[0]=transparent[1]=transparent[2]=0.f;
    return basis;
}

// Spatial frame used by the renderer fixture: a vertical edge plus flat regions.
static QImage spatialPattern(int w=96,int h=64){
    QImage image(w,h,QImage::Format_RGBA32FPx4);
    for(int y=0;y<h;++y){auto *p=reinterpret_cast<float *>(image.scanLine(y));for(int x=0;x<w;++x){
        const float gray=x==0?.02f:x==32?.9f:.25f;p[4*x]=gray;p[4*x+1]=gray;p[4*x+2]=gray;p[4*x+3]=1.f;}}
    return image;
}

// The renderer path always prepares the mask basis with sourcePixelsPerPixel=1 for a
// unit-scale neutral source, so its edges can be reproduced by prepareSpatial.
static SpatialBasis rendererMaskBasis(const SourceFrame &source,int longEdge,const Recipe &recipe,QSize &size){
    QString error;auto prefix=preparedGeometry(recipe);prefix.flipHorizontal=recipe.flipHorizontal;prefix.flipVertical=recipe.flipVertical;
    const auto neutral=source.linear;
    const auto full=renderedSize(neutral.size(),recipe),target=longEdge==0?full:previewSize(full,longEdge);
    const double scale=source.sourcePixelsPerPixel*std::max(neutral.width()*recipe.crop.width()/target.width(),neutral.height()*recipe.crop.height()/target.height());
    const double radius=recipe.sharpenMask==0?0:recipe.sharpenRadius;
    auto before=renderLinear(source,prefix,longEdge,error);
    if(before.isNull()){QTest::qFail(qPrintable(error),__FILE__,__LINE__);return {};}
    auto basis=prepareSpatial(std::move(before),scale,source.originalSize,error,false,false,radius,false,false,recipe.sharpenMask!=0);
    if(!basis){QTest::qFail(qPrintable(error),__FILE__,__LINE__);return {};}
    size=target;
    return *basis;
}

static void verifyMaskPixels(const QImage &mask,QSize expectedSize,const SpatialBasis &basis,double strength){
    QCOMPARE(mask.size(),expectedSize);
    QCOMPARE(mask.format(),QImage::Format_RGBA32FPx4);
    double residual=0;
    for(int y=0;y<mask.height();++y)for(int x=0;x<mask.width();++x){
        const auto *p=pixelAt(mask,x,y),*s=pixelAt(basis.before,x,y);
        const double expected=expectedCoefficient(strength==0?0:basis.sharpenEdges[size_t(y)*mask.width()+x],strength,p[3]<=1e-8f);
        for(int c=0;c<3;++c)residual=std::max(residual,std::abs(double(p[c])-expected));
        residual=std::max(residual,std::abs(double(p[3])-double(s[3])));
        if(p[3]<=1e-8f)for(int c=0;c<3;++c)QCOMPARE(p[c],0.f);
    }
    QVERIFY2(residual<=1e-6,qPrintable(QStringLiteral("mask residual %1").arg(residual,0,'g',12)));
}

void SharpenMaskViewTests::basisWeightEdgesAndPartialStrength(){
    const auto basis=fixedBasis();
    QCOMPARE(basis.before.size(),QSize(96,64));
    QCOMPARE(basis.sharpenEdges.size(),size_t(96*64));
    const auto unchanged=basis.before;
    for(double strength:{0.,.35,.5,1.}){
        QString error;const auto mask=renderSharpenMask(basis,strength,error);
        QVERIFY2(error.isEmpty(),qPrintable(error));
        verifyMaskPixels(mask,{96,64},basis,strength);
    }
    // Zero strength is the constant white full-coverage map.
    QString error;const auto white=renderSharpenMask(basis,0.,error);QVERIFY(error.isEmpty());
    for(int y: {0,1,2,3})for(int x:{0,63})for(int c=0;c<3;++c)QCOMPARE(pixelAt(white,x,y)[c],pixelAt(basis.before,x,y)[3]==0?0.f:1.f);
    // Full strength is black on flat pixels and white on the fixed strongest edge.
    const auto full=renderSharpenMask(basis,1.,error);QVERIFY(error.isEmpty());
    for(const auto &flat:{QPoint(0,0),QPoint(32,32),QPoint(95,63)})for(int c=0;c<3;++c)QCOMPARE(pixelAt(full,flat.x(),flat.y())[c],0.f);
    for(int c=0;c<3;++c)QCOMPARE(pixelAt(full,0,2)[c],1.f);
    // A half-scale edge falls below the full-strength ramp's 0.8 threshold.
    QCOMPARE(pixelAt(full,0,1)[0],0.f);
    // Partial strength keeps an exact floor of 1-strength before the ramp.
    for(double strength:{.35,.5}){
        const auto mask=renderSharpenMask(basis,strength,error);QVERIFY(error.isEmpty());
        const float floor=float(1-strength);
        QCOMPARE(pixelAt(mask,0,0)[0],floor);QCOMPARE(pixelAt(mask,32,32)[0],floor);
        QVERIFY(std::abs(pixelAt(mask,0,1)[0]-expectedCoefficient(32768,strength))<=1e-6);
        QVERIFY(pixelAt(mask,0,2)[0]>floor);QCOMPARE(pixelAt(mask,0,2)[0],1.f);
        QVERIFY(pixelAt(mask,0,1)[0]>=pixelAt(mask,0,0)[0]);
    }
    // Alpha is copied through and fully transparent pixels stay zeroed.
    const auto alphaMask=renderSharpenMask(basis,.6,error);QVERIFY(error.isEmpty());
    QCOMPARE(pixelAt(alphaMask,63,0)[3],.5f);QCOMPARE(pixelAt(alphaMask,63,1)[3],0.f);
    QCOMPARE(pixelAt(alphaMask,17,20)[3],1.f);
    for(int c=0;c<3;++c)QCOMPARE(pixelAt(alphaMask,63,1)[c],0.f);
    // The source frame is never modified by rendering the mask.
    QCOMPARE(basis.before,unchanged);
}

void SharpenMaskViewTests::basisWeightRejectsInvalidStrengthAndMissingEdges(){
    const auto basis=fixedBasis();
    for(double strength:{-1.,-0.001,1.000001,1.5,2.,coefficientNan,coefficientInf,-coefficientInf}){
        QString error;QVERIFY(renderSharpenMask(basis,strength,error).isNull());QVERIFY(!error.isEmpty());
    }
    const std::uint16_t edges[]{0,32768,65535};
    for(auto edge:edges)QCOMPARE(sharpenMaskWeight(edge,1.),float(expectedCoefficient(edge,1.)));
    QCOMPARE(sharpenMaskWeight(edges[0],0.),float(expectedCoefficient(edges[0],0.)));
    QCOMPARE(sharpenMaskWeight(edges[1],.5),float(expectedCoefficient(edges[1],.5)));
    QCOMPARE(sharpenMaskWeight(edges[2],.25),float(expectedCoefficient(edges[2],.25)));
    // The mask weight is monotone in the edge and bounded by [1-strength,1].
    double previous=-1;
    for(std::uint16_t edge:{std::uint16_t(0),std::uint16_t(8192),std::uint16_t(32768),std::uint16_t(49152),std::uint16_t(65535)}){
        const double value=expectedCoefficient(edge,.7);QVERIFY(value>=previous);QVERIFY(value>=.3-1e-7);QVERIFY(value<=1+1e-7);previous=value;
    }
    QString error;
    auto empty=basis;empty.sharpenEdges.clear();
    QVERIFY(renderSharpenMask(empty,.5,error).isNull());QVERIFY(!error.isEmpty());
    auto shortEdges=basis;shortEdges.sharpenEdges.resize(size_t(96*63));
    QVERIFY(renderSharpenMask(shortEdges,.5,error).isNull());QVERIFY(!error.isEmpty());
    auto nullFrame=basis;nullFrame.before=QImage{};
    QVERIFY(renderSharpenMask(nullFrame,.5,error).isNull());QVERIFY(!error.isEmpty());
    auto wrongFormat=basis;wrongFormat.before=QImage(96,64,QImage::Format_RGB32);
    QVERIFY(renderSharpenMask(wrongFormat,.5,error).isNull());QVERIFY(!error.isEmpty());
    for(float alpha:{-1.f,2.f,std::numeric_limits<float>::quiet_NaN()}){
        auto poisoned=basis;reinterpret_cast<float *>(poisoned.before.scanLine(0))[3]=alpha;
        QVERIFY(renderSharpenMask(poisoned,0,error).isNull());QVERIFY(!error.isEmpty());
    }
    // Missing edges are tolerated only when the requested strength cannot use them.
    QVERIFY(!renderSharpenMask(empty,0.,error).isNull());QVERIFY(error.isEmpty());
}

void SharpenMaskViewTests::rendererFixtureGeometryAndView(){
    SourceFrame source;source.linear=spatialPattern();source.originalSize=source.linear.size();
    source.sourcePixelsPerPixel=1;
    Recipe recipe;recipe.sharpenAmount=.6;recipe.sharpenRadius=1.5;recipe.sharpenMask=.5;
    recipe.crop={.1,.1,.8,.8};recipe.angle=11;recipe.flipHorizontal=true;
    recipe.exposure=.2;recipe.contrast=.12;recipe.vibrance=.3;
    for(const RenderView &view:{RenderView{-1,true}}){
        QString error;const auto linear=renderLinear(source,recipe,0,error,view);
        QVERIFY2(!linear.isNull(),qPrintable(error));
        QSize size;const auto basis=rendererMaskBasis(source,0,recipe,size);
        QCOMPARE(linear.size(),size);
        verifyMaskPixels(linear,size,basis,recipe.sharpenMask);
        // No sharpen or texture basis exists here, so the same fill is applied per pixel.
        const auto flat=pixelAt(linear,size.width()/2,size.height()/2);
        QVERIFY(flat[0]==flat[1]&&flat[1]==flat[2]);
        QVERIFY(flat[0]>=float(1-recipe.sharpenMask)-1e-6f);
        QVERIFY(flat[0]<=1.f);
    }
    // A crop/flip-only recipe with no sharpen amount still returns the full map.
    Recipe viewOnly;viewOnly.crop={.2,.2,.5,.5};viewOnly.angle=7;viewOnly.flipVertical=true;
    QString error;const auto plain=renderLinear(source,viewOnly,0,error,{});
    const auto masked=renderLinear(source,viewOnly,0,error,{-1,true});
    QVERIFY2(!masked.isNull(),qPrintable(error));
    QCOMPARE(masked.size(),plain.size());
    for(int y=0;y<masked.height();y+=7)for(int x=0;x<masked.width();x+=7){
        for(int c=0;c<3;++c)QCOMPARE(pixelAt(masked,x,y)[c],1.f);
        QCOMPARE(pixelAt(masked,x,y)[3],pixelAt(plain,x,y)[3]);}
    // Without a usable mask basis, sharpen amount with an active mask still renders.
    const auto directMask=renderSharpenMask(fixedBasis(),.5,error);QVERIFY(!directMask.isNull());
}

void SharpenMaskViewTests::renderFrameViewMatchesLinearCachedAndUncached(){
    SourceFrame source;source.linear=spatialPattern();source.originalSize=source.linear.size();
    source.sourcePixelsPerPixel=1;
    Recipe recipe;recipe.sharpenAmount=.5;recipe.sharpenRadius=2;recipe.sharpenMask=.6;
    recipe.texture=.2;recipe.clarity=.1;recipe.exposure=.15;recipe.grainAmount=.2;recipe.grainSize=3;
    recipe.crop={.05,.1,.9,.85};recipe.angle=6;recipe.flipHorizontal=true;
    SpatialCache cache;const RenderView view{-1,true};
    QString error;
    const auto reference=workToSrgbReference(renderLinear(source,recipe,0,error,view),error);
    QVERIFY2(error.isEmpty(),qPrintable(error));
    const auto cached=renderFrame(source,recipe,0,&cache,view);
    QVERIFY2(cached.error.isEmpty(),qPrintable(cached.error));
    QCOMPARE(cached.image.size(),reference.size());
    int difference=0;
    for(qsizetype i=0;i<reference.sizeInBytes();++i)difference=std::max(difference,std::abs(int(reference.constBits()[i])-int(cached.image.constBits()[i])));
    QVERIFY2(difference<=1,qPrintable(QStringLiteral("cached difference %1").arg(difference)));
    const auto uncached=renderFrame(source,recipe,0,nullptr,view);
    QVERIFY2(uncached.error.isEmpty(),qPrintable(uncached.error));
    difference=0;
    for(qsizetype i=0;i<reference.sizeInBytes();++i)difference=std::max(difference,std::abs(int(reference.constBits()[i])-int(uncached.image.constBits()[i])));
    QVERIFY2(difference<=1,qPrintable(QStringLiteral("uncached difference %1").arg(difference)));
    QCOMPARE(cached.image,uncached.image);
    QCOMPARE(cached.fullSize,renderedSize(source.linear.size(),recipe));
    // Reusing the same cache for the plain render afterwards is self-consistent.
    const auto normal=renderFrame(source,recipe,0,&cache);
    QVERIFY2(normal.error.isEmpty(),qPrintable(normal.error));
    const auto again=renderFrame(source,recipe,0,nullptr);
    QCOMPARE(normal.image,again.image);
}

void SharpenMaskViewTests::renderLinearMaskIgnoresToneAndGrain(){
    SourceFrame source;source.linear=spatialPattern();source.originalSize=source.linear.size();
    source.sourcePixelsPerPixel=1;
    Recipe base;base.sharpenAmount=.4;base.sharpenRadius=1;base.sharpenMask=.45;
    base.crop={.1,.1,.8,.8};base.angle=9;base.flipHorizontal=true;base.vibrance=.2;
    QString error;const auto reference=renderLinear(source,base,0,error,{-1,true});
    QVERIFY2(!reference.isNull(),qPrintable(error));
    Recipe changed=base;changed.exposure=.8;changed.contrast=-.4;changed.saturation=1.4;changed.warmth=.3;changed.tint=-.2;
    changed.highlights=.5;changed.shadows=-.5;changed.whites=.3;changed.blacks=-.3;changed.vibrance=1;
    changed.grainAmount=.6;changed.grainSize=7;changed.grainRoughness=.9;changed.vignetteAmount=-1;changed.monochrome=true;
    changed.texture=.4;changed.clarity=.3;changed.sharpenAmount=1.2;changed.sharpenThreshold=.02;
    const auto altered=renderLinear(source,changed,0,error,{-1,true});
    QVERIFY2(!altered.isNull(),qPrintable(error));
    QCOMPARE(altered,reference);
    // The grayscale guarantee: all three channels equal, alpha preserved.
    for(int y=0;y<altered.height();y+=5)for(int x=0;x<altered.width();x+=5){
        const auto *p=pixelAt(altered,x,y);QCOMPARE(p[0],p[1]);QCOMPARE(p[1],p[2]);QVERIFY(p[3]>=0.f&&p[3]<=1.f);}
    // Transparent source pixels stay zeroed through the mask view.
    auto alphaSource=source;for(int y=0;y<alphaSource.linear.height();++y){auto *p=reinterpret_cast<float *>(alphaSource.linear.scanLine(y));for(int x=0;x<8;++x){p[4*x]=.9f;p[4*x+3]=0.f;}}
    auto alphaRecipe=base;alphaRecipe.crop={0,0,1,1};alphaRecipe.angle=0;alphaRecipe.flipHorizontal=false;
    const auto alphaMask=renderLinear(alphaSource,alphaRecipe,0,error,{-1,true});
    QVERIFY2(!alphaMask.isNull(),qPrintable(error));
    for(int y=0;y<alphaMask.height();y+=4)for(int x=0;x<3;++x){const auto *p=pixelAt(alphaMask,x,y);QCOMPARE(p[3],0.f);QCOMPARE(p[0],0.f);QCOMPARE(p[1],0.f);QCOMPARE(p[2],0.f);}
}

void SharpenMaskViewTests::defaultRenderUnaffectedAndViewNotSerialized(){
    SourceFrame source;source.linear=spatialPattern();source.originalSize=source.linear.size();
    source.sourcePixelsPerPixel=1;
    Recipe recipe;recipe.sharpenAmount=.5;recipe.sharpenRadius=1.5;recipe.sharpenMask=.5;recipe.texture=.2;
    SpatialCache cache;QString error;
    const auto before=renderFrame(source,recipe,0,&cache);
    QVERIFY2(before.error.isEmpty(),qPrintable(before.error));
    const RenderView view{-1,true};
    const auto masked=renderFrame(source,recipe,0,&cache,view);
    QVERIFY2(masked.error.isEmpty(),qPrintable(masked.error));
    QVERIFY(before.image!=masked.image);
    cache.clear();
    const auto after=renderFrame(source,recipe,0,&cache);
    QCOMPARE(after.image,before.image);
    QCOMPARE(after.fullSize,before.fullSize);
    const auto linear=renderLinear(source,recipe,0,error,view);
    QVERIFY(!linear.isNull());
    QCOMPARE(linear,renderLinear(source,recipe,0,error,view));
    // The view flag is transient: no recipe or JSON surface exposes it.
    const auto json=recipeJson(recipe);
    QVERIFY(!json.contains("sharpenMaskView"));
    QVERIFY(!json.contains("pointRange"));
    QVERIFY(!json.contains("view"));
    QCOMPARE(recipeFromJson(json),recipe);
    QCOMPARE((RenderView{}),(RenderView{-1,false}));
    QVERIFY((RenderView{0,true}!=RenderView{0,false}));
}

void SharpenMaskViewTests::longEdgeAndSmallSourceGeometry(){
    // The cropped synthetic source is 4000 pixels wide; preview retains a 3840 long edge.
    SourceFrame source;source.linear=spatialPattern(8000,128);source.originalSize=source.linear.size();
    source.sourcePixelsPerPixel=1;source.raw=true;
    Recipe zoom;zoom.sharpenAmount=.4;zoom.sharpenRadius=1;zoom.sharpenMask=.5;
    zoom.crop={.25,.1,.5,.8};zoom.angle=5;zoom.flipHorizontal=true;zoom.exposure=.1;
    QString error;const auto full =renderLinear(source,zoom,0,error,{-1,true});
    QVERIFY2(!full.isNull(),qPrintable(error));
    QSize fullSize;const auto fullBasis=rendererMaskBasis(source,0,zoom,fullSize);
    QCOMPARE(full.size(),fullSize);
    verifyMaskPixels(full,fullSize,fullBasis,zoom.sharpenMask);
    const auto preview=renderLinear(source,zoom,3840,error,{-1,true});
    QVERIFY2(!preview.isNull(),qPrintable(error));
    QSize previewSize2;const auto previewBasis=rendererMaskBasis(source,3840,zoom,previewSize2);
    QCOMPARE(preview.size(),previewSize2);
    verifyMaskPixels(preview,previewSize2,previewBasis,zoom.sharpenMask);
    const auto frame=renderFrame(source,zoom,3840,nullptr,{-1,true});
    QVERIFY2(frame.error.isEmpty(),qPrintable(frame.error));
    QCOMPARE(frame.fullSize,renderedSize(source.linear.size(),zoom));
    QCOMPARE(frame.image.size(),previewSize2);QCOMPARE(qMax(frame.image.width(),frame.image.height()),3840);
    // Small sources are never upscaled by the preview bound.
    SourceFrame small;small.linear=spatialPattern(64,48);small.originalSize=small.linear.size();
    small.sourcePixelsPerPixel=1;
    Recipe tiny;tiny.sharpenAmount=.3;tiny.sharpenRadius=1;tiny.sharpenMask=.7;
    const auto smallMask=renderLinear(small,tiny,0,error,{-1,true});
    QVERIFY2(!smallMask.isNull(),qPrintable(error));
    QCOMPARE(smallMask.size(),QSize(64,48));
    QSize smallSize;const auto smallBasis=rendererMaskBasis(small,0,tiny,smallSize);
    verifyMaskPixels(smallMask,smallSize,smallBasis,tiny.sharpenMask);
    const auto smallFrame=renderFrame(small,tiny,3840,nullptr,{-1,true});
    QVERIFY2(smallFrame.error.isEmpty(),qPrintable(smallFrame.error));
    QCOMPARE(smallFrame.image.size(),QSize(64,48));
}

void SharpenMaskViewTests::defringeBranchMaskAndNeutral(){
    SourceFrame source;source.linear=spatialPattern();source.originalSize=source.linear.size();
    source.sourcePixelsPerPixel=1;
    Recipe neutral;neutral.sharpenAmount=.4;neutral.sharpenRadius=1;neutral.sharpenMask=.5;
    QString error;const auto direct=renderLinear(source,neutral,0,error,{-1,true});
    QVERIFY2(!direct.isNull(),qPrintable(error));
    Recipe routed=neutral;routed.defringe.purpleAmount=.6;routed.defringe.greenAmount=.2;
    const auto viaDefringe=renderLinear(source,routed,0,error,{-1,true});
    QVERIFY2(!viaDefringe.isNull(),qPrintable(error));
    const auto changed=renderLinear(source,routed,0,error,{-1,true});
    QCOMPARE(changed,viaDefringe);
    for(int y=0;y<direct.height();y+=6)for(int x=0;x<direct.width();x+=6){const auto *a=pixelAt(direct,x,y),*b=pixelAt(viaDefringe,x,y);
        QCOMPARE(a[3],b[3]);QVERIFY(std::abs(double(a[0])-double(b[0]))<=1.0);}
    auto off=routed;off.defringe={};off.sharpenMask=.5;
    QCOMPARE(renderLinear(source,off,0,error,{-1,true}),direct);
}

void SharpenMaskViewTests::nativeWindowModesAndCacheIsolation(){
    QTemporaryDir temporary;QVERIFY(temporary.isValid());
    QImage photo(192,128,QImage::Format_RGB888);
    for(int y=0;y<photo.height();++y)for(int x=0;x<photo.width();++x)photo.setPixelColor(x,y,x<96?QColor(30,30,30):QColor(220,180,140));
    const auto external=qEnvironmentVariable("LUMINA_MASK_TEST_PHOTO");
    const auto first=external.isEmpty()?temporary.filePath("first.jpg"):QFileInfo(external).canonicalFilePath(),second=temporary.filePath("second.jpg");
    QVERIFY(!first.isEmpty());if(external.isEmpty())QVERIFY(photo.save(first));QVERIFY(photo.save(second));
    Window window(nullptr,QStringLiteral(""));window.show();window.addFiles({first,second});
    QVERIFY(openAllInspectorSections(window));
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    auto *button=window.findChild<QPushButton *>("sharpen-mask-preview");QVERIFY(button);
    auto *loader=window.findChild<EditLoader *>();QVERIFY(loader);QSignalSpy results(loader,&EditLoader::ready);
    auto *mask=window.findChild<QDoubleSpinBox *>("sharpen-mask");QVERIFY(mask);
    Recipe recipe=window.currentRecipe();recipe.sharpenAmount=.4;recipe.sharpenMask=.75;window.applyRecipe(recipe);
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),90000);
    const auto originalRecipe=window.currentRecipe();const auto iconKey=window.filmstrip()->item(0)->icon().cacheKey();
    if(auto *choice=window.findChild<QComboBox *>("adjustment-section-choice"))for(int i=0;i<choice->count();++i)if(choice->itemText(i).contains(QStringLiteral("细节"))){choice->setCurrentIndex(i);break;}
    results.clear();button->click();QVERIFY(button->isChecked());QVERIFY(!window.neutralPickerAvailable());
    const auto deliveredMask=[&]{for(const auto &record:results){const auto result=qvariant_cast<EditResult>(record.at(0));if(result.path==first&&result.view.sharpenMask&&!result.image.isNull())return true;}return false;};
    QTRY_VERIFY_WITH_TIMEOUT(deliveredMask(),90000);QCOMPARE(window.currentRecipe(),originalRecipe);
    if(!external.isEmpty())for(const auto &record:results){const auto result=qvariant_cast<EditResult>(record.at(0));if(result.path==first&&result.view.sharpenMask&&!result.image.isNull())QCOMPARE(qMax(result.image.width(),result.image.height()),3840);}
    QCOMPARE(window.filmstrip()->item(0)->icon().cacheKey(),iconKey);
    const auto shot=qEnvironmentVariable("LUMINA_MASK_SCREENSHOT");if(external.isEmpty()&&!shot.isEmpty()){QTest::qWait(100);QVERIFY(window.grab().save(shot));}
    const auto cached=loader->preview(first,originalRecipe);QVERIFY(cached);QVERIFY(!cached->view.sharpenMask);
    auto *bar=window.findChild<QSlider *>("slider-sharpen-mask");QVERIFY(bar);
    auto *canvas=window.findChild<Canvas *>();QVERIFY(canvas);QSignalSpy paints(canvas,&Canvas::framePainted);QSignalSpy previews(&window,&Window::previewChanged);
    bar->setSliderDown(true);for(int value=76;value<=90;++value){bar->setValue(value);QTest::qWait(120);}
    qInfo()<<"mask drag previews"<<previews.count()<<"paints"<<paints.count();QVERIFY(previews.count()>=2);QVERIFY(paints.count()>=2);bar->setSliderDown(false);
    QCOMPARE(window.currentRecipe().sharpenMask,.9);QVERIFY(button->isChecked());
    button->click();QVERIFY(!button->isChecked());QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),30000);
    window.undoEdit();QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),30000);QCOMPARE(window.currentRecipe(),originalRecipe);
    button->click();QVERIFY(button->isChecked());window.compareOriginal(true);QVERIFY(!button->isChecked());window.compareOriginal(false);
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),30000);
    button->click();QVERIFY(button->isChecked());QTest::keyClick(&window,Qt::Key_Escape);QVERIFY(!button->isChecked());
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),30000);
    button->click();QVERIFY(button->isChecked());window.activate(1);QVERIFY(!button->isChecked());
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(),30000);QCOMPARE(window.currentRecipe(),Recipe{});
}
QTEST_MAIN(SharpenMaskViewTests)
#include "sharpen_mask_view_tests.moc"
