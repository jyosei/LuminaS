#include "source_coordinate_map.h"
#include "geometry_transform.h"
#include <algorithm>
#include <cmath>

bool sourceToOutput(QPointF point,QSize size,const Recipe &r,QPointF &uv){
    if(size.isEmpty()||qint64(size.width())*size.height()>45000000||!validRecipe(r)||
       !std::isfinite(point.x())||!std::isfinite(point.y()))return false;
    const auto originalPoint=point;
    const QPointF centre(size.width()/2.,size.height()/2.);
    if(r.distortion!=0){
        const QPointF delta=point-centre;const double length=std::hypot(delta.x(),delta.y());
        if(!std::isfinite(length))return false;
        if(length>0){
            // The established radial warp is globally monotone (derivative>=.55).
            // Its gain is between 1-|k| and 1+|k|, giving a finite root bracket.
            double low=0,high=length/(1-std::abs(r.distortion));
            const double inverseRadiusSquared=4/(double(size.width())*size.width()+double(size.height())*size.height());
            for(int i=0;i<80;++i){
                const double mid=low+(high-low)/2;if(mid==low||mid==high)break;
                const double t=mid*mid*inverseRadiusSquared;
                if(!std::isfinite(t))return false;
                const double mapped=mid*(1+r.distortion*t/(1+t));
                if(mapped<length)low=mid;else high=mid;
            }
            point=centre+delta*((low+(high-low)/2)/length);
        }
    }
    if(hasPerspective(r)){
        const double x=2*point.x()/size.width()-1,y=2*point.y()/size.height()-1;
        const double denominator=1-r.perspective.x()*x-r.perspective.y()*y;
        if(!std::isfinite(denominator)||denominator<=1e-8)return false;
        point={(x/denominator+1)*size.width()/2,(y/denominator+1)*size.height()/2};
    }
    const double cx=(r.crop.x()+r.crop.width()/2)*size.width(),cy=(r.crop.y()+r.crop.height()/2)*size.height();
    const double co=std::cos(r.angle*3.14159265358979323846/180),si=std::sin(r.angle*3.14159265358979323846/180);
    const double x=point.x()-cx,y=point.y()-cy;
    QPointF result(.5+(co*x-si*y)/(r.crop.width()*size.width()),.5+(si*x+co*y)/(r.crop.height()*size.height()));
    if(r.flipHorizontal)result.setX(1-result.x());if(r.flipVertical)result.setY(1-result.y());
    if(!std::isfinite(result.x())||!std::isfinite(result.y()))return false;
    // Reject roots beyond the inverse homography's valid branch as well as
    // numerical failures. This tolerance is for UI handles, never pixel colour.
    const auto back=OutputSourceMapping(size,r).map(result);
    const double tolerance=1e-7+1e-10*std::max({double(size.width()),double(size.height()),std::abs(originalPoint.x()),std::abs(originalPoint.y())});
    if(!std::isfinite(back.x())||!std::isfinite(back.y()))return false;
    if(std::hypot(back.x()-originalPoint.x(),back.y()-originalPoint.y())>tolerance)return false;
    uv=result;return true;
}
