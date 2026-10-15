#include "export_types.h"
#include "export_engine.h"
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <limits>

namespace Export {
namespace {

constexpr int kVersion=1;
constexpr int kMaximumAssets=2000;
constexpr quint64 kMaximumPixels=45000000ULL;
constexpr double kMaximumMillimetres=25400.0; // 25.4 m
constexpr double kMaximumPercent=1600.0;
constexpr int kMaximumCollisionAttempts=9999;

bool finite(double value){return std::isfinite(value);}

bool readFinite(const QJsonObject &json,const QString &key,double &target,QString &error){
    const auto value=json.value(key);
    if(value.isUndefined())return true;
    if(!value.isDouble()){error=QStringLiteral("字段 %1 不是数值").arg(key);return false;}
    if(!finite(value.toDouble())){error=QStringLiteral("字段 %1 不是有限数值").arg(key);return false;}
    target=value.toDouble();return true;
}

bool readBool(const QJsonObject &json,const QString &key,bool &target,QString &error){
    const auto value=json.value(key);
    if(value.isUndefined())return true;
    if(!value.isBool()){error=QStringLiteral("字段 %1 不是布尔值").arg(key);return false;}
    target=value.toBool();return true;
}

bool readInt(const QJsonObject &json,const QString &key,int &target,QString &error){
    const auto value=json.value(key);
    if(value.isUndefined())return true;
    if(!value.isDouble()){error=QStringLiteral("字段 %1 不是整数").arg(key);return false;}
    const double raw=value.toDouble();
    if(!finite(raw)||raw!=std::floor(raw)||raw<double(std::numeric_limits<int>::min())||raw>double(std::numeric_limits<int>::max())){
        error=QStringLiteral("字段 %1 不是有效整数").arg(key);return false;}
    target=int(raw);return true;
}

bool readEnum(const QJsonObject &json,const QString &key,int count,int &target,QString &error){
    const auto value=json.value(key);
    if(value.isUndefined())return true;
    if(!value.isDouble()){error=QStringLiteral("字段 %1 不是枚举值").arg(key);return false;}
    const double raw=value.toDouble();
    if(!finite(raw)||raw!=std::floor(raw)||raw<0||raw>=double(count)){error=QStringLiteral("字段 %1 超出枚举范围").arg(key);return false;}
    target=int(raw);return true;
}

bool readString(const QJsonObject &json,const QString &key,QString &target,QString &error){
    const auto value=json.value(key);
    if(value.isUndefined())return true;
    if(!value.isString()){error=QStringLiteral("字段 %1 不是字符串").arg(key);return false;}
    target=value.toString();return true;
}

QString settingKey(const char *name){return QString::fromLatin1(name);}

QString illegalStem(const QString &stem){
    if(stem.isEmpty())return QStringLiteral("文件名为空");
    if(stem=="."||stem=="..")return QStringLiteral("文件名保留");
    if(stem.size()>180)return QStringLiteral("文件名过长");
    for(const QChar c:stem){
        if(c==QLatin1Char('/')||c==QLatin1Char('\\')||c==QLatin1Char(':')||c==QLatin1Char('*')||
           c==QLatin1Char('?')||c==QLatin1Char('"')||c==QLatin1Char('<')||c==QLatin1Char('>')||c==QLatin1Char('|'))
            return QStringLiteral("文件名含分隔符或 Windows 非法字符");
        if(c.unicode()<32||c.category()==QChar::Other_Control)return QStringLiteral("文件名含控制字符");
    }
    if(stem.endsWith(QLatin1Char('.'))||stem.endsWith(QLatin1Char(' ')))return QStringLiteral("文件名不能以点或空格结尾");
    // Reserved DOS device names stay reserved with an extension attached.
    const QString upper=stem.section(QLatin1Char('.'),0,0).trimmed().toUpper();
    static const QStringList reserved{QStringLiteral("CON"),QStringLiteral("PRN"),QStringLiteral("AUX"),QStringLiteral("NUL"),
        QStringLiteral("COM1"),QStringLiteral("COM2"),QStringLiteral("COM3"),QStringLiteral("COM4"),QStringLiteral("COM5"),
        QStringLiteral("COM6"),QStringLiteral("COM7"),QStringLiteral("COM8"),QStringLiteral("COM9"),QStringLiteral("LPT1"),
        QStringLiteral("LPT2"),QStringLiteral("LPT3"),QStringLiteral("LPT4"),QStringLiteral("LPT5"),QStringLiteral("LPT6"),
        QStringLiteral("LPT7"),QStringLiteral("LPT8"),QStringLiteral("LPT9")};
    if(reserved.contains(upper))return QStringLiteral("文件名是 Windows 保留设备名");
    return {};
}

QString illegalSubfolder(const QString &subfolder){
    for(const QString &part:subfolder.split(QRegularExpression(QStringLiteral("[\\\\/]")),Qt::SkipEmptyParts)){
        const auto problem=illegalStem(part);
        if(!problem.isEmpty())return problem;
    }
    return {};
}

} // namespace

QString extension(Format format){
    switch(format){
    case Format::Jpeg:return QStringLiteral("jpg");
    case Format::Png:return QStringLiteral("png");
    case Format::Tiff:return QStringLiteral("tif");
    }
    return QStringLiteral("bin");
}

QString statusText(Status status){
    switch(status){
    case Status::Pending:return QStringLiteral("等待导出");
    case Status::Running:return QStringLiteral("正在导出");
    case Status::Succeeded:return QStringLiteral("已导出");
    case Status::Failed:return QStringLiteral("导出失败");
    case Status::Cancelled:return QStringLiteral("已取消");
    case Status::Skipped:return QStringLiteral("已跳过");
    }
    return QStringLiteral("未知状态");
}

QString validate(const Settings &settings){
    switch(settings.format){case Format::Jpeg:case Format::Png:case Format::Tiff:break;default:return QStringLiteral("未知输出格式");}
    switch(settings.mode){case Mode::Rgb:case Mode::Cmyk:break;default:return QStringLiteral("未知色彩模式");}
    switch(settings.sizeMode){case SizeMode::Full:case SizeMode::LongEdge:case SizeMode::ShortEdge:case SizeMode::Fit:case SizeMode::Percent:case SizeMode::Print:break;default:return QStringLiteral("未知尺寸模式");}
    switch(settings.metadata){case Metadata::None:case Metadata::Copyright:case Metadata::Camera:break;default:return QStringLiteral("未知元数据选项");}
    switch(settings.collision){case Collision::Number:case Collision::Skip:break;default:return QStringLiteral("未知重名策略");}
    if(settings.bits!=8&&settings.bits!=16)return QStringLiteral("位深只支持 8 或 16 位");
    if(settings.quality<1||settings.quality>100)return QStringLiteral("JPEG 品质必须在 1 到 100 之间");
    if(settings.intent!=0&&settings.intent!=1)return QStringLiteral("仅支持感知或相对色度转换");
    if(settings.after<0||settings.after>2)return QStringLiteral("完成后动作必须在 0 到 2 之间");
    if(!finite(settings.width)||!finite(settings.height)||!finite(settings.percent)||!finite(settings.ppi))
        return QStringLiteral("尺寸必须是有限数值");
    if(settings.width<=0||settings.height<=0)return QStringLiteral("宽度和高度必须大于 0");
    if(settings.width>1000000||settings.height>1000000)return QStringLiteral("宽度或高度过大");
    if(settings.percent<1||settings.percent>kMaximumPercent)
        return QStringLiteral("百分比必须在 1 到 %1 之间").arg(kMaximumPercent);
    if(settings.ppi<1||settings.ppi>100000)return QStringLiteral("分辨率必须在 1 到 100000 PPI 之间");
    const double printFactor=settings.printCm?10.0:1.0;
    if(settings.sizeMode==SizeMode::Print&&(settings.width*printFactor>kMaximumMillimetres||settings.height*printFactor>kMaximumMillimetres))
        return QStringLiteral("打印尺寸过大");
    if(settings.profileName.trimmed().isEmpty())return QStringLiteral("必须选择 ICC 配置文件");
    if(settings.icc.isEmpty())return QStringLiteral("ICC 配置文件内容缺失");
    if(settings.icc.size()>4*1024*1024)return QStringLiteral("ICC 配置文件超过 4MiB");
    QString profileError;if(!validProfile(settings.icc,settings.mode,profileError))return profileError;
    if(settings.profileName==QStringLiteral("ProPhoto RGB")&&settings.bits!=16)return QStringLiteral("ProPhoto RGB要求16位");
    if(settings.format==Format::Jpeg&&settings.bits!=8)return QStringLiteral("JPEG 只支持 8 位");
    if(settings.mode==Mode::Cmyk&&settings.format!=Format::Tiff)return QStringLiteral("CMYK 只在 TIFF 中提供");
    if(settings.format==Format::Tiff&&settings.mode==Mode::Rgb&&settings.bits!=8&&settings.bits!=16)
        return QStringLiteral("TIFF 支持 8 或 16 位");
    if(settings.format==Format::Png&&settings.bits!=8&&settings.bits!=16)
        return QStringLiteral("PNG 支持 8 或 16 位");
    if(settings.directory.contains(QChar(0)))return QStringLiteral("目标目录无效");
    if(!settings.besideSource&&(settings.directory.trimmed().isEmpty()||!QDir::isAbsolutePath(settings.directory)))return QStringLiteral("请选择有效的绝对导出目录");
    if(QDir::isAbsolutePath(settings.subfolder))return QStringLiteral("子文件夹不能是绝对路径");
    if(!settings.directory.isEmpty()&&QFileInfo(settings.directory).fileName().isEmpty())
        return QStringLiteral("目标目录无效");
    if(settings.subfolder.contains(QChar(0)))return QStringLiteral("子文件夹名称无效");
    if(settings.sequence){
        if(settings.prefix.trimmed().isEmpty())return QStringLiteral("序号模式必须填写文件名前缀");
        if(settings.start<0)return QStringLiteral("起始序号不能为负数");
        if(settings.start>1000000000)return QStringLiteral("起始序号过大");
    }
    return {};
}

QSize outputSize(QSize full,const Settings &settings,QString &error){
    error.clear();
    if(full.width()<=0||full.height()<=0||quint64(full.width())*quint64(full.height())>kMaximumPixels){error=QStringLiteral("原始尺寸无效");return {};}
    const double sourceWidth=double(full.width()),sourceHeight=double(full.height());
    const double ratio=sourceWidth/sourceHeight;
    double width=sourceWidth,height=sourceHeight;
    bool resolutionOnly=false;
    switch(settings.sizeMode){
    case SizeMode::Full:
        resolutionOnly=true;
        break;
    case SizeMode::LongEdge:case SizeMode::ShortEdge:{
        const double target=settings.sizeMode==SizeMode::LongEdge?settings.width:settings.height;
        if(!finite(target)||target<=0){error=QStringLiteral("长边/短边像素数必须大于 0");return {};}
        if(settings.sizeMode==SizeMode::LongEdge){width=ratio>=1?target:target*ratio;height=ratio>=1?target/ratio:target;}
        else{width=ratio>=1?target*ratio:target;height=ratio>=1?target:target/ratio;}
        break;
    }
    case SizeMode::Fit:{
        const auto scale=std::min(settings.width/sourceWidth,settings.height/sourceHeight);
        width=sourceWidth*scale;height=sourceHeight*scale;break;
    }
    case SizeMode::Percent:{
        if(!finite(settings.percent)||settings.percent<=0){error=QStringLiteral("百分比必须大于 0");return {};}
        const double factor=settings.percent/100.0;
        width=sourceWidth*factor;height=sourceHeight*factor;
        break;
    }
    case SizeMode::Print:{
        if(!finite(settings.ppi)||settings.ppi<=0){error=QStringLiteral("分辨率必须大于 0");return {};}
        const double factor=settings.printCm?10.0:1.0;
        if(!finite(settings.width)||!finite(settings.height)||settings.width<=0||settings.height<=0){error=QStringLiteral("打印尺寸必须大于 0");return {};}
        const auto scale=std::min(settings.width*factor/25.4*settings.ppi/sourceWidth,settings.height*factor/25.4*settings.ppi/sourceHeight);
        width=sourceWidth*scale;height=sourceHeight*scale;
        break;
    }
    default:error=QStringLiteral("未知尺寸模式");return {};
    }
    if(!finite(width)||!finite(height)||width<=0||height<=0){error=QStringLiteral("计算出的输出尺寸无效");return {};}
    if(resolutionOnly)return QSize(full.width(),full.height()); // PPI alone never resizes pixels
    if(settings.dontEnlarge&&(width>sourceWidth||height>sourceHeight)){width=sourceWidth;height=sourceHeight;}
    double pixels=width*height;
    if(pixels>double(kMaximumPixels)){
        if(!settings.dontEnlarge){error=QStringLiteral("输出超过 4500 万像素上限");return {};}
        const double scale=std::sqrt(double(kMaximumPixels)/pixels);
        width*=scale;height*=scale;pixels=width*height;
    }
    if(width<1||height<1){error=QStringLiteral("计算出的输出尺寸小于 1 像素");return {};}
    // The edge that defines the requested size stays exact; the other edge is rounded
    // from the preserved aspect ratio, so width/height never drift apart.
    int outWidth=0,outHeight=0;
    switch(settings.sizeMode){
    case SizeMode::LongEdge:
        if(ratio>=1){outWidth=int(width);outHeight=int(std::lround(double(outWidth)/ratio));}
        else{outHeight=int(height);outWidth=int(std::lround(double(outHeight)*ratio));}
        break;
    case SizeMode::ShortEdge:
        if(ratio>=1){outHeight=int(height);outWidth=int(std::lround(double(outHeight)*ratio));}
        else{outWidth=int(width);outHeight=int(std::lround(double(outWidth)/ratio));}
        break;
    case SizeMode::Fit:case SizeMode::Print:case SizeMode::Percent:{
        // Choose the driver that survives rounding best: the larger requested edge.
        if(width>=height){outWidth=int(width);outHeight=int(std::lround(double(outWidth)/ratio));}
        else{outHeight=int(height);outWidth=int(std::lround(double(outHeight)*ratio));}
        break;
    }
    case SizeMode::Full:
        outWidth=full.width();outHeight=full.height();break;
    default:
        outWidth=int(width);outHeight=int(height);break;
    }
    if(outWidth<1||outHeight<1){error=QStringLiteral("计算出的输出尺寸小于 1 像素");return {};}
    if(quint64(outWidth)*quint64(outHeight)>kMaximumPixels){error=QStringLiteral("输出超过 4500 万像素上限");return {};}
    return QSize(outWidth,outHeight);
}

QJsonObject settingsJson(const Settings &settings){
    QJsonObject json;
    json.insert(QStringLiteral("version"),kVersion);
    json.insert(QStringLiteral("format"),int(settings.format));
    json.insert(QStringLiteral("mode"),int(settings.mode));
    json.insert(QStringLiteral("bits"),settings.bits);
    json.insert(QStringLiteral("quality"),settings.quality);
    json.insert(QStringLiteral("zip"),settings.zip);
    json.insert(QStringLiteral("blackPoint"),settings.blackPoint);
    json.insert(QStringLiteral("intent"),settings.intent);
    json.insert(QStringLiteral("profileName"),settings.profileName);
    json.insert(QStringLiteral("icc"),settings.icc.isEmpty()?QString():QString::fromLatin1(settings.icc.toBase64()));
    json.insert(QStringLiteral("sizeMode"),int(settings.sizeMode));
    json.insert(QStringLiteral("width"),settings.width);
    json.insert(QStringLiteral("height"),settings.height);
    json.insert(QStringLiteral("percent"),settings.percent);
    json.insert(QStringLiteral("ppi"),settings.ppi);
    json.insert(QStringLiteral("dontEnlarge"),settings.dontEnlarge);
    json.insert(QStringLiteral("printCm"),settings.printCm);
    json.insert(QStringLiteral("besideSource"),settings.besideSource);
    json.insert(QStringLiteral("directory"),settings.directory);
    json.insert(QStringLiteral("subfolder"),settings.subfolder);
    json.insert(QStringLiteral("sequence"),settings.sequence);
    json.insert(QStringLiteral("prefix"),settings.prefix);
    json.insert(QStringLiteral("suffix"),settings.suffix);
    json.insert(QStringLiteral("start"),settings.start);
    json.insert(QStringLiteral("collision"),int(settings.collision));
    json.insert(QStringLiteral("metadata"),int(settings.metadata));
    json.insert(QStringLiteral("gps"),settings.gps);
    json.insert(QStringLiteral("after"),settings.after);
    return json;
}

bool settingsFromJson(const QJsonObject &json,Settings &settings,QString &error){
    error.clear();
    Settings parsed;
    int version=0;
    if(!readInt(json,QStringLiteral("version"),version,error))return false;
    if(json.value(QStringLiteral("version")).isUndefined()){error=QStringLiteral("设置缺少版本字段");return false;}
    if(version!=kVersion){error=QStringLiteral("不支持的设置版本 %1").arg(version);return false;}
    int format=int(parsed.format),mode=int(parsed.mode),sizeMode=int(parsed.sizeMode);
    int collision=int(parsed.collision),metadata=int(parsed.metadata);
    if(!readEnum(json,QStringLiteral("format"),3,format,error))return false;
    if(!readEnum(json,QStringLiteral("mode"),2,mode,error))return false;
    if(!readEnum(json,QStringLiteral("sizeMode"),6,sizeMode,error))return false;
    if(!readEnum(json,QStringLiteral("collision"),2,collision,error))return false;
    if(!readEnum(json,QStringLiteral("metadata"),3,metadata,error))return false;
    parsed.format=Format(format);parsed.mode=Mode(mode);parsed.sizeMode=SizeMode(sizeMode);
    parsed.collision=Collision(collision);parsed.metadata=Metadata(metadata);
    if(!readInt(json,QStringLiteral("bits"),parsed.bits,error))return false;
    if(!readInt(json,QStringLiteral("quality"),parsed.quality,error))return false;
    if(!readEnum(json,QStringLiteral("intent"),4,parsed.intent,error))return false;
    if(!readEnum(json,QStringLiteral("after"),3,parsed.after,error))return false;
    if(!readBool(json,QStringLiteral("zip"),parsed.zip,error))return false;
    if(!readBool(json,QStringLiteral("blackPoint"),parsed.blackPoint,error))return false;
    if(!readBool(json,QStringLiteral("dontEnlarge"),parsed.dontEnlarge,error))return false;
    if(!readBool(json,QStringLiteral("printCm"),parsed.printCm,error))return false;
    if(!readBool(json,QStringLiteral("besideSource"),parsed.besideSource,error))return false;
    if(!readBool(json,QStringLiteral("sequence"),parsed.sequence,error))return false;
    if(!readBool(json,QStringLiteral("gps"),parsed.gps,error))return false;
    if(!readString(json,QStringLiteral("profileName"),parsed.profileName,error))return false;
    if(!readString(json,QStringLiteral("directory"),parsed.directory,error))return false;
    if(!readString(json,QStringLiteral("subfolder"),parsed.subfolder,error))return false;
    if(!readString(json,QStringLiteral("prefix"),parsed.prefix,error))return false;
    if(!readString(json,QStringLiteral("suffix"),parsed.suffix,error))return false;
    if(!readInt(json,QStringLiteral("start"),parsed.start,error))return false;
    if(!readFinite(json,QStringLiteral("width"),parsed.width,error))return false;
    if(!readFinite(json,QStringLiteral("height"),parsed.height,error))return false;
    if(!readFinite(json,QStringLiteral("percent"),parsed.percent,error))return false;
    if(!readFinite(json,QStringLiteral("ppi"),parsed.ppi,error))return false;
    QString icc;
    if(!readString(json,QStringLiteral("icc"),icc,error))return false;
    if(!icc.isEmpty()){
        const auto decoded=QByteArray::fromBase64Encoding(icc.toLatin1());
        if(!decoded){error=QStringLiteral("ICC 配置内容不是有效 Base64");return false;}
        parsed.icc=*decoded;
    }
    const auto message=validate(parsed);
    if(!message.isEmpty()){error=message;return false;}
    settings=parsed;return true;
}

QJsonObject jobJson(const Job &job){
    QJsonObject json;
    json.insert(QStringLiteral("version"),kVersion);
    json.insert(QStringLiteral("id"),job.id);
    json.insert(QStringLiteral("source"),job.source);
    json.insert(QStringLiteral("hash"),job.hash);
    json.insert(QStringLiteral("path"),job.path);
    json.insert(QStringLiteral("revision"),QString::number(job.revision));
    json.insert(QStringLiteral("status"),int(job.status));
    json.insert(QStringLiteral("error"),job.error);
    json.insert(QStringLiteral("stage"),job.stage);
    json.insert(QStringLiteral("recipe"),recipeJson(job.recipe));
    json.insert(QStringLiteral("settings"),settingsJson(job.settings));
    return json;
}

bool jobFromJson(const QJsonObject &json,Job &job,QString &error){
    error.clear();
    Job parsed;
    int version=0;
    if(!readInt(json,QStringLiteral("version"),version,error))return false;
    if(json.value(QStringLiteral("version")).isUndefined()){error=QStringLiteral("任务缺少版本字段");return false;}
    if(version!=kVersion){error=QStringLiteral("不支持的任务版本 %1").arg(version);return false;}
    if(!json.value(QStringLiteral("recipe")).isObject()){error=QStringLiteral("任务缺少调整参数");return false;}
    if(!json.value(QStringLiteral("settings")).isObject()){error=QStringLiteral("任务缺少导出设置");return false;}
    if(!readString(json,QStringLiteral("id"),parsed.id,error))return false;
    if(!readString(json,QStringLiteral("source"),parsed.source,error))return false;
    if(!readString(json,QStringLiteral("hash"),parsed.hash,error))return false;
    if(!readString(json,QStringLiteral("path"),parsed.path,error))return false;
    if(!readString(json,QStringLiteral("error"),parsed.error,error))return false;
    if(!readString(json,QStringLiteral("stage"),parsed.stage,error))return false;
    int status=int(parsed.status);
    if(!readEnum(json,QStringLiteral("status"),6,status,error))return false;
    parsed.status=Status(status);
    bool revisionOk=false;
    const auto revision=json.value(QStringLiteral("revision"));
    if(revision.isString())parsed.revision=revision.toString().toULongLong(&revisionOk);
    else if(revision.isDouble()&&revision.toDouble()>=0&&revision.toDouble()<=9007199254740991.&&std::floor(revision.toDouble())==revision.toDouble()){parsed.revision=quint64(revision.toDouble());revisionOk=true;}
    if(!revisionOk){error=QStringLiteral("任务修订号无效");return false;}
    parsed.recipe=recipeFromJson(json.value(QStringLiteral("recipe")).toObject());
    if(!validRecipe(parsed.recipe)){error=QStringLiteral("任务调整参数无效");return false;}
    if(!settingsFromJson(json.value(QStringLiteral("settings")).toObject(),parsed.settings,error))return false;
    if(parsed.id.isEmpty()||QUuid(parsed.id).isNull()){error=QStringLiteral("任务标识不是有效 UUID");return false;}
    if(parsed.source.isEmpty()){error=QStringLiteral("任务缺少源文件路径");return false;}
    if(!QRegularExpression(QStringLiteral("^[0-9a-fA-F]{64}$")).match(parsed.hash).hasMatch()){error=QStringLiteral("任务缺少源文件哈希");return false;}
    if(parsed.path.trimmed().isEmpty()&&parsed.status!=Status::Skipped){error=QStringLiteral("任务缺少输出路径");return false;}
    if(!QDir::isAbsolutePath(parsed.source)||(parsed.status!=Status::Skipped&&!QDir::isAbsolutePath(parsed.path))){error=QStringLiteral("任务路径必须是绝对路径");return false;}
    job=parsed;return true;
}

QList<Job> freeze(const QList<Asset> &assets,const Settings &settings,QString &error){
    error.clear();
    QList<Job> jobs;
    if(assets.isEmpty()){error=QStringLiteral("没有可导出的照片");return jobs;}
    if(assets.size()>kMaximumAssets){error=QStringLiteral("单批最多导出 %1 张").arg(kMaximumAssets);return jobs;}
    const auto message=validate(settings);
    if(!message.isEmpty()){error=message;return jobs;}
    QList<Asset> frozen;
    QSet<QString> sources;
    for(const auto &asset:assets){
        Asset entry=asset;
        if(entry.path.trimmed().isEmpty()){error=QStringLiteral("存在没有源文件路径的照片");return {};}
        const QFileInfo info(entry.path);
        const auto canonical=info.canonicalFilePath();
        if(canonical.isEmpty()){error=QStringLiteral("源文件不存在或不可读：%1").arg(QFileInfo(entry.path).fileName());return {};}
        entry.path=QDir::cleanPath(canonical);
        if(!QRegularExpression(QStringLiteral("^[0-9a-fA-F]{64}$")).match(entry.hash).hasMatch()){error=QStringLiteral("缺少源文件哈希，无法冻结：%1").arg(info.fileName());return {};}
        if(!validRecipe(entry.recipe)){error=QStringLiteral("调整参数无效：%1").arg(info.fileName());return {};}
        const auto key=entry.path.toLower();
        if(sources.contains(key)){error=QStringLiteral("同一源文件在本次导出中重复：%1").arg(info.fileName());return {};}
        sources.insert(key);
        frozen.append(entry);
    }
    const QString subfolder=settings.subfolder.trimmed();
    if(!subfolder.isEmpty()){
        const auto problem=illegalSubfolder(subfolder);
        if(!problem.isEmpty()){error=QStringLiteral("子文件夹名称非法：%1").arg(problem);return {};}
    }
    QSet<QString> planned;
    int ordinal=0;
    for(const auto &asset:frozen){
        const QString base=QFileInfo(asset.path).completeBaseName();
        const QString stem=settings.sequence?settings.prefix+QString::number(qint64(settings.start)+ordinal)+settings.suffix:base+settings.suffix;
        ++ordinal;
        const auto problem=illegalStem(stem);
        if(!problem.isEmpty()){error=QStringLiteral("输出文件名非法（%1）：%2").arg(problem,stem);return {};}
        QDir directory;
        if(settings.besideSource)directory=QFileInfo(asset.path).absoluteDir();
        else directory=QDir(settings.directory);
        if(settings.besideSource&&!subfolder.isEmpty())directory=QDir(directory.absoluteFilePath(subfolder));
        const QString folder=QDir::cleanPath(directory.absolutePath());
        const QString suffix=QLatin1Char('.')+extension(settings.format);
        const auto taken=[&](const QString &candidate){
            const auto key=candidate.toLower();
            return sources.contains(key)||planned.contains(key)||QFileInfo::exists(candidate);
        };
        QString output=QDir::cleanPath(folder+QLatin1Char('/')+stem+suffix);
        if(taken(output)){
            if(settings.collision==Collision::Skip){
                Job job;
                job.id=QUuid::createUuid().toString(QUuid::WithoutBraces);
                job.source=asset.path;job.hash=asset.hash;job.revision=asset.revision;job.recipe=asset.recipe;job.settings=settings;
                job.status=Status::Skipped;
                job.error=QStringLiteral("同名文件已存在，按策略跳过");
                jobs.append(job);
                continue;
            }
            QString resolved;
            for(int number=1;number<=kMaximumCollisionAttempts;++number){
                const QString attempt=QDir::cleanPath(folder+QLatin1Char('/')+stem+QLatin1Char('-')+QString::number(number)+suffix);
                if(!taken(attempt)){resolved=attempt;break;}
            }
            if(resolved.isEmpty()){error=QStringLiteral("无法为 %1 找到可用输出文件名").arg(stem);return {};}
            output=resolved;
        }
        Job job;
        job.id=QUuid::createUuid().toString(QUuid::WithoutBraces);
        job.source=asset.path;job.hash=asset.hash;job.revision=asset.revision;job.recipe=asset.recipe;job.settings=settings;
        job.path=output;job.status=Status::Pending;
        planned.insert(output.toLower());
        jobs.append(job);
    }
    return jobs;
}

} // namespace Export
 
