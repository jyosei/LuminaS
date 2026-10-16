#include "local_mask_feather.h"
#include <QMap>
#include <array>
#include <algorithm>
#include <cmath>
#include <limits>

namespace LocalEdit::Detail {
namespace {
constexpr double pi=3.14159265358979323846;
bool validPoint(QPointF p){return std::isfinite(p.x())&&std::isfinite(p.y())&&std::abs(p.x())<=1e9&&std::abs(p.y())<=1e9;}
struct Axis {
    qint64 lo=0,hi=0;
    double center=0,sum=0;
};
Axis axis(double point,double radius){
    Axis a;a.center=point-.5;
    a.lo=qint64(std::ceil(a.center-radius));a.hi=qint64(std::floor(a.center+radius));
    const double theta=pi/radius,n=double(a.hi-a.lo+1);
    a.sum=n+std::sin(n*theta/2)/std::sin(theta/2)*std::cos(theta*((double(a.lo)+double(a.hi))/2-a.center));
    return a;
}
double weight(qint64 sample,const Axis &a,double radius){
    const double d=double(sample)-a.center;
    return std::abs(d)>=radius?0:1+std::cos(pi*d/radius);
}
struct Stencil {Axis x,y;double cx=0,sx=0,cy=0,sy=0;};
struct Event {int x,y,index,sign;};
using Moments=std::array<double,9>;
}
float featherSample(QPointF point,double feather,const CoverageSampler &raw,const CancellationToken &cancel){
    const auto nan=std::numeric_limits<float>::quiet_NaN();
    if(!validPoint(point)||!std::isfinite(feather)||feather<0||feather>16384||isCanceled(cancel))return nan;
    if(feather==0)return raw(point);
    const double radius=std::max(2.,feather);const auto x=axis(point.x(),radius),y=axis(point.y(),radius);
    double value=0;
    for(qint64 row=y.lo;row<=y.hi;++row){
        if(isCanceled(cancel))return nan;
        const double wy=weight(row,y,radius);if(wy==0)continue;
        double sum=0;
        for(qint64 col=x.lo;col<=x.hi;++col){
            if(isCanceled(cancel))return nan;
            const double wx=weight(col,x,radius);if(wx==0)continue;
            const float m=raw({double(col)+.5,double(row)+.5});if(!std::isfinite(m)||m<0||m>1)return nan;
            sum+=wx*m;
        }
        value+=sum*wy;
    }
    value/=x.sum*y.sum;
    if(feather<2){const auto original=raw(point);if(!std::isfinite(original))return nan;value=original+(value-original)*(feather/2);}
    return float(std::clamp(value,0.,1.));
}
bool featherPoints(const QVector<QPointF> &points,const QVector<quint8> &active,double feather,
    const CoverageSampler &raw,QVector<float> &output,QString &error,const CancellationToken &cancel,qsizetype *peakScratchBytes){
    error.clear();if(peakScratchBytes)*peakScratchBytes=0;
    if(isCanceled(cancel)){error="Canceled";return false;}
    if(points.size()!=active.size()||points.size()>512*512||!std::isfinite(feather)||feather<=0||feather>16384){error="Invalid feather point batch";return false;}
    QVector<float> result(points.size(),0);
    QMap<QPair<qint64,qint64>,QVector<int>> groups;
    for(qsizetype i=0;i<points.size();++i){
        if(!active[i])continue;
        if(!validPoint(points[i])){error="Invalid feather source point";return false;}
        const auto p=points[i];groups[{qint64(std::floor(p.x()/256)),qint64(std::floor(p.y()/256))}].append(int(i));
    }
    qsizetype groupBytes=groups.size()*qsizetype(sizeof(QPair<qint64,qint64>)+sizeof(QVector<int>));
    for(const auto &indices:groups)groupBytes+=indices.capacity()*qsizetype(sizeof(int));
    const double radius=std::max(2.,feather),theta=pi/radius;
    for(const auto &indices:groups){
        if(isCanceled(cancel)){error="Canceled";return false;}
        QVector<Stencil> stencils;stencils.reserve(indices.size());
        qint64 left=std::numeric_limits<qint64>::max(),top=left,right=-left,bottom=-left;
        for(int index:indices){
            const auto p=points[index];Stencil s;s.x=axis(p.x(),radius);s.y=axis(p.y(),radius);
            left=std::min(left,s.x.lo);right=std::max(right,s.x.hi);top=std::min(top,s.y.lo);bottom=std::max(bottom,s.y.hi);stencils.append(s);
        }
        const int width=int(right-left+1),height=int(bottom-top+1);
        // Every point is in the same original 256-pixel square. This invariant
        // bounds row storage even for extreme source aspect ratios/off-crop UVs.
        if(width<=0||height<=0||width>33026||height>33026){error="Feather group bounds exceeded";return false;}
        QVector<Event> events;events.reserve(indices.size()*4);
        for(int i=0;i<stencils.size();++i){
            auto &s=stencils[i];s.cx=std::cos(theta*(s.x.center-left));s.sx=std::sin(theta*(s.x.center-left));
            s.cy=std::cos(theta*(s.y.center-top));s.sy=std::sin(theta*(s.y.center-top));
            const auto event=[&](qint64 x,qint64 y,int sign){if(x>=left&&y>=top)events.append({int(x-left),int(y-top),i,sign});};
            event(s.x.hi,s.y.hi,1);event(s.x.lo-1,s.y.hi,-1);event(s.x.hi,s.y.lo-1,-1);event(s.x.lo-1,s.y.lo-1,1);
        }
        std::sort(events.begin(),events.end(),[](const Event &a,const Event &b){return a.y<b.y;});
        QVector<Moments> prefix(width,Moments{});QVector<double> sums(indices.size(),0);
        QVector<std::array<double,3>> xBasis(width);
        for(int x=0;x<width;++x)xBasis[x]={1,std::cos(theta*x),std::sin(theta*x)};
        const qsizetype bytes=groupBytes+result.capacity()*qsizetype(sizeof(float))+stencils.capacity()*qsizetype(sizeof(Stencil))+
            events.capacity()*qsizetype(sizeof(Event))+prefix.capacity()*qsizetype(sizeof(Moments))+
            sums.capacity()*qsizetype(sizeof(double))+xBasis.capacity()*qsizetype(sizeof(std::array<double,3>));
        if(peakScratchBytes)*peakScratchBytes=std::max(*peakScratchBytes,bytes);
        qsizetype next=0;
        for(int y=0;y<height;++y){
            if(isCanceled(cancel)){error="Canceled";return false;}
            const std::array<double,3> by{1,std::cos(theta*y),std::sin(theta*y)};Moments row{};
            for(int x=0;x<width;++x){
                if(isCanceled(cancel)){error="Canceled";return false;}
                const float coverage=raw({double(left+x)+.5,double(top+y)+.5});
                if(!std::isfinite(coverage)||coverage<0||coverage>1){error="Invalid raw mask coverage";return false;}
                for(int iy=0;iy<3;++iy)for(int ix=0;ix<3;++ix){const int k=iy*3+ix;row[k]+=coverage*xBasis[x][ix]*by[iy];prefix[x][k]+=row[k];}
            }
            while(next<events.size()&&events[next].y==y){
                const auto &e=events[next++];const auto &s=stencils[e.index];
                const std::array<double,3> bx{1,s.cx,s.sx},byPoint{1,s.cy,s.sy};double value=0;
                for(int iy=0;iy<3;++iy)for(int ix=0;ix<3;++ix)value+=prefix[e.x][iy*3+ix]*bx[ix]*byPoint[iy];
                sums[e.index]+=e.sign*value;
            }
        }
        for(int i=0;i<indices.size();++i){
            const auto &s=stencils[i];double coverage=sums[i]/(s.x.sum*s.y.sum);
            if(feather<2){const auto original=raw(points[indices[i]]);if(!std::isfinite(original)){error="Invalid raw coverage";return false;}coverage=original+(coverage-original)*(feather/2);}
            if(!std::isfinite(coverage)){error="Nonfinite feather coverage";return false;}
            result[indices[i]]=float(std::clamp(coverage,0.,1.));
        }
    }
    if(isCanceled(cancel)){error="Canceled";return false;}
    output=std::move(result);return true;
}
}
