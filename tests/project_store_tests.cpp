// S3 A3 project storage tests. Sources are tiny text files inside QTemporaryDir;
// they are only hashed, never decoded, so no image fixture is needed anywhere.
#include "core/project_store.h"
#include "core/photo_document.h"
#include "core/cancellation.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QVariant>
#include <QtTest>
#include <memory>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace LocalEdit;

namespace {

constexpr int kWidth=6000;
constexpr int kHeight=4000;

QString hexSha256(const QByteArray &bytes){
    return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
}

bool writeBytes(const QString &path,const QByteArray &bytes){
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly))return false;
    if(file.write(bytes)!=qint64(bytes.size()))return false;
    return file.flush()&&file.error()==QFile::NoError;
}

QByteArray readBytes(const QString &path){
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly))return {};
    return file.readAll();
}

// Persistent-mode source: complete SHA-256 and a known 6000x4000 normalized size.
QString makeSource(const QString &directory,const QString &name,const QByteArray &content){
    const QString path=QDir(directory).absoluteFilePath(name);
    if(!writeBytes(path,content))return {};
    return path;
}

PhotoDocument baseDocument(const QString &assetId,const QString &sourcePath,
    const QString &sourceHash){
    PhotoDocument document;
    document.asset.id=assetId;
    document.asset.sourcePath=sourcePath;
    document.asset.sourceHash=sourceHash;
    document.asset.normalizedSize=QSize(kWidth,kHeight);
    document.asset.raw=false;
    document.asset.sourceFacts=QJsonObject{{QStringLiteral("fixture"),QStringLiteral("text")}};
    document.revision=1;
    document.localIdentity=0;
    document.algorithmVersion=localAlgorithmVersion;
    return document;
}

// Global fields plus one layer per geometry kind, all component ids unique per
// document, with values inside the A2 recipe bounds.
PhotoDocument richDocument(const QString &assetId,const QString &sourcePath,
    const QString &sourceHash,double exposure){
    PhotoDocument document=baseDocument(assetId,sourcePath,sourceHash);
    document.global.exposure=exposure;
    document.global.contrast=0.25;
    document.global.saturation=1.25;
    document.global.warmth=-0.4;
    document.global.tint=0.1;
    document.global.angle=12.5;
    document.global.curves[0]=Curve{{0,0},{0.25,0.5},{1,1}};

    AdjustmentLayer brushLayer;
    brushLayer.id=assetId+QStringLiteral(".layer.brush");
    brushLayer.name=QStringLiteral("brush");
    brushLayer.opacity=0.75;
    brushLayer.parameters.exposure=0.5;
    MaskComponent brushComponent;
    brushComponent.id=assetId+QStringLiteral(".c.brush");
    brushComponent.operation=MaskOperation::Add;
    brushComponent.strength=0.9;
    BrushGeometry brushGeometry;
    brushGeometry.points=QVector<QPointF>{QPointF(1.5,2.5),QPointF(3.5,-4.25)};
    brushGeometry.radius=120;
    brushGeometry.hardness=0.4;
    brushComponent.geometry=brushGeometry;
    brushLayer.mask.components.append(brushComponent);

    AdjustmentLayer linearLayer;
    linearLayer.id=assetId+QStringLiteral(".layer.linear");
    MaskComponent linearComponent;
    linearComponent.id=assetId+QStringLiteral(".c.linear");
    linearComponent.operation=MaskOperation::Subtract;
    LinearGeometry linearGeometry;
    linearGeometry.start=QPointF(0.1,0.2);
    linearGeometry.end=QPointF(0.9,0.8);
    linearComponent.geometry=linearGeometry;
    linearLayer.mask.components.append(linearComponent);

    AdjustmentLayer radialLayer;
    radialLayer.id=assetId+QStringLiteral(".layer.radial");
    radialLayer.mask.feather=12.5;
    radialLayer.mask.invert=true;
    MaskComponent radialComponent;
    radialComponent.id=assetId+QStringLiteral(".c.radial");
    RadialGeometry radialGeometry;
    radialGeometry.center=QPointF(0.5,0.5);
    radialGeometry.radiusX=200;
    radialGeometry.radiusY=150;
    radialGeometry.angleDegrees=33.5;
    radialComponent.geometry=radialGeometry;
    radialLayer.mask.components.append(radialComponent);

    document.layers=QVector<AdjustmentLayer>{brushLayer,linearLayer,radialLayer};
    return document;
}

ProjectPhoto photoOf(const PhotoDocument &document,PhotoMark mark){
    ProjectPhoto photo;
    photo.document=std::make_shared<const PhotoDocument>(document);
    photo.mark=mark;
    return photo;
}

ProjectState projectOf(const QString &id,const QVector<ProjectPhoto> &photos,quint64 revision=1){
    ProjectState project;
    project.id=id;
    project.name=QStringLiteral("样例 Sample");
    project.revision=revision;
    project.photos=photos;
    if(!photos.isEmpty())project.activeAssetId=photos.first().document->asset.id;
    return project;
}

// A separate connection for deliberately malformed databases. The connection is
// always closed and removed before the file is handed back to the store.
class ScratchDatabase {
public:
    explicit ScratchDatabase(const QString &path){
        name=QStringLiteral("pst-scratch-")+QString::number(reinterpret_cast<quintptr>(this));
        database=QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),name);
        database.setDatabaseName(path);
    }
    ~ScratchDatabase(){
        if(database.isValid()){
            if(database.isOpen())database.close();
            database=QSqlDatabase();
        }
        QSqlDatabase::removeDatabase(name);
    }
    ScratchDatabase(const ScratchDatabase &)=delete;
    ScratchDatabase &operator=(const ScratchDatabase &)=delete;
    bool open(){return database.open();}
    bool exec(const QString &sql,const QVariantList &values={}){
        QSqlQuery query(database);
        if(!values.isEmpty()){
            if(!query.prepare(sql))return false;
            for(const QVariant &value:values)query.addBindValue(value);
        }
        return values.isEmpty()?query.exec(sql):query.exec();
    }
private:
    QString name;
    QSqlDatabase database;
};

// Saves a valid project through the public API so malformed variants can be
// derived from a known-good file.
bool saveFrozen(const QString &path,const ProjectState &project,QString &hash,QString &error){
    const ProjectSnapshot snapshot=freezeProject(project,error);
    if(!snapshot)return false;
    return saveProject(path,snapshot,hash,error,{},{});
}
}

class ProjectStoreTests:public QObject {
 Q_OBJECT
private slots:
#ifdef Q_OS_WIN
 void failedAtomicReplacementKeepsTheWholeExistingProject(){
    QTemporaryDir dir;QVERIFY(dir.isValid());
    const QByteArray content("locked-source");
    const QString source=makeSource(dir.path(),"source.txt",content);
    auto state=projectOf("locked-project",{
        photoOf(baseDocument("locked-asset",source,hexSha256(content)),PhotoMark::Keep)});
    const QString path=dir.filePath("locked.luminas");QString hash,error;
    QVERIFY2(saveFrozen(path,state,hash,error),qPrintable(error));
    const QByteArray oldBytes=readBytes(path);
    const int before=QDir(dir.path()).entryList(QDir::Files|QDir::Hidden).size();
    struct Handle {HANDLE value=INVALID_HANDLE_VALUE;
        ~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}};
    Handle held{CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()),GENERIC_READ|GENERIC_WRITE,
        FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr)};
    QVERIFY(held.value!=INVALID_HANDLE_VALUE);
    ++state.revision;state.name="updated";auto next=freezeProject(state,error);
    QString outputHash="unchanged";
    QVERIFY(!saveProject(path,next,outputHash,error,hash));QVERIFY(!error.isEmpty());
    QCOMPARE(outputHash,QString("unchanged"));QCOMPARE(readBytes(path),oldBytes);
    QCOMPARE(QDir(dir.path()).entryList(QDir::Files|QDir::Hidden).size(),before);
    const bool closed=CloseHandle(held.value);held.value=INVALID_HANDLE_VALUE;QVERIFY(closed);
    QVERIFY2(saveProject(path,next,outputHash,error,hash),qPrintable(error));
    ProjectState loaded;QString loadedHash;
    QVERIFY2(loadProject(path,loaded,loadedHash,error),qPrintable(error));
    QCOMPARE(loaded,state);QCOMPARE(loadedHash,outputHash);
 }
#endif
 void emptyGroupsAndNullDocuments(){
    QTemporaryDir dir;QVERIFY(dir.isValid());
    ProjectState empty;empty.id="empty-project";
    QString error,hash;
    auto frozen=freezeProject(empty,error);QVERIFY2(frozen,qPrintable(error));
    const QString path=dir.filePath(QStringLiteral("空组.luminas"));
    QVERIFY2(saveProject(path,frozen,hash,error),qPrintable(error));
    ProjectState loaded;QString loadedHash;
    QVERIFY2(loadProject(path,loaded,loadedHash,error),qPrintable(error));
    QCOMPARE(loaded,empty);QCOMPARE(hash,loadedHash);
    empty.photos.append(ProjectPhoto{});
    QVERIFY(!freezeProject(empty,error));QVERIFY(!error.isEmpty());
 }
 void changedSchemaAndSqlStorageTypesAreRejected(){
    QTemporaryDir dir;QVERIFY(dir.isValid());
    const QByteArray content("schema-source");
    const QString source=makeSource(dir.path(),"source.txt",content);
    const ProjectState project=projectOf("schema-project",{
        photoOf(baseDocument("schema-asset",source,hexSha256(content)),PhotoMark::Keep)});
    QString error,hash;const QString good=dir.filePath("good.luminas");
    QVERIFY2(saveFrozen(good,project,hash,error),qPrintable(error));
    const QStringList mutations{
        "PRAGMA application_id=0", "PRAGMA user_version=2",
        "ALTER TABLE photos ADD COLUMN futureNode TEXT",
        "UPDATE photos SET mark=0.5", "UPDATE photos SET document=CAST(document AS TEXT)",
        "UPDATE metadata SET value=CAST(value AS BLOB) WHERE key='projectId'"};
    for(int i=0;i<mutations.size();++i){
        const QString path=dir.filePath(QString("bad-%1.luminas").arg(i));
        QVERIFY(QFile::copy(good,path));
        {ScratchDatabase db(path);QVERIFY(db.open());QVERIFY2(db.exec(mutations[i]),qPrintable(mutations[i]));}
        const QByteArray before=readBytes(path);
        ProjectState output=project;output.id="sentinel";QString outputHash="untouched";
        QVERIFY2(!loadProject(path,output,outputHash,error),qPrintable(mutations[i]));
        QVERIFY(!error.isEmpty());QCOMPARE(output.id,QString("sentinel"));
        QCOMPARE(outputHash,QString("untouched"));QCOMPARE(readBytes(path),before);
    }
 }
 void wholeGroupRoundTripsOrderMarksAndAllGeometry(){
    QTemporaryDir dir;QVERIFY(dir.isValid());
    const QByteArray bytesA("alpha-source"),bytesB("beta-source"),bytesC("gamma-source");
    const QString pathA=makeSource(dir.path(),"a.txt",bytesA);
    const QString pathB=makeSource(dir.path(),"b.txt",bytesB);
    const QString pathC=makeSource(dir.path(),"c.txt",bytesC);
    QVERIFY(!pathA.isEmpty()&&!pathB.isEmpty()&&!pathC.isEmpty());
    ProjectState project=projectOf(QStringLiteral("project-round-trip"),{
        photoOf(richDocument(QStringLiteral("asset-a"),pathA,hexSha256(bytesA),-0.5),PhotoMark::Keep),
        photoOf(richDocument(QStringLiteral("asset-b"),pathB,hexSha256(bytesB),0.75),PhotoMark::Reject),
        photoOf(richDocument(QStringLiteral("asset-c"),pathC,hexSha256(bytesC),1.25),PhotoMark::Unmarked)},7);
    project.activeAssetId=QStringLiteral("asset-b");
    const QString path=dir.filePath(QStringLiteral("group.luminas"));

    QString error;
    const ProjectSnapshot frozen=freezeProject(project,error);
    QVERIFY2(frozen!=nullptr,qPrintable(error));
    QString savedHash;
    QVERIFY2(saveProject(path,frozen,savedHash,error,{},{}),qPrintable(error));
    QCOMPARE(savedHash.size(),64);

    ProjectState loaded;
    QString loadedHash;
    QVERIFY2(loadProject(path,loaded,loadedHash,error,{}),qPrintable(error));
    QCOMPARE(loadedHash,savedHash);
    QCOMPARE(loaded.id,project.id);
    QCOMPARE(loaded.name,project.name);
    QCOMPARE(loaded.revision,quint64(7));
    QCOMPARE(loaded.activeAssetId,QStringLiteral("asset-b"));
    QCOMPARE(loaded.photos.size(),3);
    QCOMPARE(loaded.photos.at(0).document->asset.id,QStringLiteral("asset-a"));
    QCOMPARE(loaded.photos.at(1).document->asset.id,QStringLiteral("asset-b"));
    QCOMPARE(loaded.photos.at(2).document->asset.id,QStringLiteral("asset-c"));
    QCOMPARE(loaded.photos.at(0).mark,PhotoMark::Keep);
    QCOMPARE(loaded.photos.at(1).mark,PhotoMark::Reject);
    QCOMPARE(loaded.photos.at(2).mark,PhotoMark::Unmarked);
    QVERIFY(loaded==*frozen);
    for(int index=0;index<3;++index){
        const PhotoDocument &document=*loaded.photos.at(index).document;
        QCOMPARE(document.global.saturation,1.25);
        QCOMPARE(document.global.warmth,-0.4);
        QCOMPARE(document.asset.normalizedSize,QSize(kWidth,kHeight));
        QCOMPARE(document.layers.size(),3);
        QCOMPARE(std::get<BrushGeometry>(document.layers.at(0).mask.components.at(0).geometry)
            .points.at(1),QPointF(3.5,-4.25));
        QCOMPARE(std::get<LinearGeometry>(document.layers.at(1).mask.components.at(0).geometry)
            .end,QPointF(0.9,0.8));
        const RadialGeometry &radial=std::get<RadialGeometry>(document.layers.at(2)
            .mask.components.at(0).geometry);
        QCOMPARE(radial.radiusX,200.0);
        QCOMPARE(radial.angleDegrees,33.5);
        QVERIFY(document.layers.at(2).mask.invert);
    }
 }

 void freezeDeepCopiesSoMutableAliasCannotLeak(){
    QTemporaryDir dir;QVERIFY(dir.isValid());
    const QByteArray bytes("alias-source");
    const QString sourcePath=makeSource(dir.path(),"alias.txt",bytes);
    QVERIFY(!sourcePath.isEmpty());
    ProjectState project=projectOf(QStringLiteral("project-alias"),
        {photoOf(richDocument(QStringLiteral("asset-alias"),sourcePath,hexSha256(bytes),0.0),
            PhotoMark::Keep)});
    auto mutableDocument=std::make_shared<PhotoDocument>(*project.photos[0].document);
    project.photos[0].document=mutableDocument;
    QString error;
    const ProjectSnapshot frozen=freezeProject(project,error);
    QVERIFY2(frozen!=nullptr,qPrintable(error));
    // Mutating the caller's tree must not change the frozen snapshot.
    mutableDocument->global.exposure=99;
    mutableDocument->revision=42;
    std::get<BrushGeometry>(mutableDocument->layers[0].mask.components[0].geometry)
        .points[0]=QPointF(-7,-7);
    project.photos[0].mark=PhotoMark::Reject;

    QCOMPARE(frozen->photos.first().document->global.exposure,0.0);
    QCOMPARE(frozen->photos.first().document->revision,quint64(1));
    QCOMPARE(std::get<BrushGeometry>(frozen->photos.first().document->layers[0]
        .mask.components[0].geometry).points.at(0),QPointF(1.5,2.5));
    QCOMPARE(frozen->photos.first().mark,PhotoMark::Keep);
 }

 void validProjectAcceptsGoodGroupAndRejectsDuplicates(){
    QTemporaryDir dir;QVERIFY(dir.isValid());
    const QByteArray bytesA("valid-a"),bytesB("valid-b");
    const QString sourceA=makeSource(dir.path(),"valid-a.txt",bytesA);
    const QString sourceB=makeSource(dir.path(),"valid-b.txt",bytesB);
    QVERIFY(!sourceA.isEmpty()&&!sourceB.isEmpty());
    QString error;
    const ProjectState good=projectOf(QStringLiteral("project-valid"),
        {photoOf(baseDocument(QStringLiteral("asset-a"),sourceA,hexSha256(bytesA)),
            PhotoMark::Unmarked),
         photoOf(baseDocument(QStringLiteral("asset-b"),sourceB,hexSha256(bytesB)),
             PhotoMark::Keep)});
    QVERIFY2(validProject(good,error),qPrintable(error));
    QVERIFY(error.isEmpty());

    // A duplicate asset id is not a valid group.
    ProjectState duplicateId=good;
    duplicateId.photos[1].document=std::make_shared<const PhotoDocument>(
        baseDocument(QStringLiteral("asset-a"),sourceB,hexSha256(bytesB)));
    QVERIFY(!validProject(duplicateId,error));
    QVERIFY(!error.isEmpty());

    // Two assets may not reference one source file.
    ProjectState duplicatePath=good;
    duplicatePath.photos[1].document=std::make_shared<const PhotoDocument>(
        baseDocument(QStringLiteral("asset-b"),sourceA,hexSha256(bytesA)));
    QVERIFY(!validProject(duplicatePath,error));
    QVERIFY(!error.isEmpty());

    // activeAssetId must belong to the group.
    ProjectState strayActive=good;
    strayActive.activeAssetId=QStringLiteral("asset-not-present");
    QVERIFY(!validProject(strayActive,error));
    QVERIFY(!error.isEmpty());
 }

 void missingSourceStillOpensForEditing(){
    QTemporaryDir dir;QVERIFY(dir.isValid());
    const QByteArray bytes("missing-source");
    const QString sourcePath=makeSource(dir.path(),"gone.txt",bytes);
    QVERIFY(!sourcePath.isEmpty());
    const QString path=dir.filePath(QStringLiteral("missing.luminas"));
    QString error,hash;
    QVERIFY2(saveFrozen(path,projectOf(QStringLiteral("project-missing"),
        {photoOf(richDocument(QStringLiteral("asset-gone"),sourcePath,hexSha256(bytes),0.0),
            PhotoMark::Unmarked)}),hash,error),qPrintable(error));
    QVERIFY(QFile::remove(sourcePath));

    ProjectState loaded;
    QVERIFY2(loadProject(path,loaded,hash,error,{}),qPrintable(error));
    QCOMPARE(loaded.photos.size(),1);
    QCOMPARE(loaded.photos.first().document->asset.sourcePath,sourcePath);
 }
 void sourceInspectionReportsAvailableChangedAndMissing(){
    QTemporaryDir dir;QVERIFY(dir.isValid());
    const QByteArray good("inspect-good");
    const QByteArray before("inspect-before");
    const QString goodPath=makeSource(dir.path(),"good.txt",good);
    const QString changedPath=makeSource(dir.path(),"changed.txt",before);
    const QString gonePath=dir.filePath(QStringLiteral("never-created.txt"));
    QVERIFY(!goodPath.isEmpty()&&!changedPath.isEmpty());
    ProjectState project=projectOf(QStringLiteral("project-inspect"),{
        photoOf(baseDocument(QStringLiteral("asset-good"),goodPath,hexSha256(good)),PhotoMark::Unmarked),
        photoOf(baseDocument(QStringLiteral("asset-changed"),changedPath,hexSha256(before)),PhotoMark::Unmarked),
        photoOf(baseDocument(QStringLiteral("asset-missing"),gonePath,hexSha256(before)),PhotoMark::Unmarked)});
    QString error;
    const ProjectSnapshot snapshot=freezeProject(project,error);
    QVERIFY2(snapshot!=nullptr,qPrintable(error));
    QVERIFY(writeBytes(changedPath,QByteArray("inspect-after")));

    QVector<SourceStatus> statuses;
    QVERIFY2(inspectProjectSources(snapshot,statuses,error,{}),qPrintable(error));
    QCOMPARE(statuses.size(),3);
    QCOMPARE(statuses.at(0).assetId,QStringLiteral("asset-good"));
    QCOMPARE(statuses.at(0).state,SourceState::Available);
    QCOMPARE(statuses.at(0).actualHash,hexSha256(good));
    QCOMPARE(statuses.at(1).state,SourceState::Changed);
    QCOMPARE(statuses.at(1).actualHash,hexSha256(QByteArray("inspect-after")));
    QCOMPARE(statuses.at(2).state,SourceState::Missing);
    QVERIFY(statuses.at(2).actualHash.isEmpty());
 }

 void relinkAcceptsIdenticalHashAndRejectsDifferentHash(){
    QTemporaryDir dir;QVERIFY(dir.isValid());
    const QByteArray bytes("relink-source");
    const QString originalPath=makeSource(dir.path(),"relink-a.txt",bytes);
    const QString samePath=makeSource(dir.path(),"relink-b.txt",bytes);
    const QString differentPath=makeSource(dir.path(),"relink-c.txt",QByteArray("other-content"));
    QVERIFY(!originalPath.isEmpty()&&!samePath.isEmpty()&&!differentPath.isEmpty());
    ProjectState project=projectOf(QStringLiteral("project-relink"),
        {photoOf(baseDocument(QStringLiteral("asset-relink"),originalPath,hexSha256(bytes)),
            PhotoMark::Unmarked)});
    QString error;
    const ProjectSnapshot snapshot=freezeProject(project,error);
    QVERIFY2(snapshot!=nullptr,qPrintable(error));

    // A path whose content has a different hash must never be accepted.
    ProjectSnapshot refused;
    QVERIFY(!relinkProjectSource(snapshot,QStringLiteral("asset-relink"),differentPath,refused,
        error,{}));
    QVERIFY(!error.isEmpty());
    QVERIFY(refused==nullptr);

    // Same content, new location: both project and document revision advance and
    // the stable local identity is preserved.
    ProjectSnapshot updated;
    QVERIFY2(relinkProjectSource(snapshot,QStringLiteral("asset-relink"),samePath,updated,error,{}),
        qPrintable(error));
    QVERIFY(updated!=nullptr);
    QCOMPARE(updated->revision,snapshot->revision+1);
    QCOMPARE(updated->photos.first().document->revision,
        snapshot->photos.first().document->revision+1);
    QCOMPARE(updated->photos.first().document->asset.sourcePath,samePath);
    QCOMPARE(updated->photos.first().document->asset.sourceHash,
        snapshot->photos.first().document->asset.sourceHash);
    QCOMPARE(updated->photos.first().document->localIdentity,
        snapshot->photos.first().document->localIdentity);
    QCOMPARE(updated->id,snapshot->id);

    // The unchanged path is not a relink and may return the original snapshot.
    ProjectSnapshot unchanged;
    QVERIFY2(relinkProjectSource(snapshot,QStringLiteral("asset-relink"),originalPath,unchanged,
        error,{}),qPrintable(error));
    QVERIFY(unchanged!=nullptr);
    QCOMPARE(unchanged->revision,snapshot->revision);
 }
 // Load must reject unknown versions, extra schema objects and corrupt JSON while
 // leaving the caller's output and hash untouched.
 void loadRejectsUnknownVersionExtraTableAndBadJsonWithoutTouchingOutput(){
    QTemporaryDir dir;QVERIFY(dir.isValid());
    const QByteArray bytes("legacy-source");
    const QString sourcePath=makeSource(dir.path(),"legacy.txt",bytes);
    QVERIFY(!sourcePath.isEmpty());
    const QString goodPath=dir.filePath(QStringLiteral("good.luminas"));
    QString error,goodHash;
    QVERIFY2(saveFrozen(goodPath,projectOf(QStringLiteral("project-legacy"),
        {photoOf(baseDocument(QStringLiteral("asset-legacy"),sourcePath,hexSha256(bytes)),
            PhotoMark::Unmarked)}),goodHash,error),qPrintable(error));
    const QByteArray goodBytes=readBytes(goodPath);
    QVERIFY(!goodBytes.isEmpty());

    // Sentinel outputs: a failed load must not replace them.
    ProjectState sentinel;
    sentinel.id=QStringLiteral("sentinel-id");
    sentinel.name=QStringLiteral("sentinel");
    const QString sentinelHash=QStringLiteral("sentinel-hash");

    const auto expectRejected=[&](const QString &variantPath){
        ProjectState output=sentinel;
        QString hash=sentinelHash;
        QString reason;
        QVERIFY2(!loadProject(variantPath,output,hash,reason,{}),qPrintable(variantPath));
        QVERIFY(!reason.isEmpty());
        QCOMPARE(output.id,sentinel.id);
        QCOMPARE(output.name,sentinel.name);
        QVERIFY(output.photos.isEmpty());
        QCOMPARE(hash,sentinelHash);
    };

    // Unknown user_version.
    {
        const QString path=dir.filePath(QStringLiteral("unknown-version.luminas"));
        QVERIFY(QFile::copy(goodPath,path));
        ScratchDatabase scratch(path);
        QVERIFY2(scratch.open(),"scratch open failed");
        QVERIFY(scratch.exec(QStringLiteral("PRAGMA user_version=99")));
        expectRejected(path);
    }
    // Extra table beyond metadata and photos.
    {
        const QString path=dir.filePath(QStringLiteral("extra-table.luminas"));
        QVERIFY(QFile::copy(goodPath,path));
        ScratchDatabase scratch(path);
        QVERIFY2(scratch.open(),"scratch open failed");
        QVERIFY(scratch.exec(QStringLiteral("CREATE TABLE extra(id INTEGER PRIMARY KEY)")));
        expectRejected(path);
    }
    // Extra metadata key outside the seven allowed keys.
    {
        const QString path=dir.filePath(QStringLiteral("extra-key.luminas"));
        QVERIFY(QFile::copy(goodPath,path));
        ScratchDatabase scratch(path);
        QVERIFY2(scratch.open(),"scratch open failed");
        QVERIFY(scratch.exec(QStringLiteral("INSERT INTO metadata(key,value) VALUES(?,?)"),
            {QStringLiteral("surprise"),QStringLiteral("1")}));
        expectRejected(path);
    }
    // Corrupt document JSON inside an otherwise valid row.
    {
        const QString path=dir.filePath(QStringLiteral("bad-json.luminas"));
        QVERIFY(QFile::copy(goodPath,path));
        ScratchDatabase scratch(path);
        QVERIFY2(scratch.open(),"scratch open failed");
        QVERIFY(scratch.exec(QStringLiteral("UPDATE photos SET document=? WHERE ordinal=0"),
            {QByteArray("{not valid json")}));
        expectRejected(path);
    }
    // A truncated file is not a database at all.
    {
        const QString path=dir.filePath(QStringLiteral("truncated.luminas"));
        QVERIFY(writeBytes(path,goodBytes.left(goodBytes.size()/3)));
        expectRejected(path);
    }
    // The known-good file is unaffected by all of the above.
    ProjectState reloaded;
    QString reloadedHash;
    QVERIFY2(loadProject(goodPath,reloaded,reloadedHash,error,{}),qPrintable(error));
    QCOMPARE(reloadedHash,goodHash);
    QCOMPARE(readBytes(goodPath),goodBytes);
 }
 // Every refused save must leave the previous project bytes and hash untouched.
 void saveRefusesUnsafeOverwritesAndKeepsOldProjectBytes(){
    QTemporaryDir dir;QVERIFY(dir.isValid());
    const QByteArray bytesA("save-a"),bytesB("save-b");
    const QString sourceA=makeSource(dir.path(),"save-a.txt",bytesA);
    const QString sourceB=makeSource(dir.path(),"save-b.txt",bytesB);
    QVERIFY(!sourceA.isEmpty()&&!sourceB.isEmpty());
    const QString path=dir.filePath(QStringLiteral("guarded.luminas"));

    QString error,originalHash;
    QVERIFY2(saveFrozen(path,projectOf(QStringLiteral("project-guarded"),
        {photoOf(baseDocument(QStringLiteral("asset-a"),sourceA,hexSha256(bytesA)),
            PhotoMark::Unmarked)}),originalHash,error),qPrintable(error));
    const QByteArray originalBytes=readBytes(path);
    QVERIFY(!originalBytes.isEmpty());

    // A successful replacement needs the exact current fingerprint. The group has
    // two photos, so the source guard must cover every asset, not just the first.
    const ProjectState next=projectOf(QStringLiteral("project-guarded"),
        {photoOf(baseDocument(QStringLiteral("asset-a"),sourceA,hexSha256(bytesA)),PhotoMark::Keep),
         photoOf(baseDocument(QStringLiteral("asset-b"),sourceB,hexSha256(bytesB)),
             PhotoMark::Unmarked)},2);
    const ProjectSnapshot nextSnapshot=freezeProject(next,error);
    QVERIFY2(nextSnapshot!=nullptr,qPrintable(error));

    const auto expectRefused=[&](const ProjectSnapshot &candidate,const QString &expected,
        const QString &label){
        QString hash=QStringLiteral("unchanged");
        QString reason;
        QVERIFY2(!saveProject(path,candidate,hash,reason,expected,{}),qPrintable(label));
        QVERIFY(!reason.isEmpty());
        QCOMPARE(hash,QStringLiteral("unchanged"));
        QCOMPARE(readBytes(path),originalBytes);
        // The on-disk project must still load and still hash to the original value.
        ProjectState current;
        QString currentHash;
        QVERIFY2(loadProject(path,current,currentHash,reason,{}),qPrintable(reason));
        QCOMPARE(currentHash,originalHash);
        QVERIFY(current.id==QStringLiteral("project-guarded"));
    };

    // No expected hash cannot overwrite an existing project.
    expectRefused(nextSnapshot,QString(),QStringLiteral("missing expected hash"));
    // A wrong expected fingerprint is refused.
    expectRefused(nextSnapshot,QString(64,QChar('0')),QStringLiteral("wrong fingerprint"));
    // A different projectId is refused.
    {
        const ProjectSnapshot other=freezeProject(projectOf(QStringLiteral("project-other"),
            {photoOf(baseDocument(QStringLiteral("asset-a"),sourceA,hexSha256(bytesA)),
                PhotoMark::Keep)},2),error);
        QVERIFY2(other!=nullptr,qPrintable(error));
        expectRefused(other,originalHash,QStringLiteral("different projectId"));
    }
    // An older revision is refused.
    {
        const ProjectSnapshot older=freezeProject(projectOf(QStringLiteral("project-guarded"),
            {photoOf(baseDocument(QStringLiteral("asset-a"),sourceA,hexSha256(bytesA)),
                PhotoMark::Keep)},1),error);
        QVERIFY2(older!=nullptr,qPrintable(error));
        expectRefused(older,originalHash,QStringLiteral("older revision"));
    }
    // A source file must never be used as the project target.
    {
        QString hash=QStringLiteral("unchanged");
        QString reason;
        QVERIFY2(!saveProject(sourceA,nextSnapshot,hash,reason,{},{}),qPrintable(reason));
        QVERIFY(!reason.isEmpty());
        QCOMPARE(readBytes(sourceA),bytesA);
        QCOMPARE(hash,QStringLiteral("unchanged"));
    }
    // The second photo's source is protected too.
    {
        QString hash=QStringLiteral("unchanged");
        QString reason;
        QVERIFY2(!saveProject(sourceB,nextSnapshot,hash,reason,{},{}),qPrintable(reason));
        QVERIFY(!reason.isEmpty());
        QCOMPARE(readBytes(sourceB),bytesB);
        QCOMPARE(hash,QStringLiteral("unchanged"));
    }
    // The legitimate replacement still succeeds and changes the fingerprint.
    QString replacedHash;
    QVERIFY2(saveProject(path,nextSnapshot,replacedHash,error,originalHash,{}),qPrintable(error));
    QCOMPARE(replacedHash.size(),64);
    QVERIFY(replacedHash!=originalHash);
 }

 void saveRefusesCanceledPreCanceledBusyLockAndBlockedParent(){
    QTemporaryDir dir;QVERIFY(dir.isValid());
    const QByteArray bytes("guard-source");
    const QString sourcePath=makeSource(dir.path(),"guard.txt",bytes);
    QVERIFY(!sourcePath.isEmpty());
    const ProjectState project=projectOf(QStringLiteral("project-busy"),
        {photoOf(baseDocument(QStringLiteral("asset-busy"),sourcePath,hexSha256(bytes)),
            PhotoMark::Unmarked)});
    QString error;
    const ProjectSnapshot snapshot=freezeProject(project,error);
    QVERIFY2(snapshot!=nullptr,qPrintable(error));

    // A pre-canceled token refuses before creating anything.
    const QString canceledPath=dir.filePath(QStringLiteral("canceled.luminas"));
    CancellationSource cancellation;
    cancellation.cancel();
    QString hash=QStringLiteral("unchanged");
    QVERIFY(!saveProject(canceledPath,snapshot,hash,error,{},cancellation.token()));
    QVERIFY(!error.isEmpty());
    QCOMPARE(hash,QStringLiteral("unchanged"));
    QVERIFY(!QFileInfo::exists(canceledPath));

    // An externally held QLockFile excludes this writer without blocking.
    const QString lockedPath=dir.filePath(QStringLiteral("locked.luminas"));
    QLockFile held(lockedPath+QStringLiteral(".luminas.lock"));
    QVERIFY(held.tryLock(0));
    hash=QStringLiteral("unchanged");
    QVERIFY(!saveProject(lockedPath,snapshot,hash,error,{},{}));
    QVERIFY(!error.isEmpty());
    QCOMPARE(hash,QStringLiteral("unchanged"));
    QVERIFY(!QFileInfo::exists(lockedPath));
    held.unlock();

    // A missing parent directory is not created.
    hash=QStringLiteral("unchanged");
    QVERIFY(!saveProject(dir.filePath(QStringLiteral("absent/child.luminas")),snapshot,hash,
        error,{},{}));
    QVERIFY(!error.isEmpty());
    QCOMPARE(hash,QStringLiteral("unchanged"));
    QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("absent"))));

    // A regular file where the parent directory belongs blocks the save.
    const QString blockedParent=dir.filePath(QStringLiteral("blocked"));
    QVERIFY(writeBytes(blockedParent,QByteArray("not a directory")));
    hash=QStringLiteral("unchanged");
    QVERIFY(!saveProject(blockedParent+QStringLiteral("/child.luminas"),snapshot,hash,error,{},{}));
    QVERIFY(!error.isEmpty());
    QCOMPARE(hash,QStringLiteral("unchanged"));
    QCOMPARE(readBytes(blockedParent),QByteArray("not a directory"));

    // The refused attempts left no temporary database behind.
    const QStringList leftovers=QDir(dir.path()).entryList(
        QStringList{QStringLiteral(".luminas-project-*")},QDir::Files);
    QVERIFY2(leftovers.isEmpty(),qPrintable(leftovers.join(QStringLiteral(","))));
 }
};

QTEST_GUILESS_MAIN(ProjectStoreTests)

#include "project_store_tests.moc"
