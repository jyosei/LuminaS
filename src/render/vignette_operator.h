#pragma once
#include "core/document.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

// Post-crop style effect in normalized output coordinates. Not lens correction.
class PostCropVignette {
public:
    PostCropVignette(const Recipe &r,QSize output):active(r.vignetteAmount!=0) {
        if(!active)return;
        const double start=.05+.9*r.vignetteMidpoint;
        const double end=start+(1-start)*(.1+.9*r.vignetteFeather);
        for(size_t i=0;i<gains.size();++i){
            const double radius=std::sqrt(double(i)/(gains.size()-1));
            const double t=std::clamp((radius-start)/(end-start),0.,1.);
            gains[i]=float(std::exp2(r.vignetteAmount*t*t*(3-2*t)));
        }
        xSquared.resize(size_t(output.width()));
        for(int x=0;x<output.width();++x){const double dx=2*(x+.5)/output.width()-1;xSquared[size_t(x)]=float(.5*dx*dx);}
        ySquared.resize(size_t(output.height()));
        for(int y=0;y<output.height();++y){const double dy=2*(y+.5)/output.height()-1;ySquared[size_t(y)]=float(.5*dy*dy);}
    }
    bool enabled() const {return active;}
    float gain(int x,int y) const {
        if(!active)return 1;
        const float index=std::clamp(xSquared[size_t(x)]+ySquared[size_t(y)],0.f,1.f)*float(gains.size()-1);
        const auto i=size_t(index),j=std::min(i+1,gains.size()-1);
        return gains[i]+(gains[j]-gains[i])*(index-float(i));
    }
    void apply(float *rgb,int x,int y) const {
        if(!active)return;
        const float g=gain(x,y);for(int c=0;c<3;++c)rgb[c]*=g;
    }
private:
    bool active;
    std::vector<float> xSquared,ySquared;
    std::array<float,16385> gains; // initialized only for enabled effects
};
