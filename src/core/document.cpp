#include "document.h"
#include <cmath>
#include <QJsonArray>
#include "curve_spline.h"
#include "point_color_json.h"
#include "perspective_json.h"
#include "chromatic_aberration_json.h"
#include "defringe_json.h"
QJsonObject recipeJson(const Recipe &r) {
    // Grain is an artistic global adjustment; geometry and WB remain independent.
    QJsonObject result{{"exposure",r.exposure},{"contrast",r.contrast},{"saturation",r.saturation},
        {"warmth",r.warmth},{"tint",r.tint},{"angle",r.angle},
        {"highlights",r.highlights},{"shadows",r.shadows},{"whites",r.whites},{"blacks",r.blacks},{"vibrance",r.vibrance},
        {"texture",r.texture},{"clarity",r.clarity},{"dehaze",r.dehaze},
        {"sharpenAmount",r.sharpenAmount},{"sharpenRadius",r.sharpenRadius},{"sharpenThreshold",r.sharpenThreshold},
        {"sharpenMask",r.sharpenMask},
        {"noiseLuminance",r.noiseLuminance},{"noiseChroma",r.noiseChroma},
        {"vignetteAmount",r.vignetteAmount},{"vignetteMidpoint",r.vignetteMidpoint},{"vignetteFeather",r.vignetteFeather},
        {"x",r.crop.x()},{"y",r.crop.y()},{"width",r.crop.width()},{"height",r.crop.height()}};
    result.insert("lensShading",r.lensShading);
    result.insert("distortion",r.distortion);
    result.insert("perspective",perspectiveJson(r.perspective));
    result.insert("chromaticAberration",chromaticAberrationJson(r.chromaticAberration));
    result.insert("defringe",defringeJson(r.defringe));
    result.insert("pointColors",pointColorsJson(r.pointColors));
    QJsonArray curves,hsl;for(const auto &curve:r.curves){QJsonArray points;for(auto p:curve)points.append(QJsonArray{p.x(),p.y()});curves.append(points);}
    QJsonArray curveTangents;for(const auto &values:r.curveTangents){QJsonArray tangent;for(double v:values)tangent.append(v);curveTangents.append(tangent);}
    QJsonArray luminanceTangents;for(double v:r.luminanceTangents)luminanceTangents.append(v);
    result.insert("curveTangents",curveTangents);result.insert("luminanceTangents",luminanceTangents);
    for(const auto &a:r.hsl)hsl.append(QJsonObject{{"hue",a.hue},{"saturation",a.saturation},{"luminance",a.luminance}});
    QJsonArray grading,mix;for(const auto &a:r.grading)grading.append(QJsonObject{{"hue",a.hue},{"saturation",a.saturation},{"luminance",a.luminance}});for(double value:r.monochromeMix)mix.append(value);
    result.insert("grading",grading);result.insert("gradingBlend",r.gradingBlend);result.insert("gradingBalance",r.gradingBalance);result.insert("monochrome",r.monochrome);result.insert("monochromeStrength",r.monochromeStrength);result.insert("monochromeMix",mix);
    result.insert("flipHorizontal",r.flipHorizontal);result.insert("flipVertical",r.flipVertical);
    QJsonArray primaries;for(const auto &p:r.primaries)primaries.append(QJsonObject{{"hue",p.hue},{"saturation",p.saturation}});result.insert("primaries",primaries);
    QJsonArray luminance,regions,splits;for(auto p:r.luminanceCurve)luminance.append(QJsonArray{p.x(),p.y()});for(double v:r.parametricCurve.regions)regions.append(v);for(double v:r.parametricCurve.splits)splits.append(v);result.insert("luminanceCurve",luminance);result.insert("parametricCurve",QJsonObject{{"regions",regions},{"splits",splits}});
    result.insert("curves",curves);result.insert("hsl",hsl);result.insert("grainAmount",r.grainAmount);result.insert("grainSize",r.grainSize);result.insert("grainRoughness",r.grainRoughness);return result;
}
Recipe recipeFromJson(const QJsonObject &j) {
    Recipe r;
    if(j.contains("sharpenMask")&&!j.value("sharpenMask").isDouble()){r.sharpenMask=-1;return r;}r.sharpenMask=j.value("sharpenMask").toDouble();
    for(const auto &key:{"noiseLuminance","noiseChroma"})if(j.contains(key)&&!j.value(key).isDouble()){r.gradingBlend=-1;return r;}
    r.noiseLuminance=j.value("noiseLuminance").toDouble();r.noiseChroma=j.value("noiseChroma").toDouble();
    for(const auto &key:{"vignetteAmount","vignetteMidpoint","vignetteFeather"})if(j.contains(key)&&!j.value(key).isDouble()){r.vignetteMidpoint=-1;return r;}
    r.vignetteAmount=j.value("vignetteAmount").toDouble(0);r.vignetteMidpoint=j.value("vignetteMidpoint").toDouble(.5);r.vignetteFeather=j.value("vignetteFeather").toDouble(.5);
    for(const auto &key:{"exposure","contrast","saturation","warmth","tint","angle","highlights","shadows","whites","blacks","vibrance","texture","clarity","dehaze","sharpenAmount","sharpenRadius","sharpenThreshold","x","y","width","height"})if(j.contains(key)&&!j.value(key).isDouble()){r.gradingBlend=-1;return r;}
    r.exposure=j.value("exposure").toDouble(); r.contrast=j.value("contrast").toDouble();
    r.saturation=j.value("saturation").toDouble(1); r.warmth=j.value("warmth").toDouble();
    r.tint=j.value("tint").toDouble(); r.angle=j.value("angle").toDouble();
    r.highlights=j.value("highlights").toDouble();r.shadows=j.value("shadows").toDouble();
    r.whites=j.value("whites").toDouble();r.blacks=j.value("blacks").toDouble();r.vibrance=j.value("vibrance").toDouble();
    r.texture=j.value("texture").toDouble();r.clarity=j.value("clarity").toDouble();r.dehaze=j.value("dehaze").toDouble();
    r.sharpenAmount=j.value("sharpenAmount").toDouble();r.sharpenRadius=j.value("sharpenRadius").toDouble(1);r.sharpenThreshold=j.value("sharpenThreshold").toDouble();
    r.crop={j.value("x").toDouble(),j.value("y").toDouble(),j.value("width").toDouble(1),j.value("height").toDouble(1)};
    if(j.contains("curves")){auto channels=j.value("curves").toArray();if(channels.size()!=4){r.curves[0].clear();return r;}
        for(int c=0;c<4;++c){r.curves[c].clear();for(auto point:channels[c].toArray()){auto xy=point.toArray();if(xy.size()!=2||!xy[0].isDouble()||!xy[1].isDouble()){r.curves[c].clear();break;}r.curves[c].append({xy[0].toDouble(),xy[1].toDouble()});}}
    }
    if(j.contains("hsl")){auto colors=j.value("hsl").toArray();if(colors.size()!=8){r.hsl[0].hue=2;return r;}
        for(int c=0;c<8;++c){auto a=colors[c].toObject();if(!a.value("hue").isDouble()||!a.value("saturation").isDouble()||!a.value("luminance").isDouble()){r.hsl[c].hue=2;break;}r.hsl[c]={a.value("hue").toDouble(),a.value("saturation").toDouble(),a.value("luminance").toDouble()};}
    }
    auto invalid=[&]{r.gradingBlend=-1;return r;};
    if(j.contains("distortion")&&!j.value("distortion").isDouble())return invalid();r.distortion=j.value("distortion").toDouble(0);
    if(j.contains("lensShading")&&!j.value("lensShading").isDouble())return invalid();r.lensShading=j.value("lensShading").toDouble(0);
    if(j.contains("perspective")&&!perspectiveFromJson(j.value("perspective"),r.perspective))return invalid();
    if(j.contains("chromaticAberration")&&!chromaticAberrationFromJson(j.value("chromaticAberration"),r.chromaticAberration))return invalid();
    if(j.contains("defringe")&&!defringeFromJson(j.value("defringe"),r.defringe))return invalid();
    if(j.contains("pointColors")&&!pointColorsFromJson(j.value("pointColors"),r.pointColors))return invalid();
    for(const auto &key:{"grainAmount","grainSize","grainRoughness"})if(j.contains(key)&&!j.value(key).isDouble())return invalid();r.grainAmount=j.value("grainAmount").toDouble(0);r.grainSize=j.value("grainSize").toDouble(3);r.grainRoughness=j.value("grainRoughness").toDouble(.5);
    if(j.contains("luminanceCurve")){if(!j.value("luminanceCurve").isArray())return invalid();r.luminanceCurve.clear();for(auto value:j.value("luminanceCurve").toArray()){if(!value.isArray())return invalid();const auto xy=value.toArray();if(xy.size()!=2||!xy[0].isDouble()||!xy[1].isDouble())return invalid();r.luminanceCurve.append({xy[0].toDouble(),xy[1].toDouble()});}}
    if(j.contains("curveTangents")){if(!j.value("curveTangents").isArray())return invalid();const auto arrays=j.value("curveTangents").toArray();if(arrays.size()!=4)return invalid();for(int i=0;i<4;++i){if(!arrays[i].isArray())return invalid();r.curveTangents[i].clear();for(auto value:arrays[i].toArray()){if(!value.isDouble())return invalid();r.curveTangents[i].append(value.toDouble());}}}
    if(j.contains("luminanceTangents")){if(!j.value("luminanceTangents").isArray())return invalid();r.luminanceTangents.clear();for(auto value:j.value("luminanceTangents").toArray()){if(!value.isDouble())return invalid();r.luminanceTangents.append(value.toDouble());}}
    if(j.contains("parametricCurve")){if(!j.value("parametricCurve").isObject())return invalid();const auto p=j.value("parametricCurve").toObject();for(auto it=p.begin();it!=p.end();++it)if(it.key()!="regions"&&it.key()!="splits")return invalid();const auto regions=p.value("regions").toArray(),splits=p.value("splits").toArray();if(regions.size()!=4||splits.size()!=3)return invalid();for(int i=0;i<4;++i){if(!regions[i].isDouble())return invalid();r.parametricCurve.regions[i]=regions[i].toDouble();}for(int i=0;i<3;++i){if(!splits[i].isDouble())return invalid();r.parametricCurve.splits[i]=splits[i].toDouble();}}
    if(j.contains("primaries")){const auto values=j.value("primaries").toArray();if(values.size()!=3)return invalid();for(int i=0;i<3;++i){if(!values[i].isObject())return invalid();const auto p=values[i].toObject();if(!p.value("hue").isDouble()||!p.value("saturation").isDouble())return invalid();r.primaries[i]={p.value("hue").toDouble(),p.value("saturation").toDouble()};}}
    for(const auto &key:{"flipHorizontal","flipVertical"})if(j.contains(key)&&!j.value(key).isBool())return invalid();r.flipHorizontal=j.value("flipHorizontal").toBool();r.flipVertical=j.value("flipVertical").toBool();
    if(j.contains("grading")){const auto grades=j.value("grading").toArray();if(grades.size()!=3)return invalid();for(int i=0;i<3;++i){const auto a=grades[i].toObject();for(const auto &key:{"hue","saturation","luminance"})if(!a.value(key).isDouble())return invalid();r.grading[i]={a.value("hue").toDouble(),a.value("saturation").toDouble(),a.value("luminance").toDouble()};}}
    for(const auto &key:{"gradingBlend","gradingBalance"})if(j.contains(key)&&!j.value(key).isDouble())return invalid();
    r.gradingBlend=j.value("gradingBlend").toDouble(.5);r.gradingBalance=j.value("gradingBalance").toDouble();
    if(j.contains("monochrome")&&!j.value("monochrome").isBool())return invalid();r.monochrome=j.value("monochrome").toBool();
    if(j.contains("monochromeStrength")&&!j.value("monochromeStrength").isDouble())return invalid();r.monochromeStrength=j.value("monochromeStrength").toDouble(1);
    if(j.contains("monochromeMix")){const auto mix=j.value("monochromeMix").toArray();if(mix.size()!=8)return invalid();for(int i=0;i<8;++i){if(!mix[i].isDouble())return invalid();r.monochromeMix[i]=mix[i].toDouble();}}
    return r;
}
bool validRecipe(const Recipe &r) {
    if(!std::isfinite(r.distortion)||std::abs(r.distortion)>.4)return false;
    if(!std::isfinite(r.lensShading)||std::abs(r.lensShading)>2)return false;
    if(!validPerspective(r.perspective))return false;
    if(!validChromaticAberration(r.chromaticAberration))return false;
    if(!validDefringe(r.defringe))return false;
    if(!validPointColors(r.pointColors))return false;
    if(!std::isfinite(r.grainAmount)||r.grainAmount<0||r.grainAmount>1||!std::isfinite(r.grainSize)||r.grainSize<1||r.grainSize>12||!std::isfinite(r.grainRoughness)||r.grainRoughness<0||r.grainRoughness>1)return false;
    if(!std::isfinite(r.sharpenMask)||r.sharpenMask<0||r.sharpenMask>1)return false;
    for(double v:r.parametricCurve.regions)if(!std::isfinite(v)||std::abs(v)>1)return false;
    double boundary=0;for(double v:r.parametricCurve.splits){if(!std::isfinite(v)||v-boundary<.049999999)return false;boundary=v;}if(1-boundary<.049999999)return false;
    const auto &lc=r.luminanceCurve;if(lc.size()<2||lc.size()>maximumCurvePoints||lc.first().x()!=0||lc.last().x()!=1)return false;double previousL=-1;for(auto p:lc){if(!std::isfinite(p.x())||!std::isfinite(p.y())||p.x()<0||p.x()>1||p.y()<0||p.y()>1||p.x()-previousL<1e-10)return false;previousL=p.x();}
    for(const auto &p:r.primaries)for(double v:{p.hue,p.saturation})if(!std::isfinite(v)||std::abs(v)>1)return false;
    for(double v:{r.noiseLuminance,r.noiseChroma})if(!std::isfinite(v)||v<0||v>1)return false;
    for(const auto &a:r.grading)if(!std::isfinite(a.hue)||a.hue<0||a.hue>360||!std::isfinite(a.saturation)||a.saturation<0||a.saturation>1||!std::isfinite(a.luminance)||std::abs(a.luminance)>1)return false;
    if(!std::isfinite(r.gradingBlend)||r.gradingBlend<0||r.gradingBlend>1||!std::isfinite(r.gradingBalance)||std::abs(r.gradingBalance)>1)return false;
    for(double value:r.monochromeMix)if(!std::isfinite(value)||std::abs(value)>1)return false;
    if(!std::isfinite(r.monochromeStrength)||r.monochromeStrength<0||r.monochromeStrength>1)return false;
    for(const auto &curve:r.curves){if(curve.size()<2||curve.size()>maximumCurvePoints||curve.first().x()!=0||curve.last().x()!=1)return false;double previous=-1;
        for(auto p:curve){if(!std::isfinite(p.x())||!std::isfinite(p.y())||p.x()<0||p.x()>1||p.y()<0||p.y()>1||p.x()-previous<1e-10)return false;previous=p.x();}}
    for(int c=0;c<4;++c){const auto &tangent=r.curveTangents[c];if(!tangent.isEmpty()&&tangent.size()!=r.curves[c].size()+2)return false;for(double v:tangent)if(!std::isfinite(v)||std::abs(v)>4096)return false;}
    if(!r.luminanceTangents.isEmpty()&&r.luminanceTangents.size()!=r.luminanceCurve.size()+2)return false;for(double v:r.luminanceTangents)if(!std::isfinite(v)||std::abs(v)>4096)return false;
    auto boundedSpline=[](const Curve &points,const QVector<double> &tangents){const CurveSpline spline(points,tangents);for(int i=0;i<points.size();++i)if(!std::isfinite(spline.tangent(i))||std::abs(spline.tangent(i))>4096)return false;return std::isfinite(spline.outsideTangent(false))&&std::isfinite(spline.outsideTangent(true))&&std::abs(spline.outsideTangent(false))<=4096&&std::abs(spline.outsideTangent(true))<=4096;};
    for(int c=0;c<4;++c)if(!boundedSpline(r.curves[c],r.curveTangents[c]))return false;if(!boundedSpline(r.luminanceCurve,r.luminanceTangents))return false;
    for(const auto &a:r.hsl)for(double v:{a.hue,a.saturation,a.luminance})if(!std::isfinite(v)||std::abs(v)>1)return false;
    for(double value:{r.exposure,r.contrast,r.saturation,r.warmth,r.tint,r.angle,r.highlights,r.shadows,r.whites,r.blacks,r.vibrance,r.crop.x(),r.crop.y(),r.crop.width(),r.crop.height()}) if(!std::isfinite(value)) return false;
    if(!std::isfinite(r.texture)||std::abs(r.texture)>1||!std::isfinite(r.clarity)||std::abs(r.clarity)>1)return false;
    if(!std::isfinite(r.dehaze)||std::abs(r.dehaze)>1)return false;
    if(!std::isfinite(r.vignetteAmount)||r.vignetteAmount<-2||r.vignetteAmount>2)return false;
    if(!std::isfinite(r.vignetteMidpoint)||r.vignetteMidpoint<0||r.vignetteMidpoint>1)return false;
    if(!std::isfinite(r.vignetteFeather)||r.vignetteFeather<0||r.vignetteFeather>1)return false;
    if(!std::isfinite(r.sharpenAmount)||r.sharpenAmount<0||r.sharpenAmount>2||!std::isfinite(r.sharpenRadius)||r.sharpenRadius<.5||r.sharpenRadius>4||!std::isfinite(r.sharpenThreshold)||r.sharpenThreshold<0||r.sharpenThreshold>.1)return false;
    return std::abs(r.exposure)<=5 && std::abs(r.contrast)<=1 && r.saturation>=0 && r.saturation<=2 &&
        std::abs(r.warmth)<=1 && std::abs(r.tint)<=1 && std::abs(r.angle)<=45 &&
        std::abs(r.highlights)<=1 && std::abs(r.shadows)<=1 && std::abs(r.whites)<=1 && std::abs(r.blacks)<=1 && std::abs(r.vibrance)<=1 &&
        r.crop.x()>=0 && r.crop.y()>=0 && r.crop.width()>0 && r.crop.height()>0 &&
        r.crop.right()<=1.0000001 && r.crop.bottom()<=1.0000001;
}
bool hasColorEditing(const Recipe &r){for(const auto &p:r.pointColors)if(p.hueShift!=0||p.saturationShift!=0||p.luminanceShift!=0)return true;for(double v:r.parametricCurve.regions)if(v!=0)return true;for(const auto &values:r.curveTangents)if(!values.isEmpty())return true;if(!r.luminanceTangents.isEmpty())return true;for(auto p:r.luminanceCurve)if(p.x()!=p.y())return true;for(const auto &p:r.primaries)if(p.hue!=0||p.saturation!=0)return true;if(r.monochrome)return true;for(const auto &a:r.grading)if(a.saturation!=0||a.luminance!=0)return true;for(const auto &curve:r.curves)for(auto p:curve)if(p.x()!=p.y())return true;for(const auto &a:r.hsl)if(a.hue!=0||a.saturation!=0||a.luminance!=0)return true;return false;}



