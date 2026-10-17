#include "worker_protocol.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QUuid>
#include <libraw/libraw.h>
#include <lcms2.h>
#include <cmath>

namespace RawWorker {
namespace {
bool reject(QString &error,const QString &message){error=message;return false;}
bool keys(const QJsonObject &o,std::initializer_list<const char *> names){
    if(o.size()!=qsizetype(names.size()))return false;
    for(const auto name:names)if(!o.contains(QLatin1String(name)))return false;return true;
}
bool number(const QJsonValue &v){return v.isDouble()&&std::isfinite(v.toDouble());}
bool pair(const QJsonValue &v,QPointF &p){
    if(!v.isArray())return false;const auto a=v.toArray();if(a.size()!=2||!number(a[0])||!number(a[1]))return false;
    p={a[0].toDouble(),a[1].toDouble()};return true;
}
bool geometryOnly(const Recipe &r){
    Recipe expected;expected.crop=r.crop;expected.angle=r.angle;expected.flipHorizontal=r.flipHorizontal;expected.flipVertical=r.flipVertical;
    expected.perspective=r.perspective;expected.distortion=r.distortion;expected.chromaticAberration=r.chromaticAberration;expected.lensShading=r.lensShading;
    return r==expected&&validRecipe(r);
}
QJsonObject geometryJson(const Recipe &r){return {{"crop",QJsonArray{r.crop.x(),r.crop.y(),r.crop.width(),r.crop.height()}},{"angle",r.angle},
    {"flipHorizontal",r.flipHorizontal},{"flipVertical",r.flipVertical},{"perspective",QJsonArray{r.perspective.x(),r.perspective.y()}},
    {"distortion",r.distortion},{"chromaticAberration",QJsonArray{r.chromaticAberration.x(),r.chromaticAberration.y()}},{"lensShading",r.lensShading}};}
bool parseGeometry(const QJsonObject &o,Recipe &r){
    if(!keys(o,{"crop","angle","flipHorizontal","flipVertical","perspective","distortion","chromaticAberration","lensShading"}))return false;
    if(!o["crop"].isArray())return false;const auto crop=o["crop"].toArray();if(crop.size()!=4)return false;for(const auto v:crop)if(!number(v))return false;
    for(const auto name:{"angle","distortion","lensShading"})if(!number(o[name]))return false;
    if(!o["flipHorizontal"].isBool()||!o["flipVertical"].isBool()||!pair(o["perspective"],r.perspective)||!pair(o["chromaticAberration"],r.chromaticAberration))return false;
    r.crop={crop[0].toDouble(),crop[1].toDouble(),crop[2].toDouble(),crop[3].toDouble()};r.angle=o["angle"].toDouble();
    r.distortion=o["distortion"].toDouble();r.lensShading=o["lensShading"].toDouble();r.flipHorizontal=o["flipHorizontal"].toBool();r.flipVertical=o["flipVertical"].toBool();
    return geometryOnly(r);
}
}
bool validUuid(const QString &text){const QUuid uuid(text);return text.size()==36&&!uuid.isNull()&&uuid.toString(QUuid::WithoutBraces)==text;}
bool validHash(const QString &text){if(text.size()!=64)return false;for(const auto c:text)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;return true;}
bool validRequest(const Request &r,QString &error){
    error.clear();if(!validUuid(r.id)||!validUuid(r.token)||!validHash(r.sourceHash))return reject(error,QStringLiteral("RAW任务身份或源指纹无效"));
    if(r.sourcePath.isEmpty()||r.sourcePath.size()>32767||r.sourcePath.contains(QChar(0))||!QDir::isAbsolutePath(r.sourcePath))return reject(error,QStringLiteral("RAW源路径无效"));
    if(!std::isfinite(r.warmth)||!std::isfinite(r.tint)||std::abs(r.warmth)>1||std::abs(r.tint)>1)return reject(error,QStringLiteral("RAW白平衡参数无效"));
    if(QFileInfo(r.sourcePath).suffix().compare("cr3",Qt::CaseInsensitive)!=0&&(r.warmth!=0||r.tint!=0))return reject(error,QStringLiteral("非RAW源不能烘焙RAW白平衡"));
    if(!geometryOnly(r.geometry))return reject(error,QStringLiteral("RAW任务只接受有效几何前缀"));
    if(r.operation==Operation::DecodeFull){if(r.longEdge!=0||r.geometry!=Recipe{})return reject(error,QStringLiteral("全源读取不能烘焙几何或缩放"));}
    else if(r.operation==Operation::Prepare){if(r.longEdge!=3840)return reject(error,QStringLiteral("工作帧必须采用长边4K，小图保持原尺寸"));}
    else return reject(error,QStringLiteral("未知RAW任务类型"));
    return true;
}
QJsonObject requestJson(const Request &r,QString &error){
    if(!validRequest(r,error))return {};
    return {{"type","request"},{"protocol",protocolVersion},{"id",r.id},{"token",r.token},{"sourcePath",r.sourcePath},{"sourceHash",r.sourceHash},
        {"operation",r.operation==Operation::DecodeFull?"decode":"prepare"},{"warmth",r.warmth},{"tint",r.tint},{"longEdge",r.longEdge},{"geometry",geometryJson(r.geometry)}};
}
bool requestFromJson(const QJsonObject &o,Request &out,QString &error){
    error.clear();if(!keys(o,{"type","protocol","id","token","sourcePath","sourceHash","operation","warmth","tint","longEdge","geometry"})||
       o["type"]!=QJsonValue("request")||o["protocol"]!=QJsonValue(protocolVersion))return reject(error,QStringLiteral("RAW请求版本或字段无效"));
    for(const auto name:{"id","token","sourcePath","sourceHash","operation"})if(!o[name].isString())return reject(error,QStringLiteral("RAW请求字符串类型无效"));
    if(!number(o["warmth"])||!number(o["tint"])||!number(o["longEdge"])||!o["geometry"].isObject())return reject(error,QStringLiteral("RAW请求数值类型无效"));
    Request r;r.id=o["id"].toString();r.token=o["token"].toString();r.sourcePath=o["sourcePath"].toString();r.sourceHash=o["sourceHash"].toString();r.warmth=o["warmth"].toDouble();r.tint=o["tint"].toDouble();
    const auto op=o["operation"].toString();if(op=="decode")r.operation=Operation::DecodeFull;else if(op=="prepare")r.operation=Operation::Prepare;else return reject(error,QStringLiteral("未知RAW请求操作"));
    if(o["longEdge"].toDouble()!=(r.operation==Operation::DecodeFull?0:3840))return reject(error,QStringLiteral("RAW目标边长无效"));
    r.longEdge=r.operation==Operation::DecodeFull?0:3840;
    if(!parseGeometry(o["geometry"].toObject(),r.geometry)||!validRequest(r,error))return reject(error,error.isEmpty()?QStringLiteral("RAW几何无效"):error);
    out=std::move(r);return true;
}
QString requestHash(const Request &r,QString &error){const auto json=requestJson(r,error);return error.isEmpty()?QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(json).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex()):QString{};}
QJsonObject hello(){return {{"type","hello"},{"protocol",protocolVersion},{"algorithm",algorithmVersion},{"libraw",QString::fromLatin1(LibRaw::version())},
    {"lcms",int(cmsGetEncodedCMMversion())},{"qt",QString::fromLatin1(qVersion())},{"pixelFormat","rgba32f-linear-rec2020-straight"},{"endian","little"},{"sourceMetadata",1}};}
bool validHello(const QJsonObject &json,QString &error){error.clear();if(json!=hello())return reject(error,QStringLiteral("RAW进程握手/算法/依赖版本不匹配"));return true;}
QByteArray encodeLine(const QJsonObject &o,QString &error){error.clear();auto data=QJsonDocument(o).toJson(QJsonDocument::Compact);if(data.size()>maximumLineBytes){error=QStringLiteral("RAW控制消息超限");return {};}data.append('\n');return data;}
bool decodeLine(QByteArrayView line,QJsonObject &out,QString &error){
    error.clear();if(line.isEmpty()||line.size()>maximumLineBytes)return reject(error,QStringLiteral("RAW控制行为空或超限"));
    QJsonParseError parse;const auto document=QJsonDocument::fromJson(QByteArray(line.data(),line.size()),&parse);
    if(parse.error!=QJsonParseError::NoError||!document.isObject()||document.toJson(QJsonDocument::Compact)!=line)return reject(error,QStringLiteral("RAW控制行不是规范紧凑JSON对象"));
    out=document.object();return true;
}
QJsonObject cancelJson(const Request &r){return {{"type","cancel"},{"protocol",protocolVersion},{"id",r.id}};}
bool validCancel(const QJsonObject &o,const Request &r,QString &error){error.clear();if(!validUuid(r.id)||o!=cancelJson(r))return reject(error,QStringLiteral("RAW取消身份或消息无效"));return true;}
}
