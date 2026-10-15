#pragma once
#include "core/document.h"
#include <cmath>
#include <algorithm>
#include <vector>
#include "core/curve_spline.h"
#include <optional>
class ColorOperator {
public:
    explicit ColorOperator(const Recipe &r):curves(r.curves),enabled(hasColorEditing(r)){
        for(int c=0;c<4;++c){for(auto p:curves[c])curveActive[c]|=p.x()!=p.y();curveActive[c]|=!r.curveTangents[c].isEmpty();anyCurves|=curveActive[c];if(curveActive[c]){splines[c].emplace(curves[c],r.curveTangents[c]);tables[c].resize(32769);for(size_t i=0;i<tables[c].size();++i)tables[c][i]=evaluate(*splines[c],float(i)*9/32768-1);}}
        for(const auto &a:r.hsl)hslActive|=a.hue!=0||a.saturation!=0||a.luminance!=0;
        if(hslActive){const double centers[]{0,30,60,120,180,240,270,300,360};
            for(int i=0;i<3601;++i){const double hue=i/10.;int band=0;while(band<7&&hue>centers[band+1])++band;
                double t=(hue-centers[band])/(centers[band+1]-centers[band]);t=t*t*(3-2*t);const auto &a=r.hsl[band],&b=r.hsl[(band+1)%8];
                hslTable[i]={float(30*(a.hue*(1-t)+b.hue*t)),float(1+a.saturation*(1-t)+b.saturation*t),float(std::exp2(a.luminance*(1-t)+b.luminance*t))};
            }
        }
    }
    void apply(float *v) const {
        if(!enabled)return;
        if(anyCurves)for(int c=0;c<3;++c){if(curveActive[0])v[c]=map(0,v[c]);if(curveActive[c+1])v[c]=map(c+1,v[c]);}
        if(!hslActive)return;
        const float maximum=std::max({v[0],v[1],v[2]}),minimum=std::min({v[0],v[1],v[2]}),delta=maximum-minimum;
        if(!std::isfinite(delta)){v[0]=delta;return;}if(delta<1e-7f)return;
        float h=maximum==v[0]?(v[1]-v[2])/delta:maximum==v[1]?2+(v[2]-v[0])/delta:4+(v[0]-v[1])/delta;
        h*=60;if(h<0)h+=360;const float index=h*10;const auto i=std::min(size_t(3600),size_t(index));const auto j=std::min(size_t(3600),i+1);const float f=index-float(i);
        float a[3];for(int c=0;c<3;++c)a[c]=hslTable[i][c]+(hslTable[j][c]-hslTable[i][c])*f;
        const float selection=std::clamp(delta/std::max(std::abs(maximum),.18f),0.f,1.f);
        a[0]*=selection;a[1]=1+(a[1]-1)*selection;a[2]=1+(a[2]-1)*selection;
        h+=a[0];if(h<0)h+=360;else if(h>=360)h-=360;h/=60;const int sector=int(h);const float fraction=h-sector;
        const float chroma=delta*a[1],low=(maximum+minimum-chroma)/2,x=chroma*(sector%2?1-fraction:fraction);float rgb[3]{};
        switch(sector){case 0:rgb[0]=chroma;rgb[1]=x;break;case 1:rgb[0]=x;rgb[1]=chroma;break;case 2:rgb[1]=chroma;rgb[2]=x;break;case 3:rgb[1]=x;rgb[2]=chroma;break;case 4:rgb[0]=x;rgb[2]=chroma;break;default:rgb[0]=chroma;rgb[2]=x;}
        for(int c=0;c<3;++c)v[c]=(rgb[c]+low)*a[2];
    }
private:
    static float encode(float x){return std::copysign(std::abs(x)<=.0031308f?12.92f*std::abs(x):1.055f*std::pow(std::abs(x),1/2.4f)-.055f,x);}
    static float decode(float x){return std::copysign(std::abs(x)<=.04045f?std::abs(x)/12.92f:std::pow((std::abs(x)+.055f)/1.055f,2.4f),x);}
    static float evaluate(const CurveSpline &spline,float x){return decode(float(spline.value(encode(x))));}
    float map(int c,float x) const {if(tables[c].empty())return x;if(!std::isfinite(x))return x;if(x<-1||x>8)return evaluate(*splines[c],x);const float index=(x+1)*32768/9;const auto i=size_t(index),j=std::min(i+1,tables[c].size()-1);return tables[c][i]+(tables[c][j]-tables[c][i])*(index-float(i));}
    std::array<Curve,4> curves;
    std::array<std::vector<float>,4> tables;
    std::array<std::optional<CurveSpline>,4> splines;
    std::array<std::array<float,3>,3601> hslTable{};
    bool hslActive=false;
    bool enabled=false;
    std::array<bool,4> curveActive{};
    bool anyCurves=false;
};
