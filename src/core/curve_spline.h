#pragma once
#include "document.h"
#include <array>
#include <cmath>
// Shape-preserving C1 cubic Hermite (PCHIP). UI and renderer share tangents.
class CurveSpline {
public:
    explicit CurveSpline(const Curve &points,const QVector<double> &tangents={}):points(points),m(points.size(),0){
        const int n=int(points.size());QVector<double> h(n-1),d(n-1);
        for(int i=0;i<n-1;++i){h[i]=points[i+1].x()-points[i].x();d[i]=(points[i+1].y()-points[i].y())/h[i];}
        leftSlope=d.first();rightSlope=d.last();
        if(tangents.size()==n+2){for(int i=0;i<n;++i)m[i]=tangents[i];leftSlope=tangents[n];rightSlope=tangents[n+1];return;}
        if(n==2){m[0]=m[1]=d[0];return;}
        for(int i=1;i<n-1;++i){if(d[i-1]*d[i]<=0)m[i]=0;else{const double w1=2*h[i]+h[i-1],w2=h[i]+2*h[i-1];m[i]=(w1+w2)/(w1/d[i-1]+w2/d[i]);}}
        auto endpoint=[](double a,double b,double da,double db){double value=((2*a+b)*da-a*db)/(a+b);if(value*da<=0)return 0.;if(da*db<0&&std::abs(value)>3*std::abs(da))return 3*da;return value;};
        m[0]=endpoint(h[0],h[1],d[0],d[1]);m[n-1]=endpoint(h[n-2],h[n-3],d[n-2],d[n-3]);
    }
    double tangent(int i) const{return m[i];}
    double outsideTangent(bool right) const{return right?rightSlope:leftSlope;}
    double derivative(double x) const{
        if(x<0)return leftSlope;if(x>1)return rightSlope;if(x==0)return m.first();if(x==1)return m.last();int i=0;while(i<int(points.size())-2&&x>points[i+1].x())++i;const double h=points[i+1].x()-points[i].x(),t=(x-points[i].x())/h,t2=t*t;return (6*t2-6*t)*points[i].y()/h+(3*t2-4*t+1)*m[i]+(-6*t2+6*t)*points[i+1].y()/h+(3*t2-2*t)*m[i+1];
    }
    double value(double x) const{
        if(x<=points.first().x())return points.first().y()+(x-points.first().x())*leftSlope;
        if(x>=points.last().x())return points.last().y()+(x-points.last().x())*rightSlope;
        int i=0;while(i<int(points.size())-2&&x>points[i+1].x())++i;
        const double h=points[i+1].x()-points[i].x(),t=(x-points[i].x())/h,t2=t*t,t3=t2*t;
        return (2*t3-3*t2+1)*points[i].y()+(t3-2*t2+t)*h*m[i]+(-2*t3+3*t2)*points[i+1].y()+(t3-t2)*h*m[i+1];
    }
private:
    Curve points;
    QVector<double> m;
    double leftSlope=1,rightSlope=1;
};
