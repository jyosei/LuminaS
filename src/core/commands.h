#pragma once
#include "document.h"
#include "photo_document.h"
#include <QHash>
#include <QList>
struct EditCommand { QString path; Recipe before,after; QString name;LocalEdit::DocumentSnapshot beforeDocument,afterDocument; };
class EditHistory {
public:
    bool commit(const QString &path,const Recipe &recipe,const QString &name);
    bool commitSnapshot(const QString &path,LocalEdit::DocumentSnapshot document,const QString &name,QString *error=nullptr);
    bool bindAsset(const QString &path,const LocalEdit::AssetIdentity &asset,QString &error);
    bool resetDocuments(const QList<LocalEdit::DocumentSnapshot> &documents,QString &error);
    QString undo(); QString redo();
    LocalEdit::DocumentSnapshot snapshot(const QString &path) const{return values.value(path);}
    Recipe value(const QString &path) const {const auto document=snapshot(path);return document?document->global:Recipe{};}
    bool canUndo() const{return cursor>0;}
    bool canRedo() const{return cursor<commands.size();}
    const EditCommand *nextUndo() const{return canUndo()?&commands[cursor-1]:nullptr;}
    const EditCommand *nextRedo() const{return canRedo()?&commands[cursor]:nullptr;}
private:
    QHash<QString,LocalEdit::DocumentSnapshot> values;
    QHash<QString,quint64> revisions,localRevisions;
    QList<EditCommand> commands;qsizetype cursor=0;
};
