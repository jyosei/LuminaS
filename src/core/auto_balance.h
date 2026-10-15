#pragma once
#include "neutral_balance.h"
#include <array>
#include <vector>

// Bounded, trimmed gray-world estimate on the pre-global float working frame.
// RAW selection is approximately canonicalized to the as-shot WB; this cannot
// undo sensor/16-bit clipping or WB-dependent demosaic decisions.
inline bool automaticBalance(const SourceFrame &frame,Recipe &recipe,QString &error){
    error.clear();auto fail=[&](const char *s){error=QString::fromUtf8(s);return false;};
    if(!validRecipe(recipe)||frame.linear.isNull()||frame.linear.format()!=QImage::Format_RGBA32FPx4)return fail("当前浮点预览或参数无效");
    std::array<double,9> transform{1,0,0,0,1,0,0,0,1};auto canonical=frame;
    if(frame.raw){
        const auto &a=frame.cameraToWork;
        const double d=a[0]*(double(a[4])*a[8]-double(a[5])*a[7])-a[1]*(double(a[3])*a[8]-double(a[5])*a[6])+a[2]*(double(a[3])*a[7]-double(a[4])*a[6]);
        if(!std::isfinite(d)||std::abs(d)<1e-10)return fail("RAW颜色矩阵不可逆");
        if(!std::isfinite(frame.rawWarmth)||!std::isfinite(frame.rawTint)||std::abs(frame.rawWarmth)>1||std::abs(frame.rawTint)>1)return fail("RAW白平衡元数据无效");
        const double inverse[]{(double(a[4])*a[8]-double(a[5])*a[7])/d,(double(a[2])*a[7]-double(a[1])*a[8])/d,(double(a[1])*a[5]-double(a[2])*a[4])/d,(double(a[5])*a[6]-double(a[3])*a[8])/d,(double(a[0])*a[8]-double(a[2])*a[6])/d,(double(a[2])*a[3]-double(a[0])*a[5])/d,(double(a[3])*a[7]-double(a[4])*a[6])/d,(double(a[1])*a[6]-double(a[0])*a[7])/d,(double(a[0])*a[4]-double(a[1])*a[3])/d};
        const double delta[]{std::exp2(frame.rawWarmth),std::exp2(frame.rawTint),std::exp2(-frame.rawWarmth),std::exp2(frame.rawTint)};
        for(int c=0;c<4;++c){if(!std::isfinite(frame.cameraWB[c])||frame.cameraWB[c]<=0)return fail("RAW相机白平衡数据无效");canonical.cameraWB[c]=frame.cameraWB[c]/delta[c];if(!std::isfinite(canonical.cameraWB[c])||canonical.cameraWB[c]<=0)return fail("RAW原照白平衡数据无效");}
        const double common=*std::min_element(frame.cameraWB.begin(),frame.cameraWB.end())/ *std::min_element(canonical.cameraWB.begin(),canonical.cameraWB.end());
        if(frame.rawWarmth!=0||frame.rawTint!=0){transform.fill(0);for(int c=0;c<3;++c)for(int j=0;j<3;++j)for(int k=0;k<3;++k)transform[c*3+j]+=a[c*3+k]*(common/delta[k])*inverse[k*3+j];}
        canonical.rawWarmth=canonical.rawTint=0;
    }
    struct Sample {std::array<double,3> rgb;double alpha,redLog,blueLog;};
    const int w=frame.linear.width(),h=frame.linear.height(),gx=std::min(w,128),gy=std::min(h,128);
    std::vector<int> xs(size_t(gx),0),ys(size_t(gy),0);
    auto grid=[](std::vector<int>&points,int size){const int count=int(points.size());for(int i=0;i<count/2;++i){points[size_t(i)]=int((i+.5)*size/count);points[size_t(count-1-i)]=size-1-points[size_t(i)];}if(count%2)points[size_t(count/2)]=size/2;};grid(xs,w);grid(ys,h);
    std::vector<Sample> samples;std::vector<double> red,blue;samples.reserve(size_t(gx)*gy);red.reserve(size_t(gx)*gy);blue.reserve(size_t(gx)*gy);
    for(int y:ys)for(int x:xs){const auto *p=reinterpret_cast<const float *>(frame.linear.constScanLine(y))+x*4;
        if(!std::isfinite(p[3]))return fail("统计样本含无效透明度");if(p[3]<.1f)continue;
        std::array<double,3> rgb{};for(int c=0;c<3;++c){if(!std::isfinite(p[c]))return fail("统计样本含无效像素");}
        for(int c=0;c<3;++c)for(int j=0;j<3;++j)rgb[c]+=transform[c*3+j]*p[j];
        const double yValue=.2627*rgb[0]+.6780*rgb[1]+.0593*rgb[2];
        if(yValue<.01||yValue>.8||*std::min_element(rgb.begin(),rgb.end())<=1e-6||*std::max_element(rgb.begin(),rgb.end())>1)continue;
        const double r=std::log2(rgb[0]/rgb[1]),b=std::log2(rgb[2]/rgb[1]);if(!std::isfinite(r)||!std::isfinite(b))return fail("统计颜色数据无效");
        samples.push_back({rgb,std::clamp(double(p[3]),0.,1.),r,b});red.push_back(r);blue.push_back(b);
    }
    if(samples.size()<32)return fail("可用中间调样本太少，请使用吸管或手动白平衡");
    std::sort(red.begin(),red.end());std::sort(blue.begin(),blue.end());const auto lo=(samples.size()-1)/5,hi=(samples.size()-1)*4/5;
    std::array<double,3> mean{};double weight=0;size_t kept=0;
    for(const auto &s:samples)if(s.redLog>=red[lo]&&s.redLog<=red[hi]&&s.blueLog>=blue[lo]&&s.blueLog<=blue[hi]){for(int c=0;c<3;++c)mean[c]+=s.rgb[c]*s.alpha;weight+=s.alpha;++kept;}
    if(kept<32||weight<1e-6)return fail("可靠统计样本不足，请使用吸管或手动白平衡");
    canonical.linear=QImage(1,1,QImage::Format_RGBA32FPx4);if(canonical.linear.isNull())return fail("统计缓冲分配失败");auto *p=reinterpret_cast<float *>(canonical.linear.bits());for(int c=0;c<3;++c)p[c]=float(mean[c]/weight);p[3]=1;
    auto estimated=recipe;if(!neutralBalance(canonical,{.5,.5},estimated,error)){error=QStringLiteral("自动估计失败：")+error;return false;}recipe=estimated;return true;
}
