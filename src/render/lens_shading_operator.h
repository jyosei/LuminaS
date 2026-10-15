#pragma once
#include "core/document.h"
#include <array>
#include <algorithm>
#include <cmath>
// Manual source-plane correction, not a calibrated lens profile. The squared
// normalized radial measure is zero at the centre, one at the source corners.
class LensShading {
public:
    LensShading(QSize size,double amount):size(size),inverseWidth(1./size.width()),inverseHeight(1./size.height()),active(amount!=0){
        if(active)for(size_t i=0;i<table.size();++i)table[i]=float(std::exp2(amount*double(i)/(table.size()-1)));
    }
    float gain(int x,int y) const{
        if(!active)return 1;
        const double nx=(2*x+1-size.width())*inverseWidth,ny=(2*y+1-size.height())*inverseHeight,radius=(nx*nx+ny*ny)/2;
        const double at=std::clamp(radius*radius,0.,1.)*(table.size()-1);const auto i=size_t(at),j=std::min(i+1,table.size()-1);
        return table[i]+(table[j]-table[i])*float(at-i);
    }
    bool enabled() const{return active;}
private:
    QSize size;
    double inverseWidth=1,inverseHeight=1;
    bool active=false;
    std::array<float,4097> table{};
};
