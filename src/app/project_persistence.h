#pragma once
#include "core/recovery_store.h"
#include "core/commands.h"
#include <QObject>
#include <QFutureWatcher>
#include <QTimer>
#include <functional>
#include <optional>

enum class PersistenceOperation {RecoverySave,ProjectSave,ProjectLoad,RecoveryLoad,SourceRelink};
struct PersistenceJob {
    PersistenceOperation operation=PersistenceOperation::RecoverySave;
    LocalEdit::RecoverySnapshot state;
    QString path,expectedHash,assetId;
    quint64 epoch=0,editSerial=0;
    CancellationToken cancel;
};
struct PersistenceResult {
    PersistenceOperation operation=PersistenceOperation::RecoverySave;
    QString path,hash,error;
    LocalEdit::RecoverySnapshot state;
    QVector<LocalEdit::SourceStatus> sources;
    std::shared_ptr<const EditHistory> loadedHistory;
    quint64 revision=0;
    bool discarded=false;
};
Q_DECLARE_METATYPE(PersistenceOperation)
Q_DECLARE_METATYPE(PersistenceResult)

// GUI-thread scheduling only. Owns value snapshots, never worker GUI pointers.
// Full validation, hashing, JSON/SQLite and disk access run on the global pool.
// Loaded data is delivered for the host to apply/reset; never implicitly swaps
// documents. The executor seam permits deterministic blocked-I/O tests.
class ProjectPersistence:public QObject {
    Q_OBJECT
public:
    using Executor=std::function<PersistenceResult(const PersistenceJob &)>;
    explicit ProjectPersistence(QObject *parent=nullptr,Executor executor={});
    ~ProjectPersistence() override;
    bool reset(LocalEdit::RecoverySnapshot state,const QString &recoveryPath,
        bool clean,QString &error,const QString &knownRecoveryHash={});
    bool update(LocalEdit::RecoverySnapshot state,QString &error);
    bool save(const QString &path,QString &error);
    bool open(const QString &path,bool recovery,QString &error);
    bool relink(const QString &assetId,const QString &newPath,QString &error);
    void retryRecovery();
    void cancel();
    bool busy() const{return running.has_value();}
    bool dirty() const{return latest&&latest->project->revision!=savedRevision;}
    LocalEdit::RecoverySnapshot state() const{return latest;}
    QString recoveryPath() const{return journalPath;}
    QString recoveryHash() const{return journalHash;}
    quint64 recoveredRevision() const{return journalRevision;}
    QString lastError() const{return failure;}
    static PersistenceResult perform(const PersistenceJob &job);
signals:
    void changed();
    void started(PersistenceOperation operation,quint64 revision);
    void completed(const PersistenceResult &result);
private:
    void dispatch();
    void finish();
    bool enqueue(PersistenceJob job,QString &error);
    LocalEdit::RecoverySnapshot latest;
    QString journalPath,journalHash,failure;
    quint64 epoch=0,editSerial=0,savedRevision=0,journalRevision=0;
    bool recoveryDue=false;
    QTimer idle;
    QFutureWatcher<PersistenceResult> watcher;
    CancellationSource cancellation;
    std::optional<PersistenceJob> pending,running;
    Executor executor;
};
