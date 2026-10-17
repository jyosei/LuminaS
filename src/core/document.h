#pragma once
#include <QImage>
#include <QRectF>
#include <QJsonObject>
#include <QString>
#include <memory>
#include <array>
#include <QVector>
#include <QMetaType>
#include "defringe_settings.h"
#include "source_metadata.h"
constexpr int maximumCurvePoints=64;
using Curve=QVector<QPointF>;
struct HslAdjustment { double hue=0,saturation=0,luminance=0; bool operator==(const HslAdjustment &) const = default; };
struct GradeAdjustment { double hue=0,saturation=0,luminance=0; bool operator==(const GradeAdjustment &) const = default; };
struct PrimaryAdjustment { double hue=0,saturation=0;bool operator==(const PrimaryAdjustment &) const = default; };
struct ParametricCurve {std::array<double,4> regions{};std::array<double,3> splits{.25,.5,.75};bool operator==(const ParametricCurve &) const = default;};
constexpr int maximumPointColors=8;
struct PointColorAdjustment {
    double hue=0,saturation=.5,luminance=.5; // post-tone work RGB descriptor, Y/(Y+.18)
    double hueWidth=30,saturationWidth=.25,luminanceWidth=.25,feather=.5;
    double hueShift=0,saturationShift=0,luminanceShift=0;
    bool operator==(const PointColorAdjustment &) const = default;
};
Q_DECLARE_METATYPE(PointColorAdjustment)
struct Recipe {
    double exposure=0,contrast=0,saturation=1,warmth=0,tint=0,angle=0;
    QPointF perspective{0,0}; // bounded inverse projective tilt of the normalized full canvas
    double distortion=0; // [-.4,.4], isotropic bounded radial inverse-map coefficient
    QPointF chromaticAberration{0,0}; // red/blue corner displacement in original pixels, [-10,10]
    double lensShading=0; // manual radial source-plane brightness correction, EV at corners
    double highlights=0,shadows=0,whites=0,blacks=0,vibrance=0;
    double texture=0,clarity=0,dehaze=0;
    double sharpenAmount=0,sharpenRadius=1,sharpenThreshold=0; // radius in direction-corrected original source pixels
    double sharpenMask=0;
    double noiseLuminance=0,noiseChroma=0;
    double vignetteAmount=0,vignetteMidpoint=.5,vignetteFeather=.5; // amount in EV, midpoint/feather normalized
    std::array<PrimaryAdjustment,3> primaries{};
    double grainAmount=0,grainSize=3,grainRoughness=.5;
    std::array<Curve,4> curves{Curve{{0,0},{1,1}},Curve{{0,0},{1,1}},Curve{{0,0},{1,1}},Curve{{0,0},{1,1}}};
    std::array<QVector<double>,4> curveTangents{};
    Curve luminanceCurve{{0,0},{1,1}};
    QVector<double> luminanceTangents;
    ParametricCurve parametricCurve;
    std::array<HslAdjustment,8> hsl{}; // red, orange, yellow, green, aqua, blue, purple, magenta
    QVector<PointColorAdjustment> pointColors;
    std::array<GradeAdjustment,3> grading{}; // shadows, midtones, highlights; hue in degrees
    double gradingBlend=.5,gradingBalance=0;
    bool monochrome=false;
    double monochromeStrength=1; // [0,1]
    DefringeSettings defringe;
    bool flipHorizontal=false,flipVertical=false;
    std::array<double,8> monochromeMix{};
    QRectF crop{0,0,1,1}; // normalized corrected full-canvas coordinates; outputToSource maps back to original
    bool operator==(const Recipe &) const = default;
};
struct SourceFrame {
    QString path,sha256,colorNote;
    QImage linear; // straight RGBA float32, linear Rec.2020 D65, immutable after load
    bool raw=false;
    double rawWarmth=0,rawTint=0;
    QJsonObject facts;
    QSize originalSize;
    double sourcePixelsPerPixel=1; // physical support scale of this immutable working frame
    double sourcePixelsPerPixelX=0,sourcePixelsPerPixelY=0; // 0: use legacy isotropic support
    double pixelScaleX() const{return sourcePixelsPerPixelX==0?sourcePixelsPerPixel:sourcePixelsPerPixelX;}
    double pixelScaleY() const{return sourcePixelsPerPixelY==0?sourcePixelsPerPixel:sourcePixelsPerPixelY;}
    std::array<float,9> draftBalance{1,0,0,0,1,0,0,0,1};
    std::array<float,9> cameraToWork{};
    std::array<double,4> cameraWB{};
    SourceMetadata deliveryMetadata;
    bool hasDeliveryMetadata=false; // Not sourceFacts/project identity or Recipe.
};
struct FrameResult { std::shared_ptr<const SourceFrame> source; QString error; };
QJsonObject recipeJson(const Recipe &recipe);
Recipe recipeFromJson(const QJsonObject &json);
bool validRecipe(const Recipe &recipe);
bool hasColorEditing(const Recipe &recipe);

