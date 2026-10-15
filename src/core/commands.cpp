#include "commands.h"
#include <QDebug>
bool EditHistory::commit(const QString &path,const Recipe &after,const QString &name) {
    const auto before=value(path);if(before==after||!validRecipe(after))return false;
    while(commands.size()>cursor)commands.removeLast();commands.append({path,before,after,name});cursor=commands.size();values[path]=after;return true;
}
QString EditHistory::undo(){if(!canUndo())return {};const auto &cmd=commands[--cursor];values[cmd.path]=cmd.before;return cmd.path;}
QString EditHistory::redo(){if(!canRedo())return {};const auto &cmd=commands[cursor++];values[cmd.path]=cmd.after;return cmd.path;}
