#pragma once
#include <QPointF>
#include <QSize>
#include <cmath>
#include <limits>

// Manual generic optical mapping, not a calibrated lens profile. Edge-coordinate
// input/output, anchored to the direction-normalized full source centre. Using
// the half diagonal preserves circular radial symmetry in rectangular images.
class RadialDistortion {
public:
    RadialDistortion(QSize size,double amount):centre(size.width()/2.,size.height()/2.),coefficient(amount){
        const double w=size.width(),h=size.height();
        inverseRadiusSquared=4/(w*w+h*h);
    }
    QPointF source(QPointF p) const {
        if(coefficient==0)return p;
        const double x=p.x()-centre.x(),y=p.y()-centre.y();
        const double t=(x*x+y*y)*inverseRadiusSquared;
        if(!std::isfinite(t)){const double nan=std::numeric_limits<double>::quiet_NaN();return {nan,nan};}
        const double gain=1+coefficient*t/(1+t);
        return {centre.x()+x*gain,centre.y()+y*gain};
    }
private:
    QPointF centre;
    double coefficient=0,inverseRadiusSquared=0;
};
// f(r)=r(1+k r²/(1+r²)); tangential scale >= .6 and radial derivative
// 1+k*t*(t+3)/(1+t)² >= .55 for |k|<=.4, since the ratio has maximum 9/8.
// Thus even perspective-mapped points outside the canvas never cause foldover.
