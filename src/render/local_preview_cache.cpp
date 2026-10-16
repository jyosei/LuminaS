#include "local_preview_cache.h"
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QWaitCondition>
#include <exception>

namespace LocalEdit {
namespace {
constexpr int kPollMs=20;
const char *kCanceled="Canceled";
const char *kInvalidated="Cache invalidated";
const char *kNoPayload="preview build returned no usable payload";
const char *kBuildFailed="preview build failed";
// A build is usable only when exactly one channel carries data.
bool usablePayload(const LocalPreviewCache::Ptr &payload){
    return payload&&(payload->pixels.isNull()!=payload->coverage.isEmpty());
}
void setError(QString &error,const char *text){
    if(error.isEmpty())error=QString::fromLatin1(text);
}
}
struct LocalPreviewCache::Impl {
    // Entries are shared objects with stable identity: waiters keep the object
    // alive independently of the map, so removal or replacement cannot redirect
    // them to a different build's outcome.
    struct Entry {
        Ptr ready;                 // published, owned by the cache
        QString failure;           // set when the build failed or was canceled
        bool done=false;           // the owner finished; outcome is final
        bool canceled=false;       // the owner observed its own cancellation
        quint64 stamp=0;           // LRU stamp, refreshed on hits
        int waiters=0;             // waiters that have not been reaped yet
        bool reaped=false;         // a waiter already removed it from the map
    };
    using EntryPtr=std::shared_ptr<Entry>;
    explicit Impl(qsizetype limit):budget(limit<0?0:limit){}
    qsizetype budget;
    mutable QMutex mutex;
    QWaitCondition condition;
    QHash<QByteArray,EntryPtr> entries;
    quint64 generation=0,clock=0;
    Stats counters; // monotonic counters only; current state is measured on demand
};
LocalPreviewCache::LocalPreviewCache(qsizetype budget):impl(new Impl(budget)){}
LocalPreviewCache::~LocalPreviewCache()=default;

LocalPreviewCache::Ptr LocalPreviewCache::getOrBuild(const QByteArray &key,
    const Builder &builder,QString &error,const CancellationToken &cancel)
{
    error.clear();
    if(key.isEmpty()){error=QStringLiteral("cache key must not be empty");return {};}
    if(isCanceled(cancel)){setError(error,kCanceled);return {};}
    QMutexLocker lock(&impl->mutex);
    if(isCanceled(cancel)){setError(error,kCanceled);return {};}
    const quint64 myGeneration=impl->generation;
    Impl::EntryPtr entry=impl->entries.value(key);
    if(entry&&entry->ready){ // ready hit
        entry->stamp=++impl->clock;
        ++impl->counters.hits;
        return entry->ready;
    }
    if(entry&&!entry->done){ // join the live owner
        ++impl->counters.joins;
        ++entry->waiters;
        // Wait on the entry object, never on a map slot: clear(), a failure or a
        // retry can replace the map value without disturbing this observation.
        while(!entry->done&&myGeneration==impl->generation){
            if(isCanceled(cancel))break; // this waiter's own cancel wins first
            impl->condition.wait(&impl->mutex,kPollMs);
        }
        // This waiter's cancellation takes priority over any ready outcome.
        const bool canceledHere=isCanceled(cancel);
        const bool cleared=myGeneration!=impl->generation;
        const bool ready=static_cast<bool>(entry->ready);
        if(ready)entry->stamp=++impl->clock;
        const bool ownerCanceled=entry->canceled;
        const QString failure=entry->failure;
        // A failed or cancelled row is dropped immediately, once no waiter can
        // still be reading it; successful and uncached rows stay for later hits.
        if(entry->done&&!ready&&entry->waiters<=1&&!entry->reaped
            &&impl->entries.value(key)==entry)
        {
            impl->entries.remove(key);
            entry->reaped=true;
        }
        if(--entry->waiters<0)entry->waiters=0;
        if(canceledHere){setError(error,kCanceled);return {};}
        if(cleared){setError(error,kInvalidated);return {};}
        if(ready)return entry->ready;
        if(!failure.isEmpty()){if(error.isEmpty())error=failure;return {};}
        if(ownerCanceled){setError(error,kCanceled);return {};}
        setError(error,kBuildFailed);
        return {};
    }
    if(entry){ // a finished-but-unusable row was left behind: replace it
        impl->entries.remove(key);
    }
    // Become the single owner for this key.
    Impl::EntryPtr owned(new Impl::Entry());
    impl->entries.insert(key,owned);
    ++impl->counters.builds;
    lock.unlock();

    // The builder runs outside the mutex; the cache never observes a partial value.
    Ptr produced;
    try{
        produced=builder?builder(error):Ptr();
    }catch(const std::exception &failure){
        if(error.isEmpty())error=QString::fromUtf8(failure.what());
        setError(error,kBuildFailed);
        produced.reset();
    }catch(...){
        setError(error,kBuildFailed);
        produced.reset();
    }
    const bool usable=usablePayload(produced);
    // A non-null builder result paired with a nonempty error is still a failure.
    const bool failed=!usable||!error.isEmpty()||!builder;
    if(failed)produced.reset();

    lock.relock();
    // Publish only while this generation holds and the map still points at our own
    // entry: a clear() plus a new build for the same key must never let the old
    // owner overwrite or remove the new entry.
    const bool stillOwner=impl->generation==myGeneration
        &&impl->entries.value(key)==owned;
    owned->done=true;
    if(!stillOwner){
        owned->canceled=false;
        owned->failure.clear();
        impl->condition.wakeAll();
        lock.unlock();
        if(isCanceled(cancel)){error=QString::fromLatin1(kCanceled);return {};} // no result after cancel
        // Cleared but not canceled: hand back the private result, publish nothing.
        if(produced)error.clear();
        else if(error.isEmpty())setError(error,kNoPayload);
        return produced;
    }
    if(isCanceled(cancel)){
        // A canceled owner publishes nothing and reports exactly "Canceled"; the
        // waiters observe the same outcome through the shared entry.
        owned->canceled=true;
        owned->failure=QString::fromLatin1(kCanceled);
        impl->entries.remove(key);
        owned->reaped=true;
        impl->condition.wakeAll();
        lock.unlock();
        error=QString::fromLatin1(kCanceled);
        return {};
    }
    if(failed){
        // The row is not ready, so it is not a hit; waiters read failure from the
        // shared entry while later calls start a fresh build.
        owned->failure=error.isEmpty()?QString::fromLatin1(kNoPayload):error;
        error=owned->failure;
        impl->entries.remove(key);
        owned->reaped=true;
        impl->condition.wakeAll();
        lock.unlock();
        return {};
    }
    // Admit the payload. Budget arithmetic is ordered so a huge incoming payload
    // can never overflow the comparison: first incoming<=budget, then retained.
    qsizetype retained=0;
    for(auto it=impl->entries.cbegin();it!=impl->entries.cend();++it){
        if(it.value()->ready)retained+=it.value()->ready->bytes();
    }
    const qsizetype incoming=produced->bytes();
    bool fits=incoming<=impl->budget;
    while(fits&&retained>impl->budget-incoming){
        auto victim=impl->entries.end();
        for(auto it=impl->entries.begin();it!=impl->entries.end();++it){
            const Impl::EntryPtr &candidate=it.value();
            if(!candidate->ready||candidate->ready.use_count()>1)continue;
            if(victim==impl->entries.end()||candidate->stamp<victim.value()->stamp)victim=it;
        }
        if(victim==impl->entries.end()){fits=false;break;}
        retained-=victim.value()->ready->bytes();
        impl->entries.erase(victim);
        ++impl->counters.evictions;
    }
    if(fits){
        // The entry object is unchanged, so current waiters keep observing it.
        owned->ready=produced;
        owned->stamp=++impl->clock;
        owned->failure.clear();
        impl->condition.wakeAll();
        lock.unlock();
        return produced;
    }
    // Oversize, zero budget or every candidate pinned: the payload is delivered
    // privately and never retained. Current waiters must still receive this exact
    // payload, so it is published on the shared entry while the map drops the row
    // (later calls therefore rebuild instead of taking a hit).
    ++impl->counters.uncached;
    owned->ready=produced;
    owned->stamp=++impl->clock;
    owned->failure.clear();
    impl->entries.remove(key);
    owned->reaped=true;
    impl->condition.wakeAll();
    lock.unlock();
    return produced;
}

void LocalPreviewCache::clear(){
    QMutexLocker lock(&impl->mutex);
    ++impl->generation;
    impl->entries.clear();
    impl->condition.wakeAll();
}

LocalPreviewCache::Stats LocalPreviewCache::stats() const{
    QMutexLocker lock(&impl->mutex);
    Stats snapshot=impl->counters;
    snapshot.readyEntries=0;
    snapshot.inflightEntries=0;
    snapshot.retainedBytes=0;
    snapshot.pinnedBytes=0;
    for(auto it=impl->entries.cbegin();it!=impl->entries.cend();++it){
        const Impl::EntryPtr &entry=it.value();
        if(!entry->ready){
            if(!entry->done)++snapshot.inflightEntries;
            continue;
        }
        const qsizetype size=entry->ready->bytes();
        ++snapshot.readyEntries;
        snapshot.retainedBytes+=size;
        if(entry->ready.use_count()>1)snapshot.pinnedBytes+=size;
    }
    return snapshot;
}
}
