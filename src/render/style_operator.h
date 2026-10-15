#pragma once
#include "core/document.h"
#include <algorithm>
#include <cmath>
// LuminaS's linear-workspace toning and original-color monochrome mixer.
class StyleOperator {
public:
    explicit StyleOperator(const Recipe &r):monochrome(r.monochrome&&r.monochromeStrength>0){
        for(const auto &a:r.grading)grading|=a.saturation!=0||a.luminance!=0;
        if(grading){std::array<std::array<double,3>,3> colors{};for(int i=0;i<3;++i){const double h=r.grading[i].hue/60;const int sector=int(h)%6;const double f=h-std::floor(h),x=sector%2?1-f:f;switch(sector){case 0:colors[i]={1,x,0};break;case 1:colors[i]={x,1,0};break;case 2:colors[i]={0,1,x};break;case 3:colors[i]={0,x,1};break;case 4:colors[i]={x,0,1};break;default:colors[i]={1,0,x};}const double y=.2627*colors[i][0]+.6780*colors[i][1]+.0593*colors[i][2];for(auto &c:colors[i])c=(c-y)*r.grading[i].saturation*.6;}
            const double spread=.1+.35*r.gradingBlend;
            for(int i=0;i<=4096;++i){const double y=i*4./4096,t=std::clamp(y/(y+.18)+.25*r.gradingBalance,0.,1.);const double shadows=1-smooth((t-(.25-spread))/(2*spread)),highlights=smooth((t-(.75-spread))/(2*spread));const double weights[]{shadows,std::max(0.,1-shadows-highlights),highlights};for(int c=0;c<3;++c){double tint=0;for(int b=0;b<3;++b)tint+=weights[b]*colors[b][c];gradeTable[i][c]=float(tint);}double gain=0;for(int b=0;b<3;++b)gain+=weights[b]*std::exp2(r.grading[b].luminance);gradeTable[i][3]=float(gain);}
        }
        if(monochrome){const double centers[]{0,30,60,120,180,240,270,300,360};for(int i=0;i<=3600;++i){const double h=i/10.;int band=0;while(band<7&&h>centers[band+1])++band;const double t=smooth((h-centers[band])/(centers[band+1]-centers[band]));mixTable[i]=float(std::exp2(r.monochromeMix[band]*(1-t)+r.monochromeMix[(band+1)%8]*t));}}
    }
    bool isMonochrome() const{return monochrome;}
    void toMonochrome(float *v) const {
        if(!monochrome)return;const float max=std::max({v[0],v[1],v[2]}),min=std::min({v[0],v[1],v[2]}),delta=max-min;float gain=1;if(!std::isfinite(delta)){v[0]=delta;return;}
        if(delta>1e-7f){float hue=max==v[0]?(v[1]-v[2])/delta:max==v[1]?2+(v[2]-v[0])/delta:4+(v[0]-v[1])/delta;hue*=60;if(hue<0)hue+=360;const float index=hue*10;const auto i=std::min(size_t(3600),size_t(index)),j=std::min(i+1,size_t(3600));const float selected=mixTable[i]+(mixTable[j]-mixTable[i])*(index-float(i));const float chroma=std::clamp(delta/std::max(std::abs(max),.18f),0.f,1.f);gain=1+(selected-1)*chroma;}
        const float y=(.2627f*v[0]+.6780f*v[1]+.0593f*v[2])*gain;v[0]=v[1]=v[2]=y;
    }
    void grade(float *v) const {
        if(!grading)return;const float y=.2627f*v[0]+.6780f*v[1]+.0593f*v[2];if(!std::isfinite(y)){v[0]=y;return;}const float index=std::clamp(y,0.f,4.f)*1024;const auto i=size_t(index),j=std::min(i+1,size_t(4096));const float f=index-float(i);float a[4];for(int c=0;c<4;++c)a[c]=gradeTable[i][c]+(gradeTable[j][c]-gradeTable[i][c])*f;for(int c=0;c<3;++c)v[c]=(v[c]+std::max(0.f,y)*a[c])*a[3];
    }
private:
    static double smooth(double x){x=std::clamp(x,0.,1.);return x*x*(3-2*x);}
    bool monochrome=false,grading=false;
    std::array<std::array<float,4>,4097> gradeTable{};
    std::array<float,3601> mixTable{};
};
