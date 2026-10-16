#pragma once
#include "core/photo_document.h"
#include "local_mask_math.h"
#include <algorithm>
#include <cmath>

namespace LocalEdit::Detail {
// Source edge coordinates. The first line is coverage 0, the second coverage 1;
// the half-planes outside them remain 0/1. No output-grid dependent sampling.
inline float linearGradientCoverage(const LinearGeometry &g,QPointF p){
    const double dx=g.end.x()-g.start.x(),dy=g.end.y()-g.start.y();
    const double t=((p.x()-g.start.x())*dx+(p.y()-g.start.y())*dy)/(dx*dx+dy*dy);
    return float(std::clamp(t,0.,1.));
}
// The ellipse is 1 at its centre, 0 on/outside its boundary. The cubic profile
// matches a fully soft brush head, applied in the ellipse's source-local axes.
inline float radialGradientCoverage(const RadialGeometry &g,QPointF p,double co,double si){
    const double dx=p.x()-g.center.x(),dy=p.y()-g.center.y();
    const double x=(co*dx+si*dy)/g.radiusX,y=(-si*dx+co*dy)/g.radiusY;
    if(std::abs(x)>=1||std::abs(y)>=1)return 0;
    return brushHeadCoverage(std::hypot(x,y),1,0);
}
inline float radialGradientCoverage(const RadialGeometry &g,QPointF p){
    const double angle=g.angleDegrees*3.14159265358979323846/180;
    return radialGradientCoverage(g,p,std::cos(angle),std::sin(angle));
}
}
