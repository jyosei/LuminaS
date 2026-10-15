#pragma once
#include "export_engine.h"
#include <QObject>
#include <QFutureWatcher>
#include <QThreadPool>
#include <QSet>
namespace Export {
class Queue:public QObject {
    Q_OBJECT
public:
    explicit Queue(const QString &recordPath,QObject *parent=nullptr);
    ~Queue() override;
    const QList<Job> &jobs() const{return items;}
    bool busy() const{return running;}
    QString error() const{return persistenceError;}
    bool append(const QList<Job> &jobs,QString &error);
    bool load(QString &error);
    bool start(QString &error);
    void cancel();
    bool retry(QString &error);
signals:
    void changed(); void finished();
private:
    bool save(QString &error); void dispatch();
    QString record,persistenceError; QList<Job> items;
    QSet<QString> deferred; // recovered work starts only after explicit retry
    QThreadPool pool; QFutureWatcher<Result> watcher;
    std::atomic_bool cancelled{false}; bool running=false,unreadable=false; int active=-1;
};
}
