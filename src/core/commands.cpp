#include "commands.h"
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QUuid>
#include <limits>

// S3 A2 EditHistory document history. commands.h is final: the new methods are
// implemented here only. DocumentSnapshot is the single source of truth; the
// legacy Recipe accessors are derived views over the current snapshot.
//
namespace {
constexpr quint64 maximumRevision=std::numeric_limits<quint64>::max();

// Small copy helper: bump the caller-supplied revisions, never the payload's.
LocalEdit::DocumentSnapshot copyWithCounters(const LocalEdit::PhotoDocument &source,quint64 revision,quint64 localIdentity){
    auto copy=std::make_shared<LocalEdit::PhotoDocument>(source);
    copy->revision=revision;
    copy->localIdentity=localIdentity;
    return copy;
}

// Revision/localIdentity/global/layers/algorithm are the state that matters for
// equality; asset identity is included so a metadata refinement is meaningful.
bool meaningfulChange(const LocalEdit::PhotoDocument &a,const LocalEdit::PhotoDocument &b){
    return a.global!=b.global||a.layers!=b.layers||a.algorithmVersion!=b.algorithmVersion||a.asset!=b.asset;
}

bool sameHash(const QString &a,const QString &b){return QString::compare(a,b,Qt::CaseInsensitive)==0;}

// Recursive object merge: keys provided by `update` replace, unknown old keys
// are retained. Arrays and scalars are replaced wholesale (never silently split).
QJsonObject mergeFacts(const QJsonObject &old,const QJsonObject &update){
    QJsonObject merged=old;
    for(auto it=update.constBegin();it!=update.constEnd();++it){
        if(it.value().isObject()&&merged.value(it.key()).isObject())
            merged[it.key()]=mergeFacts(merged.value(it.key()).toObject(),it.value().toObject());
        else merged[it.key()]=it.value();
    }
    return merged;
}

bool validFacts(const QJsonObject &facts){
    // 256KiB compact-encoding ceiling, same measure validDocument uses for sourceFacts.
    return QJsonDocument(facts).toJson(QJsonDocument::Compact).size()<=262144;
}
} // namespace

// Legacy global-only API. It edits the current snapshot's global Recipe while
// preserving the existing locals, asset identity and algorithm version, then
// commits through commitSnapshot so there is one source of truth.
bool EditHistory::commit(const QString &path,const Recipe &after,const QString &name) {
    const auto current=values.value(path);
    const Recipe before=current?current->global:Recipe{};
    if(before==after||!validRecipe(after))return false;
    LocalEdit::PhotoDocument document;
    if(current)document=*current;
    else{
        // First legacy edit: create the edit-state document with a stable id and
        // the raw suffix detected case-insensitively. No QImage is allocated.
        document.asset.id=QUuid::createUuid().toString(QUuid::WithoutBraces);
        document.asset.sourcePath=path;
        document.asset.raw=path.endsWith(QStringLiteral(".cr3"),Qt::CaseInsensitive);
    }
    document.global=after;
    return commitSnapshot(path,std::make_shared<LocalEdit::PhotoDocument>(document),name,nullptr);
}
// Undo/Redo restore the whole recorded payload and stable ids, then assign a
// fresh monotonic revision/localIdentity. The frozen past snapshot is never
// mutated. On counter overflow they return an empty path without moving state.
QString EditHistory::undo(){
    if(!canUndo())return {};
    const EditCommand &cmd=commands[cursor-1];
    const auto target=cmd.beforeDocument;if(!target)return {};
    const quint64 previous=revisions.value(cmd.path,0);
    const bool localsChanged=values.value(cmd.path)&&values.value(cmd.path)->layers!=target->layers;
    const quint64 baseLocal=values.value(cmd.path)?values.value(cmd.path)->localIdentity:0;
    if(previous>=maximumRevision||(localsChanged&&baseLocal>=maximumRevision))return {};
    const quint64 nextRevision=previous+1,nextLocal=localsChanged?baseLocal+1:baseLocal;
    values[cmd.path]=copyWithCounters(*target,nextRevision,nextLocal);
    revisions[cmd.path]=nextRevision;localRevisions[cmd.path]=nextLocal;
    --cursor;return cmd.path;
}
QString EditHistory::redo(){
    if(!canRedo())return {};
    const EditCommand &cmd=commands[cursor];
    const auto target=cmd.afterDocument;if(!target)return {};
    const quint64 previous=revisions.value(cmd.path,0);
    const bool localsChanged=values.value(cmd.path)&&values.value(cmd.path)->layers!=target->layers;
    const quint64 baseLocal=values.value(cmd.path)?values.value(cmd.path)->localIdentity:0;
    if(previous>=maximumRevision||(localsChanged&&baseLocal>=maximumRevision))return {};
    const quint64 nextRevision=previous+1,nextLocal=localsChanged?baseLocal+1:baseLocal;
    values[cmd.path]=copyWithCounters(*target,nextRevision,nextLocal);
    revisions[cmd.path]=nextRevision;localRevisions[cmd.path]=nextLocal;
    ++cursor;return cmd.path;
}

bool EditHistory::commitSnapshot(const QString &path,LocalEdit::DocumentSnapshot document,const QString &name,QString *error){
    const auto fail=[&](const QString &message){if(error)*error=message;return false;};
    if(error)error->clear();
    if(!document)return fail(QStringLiteral("document snapshot is null"));
    if(!validRecipe(document->global))return fail(QStringLiteral("document global recipe is invalid"));
    if(document->asset.sourcePath!=path)return fail(QStringLiteral("document sourcePath does not match the commit path"));
    QString reason;
    if(!LocalEdit::validDocument(*document,reason,false))return fail(reason);

    const auto existing=values.value(path);
    if(!existing){
        LocalEdit::PhotoDocument baseline;baseline.asset=document->asset;
        if(!meaningfulChange(baseline,*document))return false;
    }
    if(existing){
        // Stable identity: an existing asset id/path can never change.
        if(!existing->asset.id.isEmpty()&&existing->asset.id!=document->asset.id)
            return fail(QStringLiteral("document asset id cannot change"));
        if(existing->asset.sourcePath!=document->asset.sourcePath)
            return fail(QStringLiteral("document asset sourcePath cannot change"));
        // Known hash/dims may only be refined by bindAsset, never changed or cleared.
        if(!existing->asset.sourceHash.isEmpty()&&!sameHash(existing->asset.sourceHash,document->asset.sourceHash))
            return fail(QStringLiteral("document asset sourceHash cannot change"));
        if(!existing->asset.normalizedSize.isEmpty()&&existing->asset.normalizedSize!=document->asset.normalizedSize)
            return fail(QStringLiteral("document asset normalizedSize cannot change"));
        if(!existing->asset.sourceHash.isEmpty()&&existing->asset.raw!=document->asset.raw)
            return fail(QStringLiteral("document asset raw flag cannot flip once a source hash is known"));
        if(!meaningfulChange(*existing,*document))return false; // no-op: no mutation, redo intact
    }
    const quint64 previous=revisions.value(path,existing?existing->revision:quint64(1));
    if(previous>=maximumRevision)return fail(QStringLiteral("document revision counter overflow"));
    const quint64 nextRevision=previous+1;
    const bool localsChanged=existing?existing->layers!=document->layers:!document->layers.isEmpty();
    const quint64 baseLocal=existing?existing->localIdentity:0;
    if(localsChanged&&baseLocal>=maximumRevision)return fail(QStringLiteral("document localIdentity counter overflow"));
    const quint64 nextLocal=localsChanged?baseLocal+1:baseLocal;

    auto committed=copyWithCounters(*document,nextRevision,nextLocal);
    if(!existing){
        // First commit: before payload is the default global with empty layers,
        // reusing the incoming asset identity (same id) and no QImage.
        LocalEdit::PhotoDocument beforeDocument;
        beforeDocument.asset=document->asset;
        beforeDocument.global=Recipe{};
        beforeDocument.layers.clear();
        beforeDocument.algorithmVersion=document->algorithmVersion;
        EditCommand command;
        command.path=path;command.name=name;
        command.before=Recipe{};command.after=committed->global;
        command.beforeDocument=copyWithCounters(beforeDocument,1,0);
        command.afterDocument=committed;
        while(commands.size()>cursor)commands.removeLast();
        commands.append(command);cursor=commands.size();
        revisions[path]=nextRevision;localRevisions[path]=nextLocal;
        values[path]=committed;return true;
    }
    EditCommand command;
    command.path=path;command.name=name;
    command.before=existing->global;command.after=committed->global;
    command.beforeDocument=existing;command.afterDocument=committed;
    while(commands.size()>cursor)commands.removeLast(); // drop redo only after success
    commands.append(command);cursor=commands.size();
    revisions[path]=nextRevision;localRevisions[path]=nextLocal;
    values[path]=committed;return true;
}

bool EditHistory::bindAsset(const QString &path,const LocalEdit::AssetIdentity &asset,QString &error){
    error.clear();
    LocalEdit::PhotoDocument incoming;incoming.asset=asset;
    if(!LocalEdit::validDocument(incoming,error,false))return false;
    if(path.isEmpty()||asset.id.isEmpty()){error=QStringLiteral("bindAsset requires a path and a stable asset id");return false;}
    if(!asset.sourcePath.isEmpty()&&asset.sourcePath!=path){error=QStringLiteral("bindAsset sourcePath does not match the path");return false;}
    const auto current=values.value(path);
    if(!current){
        // New asset: create the default document at v1 without an undo command.
        LocalEdit::PhotoDocument document;
        document.asset=asset;document.asset.sourcePath=path;
        document.global=Recipe{};document.layers.clear();
        QString reason;
        if(!validFacts(document.asset.sourceFacts)||!LocalEdit::validDocument(document,reason,false)){error=reason.isEmpty()?QStringLiteral("invalid asset"):reason;return false;}
        revisions[path]=document.revision;localRevisions[path]=document.localIdentity;
        values[path]=copyWithCounters(document,1,0);return true;
    }
    if(!current->asset.id.isEmpty()&&current->asset.id!=asset.id){error=QStringLiteral("existing asset id cannot change");return false;}
    if(!current->asset.sourceHash.isEmpty()&&!asset.sourceHash.isEmpty()&&!sameHash(current->asset.sourceHash,asset.sourceHash)){
        error=QStringLiteral("known sourceHash cannot change");return false;}
    if(!current->asset.normalizedSize.isEmpty()&&!asset.normalizedSize.isEmpty()&&current->asset.normalizedSize!=asset.normalizedSize){
        error=QStringLiteral("known normalizedSize cannot change");return false;}
    if(!current->asset.sourceHash.isEmpty()&&current->asset.raw!=asset.raw){
        error=QStringLiteral("known raw flag cannot flip once a source hash is known");return false;}
    // Build the refined metadata before touching any state (atomic on failure).
    LocalEdit::AssetIdentity refined=current->asset;
    if(!asset.sourceHash.isEmpty())refined.sourceHash=asset.sourceHash;
    if(!asset.normalizedSize.isEmpty())refined.normalizedSize=asset.normalizedSize;
    refined.raw=asset.raw;
    refined.sourceFacts=mergeFacts(current->asset.sourceFacts,asset.sourceFacts);
    if(!validFacts(refined.sourceFacts)){error=QStringLiteral("merged sourceFacts exceed the 256KiB limit");return false;}
    auto refinedCopy=std::make_shared<LocalEdit::PhotoDocument>(*current);
    refinedCopy->asset=refined;
    QString reason;
    if(!LocalEdit::validDocument(*refinedCopy,reason,false)){error=reason.isEmpty()?QStringLiteral("invalid merged facts"):reason;return false;}
    auto nextCommands=commands;
    for(auto &command:nextCommands){ // prepare all copies before publishing
        if(command.path!=path)continue;
        auto apply=[&](LocalEdit::DocumentSnapshot &slot){
            if(!slot)return;
            auto copy=std::make_shared<LocalEdit::PhotoDocument>(*slot);
            copy->asset=refined;slot=copy;
        };
        apply(command.beforeDocument);apply(command.afterDocument);
    }
    values[path]=refinedCopy; // revision/history cursor unchanged
    commands=std::move(nextCommands);
    return true;
}

bool EditHistory::resetDocuments(const QList<LocalEdit::DocumentSnapshot> &documents,QString &error){
    error.clear();
    QHash<QString,LocalEdit::DocumentSnapshot> nextValues;
    QHash<QString,quint64> nextRevisions,nextLocalRevisions;
    QSet<QString> paths,ids;
    for(const auto &document:documents){
        if(!document){error=QStringLiteral("resetDocuments received a null document");return false;}
        if(document->asset.id.isEmpty()||document->asset.sourcePath.isEmpty()){
            error=QStringLiteral("resetDocuments requires stable asset ids and source paths");return false;}
        if(paths.contains(document->asset.sourcePath)){error=QStringLiteral("resetDocuments received a duplicate sourcePath");return false;}
        if(ids.contains(document->asset.id)){error=QStringLiteral("resetDocuments received a duplicate asset id");return false;}
        QString reason;
        if(document->algorithmVersion!=LocalEdit::localAlgorithmVersion){
            error=QStringLiteral("resetDocuments received an unknown algorithmVersion");return false;}
        if(!LocalEdit::validDocument(*document,reason,false)){error=reason.isEmpty()?QStringLiteral("invalid document"):reason;return false;}
        paths.insert(document->asset.sourcePath);ids.insert(document->asset.id);
        nextValues.insert(document->asset.sourcePath,copyWithCounters(*document,document->revision,document->localIdentity));
        nextRevisions.insert(document->asset.sourcePath,document->revision);
        nextLocalRevisions.insert(document->asset.sourcePath,document->localIdentity);
    }
    values=nextValues; // every validation passed: replace and clear the timeline
    revisions=nextRevisions;localRevisions=nextLocalRevisions;
    commands.clear();cursor=0;return true;
}
