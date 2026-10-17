#include "worker_client.h"
#include "worker_payload.h"
#include "worker_client_testing.h"
#include "worker_session.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <chrono>
#include <cmath>
#include <mutex>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace RawWorker {
namespace {
std::timed_mutex slot;
QString sourceHash(const QString &path,const CancellationToken &cancel){
    QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while(!file.atEnd()){
        if(isCanceled(cancel))return {};
        const auto bytes=file.read(1024*1024);if(bytes.isEmpty()&&file.error()!=QFile::NoError)return {};
        hash.addData(bytes);
    }
    return QString::fromLatin1(hash.result().toHex());
}
bool keys(const QJsonObject &object,std::initializer_list<const char *> names){
    if(object.size()!=qsizetype(names.size()))return false;
    for(const auto name:names)if(!object.contains(QLatin1String(name)))return false;return true;
}
bool count(const QJsonValue &value){return value.isDouble()&&std::isfinite(value.toDouble())&&value.toDouble()>=0&&value.toDouble()<=1000000&&std::floor(value.toDouble())==value.toDouble();}
#ifdef Q_OS_WIN
class Job {
    HANDLE handle=nullptr;
public:
    ~Job(){if(handle)CloseHandle(handle);}
    bool attach(qint64 pid){
        handle=CreateJobObjectW(nullptr,nullptr);if(!handle)return false;
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if(!SetInformationJobObject(handle,JobObjectExtendedLimitInformation,&limits,sizeof(limits)))return false;
        const auto process=OpenProcess(PROCESS_SET_QUOTA|PROCESS_TERMINATE,FALSE,DWORD(pid));if(!process)return false;
        const auto assigned=AssignProcessToJobObject(handle,process);CloseHandle(process);return assigned;
    }
    void terminate(){if(handle)TerminateJobObject(handle,3);}
    bool empty()const{
        if(!handle)return true;JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info{};
        return QueryInformationJobObject(handle,JobObjectBasicAccountingInformation,&info,sizeof(info),nullptr)&&info.ActiveProcesses==0;
    }
};
#endif
}
static FrameResult runImpl(const Request &request,const CancellationToken &cancel,const WorkerProgress &progress,const Testing::Options *test){
    const auto fail=[](const QString &error){return FrameResult{{},error};};
    QString error;if(!validRequest(request,error))return fail(error);
    if(QCoreApplication::instance()&&QThread::currentThread()==QCoreApplication::instance()->thread())return fail(QStringLiteral("RAW进程只能在后台任务中运行"));
#ifndef Q_OS_WIN
    return fail(QStringLiteral("当前平台尚未实现RAW进程隔离"));
#else
    const int timeoutMs=test?test->timeoutMs:180000;
    if(timeoutMs<100||timeoutMs>180000)return fail(QStringLiteral("RAW测试期限无效"));
    QElapsedTimer clock;clock.start();
    std::unique_lock<std::timed_mutex> lock(slot,std::defer_lock);
    while(!lock.try_lock_for(std::chrono::milliseconds(10))){
        if(isCanceled(cancel))return fail(QStringLiteral("已取消"));
        if(clock.elapsed()>timeoutMs)return fail(QStringLiteral("等待RAW进程槽超时"));
    }
    if(isCanceled(cancel))return fail(QStringLiteral("已取消"));
    if(sourceHash(request.sourcePath,cancel)!=request.sourceHash)return fail(QStringLiteral("RAW源指纹与任务不匹配或不可读取"));
    const auto executable=test?test->executable:QCoreApplication::applicationDirPath()+QStringLiteral("/LuminaS-raw-worker.exe");
    if(!QDir::isAbsolutePath(executable))return fail(QStringLiteral("RAW程序路径必须为绝对路径"));
    if(!QFileInfo(executable).isFile())return fail(QStringLiteral("缺少同版本LuminaS-raw-worker.exe"));
    const auto cacheRoot=QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    const auto root=test?test->sessionRoot:cacheRoot+QStringLiteral("/raw-worker");
    if((!test&&cacheRoot.isEmpty())||!QDir::isAbsolutePath(root)||(test?!QFileInfo(root).isDir():!QDir().mkpath(root))||QFileInfo(root).isSymLink())return fail(QStringLiteral("无法准备RAW私有会话目录"));
    cleanAbandonedSessions(root); // Only sealed, ended owners/workers in this root.
    QTemporaryDir session(root+QStringLiteral("/session-XXXXXX"));if(!session.isValid())return fail(QStringLiteral("无法创建RAW私有会话"));
    if(!recordSessionOwner(root,session.path(),request,error))return fail(error);
    if(test&&test->sessionCreated){try{test->sessionCreated(session.path());}catch(...){return fail(QStringLiteral("RAW会话测试回调失败"));}}
    Job job;QProcess process;process.setProgram(executable);
    auto arguments=test?test->arguments:QStringList{};arguments<<QStringLiteral("--session")<<session.path();process.setArguments(arguments);
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args){args->flags|=CREATE_NO_WINDOW;});
    struct Guard {
        QProcess &process;Job &job;QTemporaryDir &session;
        ~Guard(){
            job.terminate();if(process.state()!=QProcess::NotRunning){process.kill();process.waitForFinished(5000);}
            QElapsedTimer drain;drain.start();while(!job.empty()&&drain.elapsed()<500)QThread::msleep(1);
            // Never remove a session that a surviving process may still use.
            // Owned orphan recovery is a separate, explicitly audited step.
            if(process.state()!=QProcess::NotRunning||!job.empty())session.setAutoRemove(false);
        }
    }guard{process,job,session};
    process.start();if(!process.waitForStarted(5000))return fail(QStringLiteral("RAW进程启动失败：")+process.errorString());
    // Worker cannot decode before Request. Assign the actual process before any
    // request bytes are sent; nested-job rejection fails closed.
    if((test&&test->rejectJobAssignment)||!job.attach(process.processId()))return fail(QStringLiteral("无法建立RAW进程树保护，已拒绝解码"));
    if(!recordSessionWorker(root,session.path(),process.processId(),error))return fail(error);
    if(test&&test->started){try{test->started(process.processId());}catch(...){return fail(QStringLiteral("RAW启动测试回调失败"));}}
    QByteArray pending,diagnostics;bool helloReceived=false,requestSent=false,terminal=false;QJsonObject done;
    QString terminalError,protocolError;qint64 controlBytes=0;int messages=0;
    const auto accept=[&](const QJsonObject &message){
        if(!helloReceived){if(!validHello(message,protocolError))return;helloReceived=true;return;}
        if(!requestSent||terminal||message["protocol"]!=QJsonValue(protocolVersion)||message["id"]!=QJsonValue(request.id)){
            protocolError=QStringLiteral("RAW消息顺序、版本或身份无效");return;
        }
        const auto type=message["type"].toString();
        if(type=="progress"){
            if(!keys(message,{"type","protocol","id","phase","current","total"})||!message["phase"].isString()||message["phase"].toString().isEmpty()||message["phase"].toString().size()>64||!count(message["current"])||!count(message["total"])||message["current"].toDouble()>message["total"].toDouble())protocolError=QStringLiteral("RAW进度消息无效");
            else if(progress){try{progress(message["phase"].toString(),message["current"].toInt(),message["total"].toInt());}catch(...){protocolError=QStringLiteral("RAW进度观察回调失败");}}
        }else if(type=="done"){terminal=true;done=message;}
        else if(type=="error"){
            if(!keys(message,{"type","protocol","id","code","text"})||!message["code"].isString()||message["code"].toString().isEmpty()||message["code"].toString().size()>128||!message["text"].isString()||message["text"].toString().size()>4096){protocolError=QStringLiteral("RAW错误消息无效");return;}
            terminal=true;terminalError=message["text"].toString();if(terminalError.isEmpty())terminalError=message["code"].toString();
        }else protocolError=QStringLiteral("未知RAW进程消息");
    };
    const auto drain=[&]{
        process.setReadChannel(QProcess::StandardError);
        while(process.bytesAvailable()){
            const auto bytes=process.read(16384);if(diagnostics.size()<16384)diagnostics.append(bytes.left(16384-diagnostics.size()));
        }
        process.setReadChannel(QProcess::StandardOutput);
        while(process.bytesAvailable()){
            const auto bytes=process.read(65536);controlBytes+=bytes.size();if(controlBytes>8*1024*1024){protocolError=QStringLiteral("RAW控制输出总量超限");return;}pending.append(bytes);
            for(;;){const auto newline=pending.indexOf('\n');if(newline<0)break;auto line=pending.left(newline);pending.remove(0,newline+1);if(line.endsWith('\r'))line.chop(1);
                if(++messages>16384){protocolError=QStringLiteral("RAW控制消息数量超限");return;}
                QJsonObject message;if(!decodeLine(line,message,protocolError))return;accept(message);if(!protocolError.isEmpty())return;
            }
            if(pending.size()>maximumLineBytes+1){protocolError=QStringLiteral("RAW控制消息累积超限");return;}
        }
    };
    const auto abort=[&](const QString &reason){
        if(requestSent&&process.state()!=QProcess::NotRunning){QString ignored;process.write(encodeLine(cancelJson(request),ignored));process.waitForBytesWritten(20);process.closeWriteChannel();process.waitForFinished(100);}
        job.terminate();if(process.state()!=QProcess::NotRunning){process.kill();process.waitForFinished(5000);}return fail(reason);
    };
    while(process.state()!=QProcess::NotRunning){
        if(isCanceled(cancel))return abort(QStringLiteral("已取消"));
        if(clock.elapsed()>timeoutMs)return abort(QStringLiteral("RAW进程任务超时"));
        process.waitForReadyRead(10);drain();if(!protocolError.isEmpty())return abort(protocolError);
        if(helloReceived&&!requestSent){
            const auto data=encodeLine(requestJson(request,error),error);if(!error.isEmpty())return abort(error);
            if(process.write(data)!=data.size())return abort(QStringLiteral("无法发送RAW任务"));requestSent=true;
        }
        if(terminal)process.closeWriteChannel();
    }
    drain();if(!protocolError.isEmpty())return fail(protocolError);
    if(isCanceled(cancel))return fail(QStringLiteral("已取消"));
    if(!pending.isEmpty()||!helloReceived||!requestSent||!terminal)return fail(QStringLiteral("RAW进程意外退出或消息截断"));
    if(!terminalError.isEmpty())return fail(terminalError);
    if(process.exitStatus()!=QProcess::NormalExit||process.exitCode()!=0)return fail(QStringLiteral("RAW进程异常退出：")+QString::fromUtf8(diagnostics));
    // No child-owned frame remains resident before allocating the imported frame.
    // Process signalling and job accounting are not updated atomically. Allow a
    // bounded accounting drain; a surviving descendant still prevents import.
    QElapsedTimer jobDrain;jobDrain.start();
    while(!job.empty()&&jobDrain.elapsed()<500){if(isCanceled(cancel))return fail(QStringLiteral("已取消"));QThread::msleep(1);}
    if(!job.empty())return fail(QStringLiteral("RAW进程树尚未结束，拒绝导入结果"));
    if(sourceHash(request.sourcePath,cancel)!=request.sourceHash)return fail(QStringLiteral("RAW源在进程读取后发生变化"));
    return readPayload(session.path(),request,done,cancel);
#endif
}
FrameResult runWorker(const Request &request,const CancellationToken &cancel,const WorkerProgress &progress){return runImpl(request,cancel,progress,nullptr);}
FrameResult Testing::run(const Request &request,const Options &options,const CancellationToken &cancel,const WorkerProgress &progress){return runImpl(request,cancel,progress,&options);}
}
