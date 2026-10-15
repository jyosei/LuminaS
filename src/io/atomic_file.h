#pragma once
#include "core/cancellation.h"
#include <QByteArrayView>
#include <QString>
#include <functional>
class QIODevice;

namespace Persistence {
struct AtomicWriteStats {int attempts=0;qint64 elapsedMs=0;};
// The body must replay the same frozen bytes from the beginning on every call.
// Only Windows RenameError is retried, at most 8 attempts within a 20ms retry
// budget; no sleep, event pumping, target deletion or direct-write fallback.
// The time budget governs starting retries, not the duration of OS I/O calls.
using AtomicBody=std::function<bool(QIODevice &,QString &)>;
bool writeAtomicFile(const QString &path,const AtomicBody &body,QString &error,
    CancellationToken cancel={},AtomicWriteStats *stats=nullptr);
bool writeAtomicBytes(const QString &path,QByteArrayView bytes,QString &error,
    CancellationToken cancel={},AtomicWriteStats *stats=nullptr);
}
