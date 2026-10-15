#include "presets.h"
#include <QDir>
#include <QUuid>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QJsonDocument>
#include <algorithm>
#include "curve_spline.h"
#include <QJsonArray>
namespace {
QStringList excluded(){return {"warmth","tint","angle","perspective","distortion","chromaticAberration","x","y","width","height","flipHorizontal","flipVertical"};}
bool validName(const QString &name){if(name.trimmed().isEmpty()||name.size()>80)return false;for(auto c:name)if(c.category()==QChar::Other_Control)return false;return true;}
QJsonObject adjustmentsJson(const Recipe &r){auto json=recipeJson(r);for(const auto &key:excluded())json.remove(key);return json;}
constexpr int libraryLimit=64;
const QString librarySuffix=QStringLiteral(".luminas-preset.json");
QString compactError(const QString &error){return error.size()>80?error.left(77)+QStringLiteral("..."):error;}
QString libraryFileName(const QString &trimmedName){return QString::fromLatin1(QCryptographicHash::hash(trimmedName.toUtf8(),QCryptographicHash::Sha256).toHex())+librarySuffix;}
QFileInfoList libraryEntries(const QDir &directory){return directory.entryInfoList({QStringLiteral("*")+librarySuffix},QDir::Files|QDir::NoSymLinks,QDir::Name);}
bool managedEntry(const QDir &root,const QFileInfo &entry,QFileInfo &verified){
    if(entry.isSymLink()||!entry.isFile())return false;
    const QFileInfo resolved(entry.canonicalFilePath());
    if(!resolved.exists()||!resolved.isFile()||resolved.isSymLink())return false;
    if(resolved.absolutePath()!=root.canonicalPath())return false;
    verified=resolved;return true;
}
enum class Located { Found, Missing, Invalid };
Located locateManaged(const QString &directory,const QString &trimmedName,QFileInfo &located,QString &error){
    error.clear();QDir root(directory);
    if(directory.isEmpty()||!QFileInfo(directory).isDir()||root.canonicalPath().isEmpty()){error=QStringLiteral("预设库目录不存在或不是目录");return Located::Invalid;}
    const auto entries=libraryEntries(root);
    if(entries.size()>libraryLimit){error=QStringLiteral("预设库文件超过 %1 个，已拒绝操作").arg(libraryLimit);return Located::Invalid;}
    int matches=0;QFileInfo candidate;
    for(const auto &entry:entries){
        QFileInfo verified;
        if(!managedEntry(root,entry,verified))continue;
        AdjustmentPreset preset;QString readError;
        if(!loadPresetFile(verified.absoluteFilePath(),preset,readError))continue;
        if(preset.name!=trimmedName)continue;
        candidate=verified;++matches;
    }
    if(matches==0){error=QStringLiteral("未找到名为“%1”的预设").arg(trimmedName);return Located::Missing;}
    if(matches>1){error=QStringLiteral("预设库中存在多个名为“%1”的预设，无法确定唯一对象").arg(trimmedName);return Located::Invalid;}
    located=candidate;return Located::Found;
}
}
QVector<AdjustmentPreset> builtinPresets(){
    Recipe natural;natural.exposure=.12;natural.contrast=.08;natural.highlights=-.15;natural.shadows=.15;natural.whites=.02;natural.blacks=-.03;natural.vibrance=.12;
    Recipe monochrome;monochrome.monochrome=true;monochrome.contrast=-.1;monochrome.shadows=.1;monochrome.curves[0]={{0,.03},{.5,.52},{1,.97}};monochrome.monochromeMix[0]=.1;monochrome.monochromeMix[1]=.15;monochrome.monochromeMix[5]=-.1;
    Recipe detail;detail.sharpenAmount=.35;detail.sharpenRadius=1.2;detail.sharpenThreshold=.01;detail.texture=.12;detail.clarity=.06;
    return {{QStringLiteral("复位风格"),{}},{QStringLiteral("自然影调"),natural},{QStringLiteral("柔和黑白"),monochrome},{QStringLiteral("清晰细节"),detail}};
}
Recipe applyPreset(const Recipe &current,const AdjustmentPreset &preset){
    auto result=preset.adjustments;result.warmth=current.warmth;result.tint=current.tint;result.crop=current.crop;result.perspective=current.perspective;result.distortion=current.distortion;result.chromaticAberration=current.chromaticAberration;result.angle=current.angle;result.flipHorizontal=current.flipHorizontal;result.flipVertical=current.flipVertical;return result;
}
bool blendPreset(const Recipe &base,const AdjustmentPreset &preset,double strength,Recipe &result,QString &error){
    error.clear();if(!validRecipe(base)||!validRecipe(preset.adjustments)||!std::isfinite(strength)||strength<0||strength>1){error=QStringLiteral("预设强度或参数无效");return false;}
    const auto target=applyPreset(base,preset);if(strength==0){result=base;return true;}if(strength==1){result=target;return true;}auto lerp=[&](double a,double b){return a+(b-a)*strength;};
    auto mixValue=[&](auto &&self,const QJsonValue &a,const QJsonValue &b)->QJsonValue{if(a.isDouble()&&b.isDouble())return lerp(a.toDouble(),b.toDouble());if(a.isObject()&&b.isObject()){auto object=a.toObject();const auto other=b.toObject();for(auto it=object.begin();it!=object.end();++it)it.value()=self(self,it.value(),other.value(it.key()));return object;}if(a.isArray()&&b.isArray()){const auto aa=a.toArray(),bb=b.toArray();if(aa.size()!=bb.size())return b;QJsonArray values;for(int i=0;i<aa.size();++i)values.append(self(self,aa[i],bb[i]));return values;}return b;};
    auto candidate=recipeFromJson(mixValue(mixValue,recipeJson(base),recipeJson(target)).toObject());
    // Hue is circular; interpolating 350 to 10 must pass through 0, not 180.
    auto fringeHue=[&](double a,double b){return std::fmod(a+std::remainder(b-a,360.)*strength+360.,360.);};
    candidate.defringe.purpleHue=fringeHue(base.defringe.purpleHue,target.defringe.purpleHue);
    candidate.defringe.greenHue=fringeHue(base.defringe.greenHue,target.defringe.greenHue);
    auto curve=[&](const Curve &a,const QVector<double> &at,const Curve &b,const QVector<double> &bt,Curve &points,QVector<double> &tangents,bool luma){
        if(a==b&&at==bt){points=a;tangents=at;return true;}QVector<double> knots;for(auto p:a)knots.append(p.x());for(auto p:b)knots.append(p.x());std::sort(knots.begin(),knots.end());knots.erase(std::unique(knots.begin(),knots.end()),knots.end());if(knots.size()>maximumCurvePoints)return false;
        const CurveSpline sa(a,at),sb(b,bt);points.clear();tangents.clear();double previous=-1;for(double x:knots){if(x-previous<1e-10)return false;previous=x;const double y=lerp(sa.value(x),sb.value(x));if(y< -1e-12||y>1+1e-12)return false;points.append({x,std::clamp(y,0.,1.)});tangents.append(lerp(sa.derivative(x),sb.derivative(x)));}tangents.append(lerp(sa.outsideTangent(false),sb.outsideTangent(false)));tangents.append(lerp(luma?std::max(0.,sa.outsideTangent(true)):sa.outsideTangent(true),luma?std::max(0.,sb.outsideTangent(true)):sb.outsideTangent(true)));return true;
    };
    for(int c=0;c<4;++c)if(!curve(base.curves[c],base.curveTangents[c],target.curves[c],target.curveTangents[c],candidate.curves[c],candidate.curveTangents[c],false)){error=QStringLiteral("合并曲线超出64点或有效间距，请精简曲线");return false;}
    if(!curve(base.luminanceCurve,base.luminanceTangents,target.luminanceCurve,target.luminanceTangents,candidate.luminanceCurve,candidate.luminanceTangents,true)){error=QStringLiteral("合并亮度曲线超出有效范围");return false;}
    for(int i=0;i<3;++i){const auto &a=base.grading[i],&b=target.grading[i];double h;if(a.saturation==0)h=b.hue;else if(b.saturation==0)h=a.hue;else h=a.hue+std::remainder(b.hue-a.hue,360.)*strength;candidate.grading[i].hue=std::fmod(h+360.,360.);}
    const double mono=lerp(base.monochrome?base.monochromeStrength:0,target.monochrome?target.monochromeStrength:0);candidate.monochrome=mono>0;if(candidate.monochrome)candidate.monochromeStrength=mono;
    // Keep selection anchors fixed; fade each snapshot's adjustments, merge identical ranges.
    candidate.pointColors.clear();
    for(auto p:base.pointColors){p.hueShift*=1-strength;p.saturationShift*=1-strength;p.luminanceShift*=1-strength;candidate.pointColors.append(p);}
    std::array<bool,maximumPointColors> matched{};
    for(auto p:target.pointColors){auto selector=p;selector.hueShift=selector.saturationShift=selector.luminanceShift=0;int match=-1;
        for(int i=0;i<base.pointColors.size();++i){auto q=base.pointColors[i];q.hueShift=q.saturationShift=q.luminanceShift=0;if(!matched[i]&&q==selector){match=i;matched[i]=true;break;}}
        p.hueShift*=strength;p.saturationShift*=strength;p.luminanceShift*=strength;
        if(match<0)candidate.pointColors.append(p);else{auto &q=candidate.pointColors[match];q.hueShift+=p.hueShift;q.saturationShift+=p.saturationShift;q.luminanceShift+=p.luminanceShift;}
    }
    if(base.pointColors==target.pointColors)candidate.pointColors=base.pointColors;
    if(candidate.pointColors.size()>maximumPointColors){error=QStringLiteral("预设合并超过8个精确分色样本，请精简后重试");return false;}
    candidate=applyPreset(base,{preset.name,candidate});if(!validRecipe(candidate)){error=QStringLiteral("插值后的预设参数无效，未应用");return false;}result=candidate;return true;
}
bool savePresetFile(const QString &path,const AdjustmentPreset &preset,QString &error){
    error.clear();if(!validName(preset.name)||!validRecipe(preset.adjustments)||QFileInfo(path).suffix().compare("json",Qt::CaseInsensitive)!=0){error=QStringLiteral("预设名称、参数或JSON文件名无效");return false;}
    const QJsonObject json{{"format","LuminaS.adjustment-preset"},{"version",1},{"name",preset.name.trimmed()},{"adjustments",adjustmentsJson(preset.adjustments)}};
    QSaveFile file(path);if(!file.open(QIODevice::WriteOnly)){error=file.errorString();return false;}const auto bytes=QJsonDocument(json).toJson();if(file.write(bytes)!=bytes.size()||!file.commit()){error=file.errorString();return false;}return true;
}
bool loadPresetFile(const QString &path,AdjustmentPreset &preset,QString &error){
    error.clear();QFile file(path);if(!file.open(QIODevice::ReadOnly)){error=file.errorString();return false;}const auto bytes=file.read(256*1024+1);if(bytes.size()>256*1024){error=QStringLiteral("预设文件超过256KiB");return false;}QJsonParseError parse;const auto document=QJsonDocument::fromJson(bytes,&parse);if(parse.error!=QJsonParseError::NoError||!document.isObject()){error=QStringLiteral("预设不是有效JSON对象");return false;}const auto json=document.object();
    const QStringList top{"format","version","name","adjustments"};for(auto it=json.begin();it!=json.end();++it)if(!top.contains(it.key())){error=QStringLiteral("未知预设字段");return false;}
    if(json.value("format").toString()!="LuminaS.adjustment-preset"||!json.value("version").isDouble()||json.value("version").toDouble()!=1||!json.value("name").isString()||!validName(json.value("name").toString())||!json.value("adjustments").isObject()){error=QStringLiteral("预设格式、版本或名称不受支持");return false;}
    const auto values=json.value("adjustments").toObject(),allowed=adjustmentsJson({});for(auto it=values.begin();it!=values.end();++it)if(!allowed.contains(it.key())){error=QStringLiteral("预设包含不支持或照片专属参数：")+it.key();return false;}
    const auto recipe=recipeFromJson(values);if(!validRecipe(recipe)){error=QStringLiteral("预设参数类型或范围无效");return false;}preset={json.value("name").toString().trimmed(),recipe};return true;
}
QVector<AdjustmentPreset> loadPresetLibrary(const QString &directory,QStringList &warnings){
    warnings.clear();QVector<AdjustmentPreset> library;
    if(directory.isEmpty()||!QFileInfo(directory).isDir())return library;
    const auto entries=libraryEntries(QDir(directory));
    if(entries.size()>libraryLimit)warnings<<QStringLiteral("预设库文件超过 %1 个，只读取前 %1 个").arg(libraryLimit);
    for(int i=0;i<entries.size()&&i<libraryLimit;++i){
        AdjustmentPreset preset;QString error;
        if(!loadPresetFile(entries[i].absoluteFilePath(),preset,error)){warnings<<QStringLiteral("已跳过无效预设 %1：%2").arg(entries[i].fileName(),compactError(error));continue;}
        library.append(preset);
    }
    std::stable_sort(library.begin(),library.end(),[](const AdjustmentPreset &a,const AdjustmentPreset &b){return a.name<b.name;});
    return library;
}
bool storePresetLibrary(const QString &directory,const AdjustmentPreset &preset,QString &error){
    error.clear();if(directory.isEmpty()){error=QStringLiteral("预设库目录无效");return false;}
    const auto name=preset.name.trimmed();
    if(!validName(name)||!validRecipe(preset.adjustments)){error=QStringLiteral("预设名称或参数无效，未写入预设库");return false;}
    QDir dir(directory);
    if(!dir.exists()&&!dir.mkpath(QStringLiteral("."))){error=QStringLiteral("无法创建预设库目录：")+QDir::toNativeSeparators(directory);return false;}
    QString locateError;QFileInfo existing;
    const auto location=locateManaged(directory,name,existing,locateError);
    if(location==Located::Found){
        AdjustmentPreset normalized{name,preset.adjustments};
        return savePresetFile(existing.absoluteFilePath(),normalized,error);
    }
    if(location==Located::Invalid){error=locateError;return false;}
    const auto target=dir.absoluteFilePath(libraryFileName(name));
    QFileInfo info(target);
    if(info.isSymLink()){error=QStringLiteral("预设库目标文件是符号链接，已拒绝写入");return false;}
    if(info.exists()&&!info.isFile()){error=QStringLiteral("预设库目标位置不是普通文件，已拒绝写入");return false;}
    if(!info.exists()&&libraryEntries(dir).size()>=libraryLimit){error=QStringLiteral("预设库最多保存 %1 个预设；无法新增").arg(libraryLimit);return false;}
    auto filePath=info.absoluteFilePath();
    if(info.exists()){
        AdjustmentPreset occupant;QString readError;
        const bool validOccupant=loadPresetFile(filePath,occupant,readError);
        if(validOccupant&&occupant.name!=name){
            filePath=dir.absoluteFilePath(QString::fromLatin1(QCryptographicHash::hash((name+QUuid::createUuid().toString(QUuid::WithoutBraces)).toUtf8(),QCryptographicHash::Sha256).toHex())+librarySuffix);
            const QFileInfo fresh(filePath);
            if(fresh.isSymLink()||fresh.exists()){error=QStringLiteral("预设库文件名冲突，无法新增");return false;}
            if(libraryEntries(dir).size()>=libraryLimit){error=QStringLiteral("预设库最多保存 %1 个预设；无法新增").arg(libraryLimit);return false;}
        }
    }
    AdjustmentPreset normalized{name,preset.adjustments};
    return savePresetFile(filePath,normalized,error);
}
bool renamePresetLibrary(const QString &directory,const QString &oldName,const QString &newName,QString &error){
    error.clear();
    const auto trimmedOld=oldName.trimmed(),trimmedNew=newName.trimmed();
    if(!validName(trimmedOld)||!validName(trimmedNew)){error=QStringLiteral("预设名称无效");return false;}
    QFileInfo located;
    if(locateManaged(directory,trimmedOld,located,error)!=Located::Found)return false;
    if(trimmedNew==trimmedOld)return true;
    QString collisionError;QFileInfo collision;
    const auto collisionState=locateManaged(directory,trimmedNew,collision,collisionError);
    if(collisionState==Located::Found){error=QStringLiteral("已存在名为“%1”的预设").arg(trimmedNew);return false;}
    if(collisionState==Located::Invalid){error=collisionError;return false;}
    AdjustmentPreset preset;QString readError;
    if(!loadPresetFile(located.absoluteFilePath(),preset,readError)){error=QStringLiteral("读取预设失败：")+compactError(readError);return false;}
    AdjustmentPreset renamed{trimmedNew,preset.adjustments};
    return savePresetFile(located.absoluteFilePath(),renamed,error);
}
bool removePresetLibrary(const QString &directory,const QString &name,QString &error){
    error.clear();
    const auto trimmedName=name.trimmed();
    if(!validName(trimmedName)){error=QStringLiteral("预设名称无效");return false;}
    QFileInfo located;
    if(locateManaged(directory,trimmedName,located,error)!=Located::Found)return false;
    if(!QFile::remove(located.absoluteFilePath())){error=QStringLiteral("无法删除预设文件：")+QDir::toNativeSeparators(located.absoluteFilePath());return false;}
    return true;
}


