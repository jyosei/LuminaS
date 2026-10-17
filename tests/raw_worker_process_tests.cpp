#include <QtTest>
#include <QTemporaryDir>
#include <QProcess>
#include <QProcessEnvironment>
#include <QFile>
#include <QDir>
#include <QCryptographicHash>
#include <QUuid>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "raw/worker_protocol.h"
#include "raw/worker_payload.h"
#include "raw/source_kernel.h"
#include "render/cpu_renderer.h"
using namespace RawWorker;
namespace {
QString sha(const QByteArray &data){return QString::fromLatin1(QCryptographicHash::hash(data,QCryptographicHash::Sha256).toHex());}
QByteArray fileBytes(const QString &path){QFile f(path);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray{};}
QByteArray pixels(const QImage &image){QByteArray data;for(int y=0;y<image.height();++y)data.append(reinterpret_cast<const char *>(image.constScanLine(y)),image.width()*16);return data;}
struct Fixture {
    QTemporaryDir directory;Request request;
    bool init(QSize size={96,64}){
        if(!directory.isValid())return false;request.id=QUuid::createUuid().toString(QUuid::WithoutBraces);request.token=QUuid::createUuid().toString(QUuid::WithoutBraces);
        request.sourcePath=directory.filePath("source.jpg");QImage image(size,QImage::Format_RGB32);image.fill(QColor(45,90,140));
        if(!image.save(request.sourcePath,"JPEG",98))return false;request.sourceHash=sha(fileBytes(request.sourcePath));return true;
    }
};
struct Exchange {QList<QJsonObject> messages;QString error;QByteArray diagnostics;int exitCode=-1;bool timeout=false;};
QByteArray wire(const Request &request){QString error;const auto json=requestJson(request,error);return error.isEmpty()?encodeLine(json,error):QByteArray{};}
QJsonObject terminal(const Exchange &exchange,const QString &type){for(const auto &message:exchange.messages)if(message["type"].toString()==type)return message;return {};}
Exchange run(const Fixture &fixture,const QByteArray &input,bool closeInput=false,int timeoutMs=20000){
    Exchange result;QProcess process;
    struct Guard {QProcess &process;~Guard(){if(process.state()!=QProcess::NotRunning){process.kill();process.waitForFinished(5000);}}}guard{process};
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args){args->flags|=CREATE_NO_WINDOW;});
    process.setProcessEnvironment(QProcessEnvironment::systemEnvironment());
    process.start(QCoreApplication::applicationDirPath()+"/LuminaS-raw-worker.exe",{"--session",fixture.directory.path()});
    if(!process.waitForStarted(5000)){result.error=process.errorString();return result;}
    QElapsedTimer clock;clock.start();QByteArray pending;
    const auto pump=[&]{
        const auto stderrData=process.readAllStandardError();result.diagnostics.append(stderrData.left(qMax<qsizetype>(0,16384-result.diagnostics.size())));
        pending+=process.readAllStandardOutput();
        for(;;){
            const auto newline=pending.indexOf('\n');if(newline<0)break;
            auto line=pending.left(newline);pending.remove(0,newline+1);if(line.endsWith('\r'))line.chop(1);QJsonObject message;QString error;
            if(!decodeLine(line,message,error)){result.error=error;return false;}
            if(result.messages.size()>=10000){result.error="too many worker messages";return false;}result.messages<<message;
        }
        if(pending.size()>maximumLineBytes+1){result.error="worker output line too long";return false;}return true;
    };
    while(result.messages.isEmpty()&&process.state()!=QProcess::NotRunning&&clock.elapsed()<timeoutMs){process.waitForReadyRead(20);if(!pump())return result;}
    QString error;if(result.messages.isEmpty()||!validHello(result.messages[0],error)){result.error=error.isEmpty()?QStringLiteral("missing worker hello"):error;return result;}
    if(process.write(input)!=input.size()){result.error="request write failed";return result;}
    if(closeInput)process.closeWriteChannel();
    while(process.state()!=QProcess::NotRunning&&clock.elapsed()<timeoutMs){process.waitForReadyRead(20);if(!pump())return result;}
    if(process.state()!=QProcess::NotRunning){result.timeout=true;result.error="worker timeout";return result;}
    if(!pump())return result;if(!pending.isEmpty()){result.error="worker output truncated";return result;}
    result.exitCode=process.exitCode();return result;
}
}
class RawWorkerProcessTests:public QObject {
    Q_OBJECT
private slots:
    void decodeExitsWithInputStillOpen(){
        Fixture f;QVERIFY(f.init());const auto expected=readSourceKernel(f.request.sourcePath);QVERIFY2(expected.source,qPrintable(expected.error));
        const auto result=run(f,wire(f.request));QVERIFY2(result.error.isEmpty(),qPrintable(result.error));
        if(result.exitCode!=0)qWarning()<<"exit"<<result.exitCode<<"done"<<!terminal(result,"done").isEmpty()<<"error"<<terminal(result,"error")<<"stderr"<<result.diagnostics;
        QCOMPARE(result.exitCode,0);QVERIFY(!result.timeout);
        const auto done=terminal(result,"done");QVERIFY(!done.isEmpty());const auto received=readPayload(f.directory.path(),f.request,done);QVERIFY2(received.source,qPrintable(received.error));
        QCOMPARE(pixels(received.source->linear),pixels(expected.source->linear));QCOMPARE(received.source->originalSize,expected.source->originalSize);QCOMPARE(sha(fileBytes(f.request.sourcePath)),f.request.sourceHash);
    }
    void prepareUsesExistingGeometryKernel(){
        Fixture f;QVERIFY(f.init({320,240}));f.request.operation=Operation::Prepare;f.request.longEdge=3840;f.request.geometry.crop={.1,.15,.8,.7};f.request.geometry.angle=5;
        f.request.geometry.perspective={.02,-.03};f.request.geometry.distortion=.1;f.request.geometry.chromaticAberration={.3,-.2};f.request.geometry.flipHorizontal=true;
        const auto source=readSourceKernel(f.request.sourcePath);QVERIFY2(source.source,qPrintable(source.error));QString error;
        const auto expected=renderLinear(*source.source,f.request.geometry,3840,error);QVERIFY2(!expected.isNull(),qPrintable(error));
        const auto result=run(f,wire(f.request));QVERIFY2(result.error.isEmpty(),qPrintable(result.error));
        if(result.exitCode!=0)qWarning()<<"exit"<<result.exitCode<<"done"<<!terminal(result,"done").isEmpty()<<"error"<<terminal(result,"error")<<"stderr"<<result.diagnostics;
        QCOMPARE(result.exitCode,0);
        const auto received=readPayload(f.directory.path(),f.request,terminal(result,"done"));QVERIFY2(received.source,qPrintable(received.error));QCOMPARE(pixels(received.source->linear),pixels(expected));
        QCOMPARE(received.source->originalSize,QSize(320,240));QCOMPARE(received.source->sourcePixelsPerPixelX,320*.8/expected.width());
    }
    void cancelAndBadControl(){
        for(int mode=0;mode<3;++mode){
            Fixture f;QVERIFY(f.init({3840,2160}));auto input=wire(f.request);QString error;
            if(mode<2){auto cancel=cancelJson(f.request);if(mode==1)cancel["id"]=QUuid::createUuid().toString(QUuid::WithoutBraces);input+=encodeLine(cancel,error);QVERIFY(error.isEmpty());}
            const auto result=run(f,input,mode==2);QVERIFY2(result.error.isEmpty(),qPrintable(result.error));QVERIFY(!result.timeout);QVERIFY(result.exitCode!=0);
            QVERIFY(terminal(result,"done").isEmpty());const auto message=terminal(result,"error");QVERIFY(!message.isEmpty());
            if(mode==0)QCOMPARE(message["code"].toString(),QStringLiteral("cancelled"));
            QVERIFY(!QFile::exists(f.directory.filePath(f.request.token+".frame")));QCOMPARE(sha(fileBytes(f.request.sourcePath)),f.request.sourceHash);
        }
    }
    void malformedInitialRequests(){
        for(int mode=0;mode<3;++mode){
            Fixture f;QVERIFY(f.init());QByteArray input;QString error;
            if(mode==0)input="{bad}\n";
            else if(mode==1){auto json=requestJson(f.request,error);QVERIFY(error.isEmpty());json["protocol"]=99;input=encodeLine(json,error);QVERIFY(error.isEmpty());}
            else input=QByteArray(maximumLineBytes+2,'x')+'\n';
            const auto result=run(f,input,true);QVERIFY2(result.error.isEmpty(),qPrintable(result.error));QVERIFY(!result.timeout);QVERIFY(result.exitCode!=0);
            QVERIFY(terminal(result,"done").isEmpty());QVERIFY(!terminal(result,"error").isEmpty());QCOMPARE(sha(fileBytes(f.request.sourcePath)),f.request.sourceHash);
        }
    }
    void changedSourceAndExistingOutput(){
        for(int mode=0;mode<2;++mode){
            Fixture f;QVERIFY(f.init());const auto originalHash=f.request.sourceHash;const auto target=f.directory.filePath(f.request.token+".frame");
            if(mode==0)f.request.sourceHash=QString(64,'0');
            else {QFile file(target);QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write("existing"),8);}
            // Keep stdin open: premature EOF would test cancellation instead.
            const auto result=run(f,wire(f.request));QVERIFY2(result.error.isEmpty(),qPrintable(result.error));QVERIFY(!result.timeout);QVERIFY(result.exitCode!=0);
            QVERIFY(terminal(result,"done").isEmpty());const auto message=terminal(result,"error");QVERIFY(!message.isEmpty());
            QCOMPARE(message["code"].toString(),mode==0?QStringLiteral("source-changed"):QStringLiteral("write"));
            if(mode==0)QVERIFY(!QFile::exists(target));else QCOMPARE(fileBytes(target),QByteArray("existing"));
            QCOMPARE(sha(fileBytes(f.request.sourcePath)),originalHash);
        }
    }
    void actualSampleKernelEquivalence(){
        const auto sample=qEnvironmentVariable("LUMINA_WORKER_SAMPLE");if(sample.isEmpty())QSKIP("Set LUMINA_WORKER_SAMPLE for private kernel/worker equivalence.");
        QVERIFY(QFileInfo(sample).isAbsolute());QVERIFY(QFile::exists(sample));const auto sourceHash=sha(fileBytes(sample));QVERIFY(!sourceHash.isEmpty());
        const auto imageHash=[](const QImage &image){QCryptographicHash hash(QCryptographicHash::Sha256);for(int y=0;y<image.height();++y)hash.addData(QByteArrayView(reinterpret_cast<const char *>(image.constScanLine(y)),image.width()*16));return QString::fromLatin1(hash.result().toHex());};
        for(int mode=0;mode<2;++mode){
            Fixture f;QVERIFY(f.init());f.request.sourcePath=sample;f.request.sourceHash=sourceHash;
            if(mode==1){f.request.operation=Operation::Prepare;f.request.longEdge=3840;f.request.geometry.crop={.1,.1,.8,.8};f.request.geometry.angle=5;
                if(QFileInfo(sample).suffix().compare("cr3",Qt::CaseInsensitive)==0){f.request.warmth=.25;f.request.tint=-.1;}}
            QString expectedHash;QSize expectedSize,originalSize;std::array<float,9> matrix{};std::array<double,4> wb{};
            {
                const auto source=readSourceKernel(sample,f.request.warmth,f.request.tint);QVERIFY2(source.source,qPrintable(source.error));QString error;
                const auto image=mode==0?source.source->linear:renderLinear(*source.source,f.request.geometry,3840,error);QVERIFY2(!image.isNull(),qPrintable(error));
                expectedHash=imageHash(image);expectedSize=image.size();originalSize=source.source->originalSize;matrix=source.source->cameraToWork;wb=source.source->cameraWB;
            } // Release the reference frame before the child allocates a full source.
            const auto result=run(f,wire(f.request),false,180000);QVERIFY2(result.error.isEmpty(),qPrintable(result.error+QString::fromUtf8(result.diagnostics)));QCOMPARE(result.exitCode,0);
            const auto done=terminal(result,"done");QVERIFY(!done.isEmpty());const auto received=readPayload(f.directory.path(),f.request,done);QVERIFY2(received.source,qPrintable(received.error));
            QCOMPARE(received.source->linear.size(),expectedSize);QCOMPARE(received.source->originalSize,originalSize);QCOMPARE(imageHash(received.source->linear),expectedHash);
            QCOMPARE(received.source->cameraToWork,matrix);QCOMPARE(received.source->cameraWB,wb);QCOMPARE(sha(fileBytes(sample)),sourceHash);
            qInfo()<<"worker kernel equivalence"<<QFileInfo(sample).fileName()<<"operation"<<mode<<"sourceSha"<<sourceHash<<"pixelsSha"<<expectedHash<<"outputSize"<<expectedSize;
        }
    }
};
QTEST_GUILESS_MAIN(RawWorkerProcessTests)
#include "raw_worker_process_tests.moc"
