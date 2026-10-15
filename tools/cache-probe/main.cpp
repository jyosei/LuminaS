// Isolated cache feasibility experiment. The application loader does not use this cache.
#include "raw/raw_decoder.h"
#include "io/jpeg_loader.h"
#include <QCoreApplication>
#include <QCache>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QFileInfo>
#include <QDateTime>
#include <QTextStream>

static QString keyFor(const QString &path) {
    QFile file(path); if(!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256); if(!hash.addData(&file)) return {};
    return QFileInfo(path).canonicalFilePath()+"|"+QString::fromLatin1(hash.result().toHex())+
        "|AHD-cameraWB-v1|3840|sRGB|LibRaw0.22.2|Qt"+qVersion();
}
static QByteArray pixels(const QImage &image) {
    const auto frame=image.convertToFormat(QImage::Format_RGB32);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for(int y=0;y<frame.height();++y) hash.addData(QByteArrayView(reinterpret_cast<const char *>(frame.constScanLine(y)),frame.width()*4));
    return hash.result().toHex();
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv); const auto args=app.arguments();
    if(args.size()!=4 || QFileInfo::exists(args[3])) return 2;
    QCache<QString,QImage> cache(96*1024*1024); QJsonObject records;
    for(int source=1;source<=2;++source) {
        QElapsedTimer timer; timer.start(); const auto key=keyFor(args[source]); if(key.isEmpty()) return 1;
        QImage image;
        if(source==1) { RawOptions options; options.collectLinearDiagnostics=false; const auto raw=developRaw(args[source],options); if(!raw.error.isEmpty()) return 1; image=raw.preview; }
        else { const auto jpg=decodeJpeg(args[source]); if(!jpg.error.isEmpty()) return 1; image=jpg.image; }
        const auto missMs=timer.elapsed(); const auto digest=pixels(image);
        if(!cache.insert(key,new QImage(image),int(image.sizeInBytes()))) return 1;
        QJsonArray hits;
        for(int i=0;i<5;++i) {
            timer.restart(); const auto verifiedKey=keyFor(args[source]);
            const auto *cached=cache.object(verifiedKey); if(!cached) return 1;
            const auto hitMs=timer.elapsed(); if(pixels(*cached)!=digest) return 1;
            hits.append(double(hitMs));
        }
        records.insert(source==1?"raw":"jpeg",QJsonObject{{"missMs",double(missMs)},{"hitMs",hits},
            {"previewBytes",double(image.sizeInBytes())},{"previewRgb32Sha256",QString::fromLatin1(digest)}});
    }
    const auto twoFrameCost=cache.totalCost(); if(twoFrameCost>cache.maxCost() || cache.size()!=2) return 1;
    // Content invalidation even when timestamp is restored; only mutate a disposable copy.
    QTemporaryDir temp; if(!temp.isValid()) return 1;
    const auto copy=temp.path()+"/copy.jpg"; if(!QFile::copy(args[2],copy)) return 1;
    const auto oldKey=keyFor(copy); const auto originalTime=QFileInfo(copy).lastModified();
    const auto *jpeg=cache.object(keyFor(args[2])); if(!jpeg) return 1;
    const auto copied=*jpeg; if(!cache.insert(oldKey,new QImage(copied),int(copied.sizeInBytes()))) return 1;
    QFile changed(copy); if(!changed.open(QIODevice::ReadWrite) || !changed.seek(4096)) return 1;
    const auto byte=changed.read(1); if(byte.size()!=1 || !changed.seek(4096)) return 1;
    const char modified=char(byte[0]^1); if(changed.write(&modified,1)!=1) return 1;
    if(!changed.setFileTime(originalTime,QFileDevice::FileModificationTime)) return 1; changed.close();
    const auto newKey=keyFor(copy);
    if(newKey==oldKey || cache.contains(newKey) || QFileInfo(copy).lastModified()!=originalTime) return 1;
    if(cache.totalCost()>cache.maxCost()) return 1;
    cache.setMaxCost(1); if(!cache.isEmpty() || cache.totalCost()!=0) return 1;
    QJsonObject report{{"experimentOnly",true},{"cacheBudgetBytes",96*1024*1024},{"twoFrameCostBytes",twoFrameCost},
        {"records",records},{"fullSourceHashOnEveryHit",true},{"sameSizeSameTimestampMutationInvalidated",true},
        {"evictionVerified",true},{"scope","cache accounting, not process RAM peak; no GUI painting/queue or editing-revision integration"}};
    QSaveFile output(args[3]); if(!output.open(QIODevice::WriteOnly)) return 1;
    const auto bytes=QJsonDocument(report).toJson();
    if(output.write(bytes)!=bytes.size() || !output.commit()) return 1;
    QTextStream(stdout)<<"Cache feasibility checks passed\n"; return 0;
}
