#include "worker_session.h"
#include <QDateTime>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace RawWorker {
namespace {
constexpr auto markerName="owner.json";
QString seal(QJsonObject &object){const auto digest=QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(object).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());object["sha256"]=digest;return digest;}
bool verifiedObject(const QByteArray &bytes,QJsonObject &object){
    const auto json=QJsonDocument::fromJson(bytes);if(!json.isObject()||json.toJson(QJsonDocument::Compact)!=bytes)return false;object=json.object();
    const auto expected=object.take("sha256").toString();return validHash(expected)&&expected==QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(object).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());
}
bool decimal(const QJsonValue &value,quint64 &out){
    if(!value.isString())return false;const auto text=value.toString();bool ok=false;out=text.toULongLong(&ok);
    return ok&&!text.isEmpty()&&QString::number(out)==text;
}
bool sessionName(const QString &name){static const QRegularExpression pattern(QStringLiteral("^session-[a-zA-Z0-9]{6}$"));return pattern.match(name).hasMatch();}
bool stageName(const QString &name){static const QRegularExpression pattern(QStringLiteral("^\\.raw-stage-[a-zA-Z0-9]{6}$"));return pattern.match(name).hasMatch();}
#ifdef Q_OS_WIN
class DirectoryLock {
    HANDLE handle=INVALID_HANDLE_VALUE;
public:
    explicit DirectoryLock(const QString &path){
        handle=CreateFileW(reinterpret_cast<const wchar_t *>(path.utf16()),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(handle!=INVALID_HANDLE_VALUE){BY_HANDLE_FILE_INFORMATION info{};if(!GetFileInformationByHandle(handle,&info)||!(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)||(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))close();}
    }
    ~DirectoryLock(){close();}
    void close(){if(handle!=INVALID_HANDLE_VALUE){CloseHandle(handle);handle=INVALID_HANDLE_VALUE;}}
    bool valid()const{return handle!=INVALID_HANDLE_VALUE;}
};
quint64 creation(HANDLE process){FILETIME created{},exited{},kernel{},user{};if(!GetProcessTimes(process,&created,&exited,&kernel,&user))return 0;return (quint64(created.dwHighDateTime)<<32)|created.dwLowDateTime;}
bool provedExited(DWORD pid,quint64 expected){
    const auto process=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
    if(!process)return GetLastError()==ERROR_INVALID_PARAMETER;
    const auto actual=creation(process);const auto status=WaitForSingleObject(process,0);CloseHandle(process);
    // A mismatched creation time proves that the original owner ended, even
    // when this numeric PID has since been reused. Query/access failures keep it.
    return actual&&(actual!=expected||status==WAIT_OBJECT_0);
}
bool regularFile(const QFileInfo &file){
    const auto attributes=GetFileAttributesW(reinterpret_cast<const wchar_t *>(file.absoluteFilePath().utf16()));
    return attributes!=INVALID_FILE_ATTRIBUTES&&!(attributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))&&file.isFile();
}
#endif
}
bool recordSessionOwner(const QString &root,const QString &directory,const Request &request,QString &error){
    error.clear();
#ifndef Q_OS_WIN
    Q_UNUSED(root);Q_UNUSED(directory);Q_UNUSED(request);error=QStringLiteral("当前平台没有RAW会话身份实现");return false;
#else
    const QFileInfo rootInfo(root),info(directory);DirectoryLock rootLock(root),directoryLock(directory);
    const auto canonicalRoot=rootInfo.canonicalFilePath();
    if(!rootLock.valid()||!directoryLock.valid()||canonicalRoot.isEmpty()||info.canonicalPath()!=canonicalRoot||!sessionName(info.fileName())||!validUuid(request.id)||!validUuid(request.token)){
        error=QStringLiteral("RAW会话归属路径或身份无效");return false;
    }
    const auto born=creation(GetCurrentProcess());if(!born){error=QStringLiteral("无法读取RAW会话所有者身份");return false;}
    QJsonObject owner{{"schema","LuminaS-raw-session-1"},{"root",canonicalRoot},{"directory",info.fileName()},
        {"pid",QString::number(GetCurrentProcessId())},{"processCreated",QString::number(born)},
        {"bornUtcMs",QString::number(QDateTime::currentMSecsSinceEpoch())},{"id",request.id},{"token",request.token}};
    seal(owner);
    QFile file(QDir(directory).filePath(markerName));if(!file.open(QIODevice::WriteOnly|QIODevice::NewOnly)){error=file.errorString();return false;}
    const auto bytes=QJsonDocument(owner).toJson(QJsonDocument::Compact);
    if(file.write(bytes)!=bytes.size()||!file.flush()){error=file.errorString();return false;}return true;
#endif
}
bool recordSessionWorker(const QString &root,const QString &directory,qint64 pid,QString &error){
    error.clear();
#ifndef Q_OS_WIN
    Q_UNUSED(root);Q_UNUSED(directory);Q_UNUSED(pid);error=QStringLiteral("当前平台没有RAW进程身份实现");return false;
#else
    DirectoryLock rootLock(root),directoryLock(directory);const QFileInfo info(directory);const auto canonicalRoot=QFileInfo(root).canonicalFilePath();
    if(!rootLock.valid()||!directoryLock.valid()||info.canonicalPath()!=canonicalRoot||!sessionName(info.fileName())||pid<1||pid>MAXDWORD){error=QStringLiteral("RAW进程归属路径或身份无效");return false;}
    QFile ownerFile(QDir(directory).filePath(markerName));if(!ownerFile.open(QIODevice::ReadOnly)){error=ownerFile.errorString();return false;}
    const auto bytes=ownerFile.read(4097);QJsonObject owner;
    if(bytes.size()>4096||!verifiedObject(bytes,owner)||owner.size()!=8||owner["root"]!=QJsonValue(canonicalRoot)||owner["directory"]!=QJsonValue(info.fileName())||owner["pid"]!=QJsonValue(QString::number(GetCurrentProcessId()))||owner["processCreated"]!=QJsonValue(QString::number(creation(GetCurrentProcess())))){
        error=QStringLiteral("RAW进程所有者记录不匹配");return false;
    }
    const auto process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,DWORD(pid));if(!process){error=QStringLiteral("无法读取RAW子进程身份");return false;}
    const auto created=creation(process);CloseHandle(process);if(!created){error=QStringLiteral("RAW子进程创建时间不可读取");return false;}
    QJsonObject worker{{"schema","LuminaS-raw-process-1"},{"ownerSha",QJsonDocument::fromJson(bytes).object()["sha256"]},{"pid",QString::number(pid)},{"processCreated",QString::number(created)}};seal(worker);
    QFile file(QDir(directory).filePath("worker.json"));if(!file.open(QIODevice::WriteOnly|QIODevice::NewOnly)){error=file.errorString();return false;}
    const auto payload=QJsonDocument(worker).toJson(QJsonDocument::Compact);if(file.write(payload)!=payload.size()||!file.flush()){error=file.errorString();return false;}return true;
#endif
}
SessionCleanup cleanAbandonedSessions(const QString &root,qint64 minimumAgeMs){
    SessionCleanup result;
#ifndef Q_OS_WIN
    Q_UNUSED(root);Q_UNUSED(minimumAgeMs);result.errors<<QStringLiteral("当前平台没有RAW会话清理实现");return result;
#else
    if(minimumAgeMs<0){result.errors<<QStringLiteral("RAW会话清理期限无效");return result;}
    DirectoryLock rootLock(root);if(!rootLock.valid()){result.errors<<QStringLiteral("RAW会话根目录不可安全打开");return result;}
    const auto canonicalRoot=QFileInfo(root).canonicalFilePath();if(canonicalRoot.isEmpty())return result;
    const auto entries=QDir(root).entryInfoList(QDir::Dirs|QDir::NoDotAndDotDot|QDir::Hidden,QDir::Name);
    int scanned=0;
    for(const auto &entry:entries){
        if(!sessionName(entry.fileName()))continue;if(++scanned>1024){result.errors<<QStringLiteral("RAW会话清理数量超限");break;}
        ++result.kept;DirectoryLock directory(entry.absoluteFilePath());if(!directory.valid()||entry.canonicalPath()!=canonicalRoot)continue;
        const QFileInfo marker(QDir(entry.absoluteFilePath()).filePath(markerName));if(!regularFile(marker)||marker.size()<1||marker.size()>4096)continue;
        QFile file(marker.absoluteFilePath());if(!file.open(QIODevice::ReadOnly))continue;const auto bytes=file.read(4097);file.close();
        const auto json=QJsonDocument::fromJson(bytes);if(!json.isObject()||json.toJson(QJsonDocument::Compact)!=bytes)continue;auto owner=json.object();
        if(owner.size()!=9||owner["schema"]!=QJsonValue("LuminaS-raw-session-1")||owner["root"]!=QJsonValue(canonicalRoot)||owner["directory"]!=QJsonValue(entry.fileName())||!validUuid(owner["id"].toString())||!validUuid(owner["token"].toString()))continue;
        const auto expected=owner.take("sha256").toString();if(!validHash(expected)||expected!=QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(owner).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex()))continue;
        quint64 pid=0,created=0,born=0;if(!decimal(owner["pid"],pid)||pid<1||pid>MAXDWORD||!decimal(owner["processCreated"],created)||!created||!decimal(owner["bornUtcMs"],born))continue;
        const auto now=QDateTime::currentMSecsSinceEpoch();if(born>quint64(now)||quint64(now)-born<quint64(minimumAgeMs)||!provedExited(DWORD(pid),created))continue;
        const auto files=QDir(entry.absoluteFilePath()).entryInfoList(QDir::AllEntries|QDir::NoDotAndDotDot|QDir::Hidden|QDir::System,QDir::Name);
        const auto workerPath=QDir(entry.absoluteFilePath()).filePath("worker.json");
        if(QFileInfo::exists(workerPath)){
            if(!regularFile(QFileInfo(workerPath))||QFileInfo(workerPath).size()>4096)continue;
            QFile workerFile(workerPath);if(!workerFile.open(QIODevice::ReadOnly))continue;QJsonObject worker;const auto workerBytes=workerFile.read(4097);workerFile.close();
            quint64 workerPid=0,workerCreated=0;
            if(workerBytes.size()>4096||!verifiedObject(workerBytes,worker)||worker.size()!=4||worker["schema"]!=QJsonValue("LuminaS-raw-process-1")||worker["ownerSha"]!=QJsonValue(expected)||!decimal(worker["pid"],workerPid)||workerPid<1||workerPid>MAXDWORD||!decimal(worker["processCreated"],workerCreated)||!workerCreated||!provedExited(DWORD(workerPid),workerCreated))continue;
        }else if(files.size()!=1||files.front().fileName()!=markerName)continue;
        bool safe=true;
        for(const auto &item:files)if(!regularFile(item)||(item.fileName()!=markerName&&item.fileName()!="worker.json"&&item.fileName()!=owner["token"].toString()+".frame"&&!stageName(item.fileName()))){safe=false;break;}
        if(!safe||!provedExited(DWORD(pid),created))continue;
        for(const auto &item:files)if(item.fileName()!=markerName&&item.fileName()!="worker.json"&&!QFile::remove(item.absoluteFilePath())){safe=false;break;}
        if(!safe){result.errors<<entry.fileName()+QStringLiteral(": 无法移除已确认归属的缓存");continue;}
        if(QFileInfo::exists(workerPath)&&!QFile::remove(workerPath)){result.errors<<entry.fileName()+QStringLiteral(": 无法移除子进程记录");continue;}
        if(!QFile::remove(marker.absoluteFilePath())){result.errors<<entry.fileName()+QStringLiteral(": 无法移除所有者记录");continue;}
        directory.close();
        if(!QDir().rmdir(entry.absoluteFilePath())){result.errors<<entry.fileName()+QStringLiteral(": 会话目录未清空");continue;}
        ++result.removed;--result.kept;
    }
    return result;
#endif
}
}
