#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QUuid>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonArray>
#include <QtEndian>
#include <limits>
#include "raw/worker_protocol.h"
#include "raw/worker_payload.h"
using namespace RawWorker;
namespace {
QString sha(QByteArrayView data){return QString::fromLatin1(QCryptographicHash::hash(data,QCryptographicHash::Sha256).toHex());}
bool bytes(const QString &path,QByteArrayView data){QFile f(path);return f.open(QIODevice::WriteOnly|QIODevice::Truncate)&&f.write(data.data(),data.size())==data.size()&&f.flush();}
QByteArray bytes(const QString &path){QFile f(path);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray{};}
Request request(const QString &root){Request r;r.id=QUuid::createUuid().toString(QUuid::WithoutBraces);r.token=QUuid::createUuid().toString(QUuid::WithoutBraces);r.sourcePath=QDir(root).filePath("source.CR3");r.sourceHash=sha(QByteArrayView("fixture",7));return r;}
SourceFrame frame(const Request &r,QSize size={16,12}){
    SourceFrame f;f.path=r.sourcePath;f.sha256=r.sourceHash;f.originalSize={16,12};f.raw=true;f.rawWarmth=r.warmth;f.rawTint=r.tint;
    f.cameraToWork={1,0,0,0,1,0,0,0,1};f.cameraWB={1,1,1,1};f.facts={{"fixture","synthetic"},{"width",16},{"height",12}};
    f.linear=QImage(size,QImage::Format_RGBA32FPx4);
    for(int y=0;y<size.height();++y){auto *row=reinterpret_cast<float *>(f.linear.scanLine(y));for(int x=0;x<size.width();++x){row[x*4]=-.25f;row[x*4+1]=1.5f;row[x*4+2]=float(x+y)/32;row[x*4+3]=1;}}
    return f;
}
QByteArray pixels(const QImage &image){QByteArray out;for(int y=0;y<image.height();++y)out.append(reinterpret_cast<const char *>(image.constScanLine(y)),image.width()*16);return out;}
QString payloadPath(const QString &root,const Request &r){return QDir(root).filePath(r.token+".frame");}
}
class RawWorkerTransportTests:public QObject {
    Q_OBJECT
private slots:
    void handshakeAndCanonicalControl(){
        QString error;auto h=hello();QVERIFY(validHello(h,error));h["protocol"]=2;QVERIFY(!validHello(h,error));h=hello();h["extra"]=true;QVERIFY(!validHello(h,error));
        QTemporaryDir dir;QVERIFY(dir.isValid());auto r=request(dir.path());auto json=requestJson(r,error);QVERIFY2(error.isEmpty(),qPrintable(error));
        const auto wire=encodeLine(json,error);QVERIFY(wire.endsWith('\n'));QJsonObject parsed;QVERIFY(decodeLine(QByteArrayView(wire.constData(),wire.size()-1),parsed,error));Request back;
        QVERIFY(requestFromJson(parsed,back,error));QCOMPARE(requestJson(back,error),json);QCOMPARE(requestHash(back,error),requestHash(r,error));
        QVERIFY(validCancel(cancelJson(r),r,error));auto cancel=cancelJson(r);cancel["id"]=QUuid::createUuid().toString(QUuid::WithoutBraces);QVERIFY(!validCancel(cancel,r,error));
        const auto sentinel=QJsonObject{{"keep",true}};
        for(const auto &bad:QList<QByteArray>{QByteArray{},QByteArray("[1]"),QByteArray("{bad}"),QByteArray("{\"x\":1,\"x\":1}"),QByteArray("{ \"x\": 1 }"),QByteArray(maximumLineBytes+1,'x')}){
            parsed=sentinel;QVERIFY(!decodeLine(bad,parsed,error));QCOMPARE(parsed,sentinel);
        }
    }
    void strictRequestsPreserveOutput(){
        QTemporaryDir dir;QVERIFY(dir.isValid());const auto r=request(dir.path());QString error;const auto base=requestJson(r,error);
        const QList<QPair<QString,QJsonValue>> changes={{"protocol",true},{"id","../bad"},{"token","../escape"},{"sourcePath","relative.CR3"},{"sourceHash",QString(64,'A')},{"operation","other"},{"longEdge",1e300},{"warmth",2},{"tint","0"}};
        for(const auto &change:changes){auto bad=base;bad[change.first]=change.second;Request out=r;QVERIFY(!requestFromJson(bad,out,error));QCOMPARE(requestJson(out,error),base);}
        auto bad=base;bad["unknown"]=1;Request out=r;QVERIFY(!requestFromJson(bad,out,error));
        bad=base;auto geometry=bad["geometry"].toObject();geometry["exposure"]=.5;bad["geometry"]=geometry;QVERIFY(!requestFromJson(bad,out,error));
        auto invalid=r;invalid.geometry.exposure=.5;QVERIFY(!validRequest(invalid,error));invalid=r;invalid.geometry.crop={.1,.1,.8,.8};QVERIFY(!validRequest(invalid,error));
        auto prepared=r;prepared.operation=Operation::Prepare;prepared.longEdge=3840;prepared.geometry.crop={.25,.25,.5,.5};QVERIFY(validRequest(prepared,error));
        const auto preparedJson=requestJson(prepared,error);QVERIFY(requestFromJson(preparedJson,out,error));QCOMPARE(requestJson(out,error),preparedJson);
        prepared.longEdge=1920;QVERIFY(!validRequest(prepared,error));
    }
    void actualPayloadPreservesFloatAndFacts(){
        QTemporaryDir dir;QVERIFY(dir.isValid());auto r=request(dir.path());QVERIFY(bytes(r.sourcePath,QByteArrayView("fixture",7)));auto f=frame(r);QString error;QJsonObject done;
        QVERIFY2(writePayload(dir.path(),r,f,done,error),qPrintable(error));const auto result=readPayload(dir.path(),r,done);QVERIFY2(result.source,qPrintable(result.error));
        QCOMPARE(pixels(result.source->linear),pixels(f.linear));QCOMPARE(result.source->sha256,f.sha256);QCOMPARE(result.source->cameraToWork,f.cameraToWork);QCOMPARE(result.source->cameraWB,f.cameraWB);QCOMPARE(result.source->facts,f.facts);
        QCOMPARE(reinterpret_cast<const float *>(result.source->linear.constScanLine(0))[0],-.25f);QCOMPARE(reinterpret_cast<const float *>(result.source->linear.constScanLine(0))[1],1.5f);
        QCOMPARE(sha(bytes(r.sourcePath)),r.sourceHash);QCOMPARE(done["pixelSha256"].toString(),sha(pixels(f.linear)));
        QCOMPARE(QFileInfo(payloadPath(dir.path(),r)).size(),done["fileBytes"].toString().toLongLong());
        QCOMPARE(QDir(dir.path()).entryList({".raw-stage-*"},QDir::Files|QDir::Hidden).size(),0);
    }
    void noOverwriteAndCancellation(){
        QTemporaryDir dir;QVERIFY(dir.isValid());const auto r=request(dir.path());const auto f=frame(r);const auto target=payloadPath(dir.path(),r);QVERIFY(bytes(target,QByteArrayView("unknown",7)));
        QString error;const QJsonObject sentinel{{"keep",true}};auto done=sentinel;QVERIFY(!writePayload(dir.path(),r,f,done,error));QCOMPARE(bytes(target),QByteArray("unknown"));QCOMPARE(done,sentinel);
        auto other=r;other.token=QUuid::createUuid().toString(QUuid::WithoutBraces);CancellationSource stop;stop.cancel();QVERIFY(!writePayload(dir.path(),other,f,done,error,stop.token()));QVERIFY(!QFileInfo::exists(payloadPath(dir.path(),other)));QCOMPARE(done,sentinel);
        const auto absent=dir.filePath("not-created");QVERIFY(!writePayload(absent,r,f,done,error));QVERIFY(!QFileInfo::exists(absent));
        other.token="../escape";QVERIFY(!writePayload(dir.path(),other,f,done,error));QCOMPARE(done,sentinel);
    }
    void corruptPayloadsRejected(){
        QTemporaryDir dir;QVERIFY(dir.isValid());const auto r=request(dir.path());const auto f=frame(r);QString error;QJsonObject done;QVERIFY2(writePayload(dir.path(),r,f,done,error),qPrintable(error));
        const auto path=payloadPath(dir.path(),r);const auto original=bytes(path);QVERIFY(!original.isEmpty());
        auto bad=done;bad["fileSha256"]=QString(64,'0');QVERIFY(!readPayload(dir.path(),r,bad).source);
        bad=done;bad["id"]=QUuid::createUuid().toString(QUuid::WithoutBraces);QVERIFY(!readPayload(dir.path(),r,bad).source);
        bad=done;auto meta=bad["frame"].toObject();meta["warmth"]=.5;bad["frame"]=meta;QVERIFY(!readPayload(dir.path(),r,bad).source);
        bad=done;bad["fileBytes"]=double(original.size());QVERIFY(!readPayload(dir.path(),r,bad).source);
        QVERIFY(bytes(path,original.left(original.size()-1)));QVERIFY(!readPayload(dir.path(),r,done).source);
        QVERIFY(bytes(path,original+"x"));QVERIFY(!readPayload(dir.path(),r,done).source);
        auto giant=original;qToLittleEndian<quint32>(45000000,giant.data()+24);qToLittleEndian<quint32>(45000000,giant.data()+28);QVERIFY(bytes(path,giant));QVERIFY(!readPayload(dir.path(),r,done).source);
        QVERIFY(bytes(path,original));CancellationSource stop;stop.cancel();QVERIFY(!readPayload(dir.path(),r,done,stop.token()).source);
    }
    void nonFiniteAndContradictoryMetadata(){
        QTemporaryDir dir;QVERIFY(dir.isValid());const auto r=request(dir.path());const auto base=frame(r);QString error;
        for(const auto value:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}){
            auto f=base;reinterpret_cast<float *>(f.linear.bits())[0]=value;QJsonObject done;QVERIFY(!writePayload(dir.path(),r,f,done,error));QVERIFY(done.isEmpty());QVERIFY(!validFrame(r,f,error));
        }
        auto f=base;reinterpret_cast<float *>(f.linear.bits())[3]=2;QVERIFY(!validFrame(r,f,error));
        f=base;f.cameraWB[0]=0;QVERIFY(!validFrame(r,f,error));f=base;f.cameraToWork.fill(0);QVERIFY(!validFrame(r,f,error));
        f=base;f.facts["width"]=17;QVERIFY(!validFrame(r,f,error));f=base;f.sourcePixelsPerPixel=2;QVERIFY(!validFrame(r,f,error));
        f=base;f.linear=f.linear.convertToFormat(QImage::Format_RGBA8888);QVERIFY(!validFrame(r,f,error));
    }
    void forgedValidHashesDoNotHideNan(){
        QTemporaryDir dir;QVERIFY(dir.isValid());const auto r=request(dir.path());const auto f=frame(r);QString error;QJsonObject done;QVERIFY(writePayload(dir.path(),r,f,done,error));
        const auto path=payloadPath(dir.path(),r);auto file=bytes(path);const auto length=qFromLittleEndian<quint32>(file.constData()+12);const auto offset=32+qsizetype(length);
        qToLittleEndian<quint32>(0x7fc00000,file.data()+offset);auto meta=done["frame"].toObject();const auto pixelHash=sha(QByteArrayView(file.constData()+offset,file.size()-offset));meta["pixelSha256"]=pixelHash;
        const auto json=QJsonDocument(meta).toJson(QJsonDocument::Compact);QCOMPARE(json.size(),length);file.replace(32,length,json);
        done["frame"]=meta;done["pixelSha256"]=pixelHash;done["fileSha256"]=sha(file);QVERIFY(bytes(path,file));
        const auto result=readPayload(dir.path(),r,done);QVERIFY(!result.source);QVERIFY(result.error.contains("float"));
    }
    void preparedSmallSourceAndScale(){
        QTemporaryDir dir;QVERIFY(dir.isValid());auto r=request(dir.path());r.operation=Operation::Prepare;r.longEdge=3840;r.geometry.crop={.25,.25,.5,.5};
        auto f=frame(r,{8,6});f.sourcePixelsPerPixelX=f.sourcePixelsPerPixelY=1;QString error;QJsonObject done;
        QVERIFY2(writePayload(dir.path(),r,f,done,error),qPrintable(error));const auto result=readPayload(dir.path(),r,done);QVERIFY2(result.source,qPrintable(result.error));QCOMPARE(result.source->linear.size(),QSize(8,6));QCOMPARE(pixels(result.source->linear),pixels(f.linear));
        f.sourcePixelsPerPixelX=2;QVERIFY(!validFrame(r,f,error));f=frame(r,{16,12});f.sourcePixelsPerPixelX=f.sourcePixelsPerPixelY=1;QVERIFY(!validFrame(r,f,error));
    }
};
QTEST_APPLESS_MAIN(RawWorkerTransportTests)
#include "raw_worker_transport_tests.moc"
