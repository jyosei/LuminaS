#pragma once
#include <QWidget>
#include "core/document.h"
#include <optional>
class CurveEditor : public QWidget {
    Q_OBJECT
public:
    explicit CurveEditor(QWidget *parent=nullptr);
    void setCurve(const Curve &value,const QVector<double> &tangents={});
    void setReadOnly(bool value){readOnly=value;setFocusPolicy(value?Qt::NoFocus:Qt::StrongFocus);update();}
    void setRegionBoundaries(const std::array<double,3> &value){regionBoundaries=value;update();}
    bool isDragging() const {return dragging;}
signals:
    void curveEdited(Curve curve);
    void curveFinished();
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
private:
    void describeSelection();
    QRectF plot() const;
    QPointF toPoint(QPointF position) const;
    QPointF toScreen(QPointF point) const;
    Curve curve{{0,0},{1,1}};
    QVector<double> tangents;
    int selected=-1;
    bool dragging=false;
    bool readOnly=false;
    std::optional<std::array<double,3>> regionBoundaries;
};
