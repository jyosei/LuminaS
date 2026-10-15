#pragma once
#include "core/document.h"
#include <algorithm>
#include <array>
#include <cmath>
#include "color_operator.h"
#include "style_operator.h"
#include "primary_operator.h"
#include "luminance_curve_operator.h"
#include "point_color_operator.h"

// LuminaS's bounded, luminance-weighted tone model; not a vendor algorithm.
class ToneOperator {
public:
    explicit ToneOperator(const Recipe &r,int pointRange=-1):color(r),style(r),primary(r),luminanceCurve(r),pointColor(r),rangeSelector(rangeRecipe(r,pointRange)),rangeActive(pointRange>=0),saturation(float(r.saturation)),vibrance(float(r.vibrance)),
        monochromeStrength(r.monochrome?float(r.monochromeStrength):0.f),tonal(r.highlights!=0||r.shadows!=0||r.whites!=0||r.blacks!=0),chromatic(r.saturation!=1||r.vibrance!=0) {
        if(tonal)for(size_t i=0;i<gain.size();++i){
            const double y=double(i)*4/(gain.size()-1),t=y/(y+.18),dark=1-t;
            gain[i]=float(std::exp2(2*r.shadows*dark*dark+2*r.highlights*std::pow(t,4)+r.blacks*std::pow(dark,12)+r.whites*std::pow(t,12)));
        }
    }
    void apply(float *v) const {
        for(int c=0;c<3;++c)if(!std::isfinite(v[c]))return;
        primary.apply(v);
        float luma=.2627f*v[0]+.6780f*v[1]+.0593f*v[2];
        if(!std::isfinite(luma)){v[0]=luma;return;}
        if(tonal){
            const float index=std::clamp(luma,0.f,4.f)*float(gain.size()-1)/4;
            const auto i=size_t(index);const auto j=std::min(i+1,gain.size()-1);
            const float g=gain[i]+(gain[j]-gain[i])*(index-float(i));
            for(int c=0;c<3;++c)v[c]*=g;luma*=g;
        }
        luma=luminanceCurve.apply(v,luma);
        const bool partial=monochromeStrength>0&&monochromeStrength<1;
        float gray[4]{v[0],v[1],v[2],1};
        if(monochromeStrength==1)style.toMonochrome(v);
        else if(chromatic){float amount=saturation;
        if(vibrance!=0){const float maximum=std::max({v[0],v[1],v[2]}),minimum=std::min({v[0],v[1],v[2]});
            const float chroma=std::clamp((maximum-minimum)/std::max(std::abs(maximum),.18f),0.f,1.f);amount*=1+vibrance*(1-chroma);}
        for(int c=0;c<3;++c)v[c]=luma+(v[c]-luma)*amount;}
        color.apply(v);
        style.grade(v);
        // Mix complete color and monochrome branches in linear working RGB.
        // Curves/HSL/grading stay in each branch, preserving both endpoints.
        if(partial){style.toMonochrome(gray);color.apply(gray);style.grade(gray);for(int c=0;c<3;++c)v[c]=v[c]*(1-monochromeStrength)+gray[c]*monochromeStrength;}
        const float selection=rangeActive?rangeSelector.weight(v):1;
        pointColor.apply(v);
        if(rangeActive){const float y=.2627f*v[0]+.6780f*v[1]+.0593f*v[2];for(int c=0;c<3;++c)v[c]=y+(v[c]-y)*selection;}
    }
private:
    static Recipe rangeRecipe(const Recipe &r,int index){Recipe result;if(index>=0&&index<r.pointColors.size())result.pointColors={r.pointColors[index]};return result;}
    ColorOperator color;
    StyleOperator style;
    PrimaryOperator primary;
    LuminanceCurveOperator luminanceCurve;
    PointColorOperator pointColor;
    PointColorOperator rangeSelector;
    bool rangeActive=false;
    float saturation,vibrance,monochromeStrength;
    bool tonal,chromatic;
    std::array<float,4097> gain{};
};
