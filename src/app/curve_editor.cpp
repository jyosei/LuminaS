#include "curve_editor.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QKeyEvent>
#include <algorithm>
#include <QLineF>
#include "core/curve_spline.h"
CurveEditor::CurveEditor(QWidget *parent):QWidget(parent){setObjectName("curve-editor");setMinimumHeight(180);setMaximumHeight(220);setFocusPolicy(Qt::StrongFocus);setAccessibleName(QStringLiteral("曲线编辑器"));setToolTip(QStringLiteral("点击添加点，拖动调整；Delete删除选中的内部点；Esc取消本次调整。Ctrl+左右选择点，方向键微调，Shift加大步长，Insert插点，Home/End选端点。感知刻度，最多64点。"));}
QRectF CurveEditor::plot() const{return QRectF(12,12,width()-24,height()-24);}
QPointF CurveEditor::toPoint(QPointF position) const{auto r=plot();return {std::clamp((position.x()-r.left())/r.width(),0.,1.),std::clamp(1-(position.y()-r.top())/r.height(),0.,1.)};}
QPointF CurveEditor::toScreen(QPointF p) const{auto r=plot();return {r.left()+p.x()*r.width(),r.bottom()-p.y()*r.height()};}
void CurveEditor::setCurve(const Curve &value,const QVector<double> &valueTangents){curve=value;tangents=valueTangents;selected=-1;dragging=false;update();}
void CurveEditor::paintEvent(QPaintEvent *){QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.fillRect(rect(),palette().color(QPalette::Base));p.setPen(palette().color(QPalette::Mid));auto r=plot();if(regionBoundaries){double left=0;for(int i=0;i<4;++i){const double right=i==3?1:(*regionBoundaries)[i];QColor shade("#b99de4");shade.setAlpha(i%2?12:24);p.fillRect(QRectF(r.left()+left*r.width(),r.top(),(right-left)*r.width(),r.height()),shade);left=right;}p.setPen(QPen(QColor("#b99de4"),1,Qt::DashLine));for(double split:*regionBoundaries)p.drawLine(QPointF(r.left()+split*r.width(),r.top()),QPointF(r.left()+split*r.width(),r.bottom()));p.setPen(palette().color(QPalette::Mid));}if(hasFocus()){p.setPen(QPen(palette().color(QPalette::Highlight),1));p.drawRect(rect().adjusted(1,1,-2,-2));p.setPen(palette().color(QPalette::Mid));}for(int i=0;i<=4;++i){p.drawLine(QPointF(r.left()+r.width()*i/4,r.top()),QPointF(r.left()+r.width()*i/4,r.bottom()));p.drawLine(QPointF(r.left(),r.top()+r.height()*i/4),QPointF(r.right(),r.top()+r.height()*i/4));}p.setPen(QPen(palette().color(QPalette::Mid),1,Qt::DashLine));p.drawLine(r.bottomLeft(),r.topRight());QPainterPath path;const CurveSpline spline(curve,tangents);path.moveTo(toScreen(curve.first()));for(int i=0;i<curve.size()-1;++i){const auto a=curve[i],b=curve[i+1];const double third=(b.x()-a.x())/3;path.cubicTo(toScreen({a.x()+third,a.y()+third*spline.tangent(i)}),toScreen({b.x()-third,b.y()-third*spline.tangent(i+1)}),toScreen(b));}p.setPen(QPen(QColor("#b99de4"),2));p.drawPath(path);if(!readOnly)for(int i=0;i<curve.size();++i){p.setBrush(i==selected?QColor("#b99de4"):palette().color(QPalette::Base));p.drawEllipse(toScreen(curve[i]),5,5);}}
void CurveEditor::mousePressEvent(QMouseEvent *e){if(readOnly||e->button()!=Qt::LeftButton)return;setFocus();selected=-1;double distance=12;
    for(int i=0;i<curve.size();++i){double d=QLineF(e->position(),toScreen(curve[i])).length();if(d<distance){selected=i;distance=d;}}
    if(selected<0){auto p=toPoint(e->position());if(curve.size()>=maximumCurvePoints)return;int i=1;while(i<curve.size()&&curve[i].x()<p.x())++i;if(i==curve.size()||p.x()-curve[i-1].x()<.005||curve[i].x()-p.x()<.005)return;tangents.clear();curve.insert(i,p);selected=i;dragging=true;emit curveEdited(curve);}
    dragging=true;update();e->accept();
}
void CurveEditor::mouseMoveEvent(QMouseEvent *e){if(!dragging||selected<0)return;auto p=toPoint(e->position());if(selected==0)p.setX(0);else if(selected==curve.size()-1)p.setX(1);else {const double gap=std::min(.005,(curve[selected+1].x()-curve[selected-1].x())*.25);p.setX(std::clamp(p.x(),curve[selected-1].x()+gap,curve[selected+1].x()-gap));}if(p!=curve[selected]){tangents.clear();curve[selected]=p;emit curveEdited(curve);update();}e->accept();}
void CurveEditor::mouseReleaseEvent(QMouseEvent *e){if(e->button()==Qt::LeftButton&&dragging){dragging=false;emit curveFinished();update();e->accept();}}
void CurveEditor::describeSelection(){
    setAccessibleDescription(selected>=0&&selected<curve.size()?QStringLiteral("控制点 %1/%2，输入 %3，输出 %4").arg(selected+1).arg(curve.size()).arg(curve[selected].x(),0,'f',3).arg(curve[selected].y(),0,'f',3):QStringLiteral("尚未选择控制点；Ctrl+左右选择，Insert插入"));
}
void CurveEditor::keyPressEvent(QKeyEvent *e){
    if(readOnly){QWidget::keyPressEvent(e);return;}
    const int key=e->key();const bool previous=key==Qt::Key_Left,next=key==Qt::Key_Right;
    if(key==Qt::Key_Home||key==Qt::Key_End||((previous||next)&&(e->modifiers()&Qt::ControlModifier))){
        if(key==Qt::Key_Home)selected=0;else if(key==Qt::Key_End)selected=int(curve.size())-1;
        else if(selected<0)selected=next?0:int(curve.size())-1;
        else selected=std::clamp(selected+(next?1:-1),0,int(curve.size())-1);
        describeSelection();update();e->accept();return;
    }
    bool changed=false;
    if(key==Qt::Key_Insert&&curve.size()<maximumCurvePoints){
        int interval=0;for(int i=1;i<curve.size()-1;++i)if(curve[i+1].x()-curve[i].x()>curve[interval+1].x()-curve[interval].x())interval=i;
        const double x=(curve[interval].x()+curve[interval+1].x())*.5;
        if(x-curve[interval].x()>=.005&&curve[interval+1].x()-x>=.005){const double y=std::clamp(CurveSpline(curve,tangents).value(x),0.,1.);curve.insert(interval+1,{x,y});selected=interval+1;changed=true;}
    }else if((key==Qt::Key_Delete||key==Qt::Key_Backspace)&&selected>0&&selected<curve.size()-1){curve.removeAt(selected);selected=std::min(selected,int(curve.size())-1);changed=true;
    }else if(previous||next||key==Qt::Key_Up||key==Qt::Key_Down){
        if(selected<0){selected=0;describeSelection();update();e->accept();return;}
        auto point=curve[selected];const double step=(e->modifiers()&Qt::ShiftModifier)?.05:.005;
        if(key==Qt::Key_Up||key==Qt::Key_Down)point.setY(std::clamp(point.y()+(key==Qt::Key_Up?step:-step),0.,1.));
        else if(selected>0&&selected<curve.size()-1){const double gap=std::min(.005,(curve[selected+1].x()-curve[selected-1].x())*.25);point.setX(std::clamp(point.x()+(next?step:-step),curve[selected-1].x()+gap,curve[selected+1].x()-gap));}
        if(point!=curve[selected]){curve[selected]=point;changed=true;}
    }else {QWidget::keyPressEvent(e);return;}
    if(changed){tangents.clear();emit curveEdited(curve);emit curveFinished();}
    describeSelection();update();e->accept();
}
