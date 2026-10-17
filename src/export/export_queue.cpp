#include "export_queue.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include "io/atomic_file.h"
#include <QSet>
#include <QtConcurrentRun>
namespace Export {
Queue::Queue(const QString &path,QObject *parent):QObject(parent),record(path){
    pool.setMaxThreadCount(1);
    connect(&watcher,&QFutureWatcher<Result>::finished,this,[this]{
        const auto r=watcher.result();
        if(active>=0&&active<items.size()){
            auto &j=items[active];j.error=r.error;
            j.status=r.ok?Status::Succeeded:r.skipped?Status::Skipped:r.cancelled?Status::Cancelled:Status::Failed;
            j.stage=statusText(j.status);
        }
        active=-1;
        if(cancelled.load())for(auto &j:items)if(j.status==Status::Pending&&!deferred.contains(j.id)){j.status=Status::Cancelled;j.stage=statusText(j.status);}
        QString e;if(!save(e)){persistenceError=e;running=false;emit changed();emit finished();return;}
        emit changed();dispatch();
    });
}
Queue::~Queue(){cancelled=true;disconnect(&watcher,nullptr,this,nullptr);pool.waitForDone();}
bool Queue::save(QString &error){
    error.clear();QJsonArray jobs;for(const auto &j:items){const auto json=jobJson(j);if(json.isEmpty()){error=QStringLiteral("导出任务无法序列化");return false;}jobs.append(json);}
    const auto bytes=QJsonDocument(QJsonObject{{"version",1},{"jobs",jobs}}).toJson(QJsonDocument::Compact);
    if(bytes.size()>16*1024*1024){error=QStringLiteral("导出队列记录超过16MiB");return false;}
    if(!QDir().mkpath(QFileInfo(record).absolutePath())){error=QStringLiteral("无法创建导出队列目录");return false;}
    QString writeError;
    if(!Persistence::writeAtomicBytes(record,bytes,writeError)){error=QStringLiteral("导出队列保存失败：")+writeError;return false;}
    persistenceError.clear();return true;
}
bool Queue::load(QString &error){
    error.clear();if(running){error=QStringLiteral("导出期间不能加载记录");return false;}
    if(!QFileInfo::exists(record)){unreadable=false;return true;}
    unreadable=true;
    QFile f(record);if(!f.open(QIODevice::ReadOnly)){error=f.errorString();return false;}
    if(f.size()>16*1024*1024){error=QStringLiteral("导出记录过大");return false;}
    QJsonParseError parse;const auto doc=QJsonDocument::fromJson(f.readAll(),&parse);const auto root=doc.object();
    if(parse.error!=QJsonParseError::NoError||!doc.isObject()||root.value("version").toInt(-1)!=1||!root.value("jobs").isArray()||root.value("jobs").toArray().size()>2000){error=QStringLiteral("导出记录损坏或版本不支持");return false;}
    QList<Job> restored;QSet<QString> ids,recovered;
    for(const auto &v:root.value("jobs").toArray()){
        Job j;if(!v.isObject()||!jobFromJson(v.toObject(),j,error))return false;
        if(ids.contains(j.id)){error=QStringLiteral("重复任务标识");return false;}ids.insert(j.id);
        if(j.status==Status::Running||j.status==Status::Pending){j.status=Status::Pending;j.stage=QStringLiteral("上次未完成，点击重试后继续");recovered.insert(j.id);}
        restored.append(j);
    }
    items=restored;deferred=recovered;unreadable=false;persistenceError.clear();emit changed();return true;
}
bool Queue::append(const QList<Job> &jobs,QString &error){
    if(unreadable){error=QStringLiteral("现有导出记录无法恢复，为保护记录未覆盖；请检查记录文件");return false;}
    error.clear();if(running){error=QStringLiteral("导出正在进行，请等待本批完成");return false;}
    if(items.size()+jobs.size()>2000){error=QStringLiteral("队列最多2000项");return false;}
    QSet<QString> ids,paths;for(const auto &j:items){ids.insert(j.id);if(j.status==Status::Pending||j.status==Status::Running)paths.insert(j.path.toCaseFolded());}
    QList<Job> checked;
    for(const auto &j:jobs){const auto json=jobJson(j);if(json.isEmpty()){error=QStringLiteral("导出任务无法序列化");return false;}
        Job decoded;if(!jobFromJson(json,decoded,error))return false;
        if(ids.contains(decoded.id)||(decoded.status==Status::Pending&&paths.contains(decoded.path.toCaseFolded()))){error=QStringLiteral("队列存在重复标识或输出路径");return false;}
        ids.insert(decoded.id);paths.insert(decoded.path.toCaseFolded());checked.append(decoded);
    }
    // Store the round-tripped copies so later caller mutations cannot reach the queue.
    const auto before=items;items.append(checked);if(!save(error)){items=before;persistenceError=error;return false;}emit changed();return true;
}
bool Queue::start(QString &error){
    if(unreadable){error=QStringLiteral("导出记录未恢复，不能开始任务");return false;}
    error.clear();if(running){error=QStringLiteral("已经在导出");return false;}
    bool pending=false;for(const auto &j:items)pending|=j.status==Status::Pending&&!deferred.contains(j.id);
    if(!pending){error=QStringLiteral("没有待导出任务");return false;}
    if(!save(error)){persistenceError=error;return false;}
    cancelled=false;running=true;dispatch();return true;
}
void Queue::dispatch(){
    if(cancelled.load()){running=false;emit changed();emit finished();return;}
    active=-1;for(int i=0;i<items.size();++i)if(items[i].status==Status::Pending&&!deferred.contains(items[i].id)){active=i;break;}
    if(active<0){running=false;emit changed();emit finished();return;}
    auto &j=items[active];j.status=Status::Running;j.stage=QStringLiteral("准备导出");
    QString e;if(!save(e)){j.status=Status::Pending;persistenceError=e;running=false;active=-1;emit changed();emit finished();return;}
    const auto job=j;const auto id=j.id;emit changed();
    watcher.setFuture(QtConcurrent::run(&pool,[this,job,id]{return execute(job,&cancelled,[this,id](const QString &stage){
        QMetaObject::invokeMethod(this,[this,id,stage]{if(active>=0&&active<items.size()&&items[active].id==id){items[active].stage=stage;emit changed();}},Qt::QueuedConnection);
    });}));
}
void Queue::cancel(){if(running)cancelled=true;}
bool Queue::retry(QString &error){
    if(unreadable){error=QStringLiteral("导出记录无法恢复，未改写记录");return false;}
    error.clear();if(running){error=QStringLiteral("正在导出，不能重试");return false;}
    const auto before=items;bool any=false;for(auto &j:items)if(j.status==Status::Failed||j.status==Status::Cancelled||j.status==Status::Pending){j.status=Status::Pending;j.error.clear();j.stage.clear();any=true;}
    if(!any){error=QStringLiteral("没有失败或未完成任务");return false;}
    if(!save(error)){items=before;persistenceError=error;return false;}deferred.clear();emit changed();return true;
}
}
