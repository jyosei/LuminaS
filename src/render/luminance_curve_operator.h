#pragma once
#include "core/curve_spline.h"
#include "core/parametric_curve.h"
#include <optional>
#include <vector>
// A luminance mapping followed by one common RGB gain; no per-channel curve.
class LuminanceCurveOperator {
public:
    explicit LuminanceCurveOperator(const Recipe &r){
        bool param=false,point=false;for(double v:r.parametricCurve.regions)param|=v!=0;for(auto p:r.luminanceCurve)point|=p.x()!=p.y();
        if(param)prepare(0,parametricPoints(r.parametricCurve));if(point||!r.luminanceTangents.isEmpty())prepare(1,r.luminanceCurve,r.luminanceTangents);
    }
    float apply(float *v,float y) const{
        if(!active||y<0)return y;
        float mapped=y;for(int c=0;c<2;++c)if(splines[c])mapped=map(c,mapped);
        constexpr float toe=1e-6f;
        if(y>=toe){const float g=mapped/y;for(int c=0;c<3;++c)v[c]*=g;}
        else {const float t=y/toe,weight=t*t*(3-2*t);const float g=1-weight+mapped*(y/(toe*toe))*(3-2*t);for(int c=0;c<3;++c)v[c]=mapped+g*(v[c]-y);}
        return mapped;
    }
private:
    static float evaluate(const CurveSpline &s,const Curve &p,float x){
        const double e=x<=.0031308f?12.92*x:1.055*std::pow(double(x),1/2.4)-.055;
        double result;
        if(e<=1)result=s.value(e);
        else result=p.last().y()+(e-1)*std::max(0.,s.outsideTangent(true));
        return float(result<=.04045?result/12.92:std::pow((result+.055)/1.055,2.4));
    }
    void prepare(int c,const Curve &p,const QVector<double> &tangents={}){active=true;points[c]=p;splines[c].emplace(p,tangents);tables[c].resize(32769);for(size_t i=0;i<tables[c].size();++i)tables[c][i]=evaluate(*splines[c],p,float(i)*8/32768);}
    float map(int c,float x) const{if(!std::isfinite(x)||x<0)return x;if(x>8)return evaluate(*splines[c],points[c],x);const float index=x*32768/8;const auto i=size_t(index),j=std::min(i+1,tables[c].size()-1);return tables[c][i]+(tables[c][j]-tables[c][i])*(index-float(i));}
    std::array<Curve,2> points;
    std::array<std::optional<CurveSpline>,2> splines;
    std::array<std::vector<float>,2> tables;
    bool active=false;
};
