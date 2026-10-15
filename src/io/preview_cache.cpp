#include "preview_cache.h"
#include "preview_size.h"
#include "fast_hash.h"
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QtEndian>
#include <cmath>
#include <QElapsedTimer>
static QString key(const QString &path,const QString &sha){return QString::fromLatin1(QCryptographicHash::hash((path+"|"+sha).toUtf8(),QCryptographicHash::Sha256).toHex());}
bool PreviewCache::put(const SourceFrame &s,QJsonObject *timings){
    QElapsedTimer timer;timer.start();if(timings)*timings={};
    QMutexLocker guard(&mutex);
    if(!directory.isValid()||s.linear.isNull()||s.linear.format()!=QImage::Format_RGBA32FPx4||qMax(s.linear.width(),s.linear.height())>3840||!std::isfinite(s.sourcePixelsPerPixel)||s.sourcePixelsPerPixel<=0)return false;
    if(!acceptedImageSize(s.originalSize)||std::abs(s.sourcePixelsPerPixel-std::max(double(s.originalSize.width())/s.linear.width(),double(s.originalSize.height())/s.linear.height()))>1e-8)return false;
    const auto id=key(s.path,s.sha256);
    if(entries.contains(id)){const auto old=entries.take(id);used-=sizes.take(id);order.removeAll(id);QFile::remove(old);}
    QJsonArray matrix,wb;for(float v:s.cameraToWork)matrix.append(v);for(double v:s.cameraWB)wb.append(v);
    const auto bytes=s.linear.sizeInBytes();bool nativeHash=false;const auto pixelHash=fastSha256(QByteArrayView(reinterpret_cast<const char *>(s.linear.constBits()),bytes),&nativeHash).toHex();
    if(timings){timings->insert("pixelHashMs",double(timer.elapsed()));timings->insert("nativeHash",nativeHash);}timer.restart();
    const auto header=QJsonDocument(QJsonObject{{"path",s.path},{"sha",s.sha256},{"note",s.colorNote},{"facts",s.facts},{"raw",s.raw},{"warmth",s.rawWarmth},{"tint",s.rawTint},
        {"width",s.linear.width()},{"height",s.linear.height()},{"sourceScale",s.sourcePixelsPerPixel},{"originalWidth",s.originalSize.width()},{"originalHeight",s.originalSize.height()},{"matrix",matrix},{"wb",wb},{"pixels",QString::fromLatin1(pixelHash)}}).toJson(QJsonDocument::Compact);
    const auto headerHash=QCryptographicHash::hash(header,QCryptographicHash::Sha256);const qint64 total=44+header.size()+bytes;if(total>budget||header.size()>65536)return false;
    while(used+total>budget&&!order.isEmpty()){const auto oldest=order.takeFirst();const auto file=entries.take(oldest);used-=sizes.take(oldest);QFile::remove(file);}
    const auto file=directory.path()+"/"+id+".work";QSaveFile output(file);if(!output.open(QIODevice::WriteOnly))return false;
    quint32 length=qToLittleEndian(quint32(header.size()));
    if(output.write("LM4KF003",8)!=8||output.write(reinterpret_cast<const char *>(&length),4)!=4||output.write(headerHash)!=32||output.write(header)!=header.size()||output.write(reinterpret_cast<const char *>(s.linear.constBits()),bytes)!=bytes||!output.commit())return false;
    if(timings)timings->insert("headerAndAtomicWriteMs",double(timer.elapsed()));
    entries[id]=file;sizes[id]=total;order.append(id);used+=total;return true;
}
std::optional<SourceFrame> PreviewCache::get(const QString &path,const QString &sha){
    QMutexLocker guard(&mutex);
    const auto id=key(path,sha);if(!entries.contains(id))return {};QFile input(entries.value(id));if(!input.open(QIODevice::ReadOnly)||input.read(8)!="LM4KF003")return {};
    quint32 length=0;if(input.read(reinterpret_cast<char *>(&length),4)!=4)return {};length=qFromLittleEndian(length);if(length>65536)return {};
    const auto headerHash=input.read(32);if(headerHash.size()!=32)return {};const auto data=input.read(length);if(data.size()!=length||QCryptographicHash::hash(data,QCryptographicHash::Sha256)!=headerHash)return {};QJsonParseError error;const auto json=QJsonDocument::fromJson(data,&error);
    if(error.error!=QJsonParseError::NoError||!json.isObject())return {};const auto j=json.object();SourceFrame s;
    s.path=j.value("path").toString();s.sha256=j.value("sha").toString();if(s.path!=path||s.sha256!=sha)return {};
    const QSize size(j.value("width").toInt(),j.value("height").toInt());s.originalSize={j.value("originalWidth").toInt(),j.value("originalHeight").toInt()};
    s.sourcePixelsPerPixel=j.value("sourceScale").toDouble();if(!std::isfinite(s.sourcePixelsPerPixel)||s.sourcePixelsPerPixel<=0)return {};
    if(!acceptedImageSize(s.originalSize)||size.width()<1||size.height()<1||qMax(size.width(),size.height())>3840)return {};
    if(std::abs(s.sourcePixelsPerPixel-std::max(double(s.originalSize.width())/size.width(),double(s.originalSize.height())/size.height()))>1e-8)return {};
    const auto bytes=qint64(size.width())*size.height()*16;if(input.size()!=44+length+bytes)return {};
    s.linear=QImage(size,QImage::Format_RGBA32FPx4);if(s.linear.isNull()||s.linear.sizeInBytes()!=bytes||input.read(reinterpret_cast<char *>(s.linear.bits()),bytes)!=bytes)return {};
    const auto actual=fastSha256(QByteArrayView(reinterpret_cast<const char *>(s.linear.constBits()),bytes)).toHex();if(actual!=j.value("pixels").toString().toLatin1())return {};
    s.sourcePixelsPerPixelX=double(s.originalSize.width())/size.width();s.sourcePixelsPerPixelY=double(s.originalSize.height())/size.height();
    s.colorNote=j.value("note").toString();s.facts=j.value("facts").toObject();s.raw=j.value("raw").toBool();s.rawWarmth=j.value("warmth").toDouble();s.rawTint=j.value("tint").toDouble();
    const auto matrix=j.value("matrix").toArray(),wb=j.value("wb").toArray();if(matrix.size()!=9||wb.size()!=4)return {};
    for(int i=0;i<9;++i)s.cameraToWork[i]=float(matrix[i].toDouble());for(int i=0;i<4;++i)s.cameraWB[i]=wb[i].toDouble();
    for(float v:s.cameraToWork)if(!std::isfinite(v))return {};
    if(s.raw)for(double v:s.cameraWB)if(!std::isfinite(v)||v<=0)return {};
    order.removeAll(id);order.append(id);return s;
}
