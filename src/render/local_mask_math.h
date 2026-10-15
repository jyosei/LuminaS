#pragma once
#include <cmath>

namespace LocalEdit::Detail {
inline float brushHeadCoverage(double distance,double radius,double hardness){
    if(distance>=radius)return 0;
    const double hardRadius=radius*hardness;
    if(distance<=hardRadius)return 1;
    const double t=(distance-hardRadius)/(radius-hardRadius);
    return float((1-t)*(1-t)*(1+2*t));
}
}
