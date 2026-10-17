#pragma once
#include "worker_protocol.h"
#include "core/cancellation.h"
#include <functional>

namespace RawWorker {
// Background-only. One RAW process globally, private temporary payload, strict
// protocol, bounded lifecycle. Production never chooses an executable via PATH.
using WorkerProgress=std::function<void(const QString &,int,int)>;
FrameResult runWorker(const Request &request,const CancellationToken &cancel={},const WorkerProgress &progress={});
}
