// LuminaS bounded integration test: committed-recovery-copy process kill + restart.
//
// WHAT THIS PROVES
//   Only this: once the child process has committed its recovery copy to disk
//   (controller revision caught up and the persistence layer idle) and reported
//   that path, a hard kill of the child leaves a recovery file that a freshly
//   constructed Window can load back with the same filmstrip, selection, Keep
//   mark, recipe and source hashes.
//
// WHAT THIS DOES NOT CLAIM
//   No claim about a full disk, power loss, torn writes, or an in-flight /
//   cancelled commit. Those scenarios are explicitly out of scope here.
//
// STRUCTURE
//   QTEST_MAIN, one executable, two roles:
//   * Parent slot killAfterCommittedRecovery() spawns this same executable with
//     "childCreateRecovery" as a positional argument so QtTest runs only that slot.
//   * The child's private directory travels in LUMINA_D1_CHILD_ROOT, set only on
//     the spawned process via QProcessEnvironment. The user environment is never
//     modified.
//   * childCreateRecovery() QSKIPs when that variable is absent, so running the
//     suite normally is a no-op for the child role.
//
// All waits are bounded inside a 20-30s band, except the child's deliberate
// keep-alive, which is 90s by design.

#include <QtTest>
#include <QCoreApplication>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QSaveFile>
#include <QFile>
#include <QDir>
#include <QImage>
#include <QColor>
#include <QDebug>
#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QVector>
#include <QElapsedTimer>
#include <QCryptographicHash>
#include <functional>

#include "app/window.h"
#include "app/project_persistence.h"
#include "core/recovery_store.h"
#include "core/project_store.h"

namespace {

const char *kRootEnv = "LUMINA_D1_CHILD_ROOT";

// Bounded waits: every one sits in the required 20-30s band.
constexpr int kStorageWaitMs  = 30000; // controller exists and holds a snapshot
constexpr int kImportWaitMs   = 30000; // both sources imported, picker available
constexpr int kReflectWaitMs  = 30000; // controller reflects recipe + Keep mark
constexpr int kRecoveryWaitMs = 30000; // committed revision caught up, idle
constexpr int kReadyWaitMs    = 30000; // parent: ready.txt present and non-empty
constexpr int kRestartWaitMs  = 30000; // parent: restarted storage initialized
constexpr int kLoadWaitMs     = 30000; // parent: RecoveryLoad completed
constexpr int kKillWaitMs     = 5000;  // kill() reaping bound
constexpr int kChildLoopMs    = 90000; // child keep-alive, then it stops itself

constexpr double kExpectedExposure = 0.75;

// RAII: no code path may leave the spawned child running.
class ChildGuard {
public:
    explicit ChildGuard(QProcess *process) : proc(process) {}
    ~ChildGuard() {
        if (proc && proc->state() != QProcess::NotRunning) {
            proc->kill();
            proc->waitForFinished(kKillWaitMs);
        }
    }
    ChildGuard(const ChildGuard &) = delete;
    ChildGuard &operator=(const ChildGuard &) = delete;
private:
    QProcess *proc;
};

bool writeTextCommitted(const QString &path, const QByteArray &payload) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    if (file.write(payload) != payload.size())
        return false;
    return file.commit();
}

QString sha256OfFile(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
        return QString();
    return QString::fromLatin1(hash.result().toHex());
}

// 96x64 landscape and 64x96 portrait JPEGs, written only into the given
// temporary directory. Both images are deterministic and non-uniform so the
// encoders cannot collapse them into identical bytes.
bool makeFixtures(const QString &root, QString &landscape, QString &portrait) {
    landscape = QDir(root).filePath(QStringLiteral("source-landscape-96x64.jpg"));
    portrait  = QDir(root).filePath(QStringLiteral("source-portrait-64x96.jpg"));

    QImage wide(96, 64, QImage::Format_RGB32);
    for (int y = 0; y < wide.height(); ++y)
        for (int x = 0; x < wide.width(); ++x)
            wide.setPixel(x, y, qRgb(24 + x, 40 + y * 2, 160));

    QImage tall(64, 96, QImage::Format_RGB32);
    for (int y = 0; y < tall.height(); ++y)
        for (int x = 0; x < tall.width(); ++x)
            tall.setPixel(x, y, qRgb(150, 20 + y, 30 + x));

    if (!wide.save(landscape, "JPG", 95))
        return false;
    if (!tall.save(portrait, "JPG", 95))
        return false;
    return QFile::exists(landscape) && QFile::exists(portrait);
}

// Spin a real event loop so queued persistence results and timers are delivered.
bool waitFor(const std::function<bool()> &predicate, int timeoutMs) {
    QElapsedTimer timer;
    timer.start();
    for (;;) {
        if (predicate())
            return true;
        if (timer.elapsed() > timeoutMs)
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QTest::qWait(10);
    }
}

// The persistence controller is a private child object of Window; it is
// reachable by type without touching the private API.
ProjectPersistence *persistenceOf(Window *window) {
    return window ? window->findChild<ProjectPersistence *>() : nullptr;
}

// "Storage ready" means the controller exists and already holds a snapshot.
bool waitForStorage(Window *window, int timeoutMs) {
    return waitFor([window]() {
        ProjectPersistence *persistence = persistenceOf(window);
        return persistence && static_cast<bool>(persistence->state());
    }, timeoutMs);
}

// Does any photo document in the snapshot carry the given global exposure?
bool snapshotHasExposure(const LocalEdit::RecoverySnapshot &snapshot, double exposure) {
    if (!snapshot || !snapshot->project)
        return false;
    for (const LocalEdit::ProjectPhoto &photo : snapshot->project->photos) {
        if (photo.document && photo.document->global.exposure == exposure)
            return true;
    }
    return false;
}

bool snapshotHasMark(const LocalEdit::RecoverySnapshot &snapshot, LocalEdit::PhotoMark mark) {
    if (!snapshot || !snapshot->project)
        return false;
    for (const LocalEdit::ProjectPhoto &photo : snapshot->project->photos) {
        if (photo.mark == mark)
            return true;
    }
    return false;
}

} // namespace

class ProjectRecoveryProcessTests : public QObject {
    Q_OBJECT

private slots:
    // Child role: only meaningful when LUMINA_D1_CHILD_ROOT is set.
    void childCreateRecovery();

    // Parent role: spawn, wait for the committed marker, kill, restart, verify.
    void killAfterCommittedRecovery();
};

void ProjectRecoveryProcessTests::childCreateRecovery()
{
    const QString root = qEnvironmentVariable(kRootEnv);
    if (root.isEmpty())
        QSKIP("LUMINA_D1_CHILD_ROOT is not set; this slot only runs as a spawned child.");

    if (!QDir(root).exists())
        QVERIFY2(QDir().mkpath(root), qPrintable(QStringLiteral("cannot create %1").arg(root)));

    const auto landscape=QDir(root).filePath(QStringLiteral("source-landscape-96x64.jpg"));
    const auto portrait=QDir(root).filePath(QStringLiteral("source-portrait-64x96.jpg"));
    QVERIFY(QFile::exists(landscape));QVERIFY(QFile::exists(portrait));

    // Third Window argument is the project directory; recovery lives under it.
    const QString projectDirectory = root + QStringLiteral("/recovery");
    Window window(nullptr, QStringLiteral(""), projectDirectory);

    QVERIFY2(waitForStorage(&window, kStorageWaitMs),
             "child Window never initialized project persistence storage");
    ProjectPersistence *persistence = persistenceOf(&window);
    QVERIFY(persistence != nullptr);

    window.addFiles(QStringList{landscape, portrait});

    QVERIFY2(waitFor([&window]() {
                 ProjectPersistence *controller = persistenceOf(&window);
                 if (!controller)
                     return false;
                 const LocalEdit::RecoverySnapshot snapshot = controller->state();
                 if (!snapshot || !snapshot->project)
                     return false;
                 if (!snapshot->pending.isEmpty())
                     return false;
                 return snapshot->project->photos.size() == 2 &&
                        window.neutralPickerAvailable();
             }, kImportWaitMs),
             "child never reached two imported photos with empty pending and a neutral picker");

    Recipe recipe = window.currentRecipe();
    recipe.exposure = kExpectedExposure;
    window.applyRecipe(recipe, QStringLiteral("d1-exposure"));
    window.mark(Pick::Keep);

    // The controller must reflect both the recipe and the mark before we trust
    // any on-disk file.
    QVERIFY2(waitFor([&window]() {
                 ProjectPersistence *controller = persistenceOf(&window);
                 if (!controller)
                     return false;
                 const LocalEdit::RecoverySnapshot snapshot = controller->state();
                 return snapshotHasExposure(snapshot, kExpectedExposure) &&
                        snapshotHasMark(snapshot, LocalEdit::PhotoMark::Keep);
             }, kReflectWaitMs),
             "controller never reflected the edited recipe and the Keep mark");

    // Only after the committed revision has caught up and the layer is idle do
    // we consider the recovery copy durable. No explicit save is performed.
    QVERIFY2(waitFor([&window]() {
                 ProjectPersistence *controller = persistenceOf(&window);
                 if (!controller)
                     return false;
                 const LocalEdit::RecoverySnapshot snapshot = controller->state();
                 if (!snapshot || !snapshot->project)
                     return false;
                 return !controller->busy() &&
                        controller->recoveredRevision() == snapshot->project->revision;
             }, kRecoveryWaitMs),
             "the recovery copy never reached a committed, idle revision");

    const QString recoveryPath = window.projectRecoveryPath();
    QVERIFY2(!recoveryPath.isEmpty(), "the window reported an empty recovery path");
    QVERIFY2(QFile::exists(recoveryPath),
             "the reported recovery file is missing after the commit was observed");

    // Publish the path only now: the parent treats a non-empty ready.txt as
    // "the child has a committed recovery copy", never mere file existence.
    const QString readyPath = root + QStringLiteral("/ready.txt");
    QVERIFY2(writeTextCommitted(readyPath, recoveryPath.toUtf8()),
             "child failed to commit ready.txt");

    // Keep the event loop alive so the parent can kill us in a live state. We
    // intentionally never exit cleanly and never close the window.
    QTest::qWait(kChildLoopMs);
}

void ProjectRecoveryProcessTests::killAfterCommittedRecovery()
{
    QTemporaryDir temporary;
    QVERIFY2(temporary.isValid(), "parent could not create its temporary directory");
    const QString root = temporary.path();

    QString landscape, portrait;
    QVERIFY2(makeFixtures(root, landscape, portrait),
             "parent could not synthesize the 96x64 / 64x96 JPEG fixtures");

    // The parent computes the expected source hashes itself, from its own files.
    const QString expectedLandscapeHash = sha256OfFile(landscape);
    const QString expectedPortraitHash = sha256OfFile(portrait);
    QVERIFY2(!expectedLandscapeHash.isEmpty() && !expectedPortraitHash.isEmpty(),
             "parent could not hash its own synthesized sources");

    const QString readyPath = root + QStringLiteral("/ready.txt");
    QFile::remove(readyPath);

    QProcess child;
    QProcessEnvironment childEnvironment = QProcessEnvironment::systemEnvironment();
    childEnvironment.insert(QString::fromLatin1(kRootEnv), root);
    const auto childTemp=QDir(root).filePath(QStringLiteral("child-temp"));QVERIFY(QDir().mkpath(childTemp));
    childEnvironment.insert(QStringLiteral("TEMP"),childTemp);childEnvironment.insert(QStringLiteral("TMP"),childTemp);
    child.setProcessEnvironment(childEnvironment);
    child.setProcessChannelMode(QProcess::SeparateChannels);

    ChildGuard guard(&child);

    // Naming the function as a positional argument restricts QtTest to that
    // single slot, so the child never runs the parent slot.
    child.start(QCoreApplication::applicationFilePath(),
                QStringList{QStringLiteral("childCreateRecovery")});
    QVERIFY2(child.waitForStarted(kStorageWaitMs), "the child test process failed to start");

    // Success requires a non-empty ready.txt AND the child still running. A file
    // that merely exists before the write committed is not accepted.
    const bool ready = waitFor([&child, &readyPath]() {
        if (child.state() != QProcess::Running)
            return false;
        QFile file(readyPath);
        if (!file.exists() || !file.open(QIODevice::ReadOnly))
            return false;
        return !file.readAll().isEmpty();
    }, kReadyWaitMs);

    if (!ready) {
        child.kill();
        child.waitForFinished(kKillWaitMs);
        qWarning().noquote() << "child stdout:\n" + QString::fromUtf8(child.readAllStandardOutput());
        qWarning().noquote() << "child stderr:\n" + QString::fromUtf8(child.readAllStandardError());
        QFAIL("the child never published a non-empty ready.txt while still running");
    }

    QVERIFY2(child.state() == QProcess::Running,
             "the child was no longer running when the ready marker appeared");

    QFile readyFile(readyPath);
    QVERIFY(readyFile.open(QIODevice::ReadOnly));
    const QString recoveryPath = QString::fromUtf8(readyFile.readAll()).trimmed();
    readyFile.close();
    QVERIFY2(!recoveryPath.isEmpty(), "ready.txt contained an empty recovery path");
    QVERIFY2(QFile::exists(recoveryPath),
             "the recovery file reported by the child does not exist on disk");

    // Hard kill: the child Window is never destructed, only its process dies.
    child.kill();
    QVERIFY2(child.waitForFinished(kKillWaitMs), "the child did not terminate after kill()");
    qInfo().noquote() << "child stdout:\n" + QString::fromUtf8(child.readAllStandardOutput());
    qInfo().noquote() << "child stderr:\n" + QString::fromUtf8(child.readAllStandardError());
    QVERIFY2(child.state() == QProcess::NotRunning, "the child process is still running");
    QVERIFY2(child.exitStatus() == QProcess::CrashExit,
             "kill() was expected to report a crash exit, not a clean exit");

    // Fresh Window, different project directory, loading the orphaned recovery.
    const QString restartDirectory = root + QStringLiteral("/restart-recovery");
    Window restarted(nullptr, QStringLiteral(""), restartDirectory);
    QVERIFY2(waitForStorage(&restarted, kRestartWaitMs),
             "the restarted Window never initialized project persistence storage");
    ProjectPersistence *restartPersistence = persistenceOf(&restarted);
    QVERIFY(restartPersistence != nullptr);

    int recoveryLoads = 0;
    PersistenceResult lastLoad;
    const QMetaObject::Connection connection = QObject::connect(
        restartPersistence, &ProjectPersistence::completed, &restarted,
        [&recoveryLoads, &lastLoad](const PersistenceResult &result) {
            if (result.operation != PersistenceOperation::RecoveryLoad)
                return;
            ++recoveryLoads;
            lastLoad = result;
        });

    QString openError;
    const bool opened = restarted.openProjectFile(recoveryPath, true, openError);
    QVERIFY2(opened, qPrintable(QStringLiteral("openProjectFile(recovery) was refused: %1")
                                    .arg(openError)));

    QVERIFY2(waitFor([&recoveryLoads]() { return recoveryLoads > 0; }, kLoadWaitMs),
             "no RecoveryLoad completion arrived within the bound");
    QObject::disconnect(connection);

    QVERIFY2(lastLoad.operation == PersistenceOperation::RecoveryLoad,
             "the completion was not a RecoveryLoad");
    QVERIFY2(lastLoad.error.isEmpty(),
             qPrintable(QStringLiteral("RecoveryLoad reported: %1").arg(lastLoad.error)));
    QVERIFY2(!lastLoad.discarded, "RecoveryLoad was discarded rather than applied");

    // Restart waits for the storage snapshot; now assert the recovered session.
    QVERIFY2(waitFor([&restarted]() {
                 ProjectPersistence *controller = persistenceOf(&restarted);
                 return controller && static_cast<bool>(controller->state());
             }, kRestartWaitMs),
             "the restarted controller never held a snapshot");

    QVERIFY(restarted.filmstrip() != nullptr);
    QCOMPARE(restarted.filmstrip()->count(), 2);
    QCOMPARE(restarted.currentIndex(), 0);
    QVERIFY2(restarted.pickAt(0) == Pick::Keep, "the recovered photo is not marked Keep");

    const LocalEdit::DocumentSnapshot document = restarted.currentPhotoDocument();
    QVERIFY2(static_cast<bool>(document), "the restarted Window has no current document");
    QCOMPARE(document->global.exposure, kExpectedExposure);

    const LocalEdit::RecoverySnapshot restartedState = restartPersistence->state();
    QVERIFY2(static_cast<bool>(restartedState), "the restarted controller has no snapshot");
    QVERIFY(restartedState->pending.isEmpty());
    QVERIFY2(restartPersistence->dirty(),
             "a recovered session must still report unsaved changes");

    // Source identity and ordering, checked with the parent's own SHA-256 values
    // through the public source-inspection primitive.
    QCOMPARE(restartedState->project->photos.size(), 2);
    QVector<LocalEdit::SourceStatus> statuses;
    QString inspectError;
    QVERIFY2(LocalEdit::inspectProjectSources(restartedState->project, statuses, inspectError),
             qPrintable(QStringLiteral("source inspection failed: %1").arg(inspectError)));
    QCOMPARE(statuses.size(), 2);
    QVERIFY2(statuses.at(0).state == LocalEdit::SourceState::Available,
             "the first recovered source is not Available");
    QVERIFY2(statuses.at(1).state == LocalEdit::SourceState::Available,
             "the second recovered source is not Available");
    QCOMPARE(statuses.at(0).path, landscape);
    QCOMPARE(statuses.at(1).path, portrait);
    QCOMPARE(statuses.at(0).actualHash, expectedLandscapeHash);
    QCOMPARE(statuses.at(1).actualHash, expectedPortraitHash);
    QCOMPARE(restartedState->project->photos[0].document->asset.sourceHash,expectedLandscapeHash);
    QCOMPARE(restartedState->project->photos[1].document->asset.sourceHash,expectedPortraitHash);
}

QTEST_MAIN(ProjectRecoveryProcessTests)
#include "project_recovery_process_tests.moc"
