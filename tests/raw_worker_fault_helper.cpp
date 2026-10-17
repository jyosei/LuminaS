// Test-only RAW worker fault fixture. Reads one Request line after Hello
// from stdin and emulates one named failure after emitting canonical control
// lines. It writes only inside its own --session directory: writePayload for
// valid payloads, plus a corruption of its own token frame on the badPayload
// exception path.
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QUuid>
#include <cstdio>
#include <cstring>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "raw/worker_protocol.h"
#include "raw/worker_payload.h"
#include "raw/source_kernel.h"
using namespace RawWorker;
namespace {
HANDLE control=INVALID_HANDLE_VALUE;
bool emitJson(const QJsonObject &object){
    if(control==INVALID_HANDLE_VALUE)return false;
    QString error;const auto line=encodeLine(object,error);
    if(!error.isEmpty()||line.isEmpty())return false;
    DWORD written=0;
    return WriteFile(control,line.constData(),DWORD(line.size()),&written,nullptr)&&written==DWORD(line.size());
}
QByteArray readLine(){
    QByteArray line;char buffer[4096];
    for(;;){
        if(!std::fgets(buffer,sizeof(buffer),stdin))return line;
        line.append(buffer,qsizetype(std::strlen(buffer)));
        if(line.endsWith('\n')||line.size()>maximumLineBytes+1)return line;
    }
}
void ignoreForTest(){QElapsedTimer local;local.start();Sleep(10000);}
[[noreturn]] void quit(int code){QCoreApplication::exit(code);QCoreApplication::quit();ExitProcess(DWORD(code));}
}
int main(int argc,char **argv){
    QCoreApplication app(argc,argv);
    control=GetStdHandle(STD_OUTPUT_HANDLE);
    const auto args=QCoreApplication::arguments();QString mode,session;
    if(args.size()==2&&args[1]=="--descendant"){Sleep(20000);return 0;}
    for(qsizetype i=1;i+1<args.size();i+=2){
        if(args[i]=="--mode")mode=args[i+1];
        else if(args[i]=="--session")session=args[i+1];
    }
    if(mode.isEmpty()||!QDir::isAbsolutePath(session)||!QFileInfo(session).isDir())return 10;
    if(mode=="badHello"){auto hello=RawWorker::hello();hello["protocol"]=99;return emitJson(hello)?0:20;}
    if(mode=="noHello"){ignoreForTest();return 21;}
    if(!emitJson(RawWorker::hello()))return 20;
    auto requestBytes=readLine();
    if(requestBytes.isEmpty()||!requestBytes.endsWith('\n')||requestBytes.size()>maximumLineBytes+1)return 13;
    requestBytes.chop(1);if(requestBytes.endsWith('\r'))requestBytes.chop(1);Request request;QJsonObject message;QString error;
    if(!decodeLine(requestBytes,message,error)||!requestFromJson(message,request,error))return 14;
    if(mode=="hang"){ignoreForTest();return 22;} // cancellation message is ignored
    if(mode=="crash")ExitProcess(7);
    const auto payloadReady=[&]{return emitJson({{"type","progress"},{"protocol",protocolVersion},{"id",request.id},{"phase","payloadReady"},{"current",1},{"total",1}});};
    const auto source=readSourceKernel(request.sourcePath);
    if(!source.source)return 23;
    QJsonObject done;
    if(!writePayload(session,request,*source.source,done,error)||done.isEmpty()){
        emitJson({{"type","error"},{"protocol",protocolVersion},{"id",request.id},{"code","fixture"},{"text",error.isEmpty()?QStringLiteral("fixture payload failed"):error}});
        return 23;
    }
    if(mode=="descendant"){
        QProcess child;child.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *arguments){arguments->flags|=CREATE_NO_WINDOW;});
        child.start(QCoreApplication::applicationFilePath(),{"--descendant"});if(!child.waitForStarted(5000))return 27;
        if(!emitJson({{"type","progress"},{"protocol",protocolVersion},{"id",request.id},{"phase","grandchild-"+QString::number(child.processId())},{"current",0},{"total",1}}))return 20;
        Sleep(10000);return 0;
    }
    if(mode=="wrongId"){
        const QJsonObject wrong{{"type","progress"},{"protocol",protocolVersion},{"id",QUuid::createUuid().toString(QUuid::WithoutBraces)},{"phase","decode"},{"current",1},{"total",1}};
        return emitJson(wrong)?0:20;
    }
    if(mode=="duplicateDone")return emitJson(done)&&emitJson(done)?0:20;
    if(mode=="noiseAfterDone"){
        if(!emitJson(done))return 20;
        const char noise[]="not-json\n";DWORD written=0;
        return WriteFile(control,noise,DWORD(sizeof(noise)-1),&written,nullptr)?0:20;
    }
    if(mode=="truncatedDone"){
        QString ignored;const auto line=encodeLine(done,ignored);DWORD written=0;
        if(!WriteFile(control,line.constData(),DWORD(line.size()-1),&written,nullptr))return 20;
        return 0;
    }
    if(mode=="doneCrash"){if(!emitJson(done))return 20;ExitProcess(7);}
    if(mode=="badPayload"){
        QFile frame(QDir(session).filePath(request.token+".frame"));
        if(!frame.open(QIODevice::ReadWrite))return 24;
        const qint64 at=40;
        char value=0;
        if(at<0||at>=frame.size()||!frame.seek(at)||frame.read(&value,1)!=1)return 25;
        value=char(value^0x5a);
        if(!frame.seek(at)||frame.write(&value,1)!=1||!frame.flush())return 25;
        frame.close();
        return emitJson(done)?0:20;
    }
    if(mode=="oversize"){
        if(!payloadReady())return 20;
        const QByteArray block(16384,'x');qint64 total=0;
        while(total<=maximumLineBytes+16){
            DWORD written=0;if(!WriteFile(control,block.constData(),DWORD(block.size()),&written,nullptr))return 20;total+=written;
        }
        ignoreForTest();return 21;
    }
    if(mode!="valid")return 26;
    if(!payloadReady()||!emitJson(done))return 20;
    return 0;
}
