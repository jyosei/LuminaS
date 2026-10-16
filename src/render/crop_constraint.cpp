#include "crop_constraint.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace {
struct Range {double lo,hi;};
Range padded(double lo,double hi){return {std::nextafter(lo,-std::numeric_limits<double>::infinity()),std::nextafter(hi,std::numeric_limits<double>::infinity())};}
Range add(Range a,Range b){return padded(a.lo+b.lo,a.hi+b.hi);}
Range multiply(Range a,Range b){const std::array<double,4> p{a.lo*b.lo,a.lo*b.hi,a.hi*b.lo,a.hi*b.hi};return padded(*std::min_element(p.begin(),p.end()),*std::max_element(p.begin(),p.end()));}
Range scale(Range a,double b){return multiply(a,{b,b});}
Range dividePositive(Range a,Range b){return multiply(a,padded(1/b.hi,1/b.lo));}
Range squared(Range a){return padded(a.lo<=0&&a.hi>=0?0:std::min(a.lo*a.lo,a.hi*a.hi),std::max(a.lo*a.lo,a.hi*a.hi));}

bool containsFootprint(QSize size,const Recipe &r,const QRectF &crop){
    const double w=size.width(),h=size.height();
    const double a=r.angle*3.14159265358979323846/180,co=std::abs(std::cos(a)),si=std::abs(std::sin(a));
    const double halfX=(crop.width()*w*co+crop.height()*h*si)/2;
    const double halfY=(crop.width()*w*si+crop.height()*h*co)/2;
    const double cx=crop.center().x()*w,cy=crop.center().y()*h;
    Range x=padded(cx-halfX,cx+halfX),y=padded(cy-halfY,cy+halfY);
    if(!r.perspective.isNull()){
        const auto nx=add(scale(x,2/w),{-1,-1}),ny=add(scale(y,2/h),{-1,-1});
        const auto d=add({1,1},add(scale(nx,r.perspective.x()),scale(ny,r.perspective.y())));
        if(d.lo<=1e-8||!std::isfinite(d.hi))return false;
        x=scale(add(dividePositive(nx,d),{1,1}),w/2);y=scale(add(dividePositive(ny,d),{1,1}),h/2);
    }
    if(r.distortion!=0){
        const auto dx=add(x,{-w/2,-w/2}),dy=add(y,{-h/2,-h/2});
        auto t=scale(add(squared(dx),squared(dy)),4/(w*w+h*h));t.lo=std::max(0.,t.lo);
        // t/(1+t) is monotone, avoiding interval dependency inflation here.
        const auto fraction=padded(t.lo/(1+t.lo),t.hi/(1+t.hi));
        const auto gain=add({1,1},scale(fraction,r.distortion));
        x=add({w/2,w/2},multiply(dx,gain));y=add({h/2,h/2},multiply(dy,gain));
    }
    const double radius=.5*std::hypot(w,h);
    for(double shift:{r.chromaticAberration.x(),0.,r.chromaticAberration.y()}){
        const double gain=1+std::clamp(shift/radius,-.25,.25);
        const auto px=add({w/2,w/2},scale(add(x,{-w/2,-w/2}),gain));
        const auto py=add({h/2,h/2},scale(add(y,{-h/2,-h/2}),gain));
        constexpr double guard=1e-6;
        if(!std::isfinite(px.lo)||!std::isfinite(px.hi)||!std::isfinite(py.lo)||!std::isfinite(py.hi)||
           px.lo<.5+guard||py.lo<.5+guard||px.hi>w-.5-guard||py.hi>h-.5-guard)return false;
    }
    return true;
}
}
bool constrainedCrop(QSize size,const Recipe &r,QRectF &output,QString &error){
    error.clear();
    if(size.width()<=0||size.height()<=0||!validRecipe(r)){error=QStringLiteral("无法约束裁切：源尺寸或参数无效");return false;}
    if((r.angle==0&&r.perspective.isNull()&&r.distortion==0&&r.chromaticAberration.isNull())||containsFootprint(size,r,r.crop)){
        output=r.crop;return true;
    }
    const auto cropAt=[&](double scale){const auto centre=r.crop.center();const double w=r.crop.width()*scale,h=r.crop.height()*scale;return QRectF(centre.x()-w/2,centre.y()-h/2,w,h);};
    if(!containsFootprint(size,r,cropAt(0))){error=QStringLiteral("当前裁切中心已在有效源范围之外，请先手动移动裁切范围");return false;}
    double low=0,high=1;
    for(int i=0;i<48;++i){const double mid=(low+high)/2;if(containsFootprint(size,r,cropAt(mid)))low=mid;else high=mid;}
    const auto fitted=cropAt(low*(1-1e-10));
    if(fitted.width()*size.width()<1||fitted.height()*size.height()<1||!containsFootprint(size,r,fitted)){
        error=QStringLiteral("有效区域不足一个输出像素，请减小校正幅度或重新裁切");return false;
    }
    output=fitted;return true;
}
