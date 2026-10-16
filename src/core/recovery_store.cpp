#include "recovery_store.h"
#include "io/atomic_file.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLockFile>
#include <atomic>
#include <algorithm>

namespace LocalEdit {
namespace {
std::atomic_flag recoveryWriter=ATOMIC_FLAG_INIT;
struct Writer {
    bool owned=!recoveryWriter.test_and_set(std::memory_order_acquire);
    ~Writer(){if(owned)recoveryWriter.clear(std::memory_order_release);}
};
bool fail(QString &error,const QString &message){error=QStringLiteral("recovery store: ")+message;return false;}
bool canceled(QString &error,CancellationToken cancel){
    if(!isCanceled(cancel))return false;
    fail(error,QStringLiteral("canceled"));return true;
}
QString key(const QString &path){
    if(path.isEmpty())return {};
    const QFileInfo info(path);auto result=info.canonicalFilePath();
    if(result.isEmpty()){const auto parent=info.absoluteDir().canonicalPath();result=parent.isEmpty()?info.absoluteFilePath():QDir(parent).filePath(info.fileName());}
    result=QDir::cleanPath(QDir::fromNativeSeparators(result));
#ifdef Q_OS_WIN
    result=result.toCaseFolded();
#endif
    return result;
}
bool pathShape(const QString &path,QString &error){
    if(path.isEmpty()||path.size()>32767||path.contains(QChar(0))||!QFileInfo(path).isAbsolute())return fail(error,QStringLiteral("invalid absolute target path"));
    const QFileInfo info(path);
    if(info.isSymLink()||(info.exists()&&!info.isFile()))return fail(error,QStringLiteral("target must be a regular file, not a link or directory"));
    return true;
}
bool protectedPaths(const QString &path,const RecoveryState &state,QString &error){
    const QString lockPath=path+QStringLiteral(".luminas.lock");
    if(!pathShape(path,error)||QFileInfo(lockPath).isSymLink())return fail(error,QStringLiteral("invalid target or lock path"));
    if(!QFileInfo(path).absoluteDir().exists())return fail(error,QStringLiteral("target directory does not exist"));
    const auto target=key(path),lock=key(lockPath),explicitFile=key(state.explicitPath);
    if(!explicitFile.isEmpty()&&(target==explicitFile||lock==explicitFile))return fail(error,QStringLiteral("target or lock aliases explicit project"));
    for(const auto &source:state.order){const auto sourceKey=key(source);
        if(target==sourceKey||lock==sourceKey)return fail(error,QStringLiteral("target or lock aliases source"));}
    return true;
}
bool readBounded(const QString &path,QByteArray *bytes,QString &hash,QString &error,CancellationToken cancel){
    if(canceled(error,cancel)||!pathShape(path,error))return false;
    const QFileInfo info(path);
    if(!info.exists()||info.size()<=0||info.size()>maximumProjectBytes)return fail(error,QStringLiteral("missing, empty or oversized recovery file"));
    QFile file(path);if(!file.open(QIODevice::ReadOnly))return fail(error,file.errorString());
    const auto initialSize=file.size();
    if(initialSize<=0||initialSize>maximumProjectBytes)return fail(error,QStringLiteral("invalid file size"));
    if(bytes){bytes->clear();bytes->reserve(int(initialSize));}
    QCryptographicHash digest(QCryptographicHash::Sha256);qint64 total=0;
    while(!file.atEnd()){
        if(canceled(error,cancel))return false;
        const auto chunk=file.read(1024*1024);
        if(chunk.isEmpty()||file.error()!=QFile::NoError)return fail(error,QStringLiteral("file read failed"));
        total+=chunk.size();if(total>maximumProjectBytes)return fail(error,QStringLiteral("file grew beyond limit"));
        digest.addData(chunk);if(bytes)bytes->append(chunk);
    }
    if(canceled(error,cancel))return false;
    if(total!=initialSize||file.size()!=initialSize)return fail(error,QStringLiteral("file changed during read"));
    hash=QString::fromLatin1(digest.result().toHex());return true;
}
}
bool loadRecovery(const QString &path,RecoveryState &state,QString &fileHash,QString &error,CancellationToken cancel){
    error.clear();QByteArray bytes;QString hash;
    if(!readBounded(path,&bytes,hash,error,cancel))return false;
    QJsonParseError parse;const auto json=QJsonDocument::fromJson(bytes,&parse);
    if(parse.error!=QJsonParseError::NoError||!json.isObject())return fail(error,QStringLiteral("invalid recovery JSON: ")+parse.errorString());
    if(canceled(error,cancel))return false;
    RecoveryState candidate;if(!recoveryFromJson(json.object(),candidate,error)||canceled(error,cancel))return false;
    QString afterHash;
    if(!readBounded(path,nullptr,afterHash,error,cancel))return false;
    if(afterHash!=hash)return fail(error,QStringLiteral("recovery changed during parsing"));
    state=std::move(candidate);fileHash=hash;return true;
}
bool saveRecovery(const QString &path,RecoverySnapshot state,QString &fileHash,QString &error,const QString &expectedFileHash,CancellationToken cancel){
    error.clear();if(canceled(error,cancel))return false;
    if(!state)return fail(error,QStringLiteral("null recovery snapshot"));
    Writer writer;if(!writer.owned)return fail(error,QStringLiteral("another recovery writer is active"));
    const auto frozen=freezeRecovery(*state,error);if(!frozen||canceled(error,cancel))return false;
    if(!protectedPaths(path,*frozen,error))return false;
    QLockFile lock(path+QStringLiteral(".luminas.lock"));lock.setStaleLockTime(0);
    if(!lock.tryLock(0))return fail(error,QStringLiteral("recovery target is locked"));
    const bool existed=QFileInfo::exists(path);QString oldHash;RecoveryState old;
    if(existed){
        if(expectedFileHash.isEmpty())return fail(error,QStringLiteral("existing target requires exact expected hash"));
        if(!loadRecovery(path,old,oldHash,error,cancel))return false;
        if(oldHash!=expectedFileHash)return fail(error,QStringLiteral("existing recovery hash changed"));
        if(!protectedPaths(path,old,error))return false;
        if(old.project->id!=frozen->project->id)return fail(error,QStringLiteral("different project identity"));
        if(old.project->revision>frozen->project->revision)return fail(error,QStringLiteral("stale project revision"));
        if(old.project->revision==frozen->project->revision&&old!=*frozen)return fail(error,QStringLiteral("same revision has different recovery data"));
    }else if(!expectedFileHash.isEmpty())return fail(error,QStringLiteral("expected existing recovery disappeared"));
    const auto json=recoveryJson(*frozen,error);if(!error.isEmpty()||canceled(error,cancel))return false;
    const auto bytes=QJsonDocument(json).toJson(QJsonDocument::Compact);
    if(bytes.size()>maximumProjectBytes)return fail(error,QStringLiteral("recovery exceeds size limit"));
    const auto unchanged=[&](QString &problem){
        if(!protectedPaths(path,*frozen,problem)||canceled(problem,cancel))return false;
        if(!existed)return !QFileInfo::exists(path)||fail(problem,QStringLiteral("new target appeared"));
        QString actual;if(!readBounded(path,nullptr,actual,problem,cancel))return false;
        return actual==oldHash||fail(problem,QStringLiteral("target changed before commit"));
    };
    if(!unchanged(error))return false;
    if(existed&&old==*frozen){fileHash=oldHash;return true;}
    const auto body=[&](QIODevice &device,QString &problem){
        for(qsizetype offset=0;offset<bytes.size();){
            if(canceled(problem,cancel))return false;
            const auto count=std::min<qsizetype>(1024*1024,bytes.size()-offset);
            const auto n=device.write(bytes.constData()+offset,count);
            if(n<=0)return fail(problem,QStringLiteral("short recovery write"));offset+=n;
        }
        return unchanged(problem);
    };
    if(!Persistence::writeAtomicFile(path,body,error,cancel))return false;
    fileHash=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());return true;
}
}
