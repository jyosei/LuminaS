#include "canvas.h"
#include "display_presenter.h"
#include <QPainter>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QShowEvent>
#include <QScreen>
#include <QWindow>
#include <algorithm>
#include <cmath>
Canvas::Canvas(QWidget *parent) : QWidget(parent) {
    setAccessibleName(QStringLiteral("照片预览"));setAccessibleDescription(message);
    setMinimumSize(320, 240);
    displayPresenter = new DisplayPresenter(this);
    connect(displayPresenter,&DisplayPresenter::imageReady,this,&Canvas::imageReady);
    connect(displayPresenter,&DisplayPresenter::presentationCleared,this,[this]{presented={};update();});
    updateToolTip();
}
void Canvas::updateCursor() {
    setCursor(image.isNull()?Qt::ArrowCursor:(cropMode||pickerMode)?Qt::CrossCursor:dragging?Qt::ClosedHandCursor:Qt::OpenHandCursor);
}
void Canvas::updateToolTip(){
    QString tip=QStringLiteral("左键小手拖动 · 滚轮围绕指针缩放 · 双击适屏");
    const QString policy=displayNote;
    if(!policy.isEmpty())tip+=QStringLiteral("\n显示: %1").arg(policy);
    setToolTip(tip);
}
// Presented frames only: this is the single place that counts an actually
// displayable frame, so unconverted/accepted requests never paint as success.
void Canvas::imageReady(const QImage &ready,const QString &note){
    presented=ready;
    displayNote=note;
    if(!presented.isNull()){
        ++imageSerial;
        message.clear();
    }
    updateToolTip();
    update();
}
void Canvas::setImage(QImage value,QString context) {
    const bool reset=image.isNull()||value.size()!=image.size();
    image = std::move(value);
    liveAngle=0;
    if(reset||!displayPresenter||context!=displayPresenter->contentContext())presented={};
    message.clear();
    if(reset){dragging=false;cropDragging=false;fit();}
    if(displayPresenter)displayPresenter->setImage(image,context);
    updateCursor();update();
}
void Canvas::setLiveAngle(double angle){if(liveAngle==angle)return;liveAngle=angle;++imageSerial;update();}
void Canvas::setPickerMode(bool value){pickerMode=value;dragging=false;cropDragging=false;updateCursor();}
void Canvas::setCropMode(bool value){if(cropMode!=value){dragging=false;cropDragging=false;}if(value)pickerMode=false;cropMode=value;updateCursor();update();}
void Canvas::setMessage(QString value) { image = {}; presented={}; dragging=false;cropDragging=false;message = std::move(value);setAccessibleDescription(message);if(displayPresenter)displayPresenter->clear();updateCursor();updateToolTip();update(); }
void Canvas::setMonitorProfile(QByteArray icc,QString note){if(displayPresenter)displayPresenter->setMonitorProfile(icc,note);}
void Canvas::useSystemMonitorProfile(){if(displayPresenter)displayPresenter->useSystemMonitorProfile();}
QString Canvas::displaySnapshot() const{return displayPresenter?displayPresenter->displaySnapshot():QString();}
QString Canvas::displayProfileNote() const{return displayNote.isEmpty()&&displayPresenter?displayPresenter->profileNote():displayNote;}
void Canvas::showEvent(QShowEvent *event){
    QWidget::showEvent(event);
    // The screen is resolved once per window handle and then kept in sync by
    // screenChanged; no QScreen ever reaches the presenter or its worker.
    QScreen *current=nullptr;
    if(QWidget *top=window())if(QWindow *handle=top->windowHandle())current=handle->screen();
    if(!current)current=screen();
    if(current&&displayPresenter)displayPresenter->setScreenName(current->name());
    if(!window()||!window()->windowHandle())return;
    QWindow *handle=window()->windowHandle();
    QObject::disconnect(screenConnection);
    screenConnection=connect(handle,&QWindow::screenChanged,this,[this](QScreen *next){
        if(!next)return;
        if(displayPresenter)displayPresenter->setScreenName(next->name());
        if(window()&&window()->windowHandle())++screenSerial;
    });
}
void Canvas::fit() {
    fitting = true;
    if (!image.isNull()) {
        scale = std::min(double(width()) / image.width(), double(height()) / image.height()) * .95;
        offset = QPointF((width() - image.width() * scale) / 2, (height() - image.height() * scale) / 2);
    }
    update();
}
void Canvas::actualPixels(){
    if(image.isNull())return;
    fitting=false;scale=1.0/devicePixelRatioF();
    offset=QPointF((width()-image.width()*scale)/2,(height()-image.height()*scale)/2);
    update();
}
void Canvas::paintEvent(QPaintEvent *) {
    QPainter p(this); p.fillRect(rect(), QColor("#1a1b1e"));
    if (image.isNull()) { p.setPen(Qt::lightGray); p.drawText(rect().adjusted(20,20,-20,-20), Qt::AlignCenter | Qt::TextWordWrap, message); return; }
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.translate(offset); p.scale(scale, scale);
    p.setClipRect(QRectF(0,0,image.width(),image.height()));p.translate(image.width()/2.,image.height()/2.);p.rotate(liveAngle);p.translate(-image.width()/2.,-image.height()/2.);
    if(!presented.isNull())p.drawImage(QPointF(0,0), presented);
    else {p.setPen(QPen(QColor(200,200,200),2/scale));p.drawText(QRectF(0,0,image.width(),image.height()),Qt::AlignCenter|Qt::TextWordWrap,QStringLiteral("正在准备显示预览…"));}
    if(cropDragging){p.setPen(QPen(Qt::yellow,2/scale,Qt::DashLine));p.drawRect(QRectF(cropStart,cropEnd).normalized());}
    p.end();if(!presented.isNull()&&paintedSerial!=imageSerial){paintedSerial=imageSerial;emit framePainted();}
}
void Canvas::resizeEvent(QResizeEvent *) { if (fitting) fit(); }
void Canvas::wheelEvent(QWheelEvent *event) {
    if (image.isNull()) return;
    const int delta=event->angleDelta().y()!=0?event->angleDelta().y():event->pixelDelta().y();
    if(delta==0){event->ignore();return;}
    const double next = std::clamp(scale * std::pow(1.0015, delta), .02, 16.0);
    offset = event->position() - (event->position() - offset) * (next / scale);
    scale = next; fitting = false; update(); event->accept();
}
void Canvas::mousePressEvent(QMouseEvent *event) {
    if(pickerMode&&event->button()==Qt::LeftButton&&!image.isNull()){
        const auto p=(event->position()-offset)/scale;
        if(liveAngle==0&&p.x()>=0&&p.y()>=0&&p.x()<image.width()&&p.y()<image.height())emit neutralPicked({p.x()/image.width(),p.y()/image.height()});event->accept();return;
    }
    if(cropMode&&event->button()==Qt::LeftButton&&!image.isNull()){
        cropDragging=true;cropStart=cropEnd=(event->position()-offset)/scale;
        cropStart.setX(std::clamp(cropStart.x(),0.,double(image.width())));cropStart.setY(std::clamp(cropStart.y(),0.,double(image.height())));cropEnd=cropStart;update();event->accept();return;
    }
    if (event->button() == Qt::LeftButton && !image.isNull()) { dragging = true; previous = event->position();updateCursor();event->accept();return; }
    QWidget::mousePressEvent(event);
}
void Canvas::mouseMoveEvent(QMouseEvent *event) {
    if(cropDragging){cropEnd=(event->position()-offset)/scale;cropEnd.setX(std::clamp(cropEnd.x(),0.,double(image.width())));cropEnd.setY(std::clamp(cropEnd.y(),0.,double(image.height())));if(cropAspect>0){const auto delta=cropEnd-cropStart;const double sx=delta.x()<0?-1:1,sy=delta.y()<0?-1:1;double w=std::abs(delta.x()),h=std::abs(delta.y());if(w>h*cropAspect)h=w/cropAspect;else w=h*cropAspect;const double availableW=sx>0?image.width()-cropStart.x():cropStart.x(),availableH=sy>0?image.height()-cropStart.y():cropStart.y();const double shrink=std::min({1.,w>0?availableW/w:1.,h>0?availableH/h:1.});cropEnd=cropStart+QPointF(sx*w*shrink,sy*h*shrink);}update();event->accept();return;}
    if (dragging) { offset += event->position() - previous; previous = event->position(); fitting = false; update();event->accept();return; }
    QWidget::mouseMoveEvent(event);
}
void Canvas::mouseReleaseEvent(QMouseEvent *event) {
    if(event->button()!=Qt::LeftButton){QWidget::mouseReleaseEvent(event);return;}
    if(cropDragging){cropDragging=false;const auto rect=QRectF(cropStart,cropEnd).normalized();update();
        if(rect.width()>1&&rect.height()>1)emit cropPicked({rect.x()/image.width(),rect.y()/image.height(),rect.width()/image.width(),rect.height()/image.height()});
    }
    dragging = false;updateCursor();event->accept();
}
void Canvas::mouseDoubleClickEvent(QMouseEvent *event) {
    if(event->button()==Qt::LeftButton&&!cropMode&&!pickerMode&&!image.isNull()){dragging=false;updateCursor();fit();event->accept();return;}
    QWidget::mouseDoubleClickEvent(event);
}
