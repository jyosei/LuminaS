#pragma once
#include "project_store.h"
#include <QStringList>

namespace LocalEdit {
constexpr int recoveryFormatVersion=1;
struct PendingImport {
    QString path,error;
    PhotoMark mark=PhotoMark::Unmarked;
    bool operator==(const PendingImport &) const = default;
};
struct RecoveryState {
    ProjectSnapshot project;
    QStringList order; // full source path order: known documents + pending imports
    QVector<PendingImport> pending;
    QString activePath,explicitPath,explicitHash;
    bool operator==(const RecoveryState &other) const {
        return (project==other.project||(project&&other.project&&*project==*other.project))&&
            order==other.order&&pending==other.pending&&activePath==other.activePath&&
            explicitPath==other.explicitPath&&explicitHash==other.explicitHash;
    }
};
using RecoverySnapshot=std::shared_ptr<const RecoveryState>;
bool validRecovery(const RecoveryState &state,QString &error);
RecoverySnapshot freezeRecovery(const RecoveryState &state,QString &error);
QJsonObject recoveryJson(const RecoveryState &state,QString &error);
bool recoveryFromJson(const QJsonObject &json,RecoveryState &state,QString &error);

// Background-only blocking I/O. Same protection as the explicit project store:
// an existing target requires its exact last committed SHA, matching project ID,
// and nondecreasing revision. Never write a source or the explicit-save path.
// Outputs change only on success; cancellation preserves the previous target.
bool loadRecovery(const QString &path,RecoveryState &state,QString &fileHash,
    QString &error,CancellationToken cancel={});
bool saveRecovery(const QString &path,RecoverySnapshot state,QString &fileHash,
    QString &error,const QString &expectedFileHash={},CancellationToken cancel={});
}
