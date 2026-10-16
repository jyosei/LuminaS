#include "canvas.h"
#include "display_presenter.h"
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QFocusEvent>
#include <QShowEvent>
#include <QScreen>
#include <QWindow>
#include <algorithm>
#include <cmath>
Canvas::Canvas(QWidget *parent) : QWidget(parent) {
    setAccessibleName(QStringLiteral("照片预览"));setAccessibleDescription(message);
    setMinimumSize(320, 240);
    // Keyboard brush shortcuts are canvas-local: no application-wide filter, so a
    // focused text field elsewhere never reaches this widget.
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    displayPresenter = new DisplayPresenter(this);
    connect(displayPresenter,&DisplayPresenter::imageReadyTagged,this,[this](const QImage &ready,const QString &note,qint64 key){
        presentedCanonicalKey=key;imageReady(ready,note);
    });
    connect(displayPresenter,&DisplayPresenter::presentationCleared,this,[this]{presented={};presentedCanonicalKey=0;update();});
    updateToolTip();
}
QPointF Canvas::normalizedFor(const QPointF &position) const {
    if(image.isNull())return {};
    const QPointF p=(position-offset)/scale;
    return QPointF(p.x()/image.width(),p.y()/image.height());
}
void Canvas::updateCursor() {
    if(spaceHeld){setCursor(dragging?Qt::ClosedHandCursor:Qt::OpenHandCursor);return;}
    if(image.isNull()){setCursor(Qt::ArrowCursor);return;}
    if(cropMode||pickerMode||brushActive){setCursor(Qt::CrossCursor);return;}
    setCursor(dragging?Qt::ClosedHandCursor:Qt::OpenHandCursor);
}
void Canvas::updateToolTip(){
    QString tip=QStringLiteral("左键小手拖动 · 滚轮围绕指针缩放 · 双击适屏");
    if(brushActive)tip+=gradientMode?QStringLiteral("\n渐变: 拖动创建 · 拖控制点调整 · Alt 减去 · Space 小手 · O 覆盖 · Esc 取消"):
        QStringLiteral("\n画笔: 左键绘制 · Alt 擦除 · Space 临时小手 · [ ] 大小 · O 覆盖 · Esc 取消");
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
    // A same-size frame of the same context must not interrupt an open stroke;
    // a geometry change ends it first.
    if(reset)finishBrushGesture(false);
    // A different document/revision must never keep showing the previous mask.
    if(reset||context!=imageContext){localMask={};localMaskColors.clear();maskSourceKey=paintedMaskKey=0;gradientHandles.clear();gradientGuide.clear();gradientHandle=-1;}
    image = std::move(value);
    imageContext=context;
    liveAngle=0;
    if(reset||!displayPresenter||context!=displayPresenter->contentContext())presented={};
    message.clear();
    if(reset){stopHandPan();dragging=false;cropDragging=false;fit();}
    if(displayPresenter)displayPresenter->setImage(image,context);
    updateCursor();update();
}
void Canvas::setLiveAngle(double angle){if(liveAngle==angle)return;finishBrushGesture(false);liveAngle=angle;++imageSerial;update();}
void Canvas::setPickerMode(bool value){
    if(pickerMode==value){updateCursor();return;}
    if(value){finishBrushGesture(false);stopHandPan();}
    pickerMode=value;dragging=false;cropDragging=false;
    if(value){cropMode=false;brushActive=false;}
    updateCursor();update();
}
void Canvas::setCropMode(bool value){
    if(cropMode==value){updateCursor();return;}
    if(value){finishBrushGesture(false);stopHandPan();}
    dragging=false;cropDragging=false;
    if(value){pickerMode=false;brushActive=false;}
    cropMode=value;updateCursor();update();
}
void Canvas::setBrushMode(bool value){
    if(brushActive==value){updateCursor();return;}
    // Leaving brush mode must commit a valid open stroke first.
    finishBrushGesture(false);
    stopHandPan();
    brushActive=value;dragging=false;cropDragging=false;
    if(value){cropMode=false;pickerMode=false;}
    updateCursor();updateToolTip();update();
}
void Canvas::finishBrushGesture(bool canceled){
    if(!brushGesture)return;
    // Close the gesture state before emitting so a re-entrant finish is a no-op
    // and a handler cannot observe a half-open stroke.
    brushGesture=false;
    brushSubtract=false;
    updateCursor();update();
    emit brushFinished(canceled);
}
void Canvas::setGradientMode(bool enabled){
    if(gradientMode==enabled)return;
    finishBrushGesture(false);gradientMode=enabled;gradientHandle=-1;
    updateToolTip();update();
}
void Canvas::setGradientHandles(QVector<QPointF> handles){
    if(gradientHandles==handles)return;
    gradientHandles=std::move(handles);update();
}
void Canvas::setGradientGuide(QVector<QPointF> points){gradientGuide=std::move(points);update();}
int Canvas::gradientHandleAt(const QPointF &position) const{
    if(!gradientMode||!brushActive||image.isNull()||presented.isNull()||liveAngle!=0)return -1;
    int hit=-1;double nearest=9*9;
    for(qsizetype i=0;i<gradientHandles.size();++i){
        const auto &uv=gradientHandles[i];if(!std::isfinite(uv.x())||!std::isfinite(uv.y()))continue;
        const QPointF point=offset+QPointF(uv.x()*image.width()*scale,uv.y()*image.height()*scale);
        const auto d=point-position;const double distance=d.x()*d.x()+d.y()*d.y();
        if(distance<nearest){hit=int(i);nearest=distance;}
    }
    return hit;
}
void Canvas::setLocalMask(QImage mask){
    if(mask.isNull()||mask.format()!=QImage::Format_Indexed8||image.isNull()||mask.size()!=image.size()){
        localMask={};localMaskColors.clear();maskSourceKey=paintedMaskKey=0;update();return;
    }
    if(localMaskColors.isEmpty()){
        localMaskColors.reserve(256);
        // Coverage index 0..255 becomes the view colour; the formula is display
        // only and never feeds RGB, export or the source document.
        for(int index=0;index<256;++index)localMaskColors.append(qRgba(172,145,235,int(index*.4+.5)));
    }
    maskSourceKey=mask.cacheKey();
    mask.setColorTable(localMaskColors);
    localMask=std::move(mask);
    update();
}
void Canvas::setMessage(QString value) { finishBrushGesture(false); stopHandPan(); image = {}; imageContext.clear(); presented={}; localMask={}; localMaskColors.clear();gradientHandles.clear();gradientHandle=-1; dragging=false;cropDragging=false;leftDown=false;message = std::move(value);setAccessibleDescription(message);if(displayPresenter)displayPresenter->clear();updateCursor();updateToolTip();update(); }
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
void Canvas::stopHandPan(){ if(dragging&&!cropDragging){dragging=false;updateCursor();} else dragging=false; }
void Canvas::fit() {
    finishBrushGesture(false);
    fitting = true;
    if (!image.isNull()) {
        scale = std::min(double(width()) / image.width(), double(height()) / image.height()) * .95;
        offset = QPointF((width() - image.width() * scale) / 2, (height() - image.height() * scale) / 2);
    }
    update();
}
void Canvas::actualPixels(){
    if(image.isNull())return;
    finishBrushGesture(false);
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
    // View-only coverage drawn with the photo's own transform and clip. It is a
    // repaint of the same presented frame, so it never counts as framePainted.
    if(!presented.isNull()&&!localMask.isNull())p.drawImage(QPointF(0,0),localMask);
    if(cropDragging){p.setPen(QPen(Qt::yellow,2/scale,Qt::DashLine));p.drawRect(QRectF(cropStart,cropEnd).normalized());}
    // Draw controls in widget DIP after restoring the photo transform and clip.
    // This does not change either image serial or mask provenance.
    if(gradientMode&&brushActive&&liveAngle==0&&!presented.isNull()){
        p.resetTransform();p.setClipping(false);p.setRenderHint(QPainter::Antialiasing);
        QPainterPath guide;bool connected=false;
        for(const auto &uv:gradientGuide){
            const auto point=offset+QPointF(uv.x()*image.width()*scale,uv.y()*image.height()*scale);
            // Break a guide at failed projections and remote points, rather than
            // drawing a spurious connector across a mapping singularity.
            if(!std::isfinite(point.x())||!std::isfinite(point.y())||std::abs(point.x())>1e6||std::abs(point.y())>1e6){connected=false;continue;}
            if(connected)guide.lineTo(point);else guide.moveTo(point);connected=true;
        }
        p.setPen(QPen(QColor(222,217,237,180),1,Qt::DashLine));p.drawPath(guide);
        p.setPen(QPen(QColor("#ded9ed"),1.5));p.setBrush(QColor("#37313f"));
        for(const auto &uv:gradientHandles){
            if(!std::isfinite(uv.x())||!std::isfinite(uv.y()))continue;
            const auto point=offset+QPointF(uv.x()*image.width()*scale,uv.y()*image.height()*scale);
            if(!QRectF(rect()).adjusted(-6,-6,6,6).contains(point))continue;
            p.drawEllipse(point,5,5);
        }
    }
    const bool newFrame=!presented.isNull()&&paintedSerial!=imageSerial;
    const bool newMask=!presented.isNull()&&!localMask.isNull()&&maskSourceKey!=paintedMaskKey;
    const auto frameKey=presentedCanonicalKey,maskKey=maskSourceKey;
    if(newFrame)paintedSerial=imageSerial;
    if(newMask)paintedMaskKey=maskSourceKey;
    p.end();
    if(newFrame){emit canonicalFramePainted(frameKey);emit framePainted();}
    if(newMask)emit localMaskPainted(maskKey);
}
void Canvas::resizeEvent(QResizeEvent *) { if (fitting) fit(); }
void Canvas::wheelEvent(QWheelEvent *event) {
    if (image.isNull()) return;
    const int delta=event->angleDelta().y()!=0?event->angleDelta().y():event->pixelDelta().y();
    if(delta==0){event->ignore();return;}
    // Zooming ends an open stroke; the next stroke needs a fresh press.
    finishBrushGesture(false);
    stopHandPan();
    const double next = std::clamp(scale * std::pow(1.0015, delta), .02, 16.0);
    offset = event->position() - (event->position() - offset) * (next / scale);
    scale = next; fitting = false; update(); event->accept();
}
void Canvas::mousePressEvent(QMouseEvent *event) {
    lastPointer=event->position();
    if(event->button()==Qt::LeftButton){leftDown=true;setFocus(Qt::MouseFocusReason);}
    // Temporary hand pan: Space already held means this press pans, not paints.
    if(spaceHeld&&event->button()==Qt::LeftButton&&!image.isNull()){
        if(brushGesture)finishBrushGesture(false);
        dragging=true;previous=event->position();updateCursor();event->accept();return;
    }
    if(brushActive&&event->button()==Qt::LeftButton&&!cropMode&&!pickerMode&&liveAngle==0&&!image.isNull()&&!presented.isNull()){
        const auto p=(event->position()-offset)/scale;
        gradientHandle=gradientHandleAt(event->position());
        if(gradientHandle>=0||(p.x()>=0&&p.y()>=0&&p.x()<image.width()&&p.y()<image.height())){
            brushGesture=true;
            // Alt is frozen for this stroke at press time.
            brushSubtract=(event->modifiers()&Qt::AltModifier)!=0;
            const QPointF uv=normalizedFor(event->position());
            updateCursor();event->accept();
            emit brushPressed(uv,brushSubtract);
            return;
        }
    }
    if(pickerMode&&event->button()==Qt::LeftButton&&!image.isNull()){
        const auto p=(event->position()-offset)/scale;
        if(liveAngle==0&&p.x()>=0&&p.y()>=0&&p.x()<image.width()&&p.y()<image.height())emit neutralPicked({p.x()/image.width(),p.y()/image.height()});event->accept();return;
    }
    if(cropMode&&event->button()==Qt::LeftButton&&!image.isNull()){
        cropDragging=true;cropStart=cropEnd=(event->position()-offset)/scale;
        cropStart.setX(std::clamp(cropStart.x(),0.,double(image.width())));cropStart.setY(std::clamp(cropStart.y(),0.,double(image.height())));cropEnd=cropStart;update();event->accept();return;
    }
    if (event->button() == Qt::LeftButton && !image.isNull() && !brushActive) { dragging = true; previous = event->position();updateCursor();event->accept();return; }
    QWidget::mousePressEvent(event);
}
void Canvas::mouseMoveEvent(QMouseEvent *event) {
    lastPointer=event->position();
    if(brushGesture){
        const QPointF uv=normalizedFor(event->position());
        // A stroke may leave the image; it is only rejected for non-finite input.
        if(std::isfinite(uv.x())&&std::isfinite(uv.y())){event->accept();emit brushMoved(uv);return;}
    }
    if(cropDragging){cropEnd=(event->position()-offset)/scale;cropEnd.setX(std::clamp(cropEnd.x(),0.,double(image.width())));cropEnd.setY(std::clamp(cropEnd.y(),0.,double(image.height())));if(cropAspect>0){const auto delta=cropEnd-cropStart;const double sx=delta.x()<0?-1:1,sy=delta.y()<0?-1:1;double w=std::abs(delta.x()),h=std::abs(delta.y());if(w>h*cropAspect)h=w/cropAspect;else w=h*cropAspect;const double availableW=sx>0?image.width()-cropStart.x():cropStart.x(),availableH=sy>0?image.height()-cropStart.y():cropStart.y();const double shrink=std::min({1.,w>0?availableW/w:1.,h>0?availableH/h:1.});cropEnd=cropStart+QPointF(sx*w*shrink,sy*h*shrink);}update();event->accept();return;}
    if (dragging) { offset += event->position() - previous; previous = event->position(); fitting = false; update();event->accept();return; }
    QWidget::mouseMoveEvent(event);
}
void Canvas::mouseReleaseEvent(QMouseEvent *event) {
    if(event->button()!=Qt::LeftButton){QWidget::mouseReleaseEvent(event);return;}
    lastPointer=event->position();leftDown=false;
    if(brushGesture){
        const QPointF uv=normalizedFor(event->position());
        if(std::isfinite(uv.x())&&std::isfinite(uv.y()))emit brushMoved(uv);
        finishBrushGesture(false);
        event->accept();return;
    }
    if(cropDragging){cropDragging=false;const auto rect=QRectF(cropStart,cropEnd).normalized();update();
        if(rect.width()>1&&rect.height()>1)emit cropPicked({rect.x()/image.width(),rect.y()/image.height(),rect.width()/image.width(),rect.height()/image.height()});
    }
    dragging = false;updateCursor();event->accept();
}
void Canvas::mouseDoubleClickEvent(QMouseEvent *event) {
    // In brush mode a double click must not fit or strand an open stroke.
    if(brushActive&&event->button()==Qt::LeftButton&&!cropMode&&!pickerMode){
        if(!brushGesture)mousePressEvent(event);
        event->accept();return;
    }
    if(event->button()==Qt::LeftButton&&!cropMode&&!pickerMode&&!image.isNull()){dragging=false;updateCursor();fit();event->accept();return;}
    QWidget::mouseDoubleClickEvent(event);
}
void Canvas::keyPressEvent(QKeyEvent *event) {
    if(event->key()==Qt::Key_Space){
        if(event->isAutoRepeat()){event->accept();return;}
        spaceHeld=true;
        // Space commits the open stroke, then optionally starts a temporary pan
        // with the still-held left button; a new stroke needs a fresh press.
        finishBrushGesture(false);
        if(leftDown&&brushActive&&!image.isNull()){dragging=true;previous=lastPointer;}
        updateCursor();event->accept();return;
    }
    if(event->key()==Qt::Key_Escape){
        if(brushGesture){finishBrushGesture(true);event->accept();return;}
        event->ignore();QWidget::keyPressEvent(event);return;
    }
    if(event->key()==Qt::Key_BracketLeft){
        finishBrushGesture(false);emit brushRadiusStepRequested(-1);event->accept();return;
    }
    if(event->key()==Qt::Key_BracketRight){
        finishBrushGesture(false);emit brushRadiusStepRequested(1);event->accept();return;
    }
    if(event->key()==Qt::Key_O){
        emit overlayToggleRequested();event->accept();return;
    }
    QWidget::keyPressEvent(event);
}
void Canvas::keyReleaseEvent(QKeyEvent *event) {
    if(event->key()==Qt::Key_Space){
        if(event->isAutoRepeat()){event->accept();return;}
        spaceHeld=false;
        // Ending the temporary hand stops panning but never restarts a stroke.
        dragging=false;
        updateCursor();event->accept();return;
    }
    QWidget::keyReleaseEvent(event);
}
void Canvas::focusOutEvent(QFocusEvent *event) {
    // Losing focus commits like a host tool change and drops transient hand state.
    finishBrushGesture(false);
    spaceHeld=false;leftDown=false;dragging=false;cropDragging=false;
    updateCursor();
    QWidget::focusOutEvent(event);
}
bool Canvas::event(QEvent *event) {
    if(event->type()==QEvent::WindowDeactivate){
        finishBrushGesture(false);
        spaceHeld=false;leftDown=false;dragging=false;
        updateCursor();
    }
    return QWidget::event(event);
}
