#pragma once
#include "render/cpu_renderer.h"
#include "render/point_color_operator.h"
#include "render/geometry_transform.h"

// Input has crop/angle baked by EditLoader. Sampling stays in float working RGB.
inline bool samplePointColor(const SourceFrame &prepared,const Recipe &recipe,QPointF uv,PointColorAdjustment &picked,QString &error){
    error.clear();
    if(!std::isfinite(uv.x())||!std::isfinite(uv.y())||uv.x()<0||uv.y()<0||uv.x()>=1||uv.y()>=1){error=QStringLiteral("取样位置不在有效预览内");return false;}
    auto before=recipe;clearPreparedGeometry(before);before.pointColors.clear();before.grainAmount=0;before.vignetteAmount=0;
    const auto image=renderLinear(prepared,before,0,error);if(image.isNull())return false;
    const int x=int(uv.x()*image.width()),y=int(uv.y()*image.height());double rgb[3]{},weight=0;
    for(int j=std::max(0,y-1);j<=std::min(y+1,image.height()-1);++j)for(int i=std::max(0,x-1);i<=std::min(x+1,image.width()-1);++i){const auto *v=reinterpret_cast<const float *>(image.constScanLine(j))+i*4;const double a=v[3];if(a<=0)continue;weight+=a;for(int c=0;c<3;++c)rgb[c]+=v[c]*a;}
    if(weight<=1e-8){error=QStringLiteral("取样区域透明");return false;}float v[3];for(int c=0;c<3;++c)v[c]=float(rgb[c]/weight);
    PointColorAdjustment candidate;if(!PointColorOperator::descriptor(v,candidate)){error=QStringLiteral("请选择有色彩且亮度有效的区域");return false;}picked=candidate;return true;
}

