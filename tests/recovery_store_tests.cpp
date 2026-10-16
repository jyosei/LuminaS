#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QLockFile>
#include <QDir>
#include <functional>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "core/recovery_store.h"
#include "core/cancellation.h"

using namespace LocalEdit;

namespace {

constexpr int kWidth = 6000;
constexpr int kHeight = 4000;

QString shaOf(const QByteArray &bytes){
    return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
}
bool writeBytes(const QString &path,const QByteArray &bytes){
    QFile f(path);
    if(!f.open(QIODevice::WriteOnly))return false;
    if(f.write(bytes)!=qint64(bytes.size()))return false;
    return f.flush()&&f.error()==QFile::NoError;
}
QByteArray readBytes(const QString &path){
    QFile f(path);
    if(!f.open(QIODevice::ReadOnly))return {};
    return f.readAll();
}
PhotoDocument baseDoc(const QString &assetId,const QString &src,const QString &hash){
    PhotoDocument d;
    d.asset.id=assetId; d.asset.sourcePath=src; d.asset.sourceHash=hash;
    d.asset.normalizedSize=QSize(kWidth,kHeight); d.asset.raw=false;
    d.asset.sourceFacts=QJsonObject{{QStringLiteral("fixture"),QStringLiteral("text")}};
    d.revision=1; d.localIdentity=0; d.algorithmVersion=localAlgorithmVersion;
    return d;
}
PhotoDocument richDoc(const QString &assetId,const QString &src,const QString &hash,double exposure){
    PhotoDocument d=baseDoc(assetId,src,hash);
    d.global.exposure=exposure;
    d.global.contrast=0.25; d.global.saturation=1.25;
    d.global.warmth=-0.4; d.global.tint=0.1; d.global.angle=12.5;
    d.global.curves[0]=Curve{{0,0},{0.25,0.5},{1,1}};

    AdjustmentLayer b;
    b.id=assetId+QStringLiteral(".layer.brush");
    b.opacity=0.75; b.parameters.exposure=0.5;
    MaskComponent bc; bc.id=assetId+QStringLiteral(".c.brush");
    bc.operation=MaskOperation::Add; bc.strength=0.9;
    BrushGeometry bg; bg.points=QVector<QPointF>{QPointF(1.5,2.5),QPointF(3.5,-4.25)};
    bg.radius=120; bg.hardness=0.4; bc.geometry=bg;
    b.mask.components.append(bc);

    AdjustmentLayer l;
    l.id=assetId+QStringLiteral(".layer.linear");
    MaskComponent lc; lc.id=assetId+QStringLiteral(".c.linear");
    lc.operation=MaskOperation::Subtract;
    LinearGeometry lg; lg.start=QPointF(0.1,0.2); lg.end=QPointF(0.9,0.8);
    lc.geometry=lg; l.mask.components.append(lc);

    AdjustmentLayer r;
    r.id=assetId+QStringLiteral(".layer.radial");
    r.mask.feather=12.5; r.mask.invert=true;
    MaskComponent rc; rc.id=assetId+QStringLiteral(".c.radial");
    RadialGeometry rg; rg.center=QPointF(0.5,0.5); rg.radiusX=200; rg.radiusY=150;
    rg.angleDegrees=33.5; rc.geometry=rg; r.mask.components.append(rc);

    d.layers=QVector<AdjustmentLayer>{b,l,r};
    return d;
}
ProjectPhoto photoOf(const PhotoDocument &doc,PhotoMark mark){
    ProjectPhoto p; p.document=std::make_shared<const PhotoDocument>(doc); p.mark=mark; return p;
}

// Build a rich recovery state with 3 known photos (all marks) and interleaved
// pending imports. Pending paths have no document; known paths have documents.
// order is interleaved; known subsequence must be preserved.
RecoveryState richRecovery(const QString &dir){
    const QString s1=QDir(dir).absoluteFilePath(QStringLiteral("a.txt"));
    const QString s2=QDir(dir).absoluteFilePath(QStringLiteral("b.txt"));
    const QString s3=QDir(dir).absoluteFilePath(QStringLiteral("c.txt"));
    writeBytes(s1,QByteArray("alpha-content"));
    writeBytes(s2,QByteArray("beta-content"));
    writeBytes(s3,QByteArray("gamma-content"));

    DocumentSnapshot d1=std::make_shared<const PhotoDocument>(
        richDoc(QStringLiteral("asset-1"),s1,shaOf(QByteArray("alpha-content")),0.4));
    DocumentSnapshot d2=std::make_shared<const PhotoDocument>(
        richDoc(QStringLiteral("asset-2"),s2,shaOf(QByteArray("beta-content")),-0.3));
    DocumentSnapshot d3=std::make_shared<const PhotoDocument>(
        richDoc(QStringLiteral("asset-3"),s3,shaOf(QByteArray("gamma-content")),1.5));

    ProjectState ps;
    ps.id=QStringLiteral("proj-rich");
    ps.name=QStringLiteral("样例 Sample");
    ps.revision=7;
    ps.photos=QVector<ProjectPhoto>{
        ProjectPhoto{d1,PhotoMark::Keep},
        ProjectPhoto{d2,PhotoMark::Reject},
        ProjectPhoto{d3,PhotoMark::Unmarked}
    };
    ps.activeAssetId=QStringLiteral("asset-2");

    const QString p1=QDir(dir).absoluteFilePath(QStringLiteral("p1.txt"));
    const QString p2=QDir(dir).absoluteFilePath(QStringLiteral("p2.txt"));

    RecoveryState st;
    st.project=std::make_shared<const ProjectState>(ps);
    st.order={s1,p1,s2,p2,s3};
    st.pending=QVector<PendingImport>{
        PendingImport{p1,QStringLiteral("pending-error-1")},
        PendingImport{p2,QStringLiteral("pending-error-2")}
    };
    st.activePath=s2;
    st.explicitPath=QDir(dir).absoluteFilePath(QStringLiteral("explicit.json"));
    st.explicitHash=shaOf("explicit");
    writeBytes(st.explicitPath,"explicit");
    return st;
}


RecoveryState changedProject(RecoveryState state,const std::function<void(ProjectState &)> &change){
    auto project=std::make_shared<ProjectState>(*state.project);change(*project);state.project=project;return state;
}
} // namespace
class RecoveryStoreTests:public QObject {
    Q_OBJECT
private slots:
    void roundTripAndMissingSources(){
        QTemporaryDir dir;QVERIFY(dir.isValid());const auto state=richRecovery(dir.path());QString error,hash;
        const auto frozen=freezeRecovery(state,error);QVERIFY2(frozen,qPrintable(error));
        const auto path=dir.filePath(QStringLiteral("中文恢复.json"));
        QVERIFY2(saveRecovery(path,frozen,hash,error),qPrintable(error));QCOMPARE(hash,shaOf(readBytes(path)));
        RecoveryState loaded;QString actual;QVERIFY2(loadRecovery(path,loaded,actual,error),qPrintable(error));
        QCOMPARE(actual,hash);QVERIFY(loaded==state);QVERIFY(!readBytes(state.order.first()).isEmpty());
        QVERIFY(QFile::remove(state.order.first()));QVERIFY(loadRecovery(path,loaded,actual,error));QVERIFY(loaded==state);
        const auto before=readBytes(path);QVERIFY(saveRecovery(path,frozen,actual,error,hash));QCOMPARE(actual,hash);QCOMPARE(readBytes(path),before);
        auto newer=changedProject(state,[](auto &p){++p.revision;p.name="updated";});
        QVERIFY(saveRecovery(path,freezeRecovery(newer,error),actual,error,hash));QVERIFY(actual!=hash);
        QVERIFY(loadRecovery(path,loaded,hash,error));QVERIFY(loaded==newer);QCOMPARE(hash,actual);
    }
    void emptyAndPendingOnly(){
        QTemporaryDir dir;QVERIFY(dir.isValid());ProjectState project;project.id="empty";
        RecoveryState state;state.project=std::make_shared<const ProjectState>(project);QString error,hash;
        for(int pending=0;pending<2;++pending){
            if(pending){state.pending={{dir.filePath("unparsed.cr3"),QString()}};state.order={state.pending[0].path};state.activePath=state.order[0];}
            const auto json=recoveryJson(state,error);QVERIFY2(error.isEmpty(),qPrintable(error));RecoveryState out;
            QVERIFY(recoveryFromJson(json,out,error));QVERIFY(out==state);
            QVERIFY(saveRecovery(dir.filePath(QString::number(pending)+".json"),freezeRecovery(state,error),hash,error));
        }
    }
    void freezeProtectsMutableAliases(){
        QTemporaryDir dir;QVERIFY(dir.isValid());auto state=richRecovery(dir.path());
        auto doc=std::make_shared<PhotoDocument>(*state.project->photos[0].document);
        auto project=std::make_shared<ProjectState>(*state.project);project->photos[0].document=doc;state.project=project;
        QString error;const auto frozen=freezeRecovery(state,error);QVERIFY2(frozen,qPrintable(error));const auto expected=*frozen;
        doc->global.exposure=2;std::get<BrushGeometry>(doc->layers[0].mask.components[0].geometry).points[0]={999,888};
        project->name="changed";project->photos.clear();state.pending.clear();state.order.clear();
        QVERIFY(*frozen==expected);QCOMPARE(frozen->project->photos.size(),3);
    }
    void invalidStateRejected(){
        QTemporaryDir dir;QVERIFY(dir.isValid());const auto valid=richRecovery(dir.path());QString error;
        QVERIFY(validRecovery(valid,error));QList<RecoveryState> cases;auto bad=valid;
        bad.order.removeLast();cases<<bad;bad=valid;bad.order<<bad.order.first();cases<<bad;
        bad=valid;bad.pending<<PendingImport{bad.order.first(),"overlap"};cases<<bad;
        bad=valid;bad.pending[0].path="relative.cr3";cases<<bad;
        bad=valid;bad.pending[0].error=QString(4097,'x');cases<<bad;
        bad=valid;bad.activePath=bad.pending[0].path;cases<<bad;
        bad=valid;bad.activePath=dir.filePath("unknown");cases<<bad;
        bad=valid;bad.explicitHash="deadbeef";cases<<bad;
        bad=valid;bad.explicitPath=bad.order.first();cases<<bad;
        cases<<changedProject(valid,[](auto &p){p.revision=0;});
        cases<<changedProject(valid,[](auto &p){auto d=std::make_shared<PhotoDocument>(*p.photos[0].document);d->asset.sourceHash.clear();p.photos[0].document=d;});
        bad=valid;bad.order.swapItemsAt(0,2);cases<<bad;
        bad=valid;bad.pending.fill({dir.filePath("pending"),{}},maximumProjectPhotos+1);cases<<bad;
#ifdef Q_OS_WIN
        bad=valid;bad.order<<bad.order.first().toUpper();cases<<bad;
#endif
        for(const auto &candidate:cases){QVERIFY(!validRecovery(candidate,error));QVERIFY(!error.isEmpty());QVERIFY(!freezeRecovery(candidate,error));}
    }
    void strictJsonAndAtomicOutput(){
        QTemporaryDir dir;QVERIFY(dir.isValid());const auto state=richRecovery(dir.path());QString error;
        const auto good=recoveryJson(state,error);QVERIFY(error.isEmpty());QList<QJsonObject> cases;auto bad=good;
        bad.insert("unknown",1);cases<<bad;bad=good;bad.remove("pending");cases<<bad;
        for(const auto &value:QList<QJsonValue>{2,1.5,1e300,"1"}){bad=good;bad["version"]=value;cases<<bad;}
        for(const auto &value:QList<QJsonValue>{0.5,1e300,"keep"}){
            bad=good;auto p=bad["project"].toObject();auto a=p["photos"].toArray();auto photo=a[0].toObject();
            photo["mark"]=value;a[0]=photo;p["photos"]=a;bad["project"]=p;cases<<bad;
        }
        for(const auto &revision:{"0","01","18446744073709551616"}){
            bad=good;auto p=bad["project"].toObject();p["revision"]=revision;bad["project"]=p;cases<<bad;
        }
        bad=good;auto p=bad["project"].toObject();p["extra"]=true;bad["project"]=p;cases<<bad;
        bad=good;auto a=bad["pending"].toArray();auto pending=a[0].toObject();pending["extra"]=true;a[0]=pending;bad["pending"]=a;cases<<bad;
        bad=good;p=bad["project"].toObject();a=p["photos"].toArray();auto photo=a[0].toObject();photo["extra"]=true;a[0]=photo;p["photos"]=a;bad["project"]=p;cases<<bad;
        for(const auto &json:cases){auto out=state;QVERIFY(!recoveryFromJson(json,out,error));QVERIFY(!error.isEmpty());QVERIFY(out==state);}
    }
    void rejectedSavePreservesOldBytesAndHash(){
        QTemporaryDir dir;QVERIFY(dir.isValid());const auto state=richRecovery(dir.path());QString error,hash;
        const auto frozen=freezeRecovery(state,error);QVERIFY(frozen);const auto path=dir.filePath("rec.json");
        QVERIFY(saveRecovery(path,frozen,hash,error));const auto old=readBytes(path);
        QList<RecoveryState> cases;
        cases<<changedProject(state,[](auto &p){p.id="other";++p.revision;});
        cases<<changedProject(state,[](auto &p){--p.revision;});
        auto changed=state;changed.pending[0].error="different same revision";cases<<changed;
        for(const auto &candidate:cases){
            QString outHash="sentinel";const auto snapshot=freezeRecovery(candidate,error);QVERIFY(snapshot);
            QVERIFY(!saveRecovery(path,snapshot,outHash,error,hash));QCOMPARE(outHash,QString("sentinel"));QCOMPARE(readBytes(path),old);
        }
        for(const auto &expected:QStringList{QString(),"wronghash"}){
            QString outHash="sentinel";QVERIFY(!saveRecovery(path,frozen,outHash,error,expected));QCOMPARE(outHash,QString("sentinel"));QCOMPARE(readBytes(path),old);
        }
        const auto foreign=dir.filePath("foreign.json");QVERIFY(writeBytes(foreign,"foreign"));
        QString outHash="sentinel";QVERIFY(!saveRecovery(foreign,frozen,outHash,error,shaOf("foreign")));QCOMPARE(readBytes(foreign),QByteArray("foreign"));QCOMPARE(outHash,QString("sentinel"));
        QVERIFY(!saveRecovery(dir.filePath("missing.json"),frozen,outHash,error,hash));QVERIFY(!QFile::exists(dir.filePath("missing.json")));
    }
    void sourceExplicitAndCompanionProtected(){
        QTemporaryDir dir;QVERIFY(dir.isValid());auto state=richRecovery(dir.path());QString error,hash="sentinel";
        auto snapshot=freezeRecovery(state,error);QVERIFY(snapshot);
        for(const auto &path:QStringList{state.order[0],state.pending[0].path,state.explicitPath}){
            const auto old=readBytes(path);const bool existed=QFile::exists(path);
            QVERIFY(!saveRecovery(path,snapshot,hash,error));QCOMPARE(readBytes(path),old);QCOMPARE(QFile::exists(path),existed);QCOMPARE(hash,QString("sentinel"));
        }
        const auto path=dir.filePath("companion.json"),companion=path+".luminas.lock";
        state.pending<<PendingImport{companion,{}};state.order<<companion;snapshot=freezeRecovery(state,error);QVERIFY(snapshot);
        QVERIFY(!saveRecovery(path,snapshot,hash,error));QVERIFY(!QFile::exists(companion));QVERIFY(!QFile::exists(path));
        state=richRecovery(dir.path());state.explicitPath=companion;state.explicitHash=shaOf("explicit lock");
        QVERIFY(writeBytes(companion,"explicit lock"));snapshot=freezeRecovery(state,error);QVERIFY(snapshot);
        QVERIFY(!saveRecovery(path,snapshot,hash,error));QCOMPARE(readBytes(companion),QByteArray("explicit lock"));QVERIFY(!QFile::exists(path));
    }
    void lockCancelAndMissingParent(){
        QTemporaryDir dir;QVERIFY(dir.isValid());const auto state=richRecovery(dir.path());QString error,hash;
        const auto frozen=freezeRecovery(state,error);QVERIFY(frozen);const auto path=dir.filePath("rec.json");
        QVERIFY(saveRecovery(path,frozen,hash,error));const auto old=readBytes(path);
        QLockFile lock(path+".luminas.lock");QVERIFY(lock.tryLock(0));QString output="sentinel";
        QVERIFY(!saveRecovery(path,frozen,output,error,hash));QCOMPARE(output,QString("sentinel"));QCOMPARE(readBytes(path),old);lock.unlock();
        CancellationSource canceled;canceled.cancel();
        QVERIFY(!saveRecovery(path,frozen,output,error,hash,canceled.token()));QCOMPARE(output,QString("sentinel"));QCOMPARE(readBytes(path),old);
        auto loaded=state;QVERIFY(!loadRecovery(path,loaded,output,error,canceled.token()));QVERIFY(loaded==state);QCOMPARE(output,QString("sentinel"));
        QVERIFY(!saveRecovery(dir.filePath("missing/sub/rec.json"),frozen,output,error));QVERIFY(!QDir(dir.filePath("missing")).exists());
    }
    void brokenOrOversizedLoadPreservesOutput(){
        QTemporaryDir dir;QVERIFY(dir.isValid());const auto state=richRecovery(dir.path());QString error,hash="sentinel";
        const auto path=dir.filePath("bad.json");auto json=recoveryJson(state,error);json["version"]=2;
        for(const auto &bytes:QList<QByteArray>{"not json","{\"truncated\":",QJsonDocument(json).toJson(QJsonDocument::Compact)}){
            QVERIFY(writeBytes(path,bytes));auto out=state;QVERIFY(!loadRecovery(path,out,hash,error));QVERIFY(out==state);QCOMPARE(hash,QString("sentinel"));
        }
        QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));QVERIFY(file.resize(maximumProjectBytes+1));file.close();
        auto out=state;QVERIFY(!loadRecovery(path,out,hash,error));QVERIFY(out==state);QCOMPARE(hash,QString("sentinel"));
        QVERIFY(!loadRecovery(dir.path(),out,hash,error));QVERIFY(out==state);
    }
#ifdef Q_OS_WIN
    void heldTargetFailurePreservesPreviousRecovery(){
        QTemporaryDir dir;QVERIFY(dir.isValid());const auto state=richRecovery(dir.path());QString error,hash;
        const auto path=dir.filePath("rec.json");QVERIFY(saveRecovery(path,freezeRecovery(state,error),hash,error));const auto old=readBytes(path);
        struct Handle {HANDLE value;~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}};
        Handle held{CreateFileW(reinterpret_cast<const wchar_t *>(path.utf16()),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr)};
        QVERIFY(held.value!=INVALID_HANDLE_VALUE);RecoveryState loaded;QString output="sentinel";
        QVERIFY(loadRecovery(path,loaded,output,error));QVERIFY(loaded==state);
        const auto newer=changedProject(state,[](auto &p){++p.revision;p.name="new";});output="sentinel";
        QVERIFY(!saveRecovery(path,freezeRecovery(newer,error),output,error,hash));QCOMPARE(output,QString("sentinel"));QCOMPARE(readBytes(path),old);
    }
#endif
};
QTEST_GUILESS_MAIN(RecoveryStoreTests)
#include "recovery_store_tests.moc"

