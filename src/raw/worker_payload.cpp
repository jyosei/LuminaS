#include "worker_payload.h"
#include "io/preview_size.h"
#include "render/cpu_renderer.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTemporaryFile>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QtEndian>
#include <QSysInfo>
#include <cmath>
#include <algorithm>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <filesystem>
#endif

namespace RawWorker {
namespace {
const QByteArray magic("LUMRAW1\0",8);
bool reject(QString &e,const QString &m){e=m;return false;}
bool keys(const QJsonObject &o,std::initializer_list<const char *> names){if(o.size()!=qsizetype(names.size()))return false;for(const auto n:names)if(!o.contains(QLatin1String(n)))return false;return true;}
bool number(const QJsonValue &v){return v.isDouble()&&std::isfinite(v.toDouble());}
bool finiteJson(const QJsonValue &v,int depth=0){
    if(depth>64)return false;if(v.isDouble())return std::isfinite(v.toDouble());
    if(v.isArray()){for(const auto item:v.toArray())if(!finiteJson(item,depth+1))return false;}
    if(v.isObject()){const auto o=v.toObject();for(auto it=o.begin();it!=o.end();++it)if(!finiteJson(it.value(),depth+1))return false;}
    return true;
}
bool sizeFromJson(const QJsonValue &v,QSize &out){
    if(!v.isArray())return false;const auto a=v.toArray();if(a.size()!=2)return false;
    for(const auto n:a)if(!number(n)||n.toDouble()<1||n.toDouble()>45000000||std::floor(n.toDouble())!=n.toDouble())return false;
    out={int(a[0].toDouble()),int(a[1].toDouble())};return acceptedImageSize(out);
}
bool byteCount(const QJsonValue &v,qint64 &out){
    if(!v.isString())return false;const auto s=v.toString();if(s.isEmpty()||s.size()>10||(s.size()>1&&s[0]=='0'))return false;
    for(const auto c:s)if(c<'0'||c>'9')return false;bool ok=false;const auto n=s.toLongLong(&ok);
    if(!ok||n<32||n>maximumPayloadBytes+maximumLineBytes+32)return false;out=n;return true;
}
bool directoryPath(const QString &input,QString &out,QString &error){
    const QFileInfo info(input);if(input.isEmpty()||input.contains(QChar(0))||!QDir::isAbsolutePath(input)||!info.isDir()||info.isSymLink())return reject(error,QStringLiteral("RAW会话目录无效或尚未创建"));
    out=info.canonicalFilePath();if(out.isEmpty())return reject(error,QStringLiteral("RAW会话目录无法解析"));return true;
}
template<class T,size_t N> QJsonArray arrayJson(const std::array<T,N> &values){QJsonArray result;for(const auto v:values)result.append(double(v));return result;}
template<class T,size_t N> bool parseArray(const QJsonValue &value,std::array<T,N> &out){
    if(!value.isArray())return false;const auto values=value.toArray();if(values.size()!=qsizetype(N))return false;
    for(size_t i=0;i<N;++i){if(!number(values[qsizetype(i)]))return false;out[i]=T(values[qsizetype(i)].toDouble());if(!std::isfinite(out[i]))return false;}return true;
}
bool same(double a,double b){return std::abs(a-b)<=1e-12*std::max(1.,std::max(std::abs(a),std::abs(b)));}
bool shape(const Request &r,const SourceFrame &f,QSize pixels,QString &error){
    if(!validRequest(r,error))return false;
    if constexpr(QSysInfo::ByteOrder!=QSysInfo::LittleEndian||sizeof(float)!=4)return reject(error,QStringLiteral("RAW帧平台不是little-endian float32"));
    if(f.path!=r.sourcePath||f.sha256!=r.sourceHash||!acceptedImageSize(f.originalSize)||!acceptedImageSize(pixels))return reject(error,QStringLiteral("RAW帧源身份或尺寸不匹配"));
    const bool expectedRaw=QFileInfo(r.sourcePath).suffix().compare("cr3",Qt::CaseInsensitive)==0;
    if(f.raw!=expectedRaw||!std::isfinite(f.rawWarmth)||!std::isfinite(f.rawTint)||f.rawWarmth!=(f.raw?r.warmth:0)||f.rawTint!=(f.raw?r.tint:0))return reject(error,QStringLiteral("RAW帧白平衡或源类型矛盾"));
    for(const auto v:f.cameraToWork)if(!std::isfinite(v))return reject(error,QStringLiteral("RAW帧矩阵非有限"));
    for(const auto v:f.draftBalance)if(!std::isfinite(v))return reject(error,QStringLiteral("RAW草稿矩阵非有限"));
    for(const auto v:f.cameraWB)if(!std::isfinite(v)||(f.raw&&v<=0))return reject(error,QStringLiteral("RAW相机白平衡无效"));
    if(f.raw&&std::all_of(f.cameraToWork.begin(),f.cameraToWork.end(),[](float v){return v==0;}))return reject(error,QStringLiteral("RAW相机矩阵为空"));
    if(!std::isfinite(f.sourcePixelsPerPixel)||!std::isfinite(f.sourcePixelsPerPixelX)||!std::isfinite(f.sourcePixelsPerPixelY)||f.sourcePixelsPerPixel<=0||f.sourcePixelsPerPixelX<0||f.sourcePixelsPerPixelY<0)return reject(error,QStringLiteral("RAW帧支持尺度无效"));
    if(r.operation==Operation::DecodeFull){
        if(pixels!=f.originalSize||f.sourcePixelsPerPixel!=1||f.sourcePixelsPerPixelX!=0||f.sourcePixelsPerPixelY!=0)return reject(error,QStringLiteral("全源帧尺寸或尺度矛盾"));
    }else {
        const auto expected=previewSize(renderedSize(f.originalSize,r.geometry),r.longEdge);
        const auto x=double(f.originalSize.width())*r.geometry.crop.width()/pixels.width(),y=double(f.originalSize.height())*r.geometry.crop.height()/pixels.height();
        if(pixels!=expected||!same(f.sourcePixelsPerPixelX,x)||!same(f.sourcePixelsPerPixelY,y)||!same(f.sourcePixelsPerPixel,std::max(x,y)))return reject(error,QStringLiteral("4K工作帧尺寸或源尺度矛盾"));
    }
    if(f.colorNote.size()>4096||f.colorNote.contains(QChar(0))||!finiteJson(f.facts)||QJsonDocument(f.facts).toJson(QJsonDocument::Compact).size()>262144)return reject(error,QStringLiteral("RAW帧事实或说明超限"));
    for(const auto name:{"width","height"})if(f.facts.contains(name)){
        const auto v=f.facts[name];if(!number(v)||v.toDouble()!=(QString::fromLatin1(name)=="width"?f.originalSize.width():f.originalSize.height()))return reject(error,QStringLiteral("RAW帧源事实尺寸矛盾"));
    }
    return true;
}
bool pixelRow(const float *values,int width){
    for(int x=0;x<width;++x){for(int c=0;c<4;++c)if(!std::isfinite(values[x*4+c]))return false;
        // Preserve existing interpolation values without clipping/quantizing.
        if(values[x*4+3]<-1e-6f||values[x*4+3]>1.000001f)return false;}
    return true;
}
QJsonObject metadata(const Request &r,const SourceFrame &f,const QString &pixelHash,QString &error){return {
    {"format","rgba32f-linear-rec2020-straight"},{"requestHash",requestHash(r,error)},{"path",f.path},{"sourceHash",f.sha256},{"raw",f.raw},
    {"sourceSize",QJsonArray{f.originalSize.width(),f.originalSize.height()}},{"pixelSize",QJsonArray{f.linear.width(),f.linear.height()}},
    {"warmth",f.rawWarmth},{"tint",f.rawTint},{"scale",f.sourcePixelsPerPixel},{"scaleX",f.sourcePixelsPerPixelX},{"scaleY",f.sourcePixelsPerPixelY},
    {"cameraToWork",arrayJson(f.cameraToWork)},{"cameraWB",arrayJson(f.cameraWB)},{"draftBalance",arrayJson(f.draftBalance)},
    {"note",f.colorNote},{"facts",f.facts},{"pixelSha256",pixelHash}};}
bool parseMetadata(const QJsonObject &o,const Request &r,SourceFrame &f,QSize &pixels,QString &error){
    if(!keys(o,{"format","requestHash","path","sourceHash","raw","sourceSize","pixelSize","warmth","tint","scale","scaleX","scaleY","cameraToWork","cameraWB","draftBalance","note","facts","pixelSha256"}))return reject(error,QStringLiteral("RAW帧元数据字段不完整"));
    for(const auto name:{"format","requestHash","path","sourceHash","note","pixelSha256"})if(!o[name].isString())return reject(error,QStringLiteral("RAW帧字符串类型无效"));
    if(o["format"]!=QJsonValue("rgba32f-linear-rec2020-straight")||o["requestHash"].toString()!=requestHash(r,error)||!validHash(o["pixelSha256"].toString())||!o["raw"].isBool()||!o["facts"].isObject())return reject(error,QStringLiteral("RAW帧格式、任务或事实无效"));
    for(const auto name:{"warmth","tint","scale","scaleX","scaleY"})if(!number(o[name]))return reject(error,QStringLiteral("RAW帧数值类型无效"));
    if(!sizeFromJson(o["sourceSize"],f.originalSize)||!sizeFromJson(o["pixelSize"],pixels)||!parseArray(o["cameraToWork"],f.cameraToWork)||!parseArray(o["cameraWB"],f.cameraWB)||!parseArray(o["draftBalance"],f.draftBalance))return reject(error,QStringLiteral("RAW帧尺寸或矩阵格式无效"));
    f.path=o["path"].toString();f.sha256=o["sourceHash"].toString();f.raw=o["raw"].toBool();f.rawWarmth=o["warmth"].toDouble();f.rawTint=o["tint"].toDouble();
    f.sourcePixelsPerPixel=o["scale"].toDouble();f.sourcePixelsPerPixelX=o["scaleX"].toDouble();f.sourcePixelsPerPixelY=o["scaleY"].toDouble();f.colorNote=o["note"].toString();f.facts=o["facts"].toObject();
    return shape(r,f,pixels,error);
}
bool writeBytes(QIODevice &file,QByteArrayView bytes,QCryptographicHash &hash,const CancellationToken &cancel,QString &error){
    qsizetype position=0;while(position<bytes.size()){
        if(isCanceled(cancel))return reject(error,QStringLiteral("Canceled"));
        const auto count=std::min<qsizetype>(1024*1024,bytes.size()-position);const auto written=file.write(bytes.data()+position,count);
        if(written<=0)return reject(error,file.errorString());hash.addData(QByteArrayView(bytes.data()+position,written));position+=written;
    }return true;
}
bool commitNoReplace(const QString &from,const QString &to,QString &error){
#ifdef Q_OS_WIN
    if(!MoveFileExW(reinterpret_cast<const wchar_t *>(from.utf16()),reinterpret_cast<const wchar_t *>(to.utf16()),MOVEFILE_WRITE_THROUGH))return reject(error,QStringLiteral("RAW帧提交失败（Windows %1），未覆盖既有目标").arg(GetLastError()));
#else
    std::error_code code;std::filesystem::create_hard_link(from.toStdString(),to.toStdString(),code);if(code)return reject(error,QString::fromStdString(code.message()));
#endif
    return true;
}
struct StagingOwner {QString path;~StagingOwner(){if(!path.isEmpty())QFile::remove(path);}};
}
bool validFrame(const Request &r,const SourceFrame &f,QString &error){
    error.clear();if(!shape(r,f,f.linear.size(),error))return false;
    if(f.linear.format()!=QImage::Format_RGBA32FPx4)return reject(error,QStringLiteral("RAW帧不是RGBA32F"));
    for(int y=0;y<f.linear.height();++y)if(!pixelRow(reinterpret_cast<const float *>(f.linear.constScanLine(y)),f.linear.width()))return reject(error,QStringLiteral("RAW帧含非法float/alpha"));return true;
}
bool writePayload(const QString &directory,const Request &r,const SourceFrame &f,QJsonObject &out,QString &error,CancellationToken cancel){
    error.clear();if(isCanceled(cancel))return reject(error,QStringLiteral("Canceled"));
    if(!shape(r,f,f.linear.size(),error))return false;
    if(f.linear.format()!=QImage::Format_RGBA32FPx4)return reject(error,QStringLiteral("RAW帧不是RGBA32F"));
    QString root;if(!directoryPath(directory,root,error))return false;const auto target=QDir(root).filePath(r.token+".frame");
    if(QFileInfo::exists(target)||QFileInfo(target).isSymLink())return reject(error,QStringLiteral("RAW帧目标已存在，未覆盖"));
    const auto rowBytes=qint64(f.linear.width())*16,pixelBytes=rowBytes*f.linear.height();QCryptographicHash pixelHash(QCryptographicHash::Sha256);
    for(int y=0;y<f.linear.height();++y){
        if(isCanceled(cancel))return reject(error,QStringLiteral("Canceled"));
        if(!pixelRow(reinterpret_cast<const float *>(f.linear.constScanLine(y)),f.linear.width()))return reject(error,QStringLiteral("RAW帧含非法float/alpha"));
        pixelHash.addData(QByteArrayView(reinterpret_cast<const char *>(f.linear.constScanLine(y)),rowBytes));
    }
    const auto digest=QString::fromLatin1(pixelHash.result().toHex());const auto meta=metadata(r,f,digest,error);if(!error.isEmpty())return false;
    const auto json=QJsonDocument(meta).toJson(QJsonDocument::Compact);if(json.size()>maximumLineBytes)return reject(error,QStringLiteral("RAW帧元数据超限"));
    QByteArray header(32,'\0');std::copy(magic.begin(),magic.end(),header.begin());
    qToLittleEndian<quint32>(protocolVersion,header.data()+8);qToLittleEndian<quint32>(quint32(json.size()),header.data()+12);qToLittleEndian<quint64>(quint64(pixelBytes),header.data()+16);
    qToLittleEndian<quint32>(quint32(f.linear.width()),header.data()+24);qToLittleEndian<quint32>(quint32(f.linear.height()),header.data()+28);
    QCryptographicHash fileHash(QCryptographicHash::Sha256);
    StagingOwner stagedFile;
    {
        QTemporaryFile staged(QDir(root).filePath(".raw-stage-XXXXXX"));if(!staged.open())return reject(error,staged.errorString());
        if(!writeBytes(staged,header,fileHash,cancel,error)||!writeBytes(staged,json,fileHash,cancel,error))return false;
        for(int y=0;y<f.linear.height();++y)if(!writeBytes(staged,QByteArrayView(reinterpret_cast<const char *>(f.linear.constScanLine(y)),rowBytes),fileHash,cancel,error))return false;
        if(!staged.flush())return reject(error,staged.errorString());
        stagedFile.path=staged.fileName();staged.setAutoRemove(false);
        // QTemporaryFile::close keeps its underlying handle for reopening.
        // Destroy it before the Windows no-replace move; retain separate RAII
        // ownership of only this freshly created staging file.
    }
    if(isCanceled(cancel))return reject(error,QStringLiteral("Canceled"));
    if(!commitNoReplace(stagedFile.path,target,error))return false;
    out={{"type","done"},{"protocol",protocolVersion},{"id",r.id},{"token",r.token},{"fileBytes",QString::number(32+json.size()+pixelBytes)},
         {"pixelSha256",digest},{"fileSha256",QString::fromLatin1(fileHash.result().toHex())},{"frame",meta}};return true;
}
FrameResult readPayload(const QString &directory,const Request &r,const QJsonObject &done,const CancellationToken &cancel){
    QString error;const auto fail=[&](const QString &e){return FrameResult{{},e};};
    if(isCanceled(cancel))return fail("Canceled");if(!validRequest(r,error))return fail(error);
    if(!keys(done,{"type","protocol","id","token","fileBytes","pixelSha256","fileSha256","frame"})||done["type"]!=QJsonValue("done")||done["protocol"]!=QJsonValue(protocolVersion)||
       done["id"]!=QJsonValue(r.id)||done["token"]!=QJsonValue(r.token)||!done["pixelSha256"].isString()||!validHash(done["pixelSha256"].toString())||
       !done["fileSha256"].isString()||!validHash(done["fileSha256"].toString())||!done["frame"].isObject())return fail(QStringLiteral("RAW完成消息无效或任务身份不符"));
    qint64 fileBytes=0;if(!byteCount(done["fileBytes"],fileBytes))return fail(QStringLiteral("RAW完成消息字节数无效"));
    QString root;if(!directoryPath(directory,root,error))return fail(error);const auto path=QDir(root).filePath(r.token+".frame");const QFileInfo info(path);
    if(!info.isFile()||info.isSymLink()||info.size()!=fileBytes)return fail(QStringLiteral("RAW帧文件类型或实际长度不匹配"));
    QFile file(path);if(!file.open(QIODevice::ReadOnly))return fail(file.errorString());if(file.size()!=fileBytes)return fail(QStringLiteral("RAW帧文件长度变化"));
    const auto header=file.read(32);if(header.size()!=32||header.left(8)!=magic||qFromLittleEndian<quint32>(header.constData()+8)!=protocolVersion)return fail(QStringLiteral("RAW帧头/版本无效"));
    const auto metaBytes=qFromLittleEndian<quint32>(header.constData()+12);const auto pixelBytes=qFromLittleEndian<quint64>(header.constData()+16);
    const auto width=qFromLittleEndian<quint32>(header.constData()+24),height=qFromLittleEndian<quint32>(header.constData()+28);
    if(!metaBytes||metaBytes>maximumLineBytes||!width||!height||width>45000000||height>45000000||quint64(width)*height>45000000||pixelBytes!=quint64(width)*height*16||quint64(fileBytes)!=32+quint64(metaBytes)+pixelBytes)return fail(QStringLiteral("RAW帧尺寸/长度超限或矛盾"));
    const auto json=file.read(metaBytes);QJsonObject meta;if(json.size()!=metaBytes||!decodeLine(json,meta,error))return fail(error.isEmpty()?QStringLiteral("RAW帧元数据截断"):error);
    if(meta!=done["frame"].toObject()||meta["pixelSha256"]!=done["pixelSha256"])return fail(QStringLiteral("RAW帧元数据与完成消息不一致"));
    auto frame=std::make_shared<SourceFrame>();QSize size;if(!parseMetadata(meta,r,*frame,size,error))return fail(error);
    if(size!=QSize(int(width),int(height)))return fail(QStringLiteral("RAW帧头与元数据尺寸矛盾"));
    if(isCanceled(cancel))return fail("Canceled");frame->linear=QImage(size,QImage::Format_RGBA32FPx4);if(frame->linear.isNull())return fail(QStringLiteral("RAW帧分配失败"));
    QCryptographicHash wholeHash(QCryptographicHash::Sha256),pixelHash(QCryptographicHash::Sha256);wholeHash.addData(header);wholeHash.addData(json);
    const auto rowBytes=qint64(width)*16;
    for(int y=0;y<size.height();++y){
        qint64 position=0;auto *row=reinterpret_cast<char *>(frame->linear.scanLine(y));
        while(position<rowBytes){if(isCanceled(cancel))return fail("Canceled");const auto count=file.read(row+position,std::min<qint64>(1024*1024,rowBytes-position));if(count<=0)return fail(QStringLiteral("RAW像素读取失败/截断"));position+=count;}
        if(!pixelRow(reinterpret_cast<const float *>(row),size.width()))return fail(QStringLiteral("RAW像素含非法float/alpha"));
        const auto bytes=QByteArrayView(row,rowBytes);wholeHash.addData(bytes);pixelHash.addData(bytes);
    }
    if(file.size()!=fileBytes||!file.atEnd()||QString::fromLatin1(wholeHash.result().toHex())!=done["fileSha256"].toString()||QString::fromLatin1(pixelHash.result().toHex())!=done["pixelSha256"].toString())return fail(QStringLiteral("RAW帧读取后长度或SHA不一致"));
    if(isCanceled(cancel))return fail("Canceled");return {std::move(frame),{}};
}
}
