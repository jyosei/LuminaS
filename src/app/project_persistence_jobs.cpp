#include "project_persistence.h"

PersistenceResult ProjectPersistence::perform(const PersistenceJob &job){
    PersistenceResult result;result.operation=job.operation;result.path=job.path;
    const auto fail=[&](QString error){
        result.state.reset();result.sources.clear();result.hash.clear();result.loadedHistory.reset();
        result.error=error.isEmpty()?QStringLiteral("项目存储操作失败"):std::move(error);return result;
    };
    if(isCanceled(job.cancel))return fail(QStringLiteral("操作已取消"));
    QString error,hash;
    if(job.operation==PersistenceOperation::RecoverySave||job.operation==PersistenceOperation::ProjectSave){
        if(!job.state||!job.state->project)return fail(QStringLiteral("没有项目快照"));
        bool saved=false;
        if(job.operation==PersistenceOperation::RecoverySave)saved=LocalEdit::saveRecovery(job.path,job.state,hash,error,job.expectedHash,job.cancel);
        else {
            if(!job.state->pending.isEmpty())return fail(QStringLiteral("尚有待导入照片"));
            const auto frozen=LocalEdit::freezeRecovery(*job.state,error);if(!frozen)return fail(error);
            saved=LocalEdit::saveProject(job.path,frozen->project,hash,error,job.expectedHash,job.cancel);
        }
        if(!saved)return fail(error);
        // A successful atomic commit remains successful if cancel arrives later.
        result.state=job.state;result.revision=job.state->project->revision;result.hash=hash;return result;
    }
    LocalEdit::RecoveryState state;
    if(job.operation==PersistenceOperation::ProjectLoad){
        LocalEdit::ProjectState project;
        if(!LocalEdit::loadProject(job.path,project,hash,error,job.cancel))return fail(error);
        state.project=LocalEdit::freezeProject(project,error);if(!state.project)return fail(error);
        for(const auto &photo:state.project->photos){
            state.order<<photo.document->asset.sourcePath;
            if(photo.document->asset.id==state.project->activeAssetId)state.activePath=photo.document->asset.sourcePath;
        }
        state.explicitPath=job.path;state.explicitHash=hash;
    }else if(job.operation==PersistenceOperation::RecoveryLoad){
        if(!LocalEdit::loadRecovery(job.path,state,hash,error,job.cancel))return fail(error);
    }else return fail(QStringLiteral("未知项目操作"));
    if(isCanceled(job.cancel))return fail(QStringLiteral("操作已取消"));
    const auto frozen=LocalEdit::freezeRecovery(state,error);if(!frozen)return fail(error);
    QVector<LocalEdit::SourceStatus> sources;
    if(!LocalEdit::inspectProjectSources(frozen->project,sources,error,job.cancel))return fail(error);
    QList<LocalEdit::DocumentSnapshot> documents;for(const auto &photo:frozen->project->photos)documents<<photo.document;
    auto history=std::make_shared<EditHistory>();if(!history->resetDocuments(documents,error))return fail(error);
    if(isCanceled(job.cancel))return fail(QStringLiteral("操作已取消"));
    result.loadedHistory=std::move(history);result.state=frozen;result.hash=hash;result.sources=std::move(sources);result.revision=frozen->project->revision;return result;
}
