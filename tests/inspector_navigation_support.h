#pragma once
#include "app/window.h"
#include <QAction>
#include <QCoreApplication>
#include <QMenu>
#include <QTabBar>
#include <QToolButton>

// Older operator interaction tests exercise several sections in one scenario.
// Open them through the same public group/menu actions available to a person;
// a hidden slider must not count as a successful on-screen drag test.
inline bool openAllInspectorSections(Window &window){
    auto *groups=window.findChild<QTabBar *>("adjustment-groups");
    auto *options=window.findChild<QToolButton *>("adjustment-options");
    if(!groups||!options||!options->menu())return false;
    groups->setCurrentIndex(0);
    for(auto *action:options->menu()->actions())if(action->text()==QStringLiteral("展开当前分组")){
        action->trigger();QCoreApplication::processEvents();return true;
    }
    return false;
}
