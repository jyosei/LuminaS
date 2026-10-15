#pragma once
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QScrollArea>
#include <QSlider>
#include <QWheelEvent>

namespace Ui {
// Browsing an inspector must not silently edit the photograph. A clicked or
// keyboard-focused control keeps its native wheel behaviour; otherwise route
// the unchanged wheel delta to the containing scroll viewport.
class ParameterWheelGuard final:public QObject {
public:
    explicit ParameterWheelGuard(QWidget *root):QObject(root){
        for(auto *widget:root->findChildren<QWidget *>())
            if(qobject_cast<QAbstractSpinBox *>(widget)||qobject_cast<QSlider *>(widget)||qobject_cast<QComboBox *>(widget)){
                widget->setFocusPolicy(Qt::StrongFocus);widget->installEventFilter(this);
            }
    }
protected:
    bool eventFilter(QObject *target,QEvent *event) override {
        auto *widget=qobject_cast<QWidget *>(target);
        if(event->type()!=QEvent::Wheel||!widget||widget->hasFocus())return false;
        for(auto *parent=widget->parentWidget();parent;parent=parent->parentWidget())if(auto *area=qobject_cast<QScrollArea *>(parent)){
            auto *wheel=static_cast<QWheelEvent *>(event);auto *viewport=area->viewport();
            QWheelEvent forwarded(QPointF(viewport->mapFromGlobal(wheel->globalPosition().toPoint())),wheel->globalPosition(),wheel->pixelDelta(),wheel->angleDelta(),wheel->buttons(),wheel->modifiers(),wheel->phase(),wheel->inverted(),wheel->source(),wheel->pointingDevice());
            QCoreApplication::sendEvent(viewport,&forwarded);event->accept();return true;
        }
        return false;
    }
};
}
