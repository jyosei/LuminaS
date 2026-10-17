#pragma once
#include "worker_protocol.h"
#include <QStringList>
namespace RawWorker {
struct SessionCleanup {int removed=0,kept=0;QStringList errors;};
// Only the application's existing private RAW root is eligible. Owner marker
// records process creation time to distinguish a reused PID. No TEMP scanning.
bool recordSessionOwner(const QString &root,const QString &directory,const Request &request,QString &error);
// Immutable second marker, committed before Request dispatch. A missing marker
// is reclaimable only when the session contains no payload/staging files.
bool recordSessionWorker(const QString &root,const QString &directory,qint64 pid,QString &error);
SessionCleanup cleanAbandonedSessions(const QString &root,qint64 minimumAgeMs=60000);
}
