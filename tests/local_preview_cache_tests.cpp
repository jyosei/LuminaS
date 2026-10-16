// Standalone (QTEST_GUILESS_MAIN) tests for the bounded preview cache. Everything
// is deterministic and in-memory: no files, no images on disk, no unbounded sleeps.
// QImage only participates in payload construction; byte totals come from
// sizeInBytes()/capacity(), so they are format independent.
//
// Concurrency rules: builders block on a bounded tryAcquire(2000), and every
// blocking test installs a qScopeGuard that force-releases the semaphores and
// joins every launched future before the cache and captured locals are destroyed.
// An early QVERIFY return therefore cannot hang the suite or destroy a cache with
// a live builder.
#include <QtTest>
#include <QElapsedTimer>
#include <QScopeGuard>
#include <QSemaphore>
#include <QThread>
#include <QtConcurrentRun>
#include <atomic>
#include <memory>
#include <stdexcept>
#include "render/local_preview_cache.h"

using namespace LocalEdit;
namespace {
constexpr int kBlockMs=2000;
LocalPreviewCache::Ptr payload(int side){
    auto result=std::make_shared<LocalPreviewPayload>();
    result->pixels=QImage(side,side,QImage::Format_Grayscale8);
    result->pixels.fill(7);
    return result;
}
LocalPreviewCache::Ptr coveragePayload(int count){
    auto result=std::make_shared<LocalPreviewPayload>();
    result->coverage.resize(count);
    result->coverage.fill(.5f);
    return result;
}
LocalPreviewCache::Builder countingBuilder(int &calls,int side,QString failWith=QString()){
    return [&calls,side,failWith](QString &error)->LocalPreviewCache::Ptr{
        ++calls;
        if(!failWith.isEmpty()){error=failWith;return {};}
        return payload(side);
    };
}
bool waitUntil(const std::function<bool()> &done,int limitMs=kBlockMs){
    QElapsedTimer timer;timer.start();
    while(!done()){
        if(timer.elapsed()>limitMs)return false;
        QThread::msleep(5);
    }
    return true;
}
}
class LocalPreviewCacheTests:public QObject {
    Q_OBJECT
private slots:
    void payloadSizesAreDeterministic(){
        LocalPreviewPayload none;
        QCOMPARE(none.bytes(),qsizetype(0));
        QCOMPARE(payload(4)->bytes(),qsizetype(16));
        const auto coverage=coveragePayload(4);
        QVERIFY(coverage->coverage.capacity()>=4);
        QCOMPARE(coverage->bytes(),coverage->coverage.capacity()*qsizetype(sizeof(float)));
        QVERIFY(none.pixels.isNull()); // QImage has isNull(), never isEmpty()
        QVERIFY(none.coverage.isEmpty());
    }
    void negativeBudgetIsClampedToZero(){
        LocalPreviewCache cache(-1);
        QString error;
        int calls=0;
        const auto result=cache.getOrBuild(QByteArrayLiteral("neg"),countingBuilder(calls,4),error);
        QVERIFY(result!=nullptr);QVERIFY(error.isEmpty()); // delivered privately
        QCOMPARE(calls,1);
        const auto stats=cache.stats();
        QCOMPARE(stats.readyEntries,0);QCOMPARE(stats.retainedBytes,0);
        QCOMPARE(stats.uncached,quint64(1));
    }
    void getHitUsesBuilderOnce(){
        LocalPreviewCache cache(1<<20);
        int calls=0;
        QString error;
        const QByteArray key=QByteArrayLiteral("opaque-key-a");
        const auto first=cache.getOrBuild(key,countingBuilder(calls,8),error);
        QVERIFY(first!=nullptr);QVERIFY(error.isEmpty());
        const auto second=cache.getOrBuild(key,countingBuilder(calls,8),error);
        QVERIFY(second!=nullptr);QVERIFY(error.isEmpty());
        QCOMPARE(second.get(),first.get()); // same retained immutable payload
        QCOMPARE(calls,1);
        const auto stats=cache.stats();
        QCOMPARE(stats.hits,quint64(1));QCOMPARE(stats.builds,quint64(1));
        QCOMPARE(stats.joins,quint64(0));QCOMPARE(stats.uncached,quint64(0));
        QCOMPARE(stats.readyEntries,1);QCOMPARE(stats.inflightEntries,0);
        QCOMPARE(stats.retainedBytes,first->bytes());
    }
    void emptyKeyAndMissingBuilderAreRejected(){
        LocalPreviewCache cache(1<<20);
        int calls=0;
        QString error;
        QVERIFY(cache.getOrBuild(QByteArray(),countingBuilder(calls,4),error)==nullptr);
        QVERIFY(!error.isEmpty());QCOMPARE(calls,0);QCOMPARE(cache.stats().builds,quint64(0));
        error.clear();
        const LocalPreviewCache::Builder noBuilder;
        QVERIFY(cache.getOrBuild(QByteArrayLiteral("k"),noBuilder,error)==nullptr);
        QVERIFY(!error.isEmpty());
        QCOMPARE(cache.stats().readyEntries,0);
    }
    void failureIsNotCachedThenFreshBuildSucceeds(){
        LocalPreviewCache cache(1<<20);
        QString error;
        const QByteArray key=QByteArrayLiteral("k-fail");
        int failures=0;
        QVERIFY(cache.getOrBuild(key,countingBuilder(failures,6,QStringLiteral("boom")),error)==nullptr);
        QCOMPARE(error,QStringLiteral("boom"));
        QCOMPARE(cache.stats().builds,quint64(1));
        QCOMPARE(cache.stats().readyEntries,0);
        QCOMPARE(cache.stats().uncached,quint64(0)); // a failure is not an uncached result
        error.clear();
        int successes=0;
        const auto retry=cache.getOrBuild(key,countingBuilder(successes,6),error);
        QVERIFY(retry!=nullptr);QVERIFY(error.isEmpty());
        QCOMPARE(successes,1);QCOMPARE(failures,1);
        const auto stats=cache.stats();
        QCOMPARE(stats.builds,quint64(2));
        QCOMPARE(stats.retainedBytes,retry->bytes());
    }
    void errorWithPayloadAndBothChannelsAreFailures(){
        LocalPreviewCache cache(1<<20);
        QString error;
        // A non-null builder result paired with a nonempty error is still a failure.
        const auto errorWithPayload=[](QString &error)->LocalPreviewCache::Ptr{
            error=QStringLiteral("partial");
            return payload(4);
        };
        QVERIFY(cache.getOrBuild(QByteArrayLiteral("err-payload"),errorWithPayload,error)==nullptr);
        QCOMPARE(error,QStringLiteral("partial"));
        QCOMPARE(cache.stats().readyEntries,0);
        error.clear();
        // Both channels set is not a valid payload.
        const auto bothChannels=[](QString &)->LocalPreviewCache::Ptr{
            auto result=std::make_shared<LocalPreviewPayload>();
            result->pixels=QImage(4,4,QImage::Format_Grayscale8);
            result->coverage.resize(4);
            return result;
        };
        QVERIFY(cache.getOrBuild(QByteArrayLiteral("both"),bothChannels,error)==nullptr);
        QVERIFY(!error.isEmpty());QCOMPARE(cache.stats().readyEntries,0);
        error.clear();
        // Neither channel set is not a valid payload either.
        const auto noChannels=[](QString &)->LocalPreviewCache::Ptr{
            return std::make_shared<LocalPreviewPayload>();
        };
        QVERIFY(cache.getOrBuild(QByteArrayLiteral("none"),noChannels,error)==nullptr);
        QVERIFY(!error.isEmpty());QCOMPARE(cache.stats().readyEntries,0);
        QCOMPARE(cache.stats().hits,quint64(0));
    }
    void throwingBuilderIsFailure(){
        LocalPreviewCache cache(1<<20);
        QString error;
        const auto throwsStd=[](QString &)->LocalPreviewCache::Ptr{
            throw std::runtime_error("nope");
        };
        QVERIFY(cache.getOrBuild(QByteArrayLiteral("k-throw"),throwsStd,error)==nullptr);
        QCOMPARE(error,QStringLiteral("nope"));
        error.clear();
        const auto throwsOther=[](QString &)->LocalPreviewCache::Ptr{throw 42;};
        QVERIFY(cache.getOrBuild(QByteArrayLiteral("k-throw2"),throwsOther,error)==nullptr);
        QVERIFY(!error.isEmpty());
        const auto stats=cache.stats();
        QCOMPARE(stats.readyEntries,0);QCOMPARE(stats.inflightEntries,0);
        QCOMPARE(stats.uncached,quint64(0));
    }
    void repeatCallsAliasOneImmutablePayload(){
        LocalPreviewCache cache(1<<20);
        int calls=0;
        QString error;
        const auto built=cache.getOrBuild(QByteArrayLiteral("alias"),countingBuilder(calls,12),error);
        QVERIFY(built!=nullptr);
        const int width=built->pixels.width();
        const uchar sample=built->pixels.constScanLine(0)[0];
        const auto again=cache.getOrBuild(QByteArrayLiteral("alias"),countingBuilder(calls,12),error);
        QCOMPARE(again.get(),built.get());
        QCOMPARE(again->pixels.constScanLine(0)[0],sample);
        QCOMPARE(again->pixels.width(),width);
        QCOMPARE(calls,1);
        QCOMPARE(built.use_count(),qint64(3)); // cache + two test handles
    }
    void lruEvictsOldestUnpinned(){
        const qsizetype unit=payload(4)->bytes();
        LocalPreviewCache cache(3*unit);
        QString error;
        int calls=0;
        for(const char *key:{"a","b","c"})
            QVERIFY(cache.getOrBuild(QByteArray(key),countingBuilder(calls,4),error)!=nullptr);
        QCOMPARE(cache.stats().readyEntries,3);
        QCOMPARE(cache.stats().retainedBytes,3*unit);
        QVERIFY(cache.getOrBuild(QByteArrayLiteral("a"),countingBuilder(calls,4),error)!=nullptr);
        QCOMPARE(cache.stats().hits,quint64(1)); // touching "a" refreshes its stamp
        QVERIFY(cache.getOrBuild(QByteArrayLiteral("d"),countingBuilder(calls,4),error)!=nullptr);
        auto stats=cache.stats();
        QCOMPARE(stats.evictions,quint64(1)); // oldest unpinned "b" went first
        QCOMPARE(stats.readyEntries,3);QCOMPARE(stats.retainedBytes,3*unit);
        QCOMPARE(stats.uncached,quint64(0));
        error.clear();
        QVERIFY(cache.getOrBuild(QByteArrayLiteral("b"),countingBuilder(calls,4),error)!=nullptr);
        QCOMPARE(cache.stats().evictions,quint64(2)); // "b" had been evicted
        QVERIFY(cache.getOrBuild(QByteArrayLiteral("a"),countingBuilder(calls,4),error)!=nullptr);
        QCOMPARE(cache.stats().evictions,quint64(2)); // touched "a" survived
        QVERIFY(cache.stats().retainedBytes<=3*unit);
    }
    void retainedEntriesPinAndAreNeverEvicted(){
        const qsizetype unit=payload(4)->bytes();
        LocalPreviewCache cache(2*unit);
        QString error;
        int calls=0;
        const auto held=cache.getOrBuild(QByteArrayLiteral("held"),countingBuilder(calls,4),error);
        const auto alsoHeld=cache.getOrBuild(QByteArrayLiteral("other"),countingBuilder(calls,4),error);
        QVERIFY(held!=nullptr&&alsoHeld!=nullptr);
        auto stats=cache.stats();
        QCOMPARE(stats.readyEntries,2);QCOMPARE(stats.retainedBytes,2*unit);
        QCOMPARE(stats.pinnedBytes,2*unit);
        const auto extra=cache.getOrBuild(QByteArrayLiteral("third"),countingBuilder(calls,4),error);
        QVERIFY(extra!=nullptr);QVERIFY(error.isEmpty());
        stats=cache.stats();
        QCOMPARE(stats.evictions,quint64(0)); // nothing unpinned was evictable
        QCOMPARE(stats.uncached,quint64(1)); // returned privately instead
        QCOMPARE(stats.readyEntries,2);QCOMPARE(stats.retainedBytes,2*unit);
        QCOMPARE(extra.use_count(),qint64(1)); // the cache retained no reference
        QVERIFY(cache.getOrBuild(QByteArrayLiteral("held"),countingBuilder(calls,4),error)!=nullptr);
        stats=cache.stats();
        QCOMPARE(stats.hits,quint64(1)); // pinned "held" survived intact
        QCOMPARE(stats.readyEntries,2);QCOMPARE(stats.retainedBytes,2*unit);
        QCOMPARE(held.use_count(),qint64(2));
    }
    void droppedHandleUnpinsAndAllowsEviction(){
        const qsizetype unit=payload(4)->bytes();
        LocalPreviewCache cache(2*unit);
        QString error;
        int calls=0;
        {
            const auto temporary=cache.getOrBuild(QByteArrayLiteral("old"),countingBuilder(calls,4),error);
            QVERIFY(temporary!=nullptr);
        }
        QVERIFY(cache.getOrBuild(QByteArrayLiteral("keep"),countingBuilder(calls,4),error)!=nullptr);
        QCOMPARE(cache.stats().pinnedBytes,qsizetype(0));
        QVERIFY(cache.getOrBuild(QByteArrayLiteral("new"),countingBuilder(calls,4),error)!=nullptr);
        const auto stats=cache.stats();
        QCOMPARE(stats.evictions,quint64(1));
        QCOMPARE(stats.uncached,quint64(0));
        QCOMPARE(stats.readyEntries,2);QCOMPARE(stats.retainedBytes,2*unit);
    }
    void oversizeAndZeroBudgetAreUncached(){
        LocalPreviewCache zero(0);
        QString error;
        int calls=0;
        const auto none=zero.getOrBuild(QByteArrayLiteral("z"),countingBuilder(calls,4),error);
        QVERIFY(none!=nullptr);QVERIFY(error.isEmpty());QCOMPARE(calls,1);
        const auto zeroStats=zero.stats();
        QCOMPARE(zeroStats.uncached,quint64(1));
        QCOMPARE(zeroStats.readyEntries,0);QCOMPARE(zeroStats.retainedBytes,0);

        LocalPreviewCache tiny(payload(2)->bytes());
        const auto big=tiny.getOrBuild(QByteArrayLiteral("big"),countingBuilder(calls,64),error);
        QVERIFY(big!=nullptr);QVERIFY(error.isEmpty());
        const auto stats=tiny.stats();
        QCOMPARE(stats.evictions,quint64(0));QCOMPARE(stats.uncached,quint64(1));
        QCOMPARE(stats.readyEntries,0);QCOMPARE(stats.retainedBytes,0);
    }
    void preCancelSkipsBuilder(){
        LocalPreviewCache cache(1<<20);
        CancellationSource source;
        source.cancel();
        int calls=0;
        QString error;
        QVERIFY(cache.getOrBuild(QByteArrayLiteral("k"),countingBuilder(calls,4),error,source.token())==nullptr);
        QCOMPARE(error,QStringLiteral("Canceled"));
        QCOMPARE(calls,0);
        const auto stats=cache.stats();
        QCOMPARE(stats.builds,quint64(0));QCOMPARE(stats.inflightEntries,0);
    }
    void concurrentSameKeyBuildsOnce(){
        LocalPreviewCache cache(1<<20);
        QSemaphore entered,release;
        QString errorA,errorB;
        std::atomic_int calls{0};
        const QByteArray key=QByteArrayLiteral("shared");
        const auto builder=[&](QString &)->LocalPreviewCache::Ptr{
            ++calls;entered.release();
            if(!release.tryAcquire(1,kBlockMs))return {}; // bounded, never hangs a test
            return payload(4);
        };
        // The cleanup runs before future handles and captured locals are destroyed.
        QFuture<LocalPreviewCache::Ptr> first,second;
        const auto guard=qScopeGuard([&]{release.release(4);if(first.isStarted())first.waitForFinished();if(second.isStarted())second.waitForFinished();});
        first=QtConcurrent::run([&]{return cache.getOrBuild(key,builder,errorA);});
        second=QtConcurrent::run([&]{return cache.getOrBuild(key,builder,errorB);});
        QVERIFY(entered.tryAcquire(1,kBlockMs));
        QVERIFY(waitUntil([&]{return cache.stats().joins>=1;}));
        QCOMPARE(cache.stats().inflightEntries,1);
        release.release(2);
        first.waitForFinished();second.waitForFinished();
        const auto a=first.result(),b=second.result();
        QVERIFY(a!=nullptr&&b!=nullptr);
        QCOMPARE(calls.load(),1);
        QCOMPARE(a.get(),b.get()); // the waiter joined the owner's payload
        QVERIFY(errorA.isEmpty()&&errorB.isEmpty());
        const auto stats=cache.stats();
        QCOMPARE(stats.builds,quint64(1));QCOMPARE(stats.joins,quint64(1));
        QCOMPARE(stats.readyEntries,1);QCOMPARE(stats.inflightEntries,0);
    }
    void concurrentOversizeJoinersShareOneBuild(){
        // The build succeeds but cannot be retained: every current waiter must
        // still receive that exact payload with no error.
        LocalPreviewCache cache(0);
        QSemaphore entered,release;
        QString errorA,errorB;
        std::atomic_int calls{0};
        const QByteArray key=QByteArrayLiteral("oversize-shared");
        const auto builder=[&](QString &)->LocalPreviewCache::Ptr{
            ++calls;entered.release();
            if(!release.tryAcquire(1,kBlockMs))return {};
            return payload(8);
        };
        QFuture<LocalPreviewCache::Ptr> first,second;
        const auto guard=qScopeGuard([&]{release.release(4);if(first.isStarted())first.waitForFinished();if(second.isStarted())second.waitForFinished();});
        first=QtConcurrent::run([&]{return cache.getOrBuild(key,builder,errorA);});
        second=QtConcurrent::run([&]{return cache.getOrBuild(key,builder,errorB);});
        QVERIFY(entered.tryAcquire(1,kBlockMs));
        QVERIFY(waitUntil([&]{return cache.stats().joins>=1;}));
        release.release(2);
        first.waitForFinished();second.waitForFinished();
        const auto a=first.result(),b=second.result();
        QVERIFY(a!=nullptr&&b!=nullptr); // no "Cache invalidated" for waiters
        QVERIFY(errorA.isEmpty()&&errorB.isEmpty());
        QCOMPARE(calls.load(),1);
        QCOMPARE(a.get(),b.get()); // shared, not rebuilt
        const auto stats=cache.stats();
        QCOMPARE(stats.builds,quint64(1));QCOMPARE(stats.joins,quint64(1));
        QCOMPARE(stats.uncached,quint64(1));
        QCOMPARE(stats.readyEntries,0);QCOMPARE(stats.retainedBytes,0);
        QCOMPARE(stats.evictions,quint64(0));
    }
    void clearWhileBuildingNeverPublishes(){
        LocalPreviewCache cache(1<<20);
        QSemaphore entered,release;
        std::atomic_int calls{0};
        const QByteArray key=QByteArrayLiteral("doomed");
        QString ownerError;
        const auto builder=[&](QString &)->LocalPreviewCache::Ptr{
            ++calls;entered.release();
            if(!release.tryAcquire(1,kBlockMs))return {};
            return payload(4);
        };
        QFuture<LocalPreviewCache::Ptr> owner;
        const auto guard=qScopeGuard([&]{release.release(4);if(owner.isStarted())owner.waitForFinished();});
        owner=QtConcurrent::run([&]{return cache.getOrBuild(key,builder,ownerError);});
        QVERIFY(entered.tryAcquire(1,kBlockMs));
        QString joinerError;
        auto joiner=QtConcurrent::run([&]{return cache.getOrBuild(key,builder,joinerError);});
        const auto joinerGuard=qScopeGuard([&]{
            release.release(4);
            joiner.waitForFinished();
        });
        QVERIFY(waitUntil([&]{return cache.stats().joins>=1;}));
        cache.clear();
        QVERIFY(waitUntil([&]{return joiner.isFinished();}));
        joiner.waitForFinished();
        QCOMPARE(joinerError,QStringLiteral("Cache invalidated"));
        release.release(2);
        owner.waitForFinished();joiner.waitForFinished();
        const auto owned=owner.result();
        QVERIFY(owned!=nullptr); // private result may still be returned
        QVERIFY(ownerError.isEmpty()); // cleared but not canceled: no error
        QVERIFY(joiner.result()==nullptr);
        QCOMPARE(calls.load(),1);
        const auto stats=cache.stats();
        QCOMPARE(stats.readyEntries,0);QCOMPARE(stats.retainedBytes,0);
        QCOMPARE(stats.hits,quint64(0));QCOMPARE(stats.inflightEntries,0);
    }
    void clearThenRebuildSameKeyKeepsNewEntry(){
        // The old owner is still blocked when a new build for the SAME key lands.
        // Releasing the old owner must not overwrite or remove the new entry.
        LocalPreviewCache cache(1<<20);
        QSemaphore entered,release;
        std::atomic_int oldCalls{0};
        const QByteArray key=QByteArrayLiteral("same-key");
        QString oldError;
        const auto oldBuilder=[&](QString &)->LocalPreviewCache::Ptr{
            ++oldCalls;entered.release();
            if(!release.tryAcquire(1,kBlockMs))return {};
            return payload(4); // distinct payload, allocated before the new one only on release
        };
        auto oldOwner=QtConcurrent::run([&]{return cache.getOrBuild(key,oldBuilder,oldError);});
        const auto guard=qScopeGuard([&]{
            release.release(4);
            oldOwner.waitForFinished();
        });
        QVERIFY(entered.tryAcquire(1,kBlockMs));
        cache.clear();
        QVERIFY(waitUntil([&]{return cache.stats().inflightEntries==0;}));
        int newCalls=0;
        const auto fresh=cache.getOrBuild(key,countingBuilder(newCalls,6),oldError);
        QVERIFY(fresh!=nullptr);QVERIFY(oldError.isEmpty());
        QCOMPARE(newCalls,1);
        QCOMPARE(cache.stats().readyEntries,1);
        QCOMPARE(cache.stats().retainedBytes,fresh->bytes());
        // Release the old owner: it must return privately and publish nothing.
        release.release(1);
        oldOwner.waitForFinished();
        const auto stale=oldOwner.result();
        QVERIFY(stale!=nullptr); // its own private result
        QVERIFY(oldError.isEmpty());
        const auto afterHit=cache.getOrBuild(key,countingBuilder(newCalls,6),oldError);
        QVERIFY(afterHit!=nullptr);
        QCOMPARE(newCalls,1); // still a hit: the new entry was never disturbed
        QCOMPARE(afterHit.get(),fresh.get());
        QCOMPARE(afterHit->pixels.width(),6); // the NEW payload, not the old one
        QCOMPARE(cache.stats().hits,quint64(1));
        QCOMPARE(cache.stats().readyEntries,1);
        QCOMPARE(cache.stats().retainedBytes,fresh->bytes());
    }
    void waiterCancelDoesNotStopRunningOwner(){
        LocalPreviewCache cache(1<<20);
        QSemaphore entered,release;
        std::atomic_int calls{0};
        const QByteArray key=QByteArrayLiteral("owner-survives");
        QString ownerError;
        const auto builder=[&](QString &)->LocalPreviewCache::Ptr{
            ++calls;entered.release();
            if(!release.tryAcquire(1,kBlockMs))return {};
            return payload(4);
        };
        auto owner=QtConcurrent::run([&]{return cache.getOrBuild(key,builder,ownerError);});
        const auto guard=qScopeGuard([&]{
            release.release(4);
            owner.waitForFinished();
        });
        QVERIFY(entered.tryAcquire(1,kBlockMs));
        CancellationSource waiterCancel;
        QString waiterError;
        auto waiter=QtConcurrent::run([&]{
            return cache.getOrBuild(key,builder,waiterError,waiterCancel.token());
        });
        const auto waiterGuard=qScopeGuard([&]{
            release.release(4);
            waiter.waitForFinished();
        });
        QVERIFY(waitUntil([&]{return cache.stats().joins>=1;}));
        waiterCancel.cancel();
        waiter.waitForFinished();
        QVERIFY(waiter.result()==nullptr);
        QCOMPARE(waiterError,QStringLiteral("Canceled"));
        QCOMPARE(calls.load(),1); // the running owner was not canceled
        release.release(1);
        owner.waitForFinished();
        const auto owned=owner.result();
        QVERIFY(owned!=nullptr);QVERIFY(ownerError.isEmpty());
        const auto stats=cache.stats();
        QCOMPARE(stats.readyEntries,1);QCOMPARE(stats.retainedBytes,owned->bytes());
        int probes=0;
        QVERIFY(cache.getOrBuild(key,countingBuilder(probes,4),waiterError)!=nullptr);
        QCOMPARE(probes,0); // served from the cache
        QCOMPARE(cache.stats().hits,quint64(1));
    }
    void canceledKeyDoesNotInvalidateOtherKey(){
        // Key A's owner is canceled while key B is being built concurrently: B's
        // waiters must still receive B's payload, never "Cache invalidated".
        LocalPreviewCache cache(1<<20);
        QSemaphore enteredA,enteredB,releaseA,releaseB;
        CancellationSource cancelA;
        QString errorA,errorB1,errorB2;
        const auto builderA=[&](QString &)->LocalPreviewCache::Ptr{
            enteredA.release();
            if(!releaseA.tryAcquire(1,kBlockMs))return {};
            return payload(4);
        };
        const auto builderB=[&](QString &)->LocalPreviewCache::Ptr{
            enteredB.release();
            if(!releaseB.tryAcquire(1,kBlockMs))return {};
            return payload(6);
        };
        auto ownerA=QtConcurrent::run([&]{
            return cache.getOrBuild(QByteArrayLiteral("key-a"),builderA,errorA,cancelA.token());
        });
        auto ownerB=QtConcurrent::run([&]{
            return cache.getOrBuild(QByteArrayLiteral("key-b"),builderB,errorB1);
        });
        const auto guard=qScopeGuard([&]{
            releaseA.release(4);releaseB.release(4);
            ownerA.waitForFinished();ownerB.waitForFinished();
        });
        QVERIFY(enteredA.tryAcquire(1,kBlockMs));
        QVERIFY(enteredB.tryAcquire(1,kBlockMs));
        auto joinerB=QtConcurrent::run([&]{
            return cache.getOrBuild(QByteArrayLiteral("key-b"),builderB,errorB2);
        });
        const auto joinerBGuard=qScopeGuard([&]{
            releaseB.release(4);
            joinerB.waitForFinished();
        });
        QVERIFY(waitUntil([&]{return cache.stats().joins>=1;}));
        cancelA.cancel();
        releaseA.release(1);
        ownerA.waitForFinished();
        QVERIFY(ownerA.result()==nullptr);
        QCOMPARE(errorA,QStringLiteral("Canceled"));
        releaseB.release(2);
        ownerB.waitForFinished();joinerB.waitForFinished();
        QVERIFY(ownerB.result()!=nullptr);QVERIFY(errorB1.isEmpty());
        QVERIFY(joinerB.result()!=nullptr);QVERIFY(errorB2.isEmpty());
        QCOMPARE(ownerB.result().get(),joinerB.result().get());
        const auto stats=cache.stats();
        QCOMPARE(stats.readyEntries,1);QCOMPARE(stats.retainedBytes,ownerB.result()->bytes());
        QCOMPARE(stats.hits,quint64(0));
    }
    void buildErrorIsSurfacedToWaitersAndIsRetryable(){
        LocalPreviewCache cache(1<<20);
        QSemaphore entered,release;
        std::atomic_int calls{0};
        const QByteArray key=QByteArrayLiteral("bad-shared");
        const auto builder=[&](QString &error)->LocalPreviewCache::Ptr{
            ++calls;entered.release();
            if(!release.tryAcquire(1,kBlockMs))return {};
            error=QStringLiteral("builder failed");
            return LocalPreviewCache::Ptr();
        };
        QString ownerError;
        auto owner=QtConcurrent::run([&]{return cache.getOrBuild(key,builder,ownerError);});
        const auto guard=qScopeGuard([&]{
            release.release(4);
            owner.waitForFinished();
        });
        QVERIFY(entered.tryAcquire(1,kBlockMs));
        QString waiterError;
        auto waiter=QtConcurrent::run([&]{return cache.getOrBuild(key,builder,waiterError);});
        const auto waiterGuard=qScopeGuard([&]{
            release.release(4);
            waiter.waitForFinished();
        });
        QVERIFY(waitUntil([&]{return cache.stats().joins>=1;}));
        release.release(2);
        owner.waitForFinished();waiter.waitForFinished();
        QVERIFY(owner.result()==nullptr);
        QCOMPARE(ownerError,QStringLiteral("builder failed"));
        QVERIFY(waiter.result()==nullptr);
        QCOMPARE(waiterError,QStringLiteral("builder failed")); // real reason, not generic
        QCOMPARE(calls.load(),1);
        auto stats=cache.stats();
        QCOMPARE(stats.readyEntries,0);QCOMPARE(stats.inflightEntries,0);
        QCOMPARE(stats.uncached,quint64(0));QCOMPARE(stats.evictions,quint64(0));
        QVERIFY(cache.getOrBuild(key,builder,waiterError)==nullptr); // fresh call rebuilds
        QCOMPARE(calls.load(),2);
    }
    void clearKeepsOutsideHandlesAndResetsCurrentState(){
        LocalPreviewCache cache(1<<20);
        int calls=0;
        QString error;
        const auto real=cache.getOrBuild(QByteArrayLiteral("keep"),countingBuilder(calls,8),error);
        QVERIFY(real!=nullptr);
        const uchar sample=real->pixels.constScanLine(0)[0];
        QVERIFY(cache.getOrBuild(QByteArrayLiteral("keep"),countingBuilder(calls,8),error)!=nullptr);
        QCOMPARE(cache.stats().hits,quint64(1));
        cache.clear();
        const auto stats=cache.stats();
        QCOMPARE(stats.readyEntries,0);QCOMPARE(stats.retainedBytes,0);
        QCOMPARE(stats.pinnedBytes,0);QCOMPARE(stats.inflightEntries,0);
        QCOMPARE(stats.hits,quint64(1)); // monotonic counters survive clear()
        QCOMPARE(stats.builds,quint64(1));
        QCOMPARE(real->pixels.constScanLine(0)[0],sample); // outside handle stays valid
        QVERIFY(cache.getOrBuild(QByteArrayLiteral("keep"),countingBuilder(calls,8),error)!=nullptr);
        QCOMPARE(cache.stats().builds,quint64(2)); // the entry was really gone
    }
    void statsTrackInflightAndCoveragePayloads(){
        LocalPreviewCache cache(1<<20);
        QSemaphore entered,release;
        QString error;
        const auto builder=[&](QString &)->LocalPreviewCache::Ptr{
            entered.release();
            if(!release.tryAcquire(1,kBlockMs))return {};
            return coveragePayload(8);
        };
        auto call=QtConcurrent::run([&]{return cache.getOrBuild(QByteArrayLiteral("cov"),builder,error);});
        const auto guard=qScopeGuard([&]{
            release.release(4);
            call.waitForFinished();
        });
        QVERIFY(entered.tryAcquire(1,kBlockMs));
        QCOMPARE(cache.stats().inflightEntries,1);
        QCOMPARE(cache.stats().readyEntries,0);
        release.release(1);
        call.waitForFinished();
        const auto result=call.result();
        QVERIFY(result!=nullptr);QVERIFY(error.isEmpty());
        const auto stats=cache.stats();
        QCOMPARE(stats.inflightEntries,0);QCOMPARE(stats.readyEntries,1);
        QCOMPARE(stats.retainedBytes,result->bytes());
        QCOMPARE(stats.pinnedBytes,result->bytes()); // the test handle still pins it
        QVERIFY(result->pixels.isNull()&&!result->coverage.isEmpty());
    }
};
QTEST_GUILESS_MAIN(LocalPreviewCacheTests)
#include "local_preview_cache_tests.moc"
