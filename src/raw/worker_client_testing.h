#pragma once
// Internal test seam. Never included by application flows; production runWorker
// has no executable/root/timeout injection or environment override.
#include "worker_client.h"
#include <QStringList>
namespace RawWorker::Testing {
struct Options {
    QString executable;
    QStringList arguments;
    QString sessionRoot; // Existing private test directory, not an output file.
    int timeoutMs=2000;
    bool rejectJobAssignment=false;
    std::function<void(qint64)> started; // After successful assignment, before Request.
    std::function<void(const QString &)> sessionCreated;
};
FrameResult run(const Request &request,const Options &options,const CancellationToken &cancel={},const WorkerProgress &progress={});
}
