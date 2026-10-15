#pragma once
#include "document.h"
#include <algorithm>
// Four region-centre anchors. Bounded displacement guarantees ordered outputs.
inline Curve parametricPoints(const ParametricCurve &p){
    const auto &b=p.splits;
    const double x[]{0,b[0]/2,(b[0]+b[1])/2,(b[1]+b[2])/2,(b[2]+1)/2,1};
    Curve result{{0,0}};
    for(int i=1;i<=4;++i)result.append({x[i],x[i]+.45*p.regions[i-1]*std::min(x[i]-x[i-1],x[i+1]-x[i])});
    result.append({1,1});return result;
}
