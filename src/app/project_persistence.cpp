#include "project_persistence.h"
#include <QtConcurrentRun>
#include <QDir>
#include <exception>
#include <limits>

namespace {
bool writing(PersistenceOperation op){return op==PersistenceOperation::RecoverySave||op==PersistenceOperation::ProjectSave;}
bool loading(PersistenceOperation op){return !writing(op);}
QString pathKey(const QString &path){
    auto result=QDir::cleanPath(QDir::fromNativeSeparators(path));
#ifdef Q_OS_WIN
    result=result.toCaseFolded();
#endif
    return result;
}
bool validPath(const QString &path){return !path.isEmpty()&&path.size()<=32767&&!path.contains(QChar(0))&&QDir::isAbsolutePath(path);}
bool validHash(const QString &hash){
    if(hash.isEmpty())return true;if(hash.size()!=64)return false;
    for(const auto c:hash)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;
    return true;
}
bool reject(QString &error,const QString &message){error=message;return false;}
LocalEdit::RecoverySnapshot owned(LocalEdit::RecoverySnapshot source,QString &error){
    if(!source||!source->project||source->project->id.isEmpty()||!source->project->revision){error=QStringLiteral("没有可用的项目快照");return {};}
    if(source->order.size()>LocalEdit::maximumProjectPhotos||source->pending.size()>LocalEdit::maximumProjectPhotos||
       source->project->photos.size()+source->pending.size()>LocalEdit::maximumProjectPhotos){error=QStringLiteral("项目照片数超过上限");return {};}
    // Copy immutable values without validation/JSON/filesystem work on the GUI.
    auto state=*source;auto project=*state.project;
    for(auto &photo:project.photos)if(photo.document)photo.document=std::make_shared<const LocalEdit::PhotoDocument>(*photo.document);
    state.project=std::make_shared<const LocalEdit::ProjectState>(std::move(project));
    return std::make_shared<const LocalEdit::RecoveryState>(std::move(state));
}
}
ProjectPersistence::ProjectPersistence(QObject *parent,Executor run):QObject(parent),executor(std::move(run)){
    qRegisterMetaType<PersistenceOperation>();qRegisterMetaType<PersistenceResult>();
    idle.setSingleShot(true);idle.setInterval(2000);
    idle.setTimerType(Qt::PreciseTimer);
    connect(&idle,&QTimer::timeout,this,[this]{recoveryDue=true;dispatch();});
    connect(&watcher,&QFutureWatcher<PersistenceResult>::finished,this,&ProjectPersistence::finish);
}
ProjectPersistence::~ProjectPersistence(){idle.stop();cancellation.cancel();}
bool ProjectPersistence::reset(LocalEdit::RecoverySnapshot state,const QString &path,bool clean,QString &error,const QString &knownHash){
    error.clear();if(!validPath(path))return reject(error,QStringLiteral("恢复副本路径无效"));
    if(!validHash(knownHash))return reject(error,QStringLiteral("恢复副本指纹无效"));
    if(epoch==std::numeric_limits<quint64>::max()||editSerial==std::numeric_limits<quint64>::max())return reject(error,QStringLiteral("项目会话计数已达上限"));
    if(running&&writing(running->operation)&&pathKey(running->path)==pathKey(path))return reject(error,QStringLiteral("旧恢复写入尚未结束，请使用独立恢复路径"));
    const auto snapshot=owned(state,error);if(!snapshot)return false;
    cancellation.cancel();++epoch;++editSerial;idle.stop();pending.reset();recoveryDue=false;
    latest=snapshot;journalPath=path;journalHash=knownHash;failure.clear();
    savedRevision=clean?latest->project->revision:0;journalRevision=knownHash.isEmpty()?0:latest->project->revision;
    if(!clean&&knownHash.isEmpty())idle.start();
    emit changed();return true;
}
bool ProjectPersistence::update(LocalEdit::RecoverySnapshot state,QString &error){
    error.clear();if(!latest||!state||!state->project)return reject(error,QStringLiteral("项目尚未初始化"));
    if(state->project->id!=latest->project->id||state->project->revision<latest->project->revision)return reject(error,QStringLiteral("项目身份或版本不匹配"));
    auto normalized=*state;normalized.explicitPath=latest->explicitPath;normalized.explicitHash=latest->explicitHash;
    if(state->project->revision==latest->project->revision){
        if(normalized==*latest)return true;
        return reject(error,QStringLiteral("相同项目版本不能携带不同内容"));
    }
    if(editSerial==std::numeric_limits<quint64>::max())return reject(error,QStringLiteral("编辑会话计数已达上限"));
    const auto snapshot=owned(std::make_shared<const LocalEdit::RecoveryState>(std::move(normalized)),error);if(!snapshot)return false;
    latest=snapshot;++editSerial;failure.clear();recoveryDue=false;idle.start();
    if(running&&loading(running->operation))cancellation.cancel();
    emit changed();return true;
}
bool ProjectPersistence::enqueue(PersistenceJob job,QString &error){
    if(pending||(running&&running->epoch==epoch&&loading(running->operation))||
       (running&&running->epoch==epoch&&running->operation==PersistenceOperation::ProjectSave))return reject(error,QStringLiteral("已有项目操作正在处理"));
    job.epoch=epoch;job.editSerial=editSerial;pending=std::move(job);return true;
}
bool ProjectPersistence::save(const QString &path,QString &error){
    error.clear();if(!latest)return reject(error,QStringLiteral("项目尚未初始化"));
    if(!validPath(path)||pathKey(path)==pathKey(journalPath))return reject(error,QStringLiteral("显式项目路径无效或与恢复副本相同"));
    if(!latest->pending.isEmpty())return reject(error,QStringLiteral("还有 %1 张照片未确认源身份，完成后才能保存项目").arg(latest->pending.size()));
    PersistenceJob job;job.operation=PersistenceOperation::ProjectSave;job.path=path;job.state=latest;
    if(!latest->explicitPath.isEmpty()&&pathKey(path)==pathKey(latest->explicitPath))job.expectedHash=latest->explicitHash;
    if(!enqueue(std::move(job),error))return false;
    dispatch();return true;
}
bool ProjectPersistence::open(const QString &path,bool recovery,QString &error){
    error.clear();if(!validPath(path))return reject(error,QStringLiteral("项目路径无效"));
    PersistenceJob job;job.operation=recovery?PersistenceOperation::RecoveryLoad:PersistenceOperation::ProjectLoad;job.path=path;
    if(!enqueue(std::move(job),error))return false;
    idle.stop();recoveryDue=false;dispatch();return true;
}
void ProjectPersistence::retryRecovery(){if(latest&&dirty()){recoveryDue=true;idle.stop();dispatch();}}
void ProjectPersistence::cancel(){
    cancellation.cancel();pending.reset();idle.stop();recoveryDue=false;
    if(editSerial<std::numeric_limits<quint64>::max())++editSerial;
    emit changed();
}
void ProjectPersistence::dispatch(){
    if(running)return;
    PersistenceJob job;
    if(pending){job=std::move(*pending);pending.reset();}
    else if(recoveryDue&&latest&&dirty()&&journalRevision<latest->project->revision){
        job.operation=PersistenceOperation::RecoverySave;job.state=latest;job.path=journalPath;job.expectedHash=journalHash;job.epoch=epoch;job.editSerial=editSerial;
        recoveryDue=false;
    }else {recoveryDue=false;return;}
    if(loading(job.operation)&&job.editSerial!=editSerial){
        PersistenceResult result;result.operation=job.operation;result.path=job.path;result.discarded=true;result.error=QStringLiteral("新编辑已取消项目打开");
        emit completed(result);emit changed();dispatch();return;
    }
    cancellation=CancellationSource();job.cancel=cancellation.token();running=job;
    const auto run=executor;
    watcher.setFuture(QtConcurrent::run([job,run]{
        PersistenceResult result;
        try {result=run?run(job):ProjectPersistence::perform(job);}
        catch(const std::exception &e){result.error=QStringLiteral("项目后台任务失败：")+QString::fromUtf8(e.what());}
        catch(...){result.error=QStringLiteral("项目后台任务发生未知异常");}
        result.operation=job.operation;result.path=job.path;
        if(writing(job.operation))result.revision=job.state&&job.state->project?job.state->project->revision:0;
        return result;
    }));
    emit started(job.operation,job.state&&job.state->project?job.state->project->revision:0);emit changed();
}
void ProjectPersistence::finish(){
    if(!running)return;
    const auto job=*running;const auto value=watcher.result();running.reset();
    if(job.epoch!=epoch){emit changed();dispatch();return;}
    auto result=value;
    if(loading(job.operation)&&(job.editSerial!=editSerial||isCanceled(job.cancel))){
        result.state.reset();result.sources.clear();result.hash.clear();result.loadedHistory.reset();result.discarded=true;result.error=QStringLiteral("打开任务已取消或被新编辑取代");
    }
    failure=result.error;
    if(result.error.isEmpty()&&job.operation==PersistenceOperation::RecoverySave){journalHash=result.hash;journalRevision=job.state->project->revision;}
    if(result.error.isEmpty()&&job.operation==PersistenceOperation::ProjectSave){
        savedRevision=job.state->project->revision;
        auto updated=*latest;updated.explicitPath=result.path;updated.explicitHash=result.hash;latest=std::make_shared<const LocalEdit::RecoveryState>(std::move(updated));
        if(!dirty()){idle.stop();recoveryDue=false;}
    }
    if(loading(job.operation)&&!result.error.isEmpty()&&dirty()&&!idle.isActive()&&!recoveryDue&&journalRevision<latest->project->revision)idle.start();
    emit changed();emit completed(result);dispatch();
}
