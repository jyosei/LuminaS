// S3 A3 project storage: strict read-only SQLite load, single-writer atomic save,
// group validation/freezing, source hash inspection and hash-identical relink.
#include "core/project_store.h"
#include "io/atomic_file.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLockFile>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QTemporaryFile>
#include <QUuid>
#include <QVariant>
#include <atomic>
#include <limits>
#include <memory>
#include <utility>

namespace LocalEdit {
namespace {

constexpr qint64 copyChunkBytes=1024*1024;
constexpr int maximumProjectIdLength=128;
constexpr int maximumProjectNameLength=4096;

const QLatin1String formatValue("LuminaS.project");
const QLatin1String metadataTable("metadata");
const QLatin1String photosTable("photos");
const QLatin1String keyFormat("format");
const QLatin1String keyFormatVersion("formatVersion");
const QLatin1String keyAlgorithmVersion("algorithmVersion");
const QLatin1String keyProjectId("projectId");
const QLatin1String keyName("name");
const QLatin1String keyRevision("revision");
const QLatin1String keyActiveAssetId("activeAssetId");

QString context(const char *stage,const QString &detail,const QString &path=QString()){
    QString text=QStringLiteral("project store: ")+QString::fromUtf8(stage);
    if(!detail.isEmpty())text+=QStringLiteral(": ")+detail;
    if(!path.isEmpty())text+=QStringLiteral(": ")+path;
    return text;
}

bool fail(QString &error,const char *stage,const QString &detail,const QString &path=QString()){
    error=context(stage,detail,path);
    return false;
}

bool canceledError(QString &error,const char *stage,const QString &path){
    return fail(error,stage,QStringLiteral("canceled"),path);
}

// Windows path comparison is case-insensitive; separators are normalized so one
// file cannot be registered twice under two different spellings.
QString pathKey(const QString &path){
    const QFileInfo info(path);
    QString key=info.canonicalFilePath();
    if(key.isEmpty()){
        const QString parent=info.absoluteDir().canonicalPath();
        key=parent.isEmpty()?info.absoluteFilePath():QDir(parent).filePath(info.fileName());
    }
    key=QDir::cleanPath(key);
    key.replace(QChar('\\'),QChar('/'));
#if defined(Q_OS_WIN)
    key=key.toCaseFolded();
#endif
    return key;
}

bool validIdentifier(const QString &id){
    return !id.isEmpty()&&id.size()<=maximumProjectIdLength&&!id.contains(QChar(0));
}

bool parseDecimalUint64(const QString &text,quint64 minimum,quint64 &out){
    if(text.isEmpty()||text.size()>20)return false;
    for(const QChar c:text)if(c<QChar('0')||c>QChar('9'))return false;
    if(text.size()>1&&text.at(0)==QChar('0'))return false;
    if(text.size()==20&&text>QStringLiteral("18446744073709551615"))return false;
    bool parsedOk=false;
    const quint64 parsed=text.toULongLong(&parsedOk);
    if(!parsedOk||parsed<minimum)return false;
    out=parsed;
    return true;
}

bool absoluteSourcePath(const QString &path){
    if(path.isEmpty()||path.contains(QChar(0)))return false;
    return QFileInfo(path).isAbsolute();
}
// ---------------------------------------------------------------------------
// Group validation and freezing
// ---------------------------------------------------------------------------

bool validateState(const ProjectState &project,QString &error){
    error.clear();
    if(!validIdentifier(project.id))
        return fail(error,"validation",QStringLiteral("projectId must be 1..128 characters"));
    if(project.name.size()>maximumProjectNameLength)
        return fail(error,"validation",QStringLiteral("project name exceeds 4096 characters"));
    if(project.name.contains(QChar(0)))
        return fail(error,"validation",QStringLiteral("project name contains a null character"));
    if(project.revision<1)
        return fail(error,"validation",QStringLiteral("project revision must be at least 1"));
    if(project.photos.size()>maximumProjectPhotos)
        return fail(error,"validation",QStringLiteral("project exceeds the 2000 photo limit"));
    QSet<QString> assetIds;
    QSet<QString> sourcePaths;
    qint64 totalDocumentBytes=0;
    for(int index=0;index<project.photos.size();++index){
        const ProjectPhoto &photo=project.photos.at(index);
        const QString where=QStringLiteral("photo %1: ").arg(index);
        if(!photo.document)
            return fail(error,"validation",where+QStringLiteral("document snapshot is null"));
        QString reason;
        // Persistence mode: source hash and normalized size must be complete.
        if(!validDocument(*photo.document,reason,true))
            return fail(error,"validation",where+reason);
        if(!absoluteSourcePath(photo.document->asset.sourcePath))
            return fail(error,"validation",where+QStringLiteral("asset sourcePath must be absolute"));
        if(assetIds.contains(photo.document->asset.id))
            return fail(error,"validation",where+QStringLiteral("duplicate asset id ")+photo.document->asset.id);
        assetIds.insert(photo.document->asset.id);
        const QString key=pathKey(photo.document->asset.sourcePath);
        if(sourcePaths.contains(key))
            return fail(error,"validation",where+QStringLiteral("duplicate source path ")+photo.document->asset.sourcePath);
        sourcePaths.insert(key);
        if(photo.mark!=PhotoMark::Unmarked&&photo.mark!=PhotoMark::Keep&&photo.mark!=PhotoMark::Reject)
            return fail(error,"validation",where+QStringLiteral("mark is outside the three known values"));
        const QJsonObject encoded=documentJson(*photo.document,reason);
        if(!reason.isEmpty())
            return fail(error,"validation",where+reason);
        const QByteArray bytes=QJsonDocument(encoded).toJson(QJsonDocument::Compact);
        if(bytes.isEmpty())
            return fail(error,"validation",where+QStringLiteral("document JSON could not be encoded"));
        if(bytes.size()>maximumDocumentBytes)
            return fail(error,"validation",where+QStringLiteral("document JSON exceeds 64MiB"));
        totalDocumentBytes+=bytes.size();
        if(totalDocumentBytes>maximumProjectBytes)
            return fail(error,"validation",where+QStringLiteral("project document bytes exceed 256MiB"));
    }
    if(!project.activeAssetId.isEmpty()&&!assetIds.contains(project.activeAssetId))
        return fail(error,"validation",QStringLiteral("activeAssetId is not part of the project"));
    return true;
}

bool freezeInto(const ProjectState &project,ProjectState &frozen,QString &error){
    if(project.photos.size()>maximumProjectPhotos)
        return fail(error,"freeze",QStringLiteral("project exceeds the photo limit"));
    ProjectState copy;
    copy.id=project.id;
    copy.name=project.name;
    copy.revision=project.revision;
    copy.activeAssetId=project.activeAssetId;
    copy.photos.reserve(project.photos.size());
    for(const ProjectPhoto &photo:project.photos){
        if(!photo.document)return fail(error,"freeze",QStringLiteral("null document snapshot"));
        ProjectPhoto entry;
        entry.mark=photo.mark;
        // Deep copy the top-level document value so a mutable alias held by the
        // caller can no longer change what was frozen.
        entry.document=std::make_shared<const PhotoDocument>(*photo.document);
        copy.photos.append(entry);
    }
    if(!validateState(copy,error))return false;
    frozen=std::move(copy);
    return true;
}
// ---------------------------------------------------------------------------
// Hashing, writer exclusion and connection lifetime
// ---------------------------------------------------------------------------

bool hashFile(const QString &path,QString &hash,QString &error,CancellationToken cancel,
    qint64 maximumBytes=0){
    hash.clear();
    const QFileInfo info(path);
    if(!info.exists())return fail(error,"hash",QStringLiteral("file does not exist"),path);
    if(info.isDir())return fail(error,"hash",QStringLiteral("path is a directory"),path);
    if(info.isSymLink())return fail(error,"hash",QStringLiteral("path is a symbolic link"),path);
    if(!info.isFile())return fail(error,"hash",QStringLiteral("path is not a regular file"),path);
    if(maximumBytes>0&&info.size()>maximumBytes)
        return fail(error,"hash",QStringLiteral("file exceeds the size limit"),path);
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly))
        return fail(error,"hash",QStringLiteral("cannot open: ")+file.errorString(),path);
    QCryptographicHash digest(QCryptographicHash::Sha256);
    QByteArray buffer;
    buffer.resize(int(copyChunkBytes));
    qint64 total=0;
    for(;;){
        if(isCanceled(cancel))return canceledError(error,"hash",path);
        const qint64 read=file.read(buffer.data(),buffer.size());
        if(read<0)return fail(error,"hash",QStringLiteral("read failed: ")+file.errorString(),path);
        if(read==0)break;
        total+=read;
        if(maximumBytes>0&&total>maximumBytes)
            return fail(error,"hash",QStringLiteral("file grew beyond the size limit"),path);
        digest.addData(QByteArrayView(buffer.constData(),read));
    }
    hash=QString::fromLatin1(digest.result().toHex());
    return true;
}

// The writer is process wide: concurrent saves must fail fast instead of both
// building SQLite databases for one target. QLockFile only covers other processes.
class WriterGuard {
public:
    WriterGuard(){owned=!flag.test_and_set(std::memory_order_acquire);}
    ~WriterGuard(){if(owned)flag.clear(std::memory_order_release);}
    bool isOwned() const{return owned;}
    WriterGuard(const WriterGuard &)=delete;
    WriterGuard &operator=(const WriterGuard &)=delete;
private:
    bool owned=false;
    static std::atomic_flag flag;
};
std::atomic_flag WriterGuard::flag=ATOMIC_FLAG_INIT;

// QLockFile is unlocked on scope exit, including every early-return failure path.
class ScopedLockRelease {
public:
    explicit ScopedLockRelease(QLockFile &target):lock(target){}
    ~ScopedLockRelease(){if(lock.isLocked())lock.unlock();}
    ScopedLockRelease(const ScopedLockRelease &)=delete;
    ScopedLockRelease &operator=(const ScopedLockRelease &)=delete;
private:
    QLockFile &lock;
};
QString uniqueConnectionName(){
    return QStringLiteral("luminas-project-")+QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString sqlErrorText(const QSqlError &error){
    if(!error.isValid())return QStringLiteral("no driver error reported");
    QString text=error.text();
    const QString native=error.nativeErrorCode();
    if(!native.isEmpty())text+=QStringLiteral(" (native ")+native+QStringLiteral(")");
    return text;
}

// RAII: the connection is closed and its registration removed before the caller
// can commit anything, so a stale handle never crosses a replacement.
class ScopedConnection {
public:
    ScopedConnection(const QString &name):connectionName(name){
        database=QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),connectionName);
    }
    ~ScopedConnection(){
        if(database.isValid()){
            if(database.isOpen())database.close();
            database=QSqlDatabase();
        }
        // Every query must be destroyed first, otherwise Qt warns that the
        // connection is still in use and the registration survives.
        QSqlDatabase::removeDatabase(connectionName);
    }
    ScopedConnection(const ScopedConnection &)=delete;
    ScopedConnection &operator=(const ScopedConnection &)=delete;
    QSqlDatabase &get(){return database;}
    bool openReadOnly(QString &error,const QString &path){
        database.setDatabaseName(path);
        database.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=0"));
        if(!database.open())
            return fail(error,"connect",sqlErrorText(database.lastError()),path);
        return true;
    }
    bool openReadWrite(QString &error,const QString &path){
        if(!database.open())
            return fail(error,"connect",sqlErrorText(database.lastError()),path);
        return true;
    }
private:
    QString connectionName;
    QSqlDatabase database;
};

// Unknown tables, views or triggers are rejected rather than ignored, because a
// rewrite of a known-format project would silently drop them.
bool schemaIsExact(QSqlDatabase &database,QString &error,const QString &path){
    QSqlQuery query(database);
    if(!query.exec(QStringLiteral(
            "SELECT type,name FROM sqlite_master WHERE name NOT LIKE 'sqlite_%' ORDER BY type,name"))){
        return fail(error,"schema",sqlErrorText(query.lastError()),path);
    }
    QSet<QString> expected;
    expected.insert(QStringLiteral("table/")+metadataTable);
    expected.insert(QStringLiteral("table/")+photosTable);
    QSet<QString> actual;
    while(query.next())actual.insert(query.value(0).toString()+QStringLiteral("/")+query.value(1).toString());
    if(query.lastError().isValid())return fail(error,"schema",sqlErrorText(query.lastError()),path);
    if(actual!=expected){
        QStringList unexpected;
        for(const QString &entry:actual)if(!expected.contains(entry))unexpected.append(entry);
        unexpected.sort();
        if(!unexpected.isEmpty())
            return fail(error,"schema",QStringLiteral("unknown schema objects: ")+unexpected.join(QStringLiteral(", ")),path);
        return fail(error,"schema",QStringLiteral("required tables are missing"),path);
    }
    for(const auto &setting:QVector<QPair<QString,int>>{{"application_id",projectApplicationId},{"user_version",projectFormatVersion}}){
        QSqlQuery pragma(database);
        if(!pragma.exec("PRAGMA "+setting.first)||!pragma.next()||pragma.value(0).toInt()!=setting.second)
            return fail(error,"schema",QStringLiteral("unsupported SQLite ")+setting.first,path);
    }
    struct Column {const char *name,*type;int required,primary;};
    const auto columnsMatch=[&](const QString &table,const QVector<Column> &columns){
        QSqlQuery fields(database);
        if(!fields.exec("PRAGMA table_xinfo("+table+")"))return false;
        int index=0;
        while(fields.next()){
            if(index>=columns.size())return false;
            const auto &column=columns[index];
            if(fields.value(0).toInt()!=index||fields.value(1).toString()!=QLatin1String(column.name)||
               fields.value(2).toString().toUpper()!=QLatin1String(column.type)||
               fields.value(3).toInt()!=column.required||!fields.value(4).isNull()||
               fields.value(5).toInt()!=column.primary||fields.value(6).toInt()!=0)return false;
            ++index;
        }
        return !fields.lastError().isValid()&&index==columns.size();
    };
    if(!columnsMatch("metadata",{{"key","TEXT",0,1},{"value","TEXT",1,0}})||
       !columnsMatch("photos",{{"ordinal","INTEGER",0,1},{"asset_id","TEXT",1,0},
                                 {"mark","INTEGER",1,0},{"document","BLOB",1,0}}))
        return fail(error,"schema",QStringLiteral("unsupported table columns"),path);
    return true;
}

bool checkIntegrity(QSqlDatabase &database,QString &error,const QString &path){
    QSqlQuery query(database);
    if(!query.exec(QStringLiteral("PRAGMA quick_check(1)")))
        return fail(error,"integrity",sqlErrorText(query.lastError()),path);
    if(!query.next())
        return fail(error,"integrity",QStringLiteral("integrity check returned no row"),path);
    const QString result=query.value(0).toString();
    if(result.compare(QStringLiteral("ok"),Qt::CaseInsensitive)!=0)
        return fail(error,"integrity",QStringLiteral("integrity check failed: ")+result,path);
    return true;
}

bool readMetadata(QSqlDatabase &database,QHash<QString,QString> &values,QString &error,const QString &path){
    QSqlQuery query(database);
    if(!query.exec(QStringLiteral("SELECT key,value,typeof(key),typeof(value) FROM metadata")))
        return fail(error,"metadata",sqlErrorText(query.lastError()),path);
    while(query.next()){
        if(query.value(2).toString()!="text"||query.value(3).toString()!="text")
            return fail(error,"metadata",QStringLiteral("metadata keys and values must be text"),path);
        const QString key=query.value(0).toString();
        if(key.isEmpty())
            return fail(error,"metadata",QStringLiteral("metadata key must not be empty"),path);
        if(values.contains(key))
            return fail(error,"metadata",QStringLiteral("duplicate metadata key ")+key,path);
        if(query.value(1).isNull())
            return fail(error,"metadata",QStringLiteral("metadata value is NULL for key ")+key,path);
        values.insert(key,query.value(1).toString());
    }
    if(query.lastError().isValid())return fail(error,"metadata",sqlErrorText(query.lastError()),path);
    const QVector<QString> requiredKeys{QString(keyFormat),QString(keyFormatVersion),
        QString(keyAlgorithmVersion),QString(keyProjectId),QString(keyName),QString(keyRevision),
        QString(keyActiveAssetId)};
    QSet<QString> allowed;
    for(const QString &key:requiredKeys)allowed.insert(key);
    for(auto it=values.constBegin();it!=values.constEnd();++it){
        if(!allowed.contains(it.key()))
            return fail(error,"metadata",QStringLiteral("unknown metadata key ")+it.key(),path);
    }
    for(const QString &required:requiredKeys){
        if(!values.contains(required))
            return fail(error,"metadata",QStringLiteral("missing metadata key ")+required,path);
    }
    if(values.value(keyFormat)!=formatValue)
        return fail(error,"metadata",QStringLiteral("format is not LuminaS.project"),path);
    if(values.value(keyFormatVersion)!=QStringLiteral("1"))
        return fail(error,"metadata",QStringLiteral("formatVersion is not 1"),path);
    if(values.value(keyAlgorithmVersion)!=QString::number(localAlgorithmVersion))
        return fail(error,"metadata",QStringLiteral("algorithmVersion is not the local algorithm version"),path);
    return true;
}

struct PhotoRow {
    int ordinal=0;
    QString assetId;
    int mark=0;
    qint64 length=0;
};

bool readPhotoRoster(QSqlDatabase &database,QVector<PhotoRow> &rows,QString &error,
    const QString &path,CancellationToken cancel){
    QSqlQuery count(database);
    if(!count.exec(QStringLiteral("SELECT COUNT(*),MIN(ordinal),MAX(ordinal) FROM photos")))
        return fail(error,"photos",sqlErrorText(count.lastError()),path);
    if(!count.next())
        return fail(error,"photos",QStringLiteral("roster query returned no row"),path);
    const qint64 countRows=count.value(0).toLongLong();
    if(countRows==0){rows.clear();return true;}
    if(countRows>maximumProjectPhotos)
        return fail(error,"photos",QStringLiteral("project exceeds the 2000 photo limit"),path);
    // Contiguous ordinals from 0 mean min is 0 and max is rows-1, so the range is
    // pinned before any BLOB is touched.
    if(count.value(1).toLongLong()!=0||count.value(2).toLongLong()!=countRows-1)
        return fail(error,"photos",QStringLiteral("ordinals are not contiguous from 0"),path);
    QSqlQuery query(database);
    query.setForwardOnly(true);
    if(!query.exec(QStringLiteral("SELECT ordinal,asset_id,mark,length(document),typeof(ordinal),typeof(asset_id),typeof(mark),typeof(document) FROM photos ORDER BY ordinal")))
        return fail(error,"photos",sqlErrorText(query.lastError()),path);
    QSet<QString> assetIds;
    qint64 totalDocumentBytes=0;
    qint64 expectedOrdinal=0;
    rows.clear();
    rows.reserve(int(countRows));
    while(query.next()){
        if(isCanceled(cancel))return canceledError(error,"photos",path);
        if(query.value(4).toString()!="integer"||query.value(5).toString()!="text"||
           query.value(6).toString()!="integer"||query.value(7).toString()!="blob")
            return fail(error,"photos",QStringLiteral("invalid SQLite storage type"),path);
        const qint64 ordinal=query.value(0).toLongLong();
        if(ordinal!=expectedOrdinal)
            return fail(error,"photos",QStringLiteral("ordinal sequence breaks at %1").arg(ordinal),path);
        ++expectedOrdinal;
        const QString assetId=query.value(1).toString();
        if(assetId.isEmpty()||assetId.size()>maximumProjectIdLength)
            return fail(error,"photos",QStringLiteral("asset_id must be 1..128 characters"),path);
        if(assetIds.contains(assetId))
            return fail(error,"photos",QStringLiteral("duplicate asset_id ")+assetId,path);
        assetIds.insert(assetId);
        if(query.value(2).isNull())
            return fail(error,"photos",QStringLiteral("mark is NULL"),path);
        const qint64 mark=query.value(2).toLongLong();
        if(mark<0||mark>2)
            return fail(error,"photos",QStringLiteral("mark is outside the three known values"),path);
        // The declared length is bounded before the BLOB is ever materialized.
        if(query.value(3).isNull())
            return fail(error,"photos",QStringLiteral("document BLOB is NULL"),path);
        const qint64 documentBytes=query.value(3).toLongLong();
        if(documentBytes<=0)
            return fail(error,"photos",QStringLiteral("document BLOB is empty"),path);
        if(documentBytes>maximumDocumentBytes)
            return fail(error,"photos",QStringLiteral("document BLOB exceeds 64MiB"),path);
        totalDocumentBytes+=documentBytes;
        if(totalDocumentBytes>maximumProjectBytes)
            return fail(error,"photos",QStringLiteral("document bytes exceed 256MiB"),path);
        PhotoRow row;
        row.ordinal=int(ordinal);
        row.assetId=assetId;
        row.mark=int(mark);
        row.length=documentBytes;
        rows.append(row);
    }
    if(query.lastError().isValid())return fail(error,"photos",sqlErrorText(query.lastError()),path);
    if(rows.size()!=int(countRows))
        return fail(error,"photos",QStringLiteral("row count changed while reading"),path);
    return true;
}

bool readPhotoDocuments(QSqlDatabase &database,const QVector<PhotoRow> &rows,
    QVector<ProjectPhoto> &photos,QString &error,const QString &path,CancellationToken cancel){
    photos.clear();
    photos.reserve(rows.size());
    for(const PhotoRow &row:rows){
        if(isCanceled(cancel))return canceledError(error,"photos",path);
        QSqlQuery query(database);
        query.prepare(QStringLiteral("SELECT document FROM photos WHERE ordinal=?"));
        query.addBindValue(row.ordinal);
        if(!query.exec())
            return fail(error,"photos",sqlErrorText(query.lastError()),path);
        if(!query.next())
            return fail(error,"photos",QStringLiteral("missing document BLOB for ordinal %1").arg(row.ordinal),path);
        const QByteArray raw=query.value(0).toByteArray();
        // Re-check against the declared size: a value that shrank or grew between
        // the length query and this read is not a stable database.
        if(raw.size()!=row.length)
            return fail(error,"photos",QStringLiteral("document BLOB length changed while reading"),path);
        QJsonParseError parseError{};
        const QJsonDocument parsed=QJsonDocument::fromJson(raw,&parseError);
        if(parseError.error!=QJsonParseError::NoError)
            return fail(error,"photos",QStringLiteral("document JSON is invalid: ")+parseError.errorString(),path);
        if(!parsed.isObject())
            return fail(error,"photos",QStringLiteral("document JSON is not an object"),path);
        PhotoDocument document;
        QString reason;
        // Persistence mode: incomplete source identity is refused, not defaulted.
        if(!documentFromJson(parsed.object(),document,reason,true))
            return fail(error,"photos",QStringLiteral("document JSON is invalid: ")+reason,path);
        if(document.asset.id!=row.assetId)
            return fail(error,"photos",QStringLiteral("asset_id column disagrees with the document"),path);
        ProjectPhoto photo;
        photo.mark=row.mark==0?PhotoMark::Unmarked:(row.mark==1?PhotoMark::Keep:PhotoMark::Reject);
        photo.document=std::make_shared<const PhotoDocument>(std::move(document));
        photos.append(photo);
    }
    return true;
}
// Reads a project database into a complete temporary state. The caller's output
// is only replaced after every record has been validated.
bool readProjectFile(const QString &path,ProjectState &project,QString &error,
    CancellationToken cancel){
    const QFileInfo info(path);
    if(!info.exists())return fail(error,"open",QStringLiteral("file does not exist"),path);
    if(info.isDir())return fail(error,"open",QStringLiteral("path is a directory"),path);
    if(info.isSymLink())return fail(error,"open",QStringLiteral("path is a symbolic link"),path);
    if(!info.isFile())return fail(error,"open",QStringLiteral("path is not a regular file"),path);
    if(info.size()>maximumProjectBytes)
        return fail(error,"open",QStringLiteral("project file exceeds 256MiB"),path);

    ScopedConnection scoped(uniqueConnectionName());
    QSqlDatabase &database=scoped.get();
    if(!scoped.openReadOnly(error,path))return false;
    if(!checkIntegrity(database,error,path))return false;
    if(!schemaIsExact(database,error,path))return false;
    QHash<QString,QString> metadata;
    if(!readMetadata(database,metadata,error,path))return false;
    ProjectState candidate;
    candidate.id=metadata.value(keyProjectId);
    candidate.name=metadata.value(keyName);
    if(!parseDecimalUint64(metadata.value(keyRevision),1,candidate.revision))
        return fail(error,"metadata",QStringLiteral("revision is not a canonical uint64 decimal"),path);
    candidate.activeAssetId=metadata.value(keyActiveAssetId);
    QVector<PhotoRow> rows;
    if(!readPhotoRoster(database,rows,error,path,cancel))return false;
    if(!readPhotoDocuments(database,rows,candidate.photos,error,path,cancel))return false;
    if(!validateState(candidate,error))return false;
    project=std::move(candidate);
    return true;
}
// ---------------------------------------------------------------------------
// Write path
// ---------------------------------------------------------------------------

QByteArray encodeDocument(const PhotoDocument &document,QString &error){
    QString reason;
    const QJsonObject object=documentJson(document,reason);
    if(!reason.isEmpty()){
        fail(error,"encode",reason);
        return {};
    }
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

bool createSchema(QSqlDatabase &database,QString &error,const QString &path){
    QSqlQuery query(database);
    if(!query.exec(QStringLiteral("PRAGMA application_id=")+QString::number(projectApplicationId)))
        return fail(error,"pragma",sqlErrorText(query.lastError()),path);
    if(!query.exec(QStringLiteral("PRAGMA user_version=")+QString::number(projectFormatVersion)))
        return fail(error,"pragma",sqlErrorText(query.lastError()),path);
    if(!query.exec(QStringLiteral("PRAGMA journal_mode=DELETE")))
        return fail(error,"pragma",sqlErrorText(query.lastError()),path);
    if(!query.exec(QStringLiteral("PRAGMA synchronous=FULL")))
        return fail(error,"pragma",sqlErrorText(query.lastError()),path);
    const QStringList statements{
        QStringLiteral("CREATE TABLE metadata(key TEXT PRIMARY KEY,value TEXT NOT NULL)"),
        QStringLiteral("CREATE TABLE photos(ordinal INTEGER PRIMARY KEY,asset_id TEXT NOT NULL UNIQUE,"
                       "mark INTEGER NOT NULL,document BLOB NOT NULL)")};
    for(const QString &statement:statements){
        if(!query.exec(statement))
            return fail(error,"schema",sqlErrorText(query.lastError()),path);
    }
    return true;
}

bool writeMetadataRows(QSqlDatabase &database,const ProjectState &project,QString &error,
    const QString &path){
    const QVector<QPair<QString,QString>> entries{
        {QString(keyFormat),QString(formatValue)},
        {QString(keyFormatVersion),QStringLiteral("1")},
        {QString(keyAlgorithmVersion),QString::number(localAlgorithmVersion)},
        {QString(keyProjectId),project.id},
        {QString(keyName),project.name},
        {QString(keyRevision),QString::number(project.revision)},
        {QString(keyActiveAssetId),project.activeAssetId}};
    QSqlQuery query(database);
    query.prepare(QStringLiteral("INSERT INTO metadata(key,value) VALUES(?,?)"));
    for(const auto &entry:entries){
        query.bindValue(0,entry.first);
        query.bindValue(1,entry.second.isNull()?QStringLiteral(""):entry.second);
        if(!query.exec())
            return fail(error,"metadata",sqlErrorText(query.lastError()),path);
    }
    return true;
}

bool writePhotoRows(QSqlDatabase &database,const ProjectState &project,QString &error,
    const QString &path,CancellationToken cancel){
    QSqlQuery query(database);
    query.prepare(QStringLiteral("INSERT INTO photos(ordinal,asset_id,mark,document) VALUES(?,?,?,?)"));
    for(int ordinal=0;ordinal<project.photos.size();++ordinal){
        if(isCanceled(cancel))return canceledError(error,"photos",path);
        const ProjectPhoto &photo=project.photos.at(ordinal);
        QString encodeError;
        const QByteArray bytes=encodeDocument(*photo.document,encodeError);
        if(bytes.isEmpty()){
            error=encodeError.isEmpty()
                ?context("encode",QStringLiteral("document JSON is empty"),path):encodeError;
            return false;
        }
        if(bytes.size()>maximumDocumentBytes)
            return fail(error,"photos",QStringLiteral("document JSON exceeds 64MiB"),path);
        query.bindValue(0,ordinal);
        query.bindValue(1,photo.document->asset.id);
        query.bindValue(2,photo.mark==PhotoMark::Unmarked?0:(photo.mark==PhotoMark::Keep?1:2));
        query.bindValue(3,bytes);
        if(!query.exec())
            return fail(error,"photos",sqlErrorText(query.lastError()),path);
    }
    return true;
}

// Builds the frozen state into a fresh temporary SQLite database next to the
// target. A closed, fully committed database is the only thing this returns.
bool buildTemporaryDatabase(const QString &target,const ProjectState &project,
    QTemporaryFile &temporary,QString &error,CancellationToken cancel){
    const QFileInfo targetInfo(target);
    const QDir directory=targetInfo.absoluteDir();
    temporary.setFileTemplate(directory.absoluteFilePath(QStringLiteral(".luminas-project-XXXXXX.sqlite")));
    if(!temporary.open())
        return fail(error,"temporary",QStringLiteral("cannot create temporary database: ")+temporary.errorString(),target);
    // Created and closed first, so SQLite opens a real path this process owns.
    temporary.close();
    const QString temporaryPath=temporary.fileName();
    {
        ScopedConnection scoped(uniqueConnectionName());
        QSqlDatabase &database=scoped.get();
        database.setDatabaseName(temporaryPath);
        if(!scoped.openReadWrite(error,target))return false;
        if(!createSchema(database,error,target))return false;
        if(!database.transaction())
            return fail(error,"transaction",sqlErrorText(database.lastError()),target);
        if(!writeMetadataRows(database,project,error,target))return false;
        if(!writePhotoRows(database,project,error,target,cancel))return false;
        if(isCanceled(cancel))return canceledError(error,"transaction",target);
        if(!database.commit())
            return fail(error,"transaction",sqlErrorText(database.lastError()),target);
    }
    return true;
}

// The temporary database must read back as exactly the frozen input before it can
// replace the target.
bool verifyTemporaryDatabase(const QString &temporaryPath,const ProjectState &project,
    QString &error){
    ProjectState reread;
    if(!readProjectFile(temporaryPath,reread,error,{}))return false;
    if(reread!=project)
        return fail(error,"verify",QStringLiteral("temporary database does not match the frozen input"),temporaryPath);
    return true;
}

bool copyDatabaseIntoDevice(const QString &temporaryPath,QIODevice &device,QString &error,
    CancellationToken cancel){
    QFile source(temporaryPath);
    if(!source.open(QIODevice::ReadOnly))
        return fail(error,"copy",QStringLiteral("cannot reopen temporary database: ")+source.errorString(),temporaryPath);
    QByteArray buffer;
    buffer.resize(int(copyChunkBytes));
    for(;;){
        if(isCanceled(cancel))return canceledError(error,"copy",temporaryPath);
        const qint64 read=source.read(buffer.data(),buffer.size());
        if(read<0)
            return fail(error,"copy",QStringLiteral("read failed: ")+source.errorString(),temporaryPath);
        if(read==0)break;
        qint64 offset=0;
        while(offset<read){
            const qint64 written=device.write(buffer.constData()+offset,read-offset);
            if(written<=0)
                return fail(error,"copy",QStringLiteral("short write"),temporaryPath);
            offset+=written;
        }
    }
    return true;
}

// A target path must never be one of the project's own source images, and it must
// be a concrete regular file rather than a link, directory or invalid path.
bool validateTargetPath(const QString &target,const ProjectState &project,QString &error){
    if(target.isEmpty()||target.contains(QChar(0)))
        return fail(error,"target",QStringLiteral("target path is empty or invalid"));
    if(!QFileInfo(target).isAbsolute())
        return fail(error,"target",QStringLiteral("target path must be absolute"),target);
    const QFileInfo info(target);
    if(info.isSymLink())return fail(error,"target",QStringLiteral("target path is a symbolic link"),target);
    if(info.exists()&&info.isDir())
        return fail(error,"target",QStringLiteral("target path is a directory"),target);
    // Check the target and its lock companion against every source path, so a save
    // can never truncate an original photo through a symlink or a plain rename.
    const QString lockPath=target+QStringLiteral(".luminas.lock");
    if(QFileInfo(lockPath).isSymLink())return fail(error,"target",QStringLiteral("lock path is a symbolic link"),target);
    const QString targetKey=pathKey(target);
    const QString lockKey=pathKey(lockPath);
    for(const ProjectPhoto &photo:project.photos){
        const QString sourceKey=pathKey(photo.document->asset.sourcePath);
        if(sourceKey==targetKey)
            return fail(error,"target",QStringLiteral("target is a project source file"),target);
        if(sourceKey==lockKey)
            return fail(error,"target",QStringLiteral("lock path is a project source file"),target);
    }
    return true;
}
} // namespace

bool validProject(const ProjectState &project,QString &error){
    return validateState(project,error);
}

ProjectSnapshot freezeProject(const ProjectState &project,QString &error){
    ProjectState frozen;
    if(!freezeInto(project,frozen,error))return {};
    return std::make_shared<const ProjectState>(std::move(frozen));
}

bool loadProject(const QString &path,ProjectState &project,QString &fileHash,
    QString &error,CancellationToken cancel){
    error.clear();
    if(path.isEmpty()||path.contains(QChar(0))){
        fail(error,"open",QStringLiteral("path is empty or invalid"));
        return false;
    }
    // The file identity is captured before and after the read so a change made
    // while parsing cannot be reported as a successful load.
    QString beforeHash;
    if(!hashFile(path,beforeHash,error,cancel,maximumProjectBytes))return false;
    ProjectState candidate;
    if(!readProjectFile(path,candidate,error,cancel))return false;
    QString afterHash;
    if(!hashFile(path,afterHash,error,cancel,maximumProjectBytes))return false;
    if(beforeHash!=afterHash){
        fail(error,"open",QStringLiteral("project file changed while it was being read"),path);
        return false;
    }
    project=std::move(candidate);
    fileHash=afterHash;
    return true;
}
bool saveProject(const QString &path,ProjectSnapshot project,QString &fileHash,
    QString &error,const QString &expectedFileHash,CancellationToken cancel){
    error.clear();
    if(isCanceled(cancel))return canceledError(error,"save",path);
    if(!project){
        fail(error,"save",QStringLiteral("project snapshot is null"));
        return false;
    }
    // Freeze again from the caller's snapshot so a mutable alias cannot change
    // what is being written mid-save, and so validation covers the exact bytes.
    ProjectState frozen;
    if(!freezeInto(*project,frozen,error))return false;
    if(!validateTargetPath(path,frozen,error))return false;

    const QFileInfo targetInfo(path);
    const QDir parent=targetInfo.absoluteDir();
    if(!parent.exists()){
        fail(error,"target",QStringLiteral("parent directory does not exist"),path);
        return false;
    }
    if(targetInfo.exists()&&targetInfo.isSymLink()){
        fail(error,"target",QStringLiteral("target path is a symbolic link"),path);
        return false;
    }
    const bool targetExists=targetInfo.exists();

    WriterGuard writer;
    if(!writer.isOwned()){
        fail(error,"writer",QStringLiteral("another save is already in progress"),path);
        return false;
    }
    QLockFile lock(path+QStringLiteral(".luminas.lock"));
    lock.setStaleLockTime(0);
    if(!lock.tryLock(0)){
        fail(error,"lock",QStringLiteral("another process holds the project lock"),path);
        return false;
    }
    ScopedLockRelease release(lock);

    // An existing target is only replaced when it loads as this same project at a
    // not-newer revision with a byte-exact expected hash.
    QString originalHash;
    ProjectState original;
    if(targetExists){
        if(expectedFileHash.isEmpty()){
            fail(error,"replace",QStringLiteral("existing target requires an expected file hash"),path);
            return false;
        }
        if(!hashFile(path,originalHash,error,cancel,maximumProjectBytes))return false;
        if(originalHash!=expectedFileHash){
            fail(error,"replace",QStringLiteral("expected file hash does not match the target"),path);
            return false;
        }
        if(!readProjectFile(path,original,error,cancel))return false;
        if(original.id!=frozen.id){
            fail(error,"replace",QStringLiteral("target belongs to a different project"),path);
            return false;
        }
        if(frozen.revision<original.revision){
            fail(error,"replace",QStringLiteral("incoming revision is older than the target"),path);
            return false;
        }
        if(frozen.revision==original.revision&&frozen!=original){
            fail(error,"replace",QStringLiteral("same revision cannot change project content"),path);
            return false;
        }
    }else if(!expectedFileHash.isEmpty()){
        fail(error,"replace",QStringLiteral("expected file hash was given for a missing target"),path);
        return false;
    }

    QTemporaryFile temporary;
    temporary.setAutoRemove(true);
    if(!buildTemporaryDatabase(path,frozen,temporary,error,cancel))return false;
    const QString temporaryPath=temporary.fileName();
    if(!verifyTemporaryDatabase(temporaryPath,frozen,error))return false;
    if(isCanceled(cancel)){
        canceledError(error,"save",path);
        return false;
    }
    QString temporaryHash;
    if(!hashFile(temporaryPath,temporaryHash,error,cancel,maximumProjectBytes))return false;

    QString commitError;
    const bool committed=Persistence::writeAtomicFile(path,
        [&](QIODevice &device,QString &bodyError){
            // Replay the frozen bytes from the start on every attempt.
            if(isCanceled(cancel)){
                bodyError=context("commit",QStringLiteral("canceled"),path);
                return false;
            }
            // The target must still be exactly what we validated before the
            // replacement; an external change aborts the commit.
            const QFileInfo current(path);
            if(current.isSymLink()){
                bodyError=context("commit",QStringLiteral("target became a symbolic link"),path);return false;
            }
            const bool existsNow=current.exists();
            if(existsNow!=targetExists){
                bodyError=context("commit",QStringLiteral("target appeared or disappeared during save"),path);
                return false;
            }
            if(existsNow){
                QString currentHash;
                if(!hashFile(path,currentHash,bodyError,cancel,maximumProjectBytes))return false;
                if(currentHash!=originalHash){
                    bodyError=context("commit",QStringLiteral("target changed during save"),path);
                    return false;
                }
            }
            if(!copyDatabaseIntoDevice(temporaryPath,device,bodyError,cancel))return false;
            // Recheck after the copy as well: retries may replay a large file.
            const QFileInfo afterCopy(path);
            if(afterCopy.isSymLink()||afterCopy.exists()!=targetExists){
                bodyError=context("commit",QStringLiteral("target appeared or disappeared during copy"),path);return false;
            }
            if(targetExists){
                QString finalHash;
                if(!hashFile(path,finalHash,bodyError,cancel,maximumProjectBytes))return false;
                if(finalHash!=originalHash){bodyError=context("commit",QStringLiteral("target changed during copy"),path);return false;}
            }
            return true;
        },commitError,cancel);
    if(!committed){
        error=commitError.isEmpty()?context("commit",QStringLiteral("atomic replacement failed"),path):commitError;
        return false;
    }
    fileHash=temporaryHash;
    return true;
}
bool inspectProjectSources(ProjectSnapshot project,QVector<SourceStatus> &statuses,
    QString &error,CancellationToken cancel){
    error.clear();
    if(isCanceled(cancel))return canceledError(error,"inspect",{});
    QVector<SourceStatus> collected;
    if(!project){
        fail(error,"inspect",QStringLiteral("project snapshot is null"));
        return false;
    }
    ProjectState frozen;
    if(!freezeInto(*project,frozen,error))return false;
    collected.reserve(frozen.photos.size());
    for(const ProjectPhoto &photo:frozen.photos){
        if(isCanceled(cancel))return canceledError(error,"inspect",{});
        const QString &sourcePath=photo.document->asset.sourcePath;
        SourceStatus status;
        status.assetId=photo.document->asset.id;
        status.path=sourcePath;
        const QFileInfo info(sourcePath);
        if(!info.exists()){
            status.state=SourceState::Missing;
            collected.append(status);
            continue;
        }
        if(!info.isFile()){
            status.state=SourceState::Unreadable;status.error=QStringLiteral("source is not a regular file");
            collected.append(status);continue;
        }
        QString actual;
        QString reason;
        // Read only; no image decoding and no full-frame allocation.
        if(!hashFile(sourcePath,actual,reason,cancel)){
            if(isCanceled(cancel))return canceledError(error,"inspect",sourcePath);
            status.state=SourceState::Unreadable;
            status.error=reason;
            collected.append(status);
            continue;
        }
        status.actualHash=actual;
        status.state=actual.compare(photo.document->asset.sourceHash,Qt::CaseInsensitive)==0?SourceState::Available:SourceState::Changed;
        collected.append(status);
    }
    statuses=std::move(collected);
    return true;
}

bool relinkProjectSource(ProjectSnapshot project,const QString &assetId,
    const QString &newPath,ProjectSnapshot &updated,QString &error,
    CancellationToken cancel){
    error.clear();
    if(isCanceled(cancel))return canceledError(error,"relink",newPath);
    if(!project){
        fail(error,"relink",QStringLiteral("project snapshot is null"));
        return false;
    }
    ProjectState frozen;
    if(!freezeInto(*project,frozen,error))return false;
    int index=-1;
    for(int i=0;i<frozen.photos.size();++i){
        if(frozen.photos.at(i).document->asset.id==assetId){index=i;break;}
    }
    if(index<0){
        fail(error,"relink",QStringLiteral("asset id is not part of the project")+QStringLiteral(": ")+assetId);
        return false;
    }
    if(newPath.isEmpty()||newPath.contains(QChar(0))||!QFileInfo(newPath).isAbsolute()){
        fail(error,"relink",QStringLiteral("new source path must be a non-empty absolute path"));
        return false;
    }
    // An unchanged path is not a relink: hand back the original snapshot.
    const QString &oldPath=frozen.photos.at(index).document->asset.sourcePath;
    if(pathKey(oldPath)==pathKey(newPath)){
        updated=project;
        return true;
    }
    // Windows path uniqueness: the new path must not collide with another asset.
    const QString newKey=pathKey(newPath);
    for(int i=0;i<frozen.photos.size();++i){
        if(i==index)continue;
        if(pathKey(frozen.photos.at(i).document->asset.sourcePath)==newKey){
            fail(error,"relink",QStringLiteral("new source path is already used by another asset"));
            return false;
        }
    }
    const QString expectedHash=frozen.photos.at(index).document->asset.sourceHash;
    QString actualHash;
    if(!hashFile(newPath,actualHash,error,cancel))return false;
    if(actualHash.compare(expectedHash,Qt::CaseInsensitive)!=0){
        fail(error,"relink",QStringLiteral("new source content does not match the recorded hash"),newPath);
        return false;
    }
    if(frozen.revision==std::numeric_limits<quint64>::max()){
        fail(error,"relink",QStringLiteral("project revision would overflow"));
        return false;
    }
    if(frozen.photos.at(index).document->revision==std::numeric_limits<quint64>::max()){
        fail(error,"relink",QStringLiteral("document revision would overflow"));
        return false;
    }
    ProjectState candidate=frozen;
    PhotoDocument document=*candidate.photos.at(index).document;
    document.asset.sourcePath=newPath;
    // A relink advances both the project and the affected document revision while
    // the stable local identity is preserved.
    ++document.revision;
    ++candidate.revision;
    candidate.photos[index].document=std::make_shared<const PhotoDocument>(std::move(document));
    if(!validateState(candidate,error))return false;
    updated=std::make_shared<const ProjectState>(std::move(candidate));
    return true;
}
} // namespace LocalEdit
