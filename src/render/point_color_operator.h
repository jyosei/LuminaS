#pragma once
#include "core/document.h"
#include <algorithm>
#include <cmath>

// A bounded global color selection in post-tone linear work RGB, not a local mask.
class PointColorOperator {
public:
    explicit PointColorOperator(const Recipe &r):points(r.pointColors){
        for(const auto &p:points)active|=p.hueShift!=0||p.saturationShift!=0||p.luminanceShift!=0;
    }
    static bool descriptor(const float *v,PointColorAdjustment &out){
        for(int c=0;c<3;++c)if(!std::isfinite(v[c]))return false;
        const double maximum=std::max({v[0],v[1],v[2]}),minimum=std::min({v[0],v[1],v[2]}),delta=maximum-minimum;
        const double y=.2627*v[0]+.6780*v[1]+.0593*v[2];
        if(delta<1e-7||y<=0)return false;
        double h=maximum==v[0]?(v[1]-v[2])/delta:maximum==v[1]?2+(v[2]-v[0])/delta:4+(v[0]-v[1])/delta;
        h=std::fmod(h*60+360,360.);
        out.hue=h;out.saturation=std::clamp(delta/std::max(std::abs(maximum),.18),0.,1.);out.luminance=y/(y+.18);return true;
    }
    float weight(const float *v) const{
        PointColorAdjustment d;if(!descriptor(v,d))return 0;double total=0;
        for(const auto &p:points)total+=selection(p,d);return float(std::min(1.,total));
    }
    void apply(float *v) const{
        if(!active)return;PointColorAdjustment d;if(!descriptor(v,d))return;
        double sum=0,dh=0,ds=0,dl=0;
        // Every selection sees the same input; overlapping edits are order independent.
        for(const auto &p:points){const double strength=std::max({std::abs(p.hueShift),std::abs(p.saturationShift),std::abs(p.luminanceShift)});if(strength==0)continue;const double w=selection(p,d);sum+=w*strength;dh+=w*p.hueShift;ds+=w*p.saturationShift;dl+=w*p.luminanceShift;}
        if(sum==0||(dh==0&&ds==0&&dl==0))return;
        const double normalizer=std::max(1.,sum);dh=60*dh/normalizer;ds/=normalizer;dl/=normalizer;
        const double maximum=std::max({v[0],v[1],v[2]}),minimum=std::min({v[0],v[1],v[2]});
        const double h=std::fmod(d.hue+dh+360.,360.)/60,delta=(maximum-minimum)*(1+ds),low=(maximum+minimum-delta)/2;
        const int sector=int(h);const double fraction=h-sector,x=delta*(sector%2?1-fraction:fraction);double rgb[3]{};
        switch(sector){case 0:rgb[0]=delta;rgb[1]=x;break;case 1:rgb[0]=x;rgb[1]=delta;break;case 2:rgb[1]=delta;rgb[2]=x;break;case 3:rgb[1]=x;rgb[2]=delta;break;case 4:rgb[0]=x;rgb[2]=delta;break;default:rgb[0]=delta;rgb[2]=x;}
        const double gain=std::exp2(dl);for(int c=0;c<3;++c)v[c]=float((rgb[c]+low)*gain);
    }
private:
    static double shoulder(double distance,double width,double feather){
        if(distance>=width)return 0;const double core=width*(1-feather);if(distance<=core)return 1;
        const double t=(distance-core)/(width-core);return 1-t*t*(3-2*t);
    }
    static double selection(const PointColorAdjustment &p,const PointColorAdjustment &d){
        return shoulder(std::abs(std::remainder(d.hue-p.hue,360.)),p.hueWidth,p.feather)*
            shoulder(std::abs(d.saturation-p.saturation),p.saturationWidth,p.feather)*
            shoulder(std::abs(d.luminance-p.luminance),p.luminanceWidth,p.feather)*d.saturation;
    }
    QVector<PointColorAdjustment> points;
    bool active=false;
};
