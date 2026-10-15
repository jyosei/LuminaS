#pragma once
#include "photo_document.h"
#include "cancellation.h"

namespace LocalEdit {
constexpr int projectFormatVersion=1;
constexpr int projectApplicationId=0x4c534d31;
constexpr int maximumProjectPhotos=2000;
constexpr qint64 maximumProjectBytes=256LL*1024*1024;
constexpr qint64 maximumDocumentBytes=64LL*1024*1024;
enum class PhotoMark {Unmarked,Keep,Reject};
struct ProjectPhoto {
    DocumentSnapshot document;
    PhotoMark mark=PhotoMark::Unmarked;
    bool operator==(const ProjectPhoto &other) const {
        return mark==other.mark&&(document==other.document||
            (document&&other.document&&*document==*other.document));
    }
};
struct ProjectState {
    QString id,name,activeAssetId;
    quint64 revision=1;
    QVector<ProjectPhoto> photos;
    bool operator==(const ProjectState &) const = default;
};
using ProjectSnapshot=std::shared_ptr<const ProjectState>;
bool validProject(const ProjectState &project,QString &error);
ProjectSnapshot freezeProject(const ProjectState &project,QString &error);

// Blocking storage primitives: call from background work, never the GUI thread.
// Existing targets require their exact loaded/last-committed SHA-256. Outputs
// change only on success; unknown, corrupt, changed or source targets are kept.
bool loadProject(const QString &path,ProjectState &project,QString &fileHash,
    QString &error,CancellationToken cancel={});
bool saveProject(const QString &path,ProjectSnapshot project,QString &fileHash,
    QString &error,const QString &expectedFileHash={},CancellationToken cancel={});

enum class SourceState {Available,Missing,Changed,Unreadable};
struct SourceStatus {
    QString assetId,path,actualHash,error;
    SourceState state=SourceState::Unreadable;
};
bool inspectProjectSources(ProjectSnapshot project,QVector<SourceStatus> &statuses,
    QString &error,CancellationToken cancel={});
bool relinkProjectSource(ProjectSnapshot project,const QString &assetId,
    const QString &newPath,ProjectSnapshot &updated,QString &error,
    CancellationToken cancel={});
}
