#pragma once
#include "core/cancellation.h"
#include "core/document.h"
#include "core/photo_document.h"
#include <QFutureWatcher>
#include <QThreadPool>
#include <optional>
#include "io/preview_cache.h"
#include <QSet>
#include <QDateTime>
#include <atomic>
#include "render/spatial_operator.h"
#include "render/cpu_renderer.h"
namespace LocalEdit {class LocalPreviewCache;}
struct EditTag {quint64 session=0,revision=0,draft=0,job=0,geometry=0;bool interactive=false;quint64 localIdentity=0,viewEpoch=0;bool operator==(const EditTag &) const = default;};
Q_DECLARE_METATYPE(EditTag)
// Same-generation identity: session/revision/geometry/local identity/view epoch must all
// match. draft and job are delivery ordering, never identity.
inline bool sameEditGeneration(const EditTag &a,const EditTag &b){
    return a.session==b.session&&a.revision==b.revision&&a.geometry==b.geometry&&a.localIdentity==b.localIdentity&&a.viewEpoch==b.viewEpoch;
}
// Exact match, or interactive feedback from the same generation (path and RenderView are
// checked separately by the caller). Never widens non-interactive or cross-view results.
inline bool acceptsEditTag(const EditTag &actual,const EditTag &expected){
    return actual==expected||((actual.interactive&&expected.interactive)&&sameEditGeneration(actual,expected));
}
struct EditResult {QString path,error,colorNote,sourceHash;QImage image;QSize originalSize,fullSize;EditTag tag;double elapsedMs=0,angle=0;Recipe recipe;bool prefetch=false,sourceRebuilt=false; qint64 sourceBytes=0;QDateTime sourceMtime;SourceFrame workFrame;QRectF workCrop;double workAngle=0;RenderView view;QPointF workPerspective;double workDistortion=0,workLensShading=0;QPointF workChromaticAberration;std::shared_ptr<const SourceFrame> fullGeometry;LocalEdit::DocumentSnapshot document;QImage localMask;double effectElapsedMs=0,maskElapsedMs=0;};
Q_DECLARE_METATYPE(EditResult)
class EditLoader:public QObject {
    Q_OBJECT
public:
    explicit EditLoader(QObject *parent=nullptr);~EditLoader() override;
    void request(const QString &path,const Recipe &recipe,EditTag tag,const RenderView &view={});
    bool requestDocument(const QString &path,LocalEdit::DocumentSnapshot document,EditTag tag,
        QString &error,const RenderView &view={});
    void prefetch(const QStringList &paths);
    std::optional<EditResult> preview(const QString &path,const Recipe &recipe);
    bool pickNeutral(const QString &path,const Recipe &recipe,EditTag tag,QPointF uv,Recipe &picked,QString &error) const;
    bool canPickNeutral(const QString &path,const Recipe &recipe,EditTag tag) const;
    bool autoBalance(const QString &path,const Recipe &recipe,EditTag tag,Recipe &estimated,QString &error) const;
    bool requestPointColorSample(const QString &path,const Recipe &recipe,EditTag tag,QPointF uv,QString &error);
    bool canSamplePointColor(const QString &path,const Recipe &recipe,EditTag tag) const;
    void cancelPointColorSample(){++pointSampleGeneration;}
    void invalidate();
    void resetImports();
signals:
    // Diagnostic enqueue point, on the GUI thread and before worker dispatch.
    // Paired with actual frame provenance; ready is not proof of a screen paint.
    void requestQueued(const QString &path,EditTag tag);
    void pointColorSampleStateChanged();
    void pointColorSampled(const QString &path,EditTag tag,PointColorAdjustment point,const QString &error);
    void ready(const EditResult &result);
    void prefetched(const EditResult &result);
    void prefetchProgress(int completed,int total,int failures);
private:
    struct PointSampleResult {QString path,error;Recipe recipe;EditTag tag;PointColorAdjustment point;quint64 generation=0;};
    QThreadPool pointSamplePool;
    QFutureWatcher<PointSampleResult> pointSampleWatcher;
    bool pointSampleBusy=false;
    quint64 pointSampleGeneration=0;
    struct Request {QString path;Recipe recipe;EditTag tag;bool prefetch=false;SourceFrame base;double baseAngle=0;qint64 baseBytes=0;QDateTime baseMtime;RenderView view;std::shared_ptr<const SourceFrame> fullGeometry;CancellationToken cancel;LocalEdit::DocumentSnapshot document;};void dispatch();
    void queueRequest(const QString &path,const Recipe &recipe,EditTag tag,const RenderView &view,
        LocalEdit::DocumentSnapshot document);
    static bool compatible(const Request &active,const Request &next);
    bool acceptsResult(const EditResult &result) const;
    EditResult process(const Request &request);
    EditResult processLive(const Request &request);
    void dispatchLive();
    void remember(const EditResult &result);
    QThreadPool pool;QFutureWatcher<EditResult> watcher;
    QThreadPool prefetchPool;QFutureWatcher<EditResult> prefetchWatcher;
    bool prefetchBusy=false;
    QThreadPool livePool;QFutureWatcher<EditResult> liveWatcher;
    bool liveBusy=false;std::optional<Request> livePending;
    SourceFrame lastBase;QRectF lastCrop;double lastAngle=0;QPointF lastPerspective;double lastDistortion=0,lastLensShading=0;QPointF lastChromaticAberration;std::shared_ptr<const SourceFrame> lastFullGeometry;
    qint64 lastBytes=0;QDateTime lastMtime;
    Recipe latestRecipe;
    RenderView latestView;
    bool busy=false;std::atomic_bool closing{false};std::optional<Request> pending;
    CancellationSource currentCancel,liveCancel,prefetchCancel;
    std::optional<Request> currentRequest,liveRequest,prefetchRequest;
    bool hasCurrentRequest=false;
    EditTag latest;
    QString latestPath;
    SourceFrame proxy;
    QRectF proxyCrop;
    double proxyAngle=0;QPointF proxyPerspective;double proxyDistortion=0,proxyLensShading=0;QPointF proxyChromaticAberration;
    std::shared_ptr<const SourceFrame> cached; // immutable source shared with perspective live requests
    PreviewCache workCache;
    SpatialCache spatialCache;
    std::unique_ptr<LocalEdit::LocalPreviewCache> localPreviewCache;
    QStringList prefetchQueue,displayOrder;
    QSet<QString> imported;
    QHash<QString,EditResult> displayCache;
    qint64 displayBytes=0;
    int completed=0,failures=0;
};



