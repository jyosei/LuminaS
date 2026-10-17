#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <exception>
#include <algorithm>
#include "raw/source_kernel.h"
#include "raw/worker_protocol.h"
#include "raw/worker_payload.h"
#include "render/cpu_renderer.h"

namespace {
using namespace RawWorker;
class Output {
    std::mutex mutex;
public:
    bool send(const QJsonObject &message){
        QString error;const auto data=encodeLine(message,error);if(!error.isEmpty())return false;
        std::lock_guard lock(mutex);const auto handle=GetStdHandle(STD_OUTPUT_HANDLE);if(!handle||handle==INVALID_HANDLE_VALUE)return false;
        qsizetype position=0;while(position<data.size()){
            DWORD written=0;if(!WriteFile(handle,data.constData()+position,DWORD(data.size()-position),&written,nullptr)||!written)return false;position+=written;
        }return true;
    }
    bool error(const QString &id,const QString &code,const QString &text){return send({{"type","error"},{"protocol",protocolVersion},{"id",id},{"code",code},{"text",text.left(4096)}});}
};
class Input {
    QByteArray pending;
    HANDLE handle=GetStdHandle(STD_INPUT_HANDLE);
public:
    // 1:line, 0:EOF, -1:invalid/I/O, 2:stopping. Single consumer.
    int read(QByteArray &line,const std::atomic_bool *stop=nullptr){
        for(;;){
            if(stop&&stop->load(std::memory_order_acquire))return 2;
            const auto newline=pending.indexOf('\n');
            if(newline>=0){line=pending.left(newline);pending.remove(0,newline+1);if(line.endsWith('\r'))line.chop(1);return line.size()<=maximumLineBytes?1:-1;}
            if(pending.size()>maximumLineBytes+1||!handle||handle==INVALID_HANDLE_VALUE)return -1;
            // QProcess owns a pipe. With this single reader, bytes reported by
            // PeekNamedPipe cannot be consumed elsewhere. Never enter a blocking
            // empty-pipe read: stopping must work while the parent keeps stdin open.
            DWORD available=0;
            if(!PeekNamedPipe(handle,nullptr,0,nullptr,&available,nullptr)){
                const auto error=GetLastError();if(error==ERROR_BROKEN_PIPE||error==ERROR_HANDLE_EOF)return pending.isEmpty()?0:-1;return -1;
            }
            if(!available){Sleep(2);continue;}
            char buffer[4096];DWORD read=0;
            if(!ReadFile(handle,buffer,std::min<DWORD>(available,sizeof(buffer)),&read,nullptr)){
                if(stop&&stop->load(std::memory_order_acquire))return 2;
                const auto error=GetLastError();if(error==ERROR_BROKEN_PIPE||error==ERROR_HANDLE_EOF)return pending.isEmpty()?0:-1;return -1;
            }
            if(!read)return pending.isEmpty()?0:-1;pending.append(buffer,read);
        }
    }
};
class Control {
    Input &input;const Request &request;const CancellationSource &cancel;
    std::atomic_bool stopping{false},readerDone{false},protocolBad{false};
    std::thread reader;
    bool stopped=false;
    void run() noexcept{
        try {
            QByteArray line;
            const auto status=input.read(line,&stopping);
            if(status!=2&&!stopping.load(std::memory_order_acquire)){
                if(status==1){QJsonObject message;QString error;if(!decodeLine(line,message,error)||!validCancel(message,request,error))protocolBad.store(true,std::memory_order_release);}
                else if(status<0)protocolBad.store(true,std::memory_order_release);
                // Valid Cancel, premature EOF, malformed input all cancel.
                cancel.cancel();
            }
        }catch(...){protocolBad.store(true,std::memory_order_release);cancel.cancel();}
        readerDone.store(true,std::memory_order_release);
    }
public:
    Control(Input &in,const Request &r,const CancellationSource &c):input(in),request(r),cancel(c){reader=std::thread([this]{run();});}
    void stop() noexcept{
        if(stopped)return;stopped=true;stopping.store(true,std::memory_order_release);
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(500);
        while(!readerDone.load(std::memory_order_acquire)&&std::chrono::steady_clock::now()<deadline){CancelSynchronousIo(reader.native_handle());Sleep(1);}
        if(!readerDone.load(std::memory_order_acquire)){
            constexpr char message[]="raw-worker control input did not stop within 500ms\n";DWORD written=0;WriteFile(GetStdHandle(STD_ERROR_HANDLE),message,sizeof(message)-1,&written,nullptr);
            TerminateProcess(GetCurrentProcess(),3);std::abort();
        }
        if(reader.joinable())reader.join();
    }
    bool bad() const{return protocolBad.load(std::memory_order_acquire);}
    ~Control(){stop();}
};
int execute(const QString &directory,const Request &request,Output &output,const CancellationSource &cancel){
    const auto token=cancel.token();
    const auto progress=[&](const QString &phase,int current,int total){
        if(!output.send({{"type","progress"},{"protocol",protocolVersion},{"id",request.id},{"phase",phase},{"current",current},{"total",total}}))cancel.cancel();
    };
    const auto fail=[&](const QString &code,const QString &error){output.error(request.id,isCanceled(token)?QStringLiteral("cancelled"):code,error);return 2;};
    const auto read=readSourceKernel(request.sourcePath,request.warmth,request.tint,token,progress);
    if(isCanceled(token)||!read.source||!read.error.isEmpty())return fail("read",read.error.isEmpty()?QStringLiteral("Canceled"):read.error);
    if(read.source->sha256!=request.sourceHash)return fail("source-changed",QStringLiteral("源指纹与任务记录不匹配"));
    auto frame=*read.source;
    if(request.operation==Operation::Prepare){
        progress("prepare",0,1);QString error;frame.linear=renderLinear(*read.source,request.geometry,request.longEdge,error,{},token);
        if(isCanceled(token)||!error.isEmpty()||frame.linear.isNull())return fail("prepare",error.isEmpty()?QStringLiteral("准备工作帧失败或已取消"):error);
        frame.sourcePixelsPerPixelX=double(frame.originalSize.width())*request.geometry.crop.width()/frame.linear.width();
        frame.sourcePixelsPerPixelY=double(frame.originalSize.height())*request.geometry.crop.height()/frame.linear.height();
        frame.sourcePixelsPerPixel=std::max(frame.sourcePixelsPerPixelX,frame.sourcePixelsPerPixelY);progress("prepare",1,1);
    }
    progress("write",0,1);QJsonObject done;QString error;
    if(!writePayload(directory,request,frame,done,error,token))return fail("write",error);
    // Successful commit remains a commit. The parent still rejects a canceled
    // job or a nonzero exit even if a terminal message was already transmitted.
    if(!output.send(done))return 3;return 0;
}
}
int main(int argc,char **argv){
    qputenv("OMP_NUM_THREADS","4");qputenv("OMP_WAIT_POLICY","PASSIVE");QCoreApplication app(argc,argv);app.setApplicationName("LuminaS-raw-worker");
    Output output;QString id;
    try {
        const auto args=app.arguments();
        if(args.size()!=3||args[1]!="--session")return 2;
        const QFileInfo directory(args[2]);if(args[2].contains(QChar(0))||!directory.isAbsolute()||!directory.isDir()||directory.isSymLink()||directory.canonicalFilePath().isEmpty())return 2;
        if(!output.send(hello()))return 3;
        Input input;QByteArray line;if(input.read(line)!=1){output.error({},"request",QStringLiteral("请求为空、截断或超限"));return 2;}
        QJsonObject message;Request request;QString error;
        if(!decodeLine(line,message,error)||!requestFromJson(message,request,error)){output.error({},"request",error);return 2;}
        id=request.id;CancellationSource cancel;Control control(input,request,cancel);
        const auto result=execute(directory.canonicalFilePath(),request,output,cancel);control.stop();return control.bad()?3:result;
    }catch(const std::exception &e){output.error(id,"internal",QString::fromUtf8(e.what()));return 3;}
    catch(...){output.error(id,"internal",QStringLiteral("未知RAW进程错误"));return 3;}
}
