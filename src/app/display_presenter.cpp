#include "app/display_presenter.h"
#include "color/monitor_profile.h"
#include <QColorSpace>
#include <QMetaType>
#include <QTimer>
#include <QFileInfo>
#include <QtConcurrentRun>

namespace {
// Monitors change profile rarely; a bounded re-check is enough and no GUI work
// is involved. Missing profiles stay on the Windows sRGB fallback contract.
constexpr int profilePollIntervalMs = 5000;

bool isPlainSrgb(const QImage &image)
{
    return !image.colorSpace().isValid() || image.colorSpace() == QColorSpace(QColorSpace::SRgb);
}

QString fallbackNote(const QString &reason)
{
    const QString detail = reason.trimmed().isEmpty()
        ? QStringLiteral("显示器色彩转换不可用") : reason.trimmed();
    return QStringLiteral("%1；显示按 sRGB 回退。").arg(detail);
}
} // namespace

Q_DECLARE_METATYPE(MonitorProfileLookupResult)

DisplayPresenter::DisplayPresenter(QObject *parent)
    : DisplayPresenter(DisplayQueue::Transform(transformDisplay), parent) {}

DisplayPresenter::DisplayPresenter(DisplayQueue::Transform transform, QObject *parent)
    : QObject(parent), queue(new DisplayQueue(this, std::move(transform))), profilePoll(new QTimer(this))
{
    qRegisterMetaType<MonitorProfileLookupResult>("MonitorProfileLookupResult");
    connect(queue, &DisplayQueue::frameReady, this, [this](const DisplayFrame &frame) {
        if (frame.context != currentEpoch) return;      // frame of an obsolete epoch
        if (frame.image.isNull()) {
            // The transform refused or failed. A null error with a null image is an
            // empty-canonical signal and must not clear the current presentation.
            if (canonical.isNull() || frame.error.trimmed().isEmpty()) return;
            if (isPlainSrgb(canonical)) {
                // Production canonical frames are already sRGB, so showing the canonical
                // pixels with an sRGB tag and a clear note is a lossless fallback and
                // needs no additional colour maths here.
                QImage fallback = canonical;
                fallback.setColorSpace(QColorSpace(QColorSpace::SRgb));
                note = fallbackNote(frame.error);
                emit imageReady(fallback, note);
                return;
            }
            // Tagged input: faking a conversion would misrepresent the pixels, so the
            // failure is reported and no converted frame is presented.
            note = frame.error.trimmed();
            emit imageReady(QImage(), note);
            return;
        }
        emit imageReady(frame.image, note);
    });
    connect(&profileWatcher, &QFutureWatcher<MonitorProfileLookupResult>::finished,
            this, [this] { finishProfileLookup(); });
    profilePoll->setInterval(profilePollIntervalMs);
    connect(profilePoll, &QTimer::timeout, this, [this] {
        // Re-queries only the screen the window currently sits on. Same-profile
        // results do not recompute anything, so this stays cheap.
        if (manualOverride || wantedScreen.isEmpty() || screen != wantedScreen) return;
        requestProfileLookup(screen);
    });
    profilePoll->start();
}

DisplayPresenter::~DisplayPresenter()
{
    // Detach first so a finishing lookup cannot re-enter this object, then let the
    // watcher wait for the QtConcurrent job. The job captured the screen name by
    // value and owns no QObject, so it stays valid while it is waited on.
    disconnect(queue, nullptr, this, nullptr);
    disconnect(&profileWatcher, nullptr, this, nullptr);
    profileWatcher.cancel();
    // The lookup owns only copied inputs. Detaching it avoids blocking the GUI
    // destructor on file I/O and cannot re-enter this destroyed presenter.
}

void DisplayPresenter::setImage(QImage image, const QString &imageContext)
{
    if (image.isNull()) { clear(); return; }
    const bool newContext = imageContext != context;
    const bool newSize = canonical.isNull() || image.size() != canonical.size();
    if (newContext || newSize) ++currentEpoch;
    canonical = std::move(image);
    context = imageContext;
    if (newContext || newSize) {
        // Everything still running belongs to the previous epoch and must not be shown.
        queue->invalidate();
        emit presentationCleared();
    }
    requestCurrentEpoch();
}

void DisplayPresenter::clear()
{
    canonical = QImage();
    context.clear();
    ++currentEpoch;
    queue->invalidate();
    emit presentationCleared();
}

void DisplayPresenter::setMonitorProfile(const QByteArray &icc, const QString &profileNote)
{
    manualOverride = true;
    profilePending = false;
    ++currentEpoch;                       // the old device frame is no longer valid
    queue->invalidate();
    QString invalid;profileIcc=validDisplayProfile(icc,invalid)?icc:QByteArray();
    note=invalid.isEmpty()?profileNote:fallbackNote(invalid);
    emit displayProfileChanged(profileIcc);
    emit presentationCleared();
    requestCurrentEpoch();
}

void DisplayPresenter::useSystemMonitorProfile()
{
    manualOverride = false;
    ++currentEpoch;                       // the old device frame is no longer valid
    queue->invalidate();
    profileIcc.clear();
    note.clear();
    emit displayProfileChanged({});
    emit presentationCleared();
    if (!screen.isEmpty()){markProfilePending();requestProfileLookup(screen,true);}
    else {profilePending=false;requestCurrentEpoch();}
}

void DisplayPresenter::setScreenName(const QString &screenName)
{
    if (manualOverride || screenName == screen) return;
    screen = screenName;
    if (screenName.isEmpty()) {
        // No screen: the Windows contract is sRGB. No OS query is attempted.
        profileIcc.clear();
        emit displayProfileChanged({});
        note = QStringLiteral("无法确定当前显示器，显示按 sRGB 回退。");
        profilePending = false;
        ++currentEpoch;
        queue->invalidate();
        emit presentationCleared();
        requestCurrentEpoch();
        return;
    }
    markProfilePending();
    requestProfileLookup(screenName);
}

void DisplayPresenter::markProfilePending()
{
    // The previous device frame must disappear immediately; the new profile is
    // resolved asynchronously and starts a fresh epoch when it is applied.
    profilePending = true;
    note = QStringLiteral("正在读取当前显示器的 ICC 配置文件…");
    profileIcc.clear();
    emit displayProfileChanged({});
    ++currentEpoch;
    queue->invalidate();
    emit presentationCleared();
}

void DisplayPresenter::requestCurrentEpoch()
{
    if (canonical.isNull()||profilePending) return;
    queue->request(canonical,profileIcc,currentEpoch);
}

void DisplayPresenter::requestProfileLookup(const QString &screenName, bool forceRefresh)
{
    if (screenName.isEmpty()) return;
    if (manualOverride) return;
    if (!forceRefresh && profilePending && screenName != screen) return;
    wantedScreen = screenName;
    if(profileWatcher.isRunning())return; // completion dispatches the newest wanted screen
    if (screenName != screen) {
        // A different screen than the last applied one: clear the stale device
        // frame before the lookup for the new screen even starts.
        markProfilePending();
    }
    // The worker captures only the screen name by value: no this, no QObject.
    profileWatcher.setFuture(QtConcurrent::run([screenName] {
        MonitorProfileLookupResult result;
        const MonitorProfileData data = readMonitorProfile(screenName);
        result.screenName = screenName;
        result.icc = data.icc;
        QString invalid;
        if(!validDisplayProfile(result.icc,invalid)){result.icc.clear();result.note=fallbackNote(invalid);return result;}
        result.note = data.icc.isEmpty()?data.note:
            QStringLiteral("系统显示配置：%1 · SDR").arg(QFileInfo(data.profilePath).fileName());
        return result;
    }));
}

void DisplayPresenter::finishProfileLookup()
{
    const MonitorProfileLookupResult result = profileWatcher.result();
    if(!manualOverride&&!wantedScreen.isEmpty()&&result.screenName!=wantedScreen){
        requestProfileLookup(wantedScreen,true);return;
    }
    // Only the newest wanted screen may be applied, and a manual override that
    // arrived while the lookup was running always wins.
    applySystemProfile(result,
        !manualOverride && result.screenName == wantedScreen && result.screenName == screen);
}

void DisplayPresenter::applySystemProfile(const MonitorProfileLookupResult &result, bool checked)
{
    if (!checked) return;
    // Identical bytes and note keep the current presentation: no recompute, no epoch bump.
    if (!profilePending && result.icc == profileIcc && result.note == note) return;
    profilePending = false;
    // Missing ICC keeps the Windows sRGB fallback; nothing else is inferred here.
    profileIcc = result.icc;
    emit displayProfileChanged(profileIcc);
    note = result.note;
    ++currentEpoch;                       // the frame for the previous profile is obsolete
    queue->invalidate();
    emit presentationCleared();
    requestCurrentEpoch();
}

QString DisplayPresenter::displaySnapshot() const
{
    const QString mode = manualOverride ? QStringLiteral("手动")
        : profilePending ? QStringLiteral("系统（查询中）") : QStringLiteral("系统");
    const QString screenText = screen.isEmpty() ? QStringLiteral("未绑定显示器") : screen;
    const QString profileText = manualOverride
        ? (profileIcc.isEmpty() ? QStringLiteral("覆盖为空（sRGB）")
                                : QStringLiteral("手动覆盖 %1 字节").arg(profileIcc.size()))
        : (profilePending ? QStringLiteral("查询中")
                          : profileIcc.isEmpty() ? QStringLiteral("无配置文件（sRGB 回退）")
                                                 : QStringLiteral("系统配置文件 %1 字节").arg(profileIcc.size()));
    QString summary = QStringLiteral("%1 · %2 · %3 · epoch %4").arg(mode, screenText, profileText).arg(currentEpoch);
    if (!note.isEmpty()) summary += QStringLiteral(" · %1").arg(note);
    return summary;
}
