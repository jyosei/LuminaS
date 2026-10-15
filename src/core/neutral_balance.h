#pragma once
#include "document.h"
#include <cmath>
#include <algorithm>
// Sample the geometry-prepared, pre-adjustment float frame. Flips remain in Recipe.
inline bool neutralBalance(const SourceFrame &frame,QPointF uv,Recipe &recipe,QString &error){
    error.clear();auto fail=[&](const char *s){error=QString::fromUtf8(s);return false;};
    if(frame.linear.isNull()||frame.linear.format()!=QImage::Format_RGBA32FPx4||!std::isfinite(uv.x())||!std::isfinite(uv.y())||uv.x()<0||uv.y()<0||uv.x()>=1||uv.y()>=1)return fail("取样位置不在有效预览内");
    int x=int(uv.x()*frame.linear.width()),y=int(uv.y()*frame.linear.height());
    if(recipe.flipHorizontal)x=frame.linear.width()-1-x;if(recipe.flipVertical)y=frame.linear.height()-1-y;
    double p[3]{},weight=0;
    for(int j=std::max(0,y-1);j<=std::min(y+1,frame.linear.height()-1);++j)for(int i=std::max(0,x-1);i<=std::min(x+1,frame.linear.width()-1);++i){const float *v=reinterpret_cast<const float *>(frame.linear.constScanLine(j))+4*i;if(!std::isfinite(v[3])||v[3]<=0)continue;for(int c=0;c<3;++c){if(!std::isfinite(v[c]))return fail("取样含无效像素");p[c]+=v[c]*v[3];}weight+=v[3];}
    if(weight<1e-6)return fail("取样区域透明");for(auto &v:p)v/=weight;
    double w=0,t=0;
    if(frame.raw){const auto &a=frame.cameraToWork;const double d=a[0]*(double(a[4])*a[8]-double(a[5])*a[7])-a[1]*(double(a[3])*a[8]-double(a[5])*a[6])+a[2]*(double(a[3])*a[7]-double(a[4])*a[6]);if(!std::isfinite(d)||std::abs(d)<1e-10)return fail("RAW颜色矩阵不可逆");
        const double b[]{(double(a[4])*a[8]-double(a[5])*a[7])/d,(double(a[2])*a[7]-double(a[1])*a[8])/d,(double(a[1])*a[5]-double(a[2])*a[4])/d,(double(a[5])*a[6]-double(a[3])*a[8])/d,(double(a[0])*a[8]-double(a[2])*a[6])/d,(double(a[2])*a[3]-double(a[0])*a[5])/d,(double(a[3])*a[7]-double(a[4])*a[6])/d,(double(a[1])*a[6]-double(a[0])*a[7])/d,(double(a[0])*a[4]-double(a[1])*a[3])/d};double g[3];
        for(int c=0;c<3;++c){const double camera=b[c*3]*p[0]+b[c*3+1]*p[1]+b[c*3+2]*p[2],neutral=b[c*3]+b[c*3+1]+b[c*3+2];if(camera<=1e-6||neutral<=0)return fail("请选择明亮、未过曝的中性区域");g[c]=neutral/camera;}
        w=frame.rawWarmth+.5*std::log2(g[0]/g[2]);t=frame.rawTint+std::log2(g[1]/std::sqrt(g[0]*g[2]));
    }else{for(auto v:p)if(v<=1e-6)return fail("请选择明亮、未过曝的中性区域");w=-.5*std::log2(p[0]/p[2]);t=std::log2(std::sqrt(p[0]*p[2])/p[1]);}
    if(!std::isfinite(w)||!std::isfinite(t)||std::abs(w)>1||std::abs(t)>1)return fail("该取样超出当前白平衡调整范围，请换一个中性区域");recipe.warmth=w;recipe.tint=t;return true;
}
