#pragma once
#include "core/document.h"
#include <QTemporaryDir>
#include <QHash>
#include <QStringList>
#include <optional>
#include <QMutex>
// Session-private, worker-owned float32 4K cache; removed on close.
class PreviewCache {
public:
    explicit PreviewCache(qint64 limit=4LL*1024*1024*1024):budget(limit){}
    bool put(const SourceFrame &frame,QJsonObject *timings=nullptr);
    std::optional<SourceFrame> get(const QString &path,const QString &sha);
    qint64 usedBytes() const{QMutexLocker guard(&mutex);return used;}
    QString storageDirectory() const{return directory.path();} // diagnostics for this private session only
private:
    QTemporaryDir directory;
    QHash<QString,QString> entries;
    QHash<QString,qint64> sizes;
    QStringList order;
    qint64 budget,used=0;
    mutable QMutex mutex;
};
