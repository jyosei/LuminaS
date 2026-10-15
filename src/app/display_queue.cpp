#include "display_queue.h"
#include <QtConcurrentRun>
#include <QPointer>
DisplayQueue::DisplayQueue(QObject *parent):DisplayQueue(parent,transformDisplay){}
DisplayQueue::DisplayQueue(QObject *parent,Transform operation):QObject(parent),transform(std::move(operation)){
    pool.setMaxThreadCount(1);
    connect(&watcher,&QFutureWatcher<DisplayFrame>::finished,this,&DisplayQueue::finished);
}
DisplayQueue::~DisplayQueue(){
    if(cancelToken)cancelToken->store(true);
    disconnect(&watcher,nullptr,this,nullptr);pool.waitForDone();
}
void DisplayQueue::request(QImage source,const QByteArray &targetIcc,quint64 context){
    if(context!=currentContext){
        invalidatedContext=currentContext;currentContext=context;++currentGeneration;
        if(cancelToken)cancelToken->store(true);pending.reset();
    }
    Request next{std::move(source),targetIcc,context,nextSerial++,currentGeneration};
    if(active)pending=std::move(next);else start(next);
}
void DisplayQueue::invalidate(){
    invalidatedContext=currentContext;++currentGeneration;pending.reset();
    if(cancelToken)cancelToken->store(true);
}
void DisplayQueue::start(const Request &request){
    active=true;activeGeneration=request.generation;cancelToken=std::make_shared<std::atomic<bool>>(false);
    const auto token=cancelToken;const auto operation=transform;
    watcher.setFuture(QtConcurrent::run(&pool,[request,token,operation]{
        const auto converted=operation(request.image,request.targetIcc,token);
        return DisplayFrame{converted.image,request.image,converted.error,request.context,request.serial};
    }));
}
void DisplayQueue::finished(){
    const auto result=watcher.result();const auto completedGeneration=activeGeneration;
    active=false;cancelToken.reset();
    // Keep active true while emitting: a reentrant request replaces pending,
    // rather than starting a second job before this completion drains it.
    active=true;
    if(completedGeneration==currentGeneration&&result.context==currentContext&&result.serial>lastDelivered){
        lastDelivered=result.serial;const QPointer<DisplayQueue> alive(this);emit frameReady(result);if(!alive)return;
    }
    active=false;
    if(pending){
        const auto next=std::move(*pending);pending.reset();
        if(next.generation==currentGeneration&&next.context==currentContext)start(next);
    }
}
