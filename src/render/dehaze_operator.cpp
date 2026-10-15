#include "dehaze_operator.h"
#include "spatial_operator.h"
#include "io/preview_size.h"
#include <algorithm>
#include <cmath>
#include <deque>
namespace {
std::vector<float> minimum(const std::vector<float> &input,int w,int h,int radius){
    std::vector<float> temporary(input.size()),out(input.size());
    auto line=[&](const std::vector<float> &src,std::vector<float> &dst,int offset,int stride,int n){std::deque<int> q;int added=0;for(int i=0;i<n;++i){const int right=std::min(n-1,i+radius);while(added<=right){while(!q.empty()&&src[size_t(offset+q.back()*stride)]>=src[size_t(offset+added*stride)])q.pop_back();q.push_back(added++);}while(!q.empty()&&q.front()<i-radius)q.pop_front();dst[size_t(offset+i*stride)]=src[size_t(offset+q.front()*stride)];}};
    for(int y=0;y<h;++y)line(input,temporary,y*w,1,w);for(int x=0;x<w;++x)line(temporary,out,x,w,h);return out;
}
float luma(const std::array<float,3> &v){return .2627f*v[0]+.6780f*v[1]+.0593f*v[2];}
}
std::shared_ptr<const HazeBasis> prepareHaze(const QImage &image,double scale,QSize original,QString &error){
    if(image.isNull()||image.format()!=QImage::Format_RGBA32FPx4||!std::isfinite(scale)||scale<=0){error="Dehaze input/scale";return {};}
    auto result=std::make_shared<HazeBasis>();result->size=previewSize(image.size(),1024);const int w=result->size.width(),h=result->size.height();const size_t n=size_t(w)*h;
    std::vector<std::array<float,3>> rgb(n);std::vector<float> alpha(n),dark(n),guide(n);const double factor=std::max(double(image.width())/w,double(image.height())/h);const double edge=original.isValid()?qMax(original.width(),original.height()):qMax(image.width(),image.height());
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){const double sx=(x+.5)*image.width()/w-.5,sy=(y+.5)*image.height()/h-.5;const int ix=int(std::floor(sx)),iy=int(std::floor(sy));const float fx=float(sx-ix),fy=float(sy-iy);std::array<float,3> v{};float a=0;
        for(int j=0;j<2;++j)for(int i=0;i<2;++i){const auto *p=reinterpret_cast<const float *>(image.constScanLine(std::clamp(iy+j,0,image.height()-1)))+4*std::clamp(ix+i,0,image.width()-1);const float weight=(i?fx:1-fx)*(j?fy:1-fy);for(int c=0;c<4;++c)if(!std::isfinite(p[c])){error="Dehaze NaN/Inf";return {};}const float coverage=std::clamp(p[3],0.f,1.f);a+=weight*coverage;for(int c=0;c<3;++c)v[c]+=p[c]*coverage*weight;}
        const auto k=size_t(y)*w+x;if(a>1e-8f)for(auto &c:v)c/=a;rgb[k]=v;alpha[k]=a;dark[k]=a>.95f?std::max(0.f,std::min({v[0],v[1],v[2]})):INFINITY;
    }
    const int radius=std::clamp(int(std::lround(std::min(7.5*edge/3840/(scale*factor),double(std::max(w,h)-1)))),0,std::max(w,h)-1);auto darkLocal=minimum(dark,w,h,radius);
    std::vector<size_t> candidates;for(size_t i=0;i<n;++i)if(alpha[i]>.95f&&std::isfinite(darkLocal[i]))candidates.push_back(i);
    if(candidates.empty()){result->a.assign(n,0);result->b.assign(n,1);return result;}
    const auto count=std::max(size_t(1),candidates.size()/1000);std::nth_element(candidates.begin(),candidates.begin()+ptrdiff_t(count-1),candidates.end(),[&](size_t a,size_t b){return darkLocal[a]>darkLocal[b];});size_t selected=candidates[0];for(size_t i=0;i<count;++i)if(luma(rgb[candidates[i]])>luma(rgb[selected]))selected=candidates[i];for(int c=0;c<3;++c)result->air[c]=std::max(.001f,rgb[selected][c]);result->airY=std::max(.001f,luma(result->air));
    for(size_t i=0;i<n;++i){dark[i]=alpha[i]>1e-8f?std::max(0.f,std::min({rgb[i][0]/result->air[0],rgb[i][1]/result->air[1],rgb[i][2]/result->air[2]})):INFINITY;guide[i]=std::clamp(luma(rgb[i])/result->airY,0.f,4.f);}darkLocal=minimum(dark,w,h,radius);
    std::vector<float> transmission(n);for(size_t i=0;i<n;++i)transmission[i]=std::isfinite(darkLocal[i])?std::clamp(1-.95f*darkLocal[i],.1f,1.f):1;
    const int guidedRadius=std::clamp(int(std::lround(std::min(40*edge/3840/(scale*factor),double(std::max(w,h)-1)))),0,std::max(w,h)-1);auto weights=spatialMean(alpha,w,h,guidedRadius);
    auto mean=[&](const std::vector<float> &v){auto value=v;for(size_t i=0;i<n;++i)value[i]*=alpha[i];value=spatialMean(std::move(value),w,h,guidedRadius);for(size_t i=0;i<n;++i)value[i]=weights[i]>1e-8f?value[i]/weights[i]:0;return value;};
    auto meanI=mean(guide),meanP=mean(transmission);std::vector<float> ii(n),ip(n);for(size_t i=0;i<n;++i){ii[i]=guide[i]*guide[i];ip[i]=guide[i]*transmission[i];}auto meanII=mean(ii),meanIP=mean(ip);std::vector<float> a(n),b(n);for(size_t i=0;i<n;++i){const float variance=std::max(0.f,meanII[i]-meanI[i]*meanI[i]),covariance=meanIP[i]-meanI[i]*meanP[i];a[i]=covariance/(variance+.001f);b[i]=meanP[i]-a[i]*meanI[i];}result->a=mean(a);result->b=mean(b);return result;
}
void applyHaze(const HazeBasis &basis,int x,int y,QSize size,float *v,double strength){
    if(strength==0)return;float transmission=1;
    if(strength>0){const float sx=float((x+.5)*basis.size.width()/size.width()-.5),sy=float((y+.5)*basis.size.height()/size.height()-.5);const int ix=int(std::floor(sx)),iy=int(std::floor(sy));const float fx=sx-ix,fy=sy-iy;float a=0,b=0;for(int j=0;j<2;++j)for(int i=0;i<2;++i){const auto k=size_t(std::clamp(iy+j,0,basis.size.height()-1))*basis.size.width()+std::clamp(ix+i,0,basis.size.width()-1);const float weight=(i?fx:1-fx)*(j?fy:1-fy);a+=basis.a[k]*weight;b+=basis.b[k]*weight;}const float guide=std::clamp((.2627f*v[0]+.6780f*v[1]+.0593f*v[2])/basis.airY,0.f,4.f),full=std::clamp(a*guide+b,.1f,1.f);transmission=1+float(strength)*(full-1);for(int c=0;c<3;++c)v[c]=basis.air[c]+(v[c]-basis.air[c])/transmission;
    }else{transmission=1+float(strength)*.6f;for(int c=0;c<3;++c)v[c]=v[c]*transmission+basis.air[c]*(1-transmission);}
}
HazeSampler::HazeSampler(const HazeBasis &b,QSize size,double amount):basis(b),strength(float(amount)),inverseAir(1/b.airY){
    if(strength<=0)return;auto axis=[](int pixels,int target){std::vector<Axis> result(static_cast<size_t>(pixels));for(int i=0;i<pixels;++i){const double point=(i+.5)*target/pixels-.5;const int lo=int(std::floor(point));result[size_t(i)]={std::clamp(lo,0,target-1),std::clamp(lo+1,0,target-1),float(point-lo)};}return result;};columns=axis(size.width(),basis.size.width());rows=axis(size.height(),basis.size.height());
}
void HazeSampler::apply(int x,int y,float *v) const {
    if(strength<0){const float t=1+strength*.6f;for(int c=0;c<3;++c)v[c]=v[c]*t+basis.air[c]*(1-t);return;}if(strength==0)return;const auto &cx=columns[size_t(x)],&cy=rows[size_t(y)];const auto k00=size_t(cy.lo)*basis.size.width()+cx.lo,k10=size_t(cy.lo)*basis.size.width()+cx.hi,k01=size_t(cy.hi)*basis.size.width()+cx.lo,k11=size_t(cy.hi)*basis.size.width()+cx.hi;
    auto interpolate=[&](const std::vector<float> &data){const float low=data[k00]+(data[k10]-data[k00])*cx.fraction,high=data[k01]+(data[k11]-data[k01])*cx.fraction;return low+(high-low)*cy.fraction;};const float a=interpolate(basis.a),b=interpolate(basis.b),guide=std::clamp((.2627f*v[0]+.6780f*v[1]+.0593f*v[2])*inverseAir,0.f,4.f),full=std::clamp(a*guide+b,.1f,1.f),inverse=1/(1+strength*(full-1));for(int c=0;c<3;++c)v[c]=basis.air[c]+(v[c]-basis.air[c])*inverse;
}
