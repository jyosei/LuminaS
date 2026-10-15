#pragma once
#include "document.h"
#include <QHash>
#include <QList>
struct EditCommand { QString path; Recipe before,after; QString name; };
class EditHistory {
public:
    bool commit(const QString &path,const Recipe &recipe,const QString &name);
    QString undo(); QString redo();
    Recipe value(const QString &path) const {return values.value(path);}
    bool canUndo() const{return cursor>0;}
    bool canRedo() const{return cursor<commands.size();}
    const EditCommand *nextUndo() const{return canUndo()?&commands[cursor-1]:nullptr;}
    const EditCommand *nextRedo() const{return canRedo()?&commands[cursor]:nullptr;}
private:
    QHash<QString,Recipe> values;QList<EditCommand> commands;qsizetype cursor=0;
};
