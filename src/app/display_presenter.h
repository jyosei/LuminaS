#ifndef LUMINA_APP_DISPLAY_PRESENTER_H
#define LUMINA_APP_DISPLAY_PRESENTER_H

#include "app/display_queue.h"
#include <QByteArray>
#include <QFutureWatcher>
#include <QImage>
#include <QObject>
#include <QString>

class QTimer;

struct MonitorProfileLookupResult {
    QString screenName,note;
    QByteArray icc;
};

// Bounded display presentation plumbing: no colour maths.
//
// DisplayPresenter owns the canonical (source-tagged) image and the single
// DisplayQueue that converts it for the current monitor profile. It never
// inspects ICC bytes, never reads the registry, EDID or any OS setting, and
// never touches Recipe, rendering or export: it only decides *which* canonical
// image and *which* profile bytes are handed to the injected GPT transform.
//
// Rules implemented here:
//  * a new context or a new canonical size starts a new epoch, invalidates the
//    queue and asks the canvas to clear the old presentation, so frames of the
//    previous epoch can never be committed;
//  * same-context updates keep the running intermediate job and only replace
//    the newest waiting request (DisplayQueue behaviour) so live editing stays
//    responsive;
//  * frames are delivered only when the queue context equals the current epoch;
//  * a transform error for a plain sRGB canonical falls back to the canonical
//    pixels with sRGB and a clear note; when the canonical carries a custom
//    tag the failure is reported instead of faking a conversion;
//  * a manual monitor profile overrides the OS query for QA and the public API
//    and starts no OS query at all; restoring the system profile re-requests
//    the canonical frame for the pending screen.
class DisplayPresenter:public QObject {
    Q_OBJECT
public:
    explicit DisplayPresenter(QObject *parent=nullptr);
    // Test/QA seam: injects a different conversion entry point. Must be set
    // before any setImage/setMonitorProfile call; the default is the GPT-owned
    // transformDisplay and no maths is added here.
    explicit DisplayPresenter(DisplayQueue::Transform transform,QObject *parent=nullptr);
    ~DisplayPresenter() override;

    // Canonical frame entry. 'context' identifies the caller's document /
    // revision / geometry so live frames of the same context are allowed.
    void setImage(QImage image,const QString &context=QString());
    // Drops everything: canonical image, epoch, waiting and running conversion.
    void clear();

    // Manual profile override (QA + public API). Starts no OS query.
    void setMonitorProfile(const QByteArray &icc,const QString &note=QString());
    // Restores automatic per-screen system profile handling.
    void useSystemMonitorProfile();
    bool manualProfileActive() const {return manualOverride;}
    // Screen the automatic path is currently bound to (empty = none).
    void setScreenName(const QString &screenName);

    QImage canonicalImage() const {return canonical;}
    QString contentContext() const {return context;}
    QByteArray monitorIcc() const {return profileIcc;}
    QString profileNote() const {return note;}
    quint64 epoch() const {return currentEpoch;}
    bool pendingProfile() const {return profilePending;}
    // Diagnostics for canvas tooltips. Never used for export or Recipe.
    QString displaySnapshot() const;

signals:
    void imageReady(const QImage &image,const QString &note);
    void presentationCleared();
    void displayProfileChanged(const QByteArray &effectiveIcc);

private:
    void requestCurrentEpoch();
    void requestProfileLookup(const QString &screenName,bool forceRefresh=false);
    void finishProfileLookup();
    void applySystemProfile(const MonitorProfileLookupResult &result,bool checked);
    void markProfilePending();

    DisplayQueue *queue=nullptr;
    QTimer *profilePoll=nullptr;
    QFutureWatcher<MonitorProfileLookupResult> profileWatcher;
    QImage canonical;
    QString context;
    QString screen;
    QString note;
    QByteArray profileIcc;
    quint64 currentEpoch=1;
    bool manualOverride=false;
    bool profilePending=false;
    QString wantedScreen,pendingScreen;
    quint64 profileRequest=0,appliedRequest=0;
};

#endif // LUMINA_APP_DISPLAY_PRESENTER_H
