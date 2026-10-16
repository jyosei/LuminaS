#pragma once
#include "core/cancellation.h"
#include <QByteArray>
#include <QImage>
#include <QString>
#include <QVector>
#include <functional>
#include <memory>

namespace LocalEdit {
// Immutable cache value: a rendered frame or a coverage channel.
// Exactly one of pixels/coverage must be nonempty for a cache build to succeed.
struct LocalPreviewPayload {
    QImage pixels;
    QVector<float> coverage;
    qsizetype bytes() const{
        return pixels.sizeInBytes()+coverage.capacity()*qsizetype(sizeof(float));
    }
};

// Bounded, thread-safe, get-or-build cache of opaque byte-string keys. The key is
// never interpreted or hashed here; callers own key derivation. Keys are copied.
//
// Ownership: entries are shared objects, not map slots. An owner captures its own
// entry handle and the generation it started in, and may publish only while that
// generation still holds and the map still points at its own entry. clear() is the
// only operation that advances the generation, so a cancellation, a failure or an
// uncached result never invalidates unrelated keys. Waiters hold the same entry
// handle, so they observe the outcome directly and a retry or removal cannot
// hijack them; a successful but unretained build is still delivered to every
// current waiter as the same payload.
//
// Lifetime: callers join every in-flight call before destroying the cache; the
// destructor does not wait and must not be used to abandon running builders. A
// negative budget is clamped to zero (nothing is ever retained).
class LocalPreviewCache {
public:
    using Ptr=std::shared_ptr<const LocalPreviewPayload>;
    using Builder=std::function<Ptr(QString &error)>;
    struct Stats {
        quint64 hits=0,builds=0,joins=0,evictions=0,uncached=0;
        qsizetype retainedBytes=0,pinnedBytes=0;
        int readyEntries=0,inflightEntries=0;
    };
    explicit LocalPreviewCache(qsizetype budget=256*1024*1024);
    ~LocalPreviewCache();
    LocalPreviewCache(const LocalPreviewCache &)=delete;
    LocalPreviewCache &operator=(const LocalPreviewCache &)=delete;
    // Ready hit or a successful build returns a payload. A canceled call, a failed
    // build or an invalid payload returns null with error set. A canceled owner
    // reports exactly "Canceled"; a waiter whose generation was cleared reports
    // "Cache invalidated".
    Ptr getOrBuild(const QByteArray &key,const Builder &builder,QString &error,
        const CancellationToken &cancel={});
    // Advances the generation, drops every map entry and wakes all waiters.
    void clear();
    Stats stats() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
