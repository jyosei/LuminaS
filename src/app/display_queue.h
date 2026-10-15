#pragma once
#include "color/display_transform.h"
#include <QByteArray>
#include <QImage>
#include <QMetaType>
#include <QObject>
#include <QThreadPool>
#include <QFutureWatcher>
#include <QString>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
struct DisplayFrame {QImage image,canonical;QString error;quint64 context=0,serial=0;};
Q_DECLARE_METATYPE(DisplayFrame)
// Display-only conversion queue. It owns no colour maths: every frame is produced by an
// injected Transform, so the source image, profile and cancel token are copied into a worker
// lambda that never touches this object or any other QObject. At most one conversion runs and
// at most one request waits; a newer request for the same context replaces the waiting one
// without cancelling the running job, while a request for another context invalidates the
// running job's delivery.
class DisplayQueue:public QObject {
    Q_OBJECT
public:
    using Transform=std::function<DisplayTransformResult(const QImage &,const QByteArray &,
        const std::shared_ptr<std::atomic<bool>> &)>;
    explicit DisplayQueue(QObject *parent=nullptr);
    DisplayQueue(QObject *parent,Transform transform);
    ~DisplayQueue() override;
    // context identifies the caller's frame of reference (document/revision). It is never
    // rewritten here, so callers can keep comparing their own values.
    void request(QImage source,const QByteArray &targetIcc,quint64 context);
    // Drops the waiting request and cancels the running one. currentContext is left alone;
    // an internal generation makes the invalidated job stale even if context is reused.
    void invalidate();
    // Caller-owned context cancelled so far, plus the internally advanced generation.
    quint64 lastInvalidatedContext() const {return invalidatedContext;}
    quint64 generation() const {return currentGeneration;}
    quint64 lastDeliveredSerial() const {return lastDelivered;}
    bool busy() const {return active;}
    bool hasPending() const {return pending.has_value();}
signals:
    void frameReady(DisplayFrame frame);
private:
    struct Request {QImage image;QByteArray targetIcc;quint64 context=0,serial=0,generation=0;};
    void start(const Request &request);
    void finished();
    Transform transform;
    QThreadPool pool;
    QFutureWatcher<DisplayFrame> watcher;
    std::optional<Request> pending;
    std::shared_ptr<std::atomic<bool>> cancelToken;
    quint64 activeGeneration=0;
    quint64 currentContext=0,nextSerial=1,currentGeneration=1;
    quint64 lastDelivered=0,invalidatedContext=0;
    bool active=false;
};
