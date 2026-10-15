#pragma once
#include "core/document.h"
#include "color/color_pipeline.h"
#include "lens_shading_operator.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

class ChromaticBasis {
public:
    std::array<double,9> forward{},inverse{};
    double condition=0;
    bool valid=false;
    explicit ChromaticBasis(const std::array<double,9> &matrix):forward(matrix){
        double norm=0;for(int i=0;i<3;++i){double row=0;for(int j=0;j<3;++j){if(!std::isfinite(matrix[i*3+j]))return;row+=std::abs(matrix[i*3+j]);}norm=std::max(norm,row);}
        if(norm<=0)return;
        std::array<double,9> a;for(int i=0;i<9;++i)a[i]=matrix[i]/norm;
        const double d=a[0]*(a[4]*a[8]-a[5]*a[7])-a[1]*(a[3]*a[8]-a[5]*a[6])+a[2]*(a[3]*a[7]-a[4]*a[6]);
        if(!std::isfinite(d)||std::abs(d)<1e-12)return;
        inverse={a[4]*a[8]-a[5]*a[7],a[2]*a[7]-a[1]*a[8],a[1]*a[5]-a[2]*a[4],
                 a[5]*a[6]-a[3]*a[8],a[0]*a[8]-a[2]*a[6],a[2]*a[3]-a[0]*a[5],
                 a[3]*a[7]-a[4]*a[6],a[1]*a[6]-a[0]*a[7],a[0]*a[4]-a[1]*a[3]};
        for(double &v:inverse){v/=d*norm;if(!std::isfinite(v))return;}
        double inverseNorm=0;for(int i=0;i<3;++i)inverseNorm=std::max(inverseNorm,std::abs(inverse[i*3])+std::abs(inverse[i*3+1])+std::abs(inverse[i*3+2]));
        condition=norm*inverseNorm;valid=std::isfinite(condition)&&condition<=1e4;
    }
    static ChromaticBasis forSource(const SourceFrame &s){
        if(s.raw){std::array<double,9> matrix;std::copy(s.cameraToWork.begin(),s.cameraToWork.end(),matrix.begin());return ChromaticBasis(matrix);}
        // JPEG ICC has already been decoded into work RGB. These are virtual
        // linear-sRGB reference channels, not the original camera primaries.
        static const ChromaticBasis jpeg=[] {
            std::array<double,9> matrix{};auto input=linearSrgbProfile(),work=workProfile();
            if(!input||!work){if(input)cmsCloseProfile(input);if(work)cmsCloseProfile(work);return ChromaticBasis(matrix);}
            auto transform=cmsCreateTransform(input,TYPE_RGB_FLT,work,TYPE_RGB_FLT,INTENT_RELATIVE_COLORIMETRIC,cmsFLAGS_NOOPTIMIZE|cmsFLAGS_NOCACHE);
            cmsCloseProfile(input);cmsCloseProfile(work);
            if(transform){const float basis[]{1,0,0,0,1,0,0,0,1};float result[9];cmsDoTransform(transform,basis,result,3);cmsDeleteTransform(transform);
                for(int i=0;i<3;++i)for(int j=0;j<3;++j)matrix[i*3+j]=result[j*3+i];}
            return ChromaticBasis(matrix);
        }();return jpeg;
    }
};

class ChromaticSampler {
public:
    ChromaticSampler(const SourceFrame &s,const Recipe &r):source(s),lens(s.linear.size(),r.lensShading),active(!r.chromaticAberration.isNull()),basis(active?ChromaticBasis::forSource(s):ChromaticBasis(std::array<double,9>{})),centre(s.linear.width()/2.,s.linear.height()/2.),width(s.linear.width()),height(s.linear.height()),stride(s.linear.bytesPerLine()),data(s.linear.constBits()){
        if(!active){usable=true;return;}
        const double w=s.originalSize.isValid()?s.originalSize.width():s.linear.width()*s.pixelScaleX();
        const double h=s.originalSize.isValid()?s.originalSize.height():s.linear.height()*s.pixelScaleY();
        const double radius=.5*std::hypot(w,h);
        if(!basis.valid||!std::isfinite(w)||!std::isfinite(h)||w<=0||h<=0||!std::isfinite(radius)||radius<=0)return;
        scales={1+std::clamp(r.chromaticAberration.x()/radius,-.25,.25),1.,1+std::clamp(r.chromaticAberration.y()/radius,-.25,.25)};usable=true;
    }
    bool enabled() const{return active;}
    bool valid() const{return usable;}
    QPointF position(QPointF p,int channel) const{return centre+(p-centre)*scales[channel];}
    // Common alpha is the minimum channel coverage. Each straight component
    // divides by its own coverage before reassembly, avoiding black edge bleed.
    bool sample(QPointF p,float out[4]) const {
        std::array<double,3> components{};double alpha=1;
        for(int c=0;c<3;++c){const auto at=position(p,c);const double x=at.x()-.5,y=at.y()-.5;double value=0,coverage=0;
            if(std::isfinite(x)&&std::isfinite(y)&&x>=-1&&y>=-1&&x<=width&&y<=height){
                const int ix=int(std::floor(x)),iy=int(std::floor(y));const double fx=x-ix,fy=y-iy;
                for(int j=0;j<2;++j)for(int i=0;i<2;++i){const int sx=ix+i,sy=iy+j;if(sx<0||sy<0||sx>=width||sy>=height)continue;
                    const auto *v=reinterpret_cast<const float *>(data+sy*stride)+sx*4;
                    for(int n=0;n<4;++n)if(!std::isfinite(v[n]))return false;
                    const double weight=(i?fx:1-fx)*(j?fy:1-fy)*std::clamp(double(v[3]),0.,1.);
                    value+=(basis.inverse[c*3]*v[0]+basis.inverse[c*3+1]*v[1]+basis.inverse[c*3+2]*v[2])*weight*(lens.enabled()?lens.gain(sx,sy):1.f);coverage+=weight;
                }
            }
            components[c]=coverage>1e-8?value/coverage:0;alpha=std::min(alpha,coverage);
        }
        out[3]=float(std::clamp(alpha,0.,1.));
        for(int c=0;c<3;++c){out[c]=out[3]>1e-8f?float(basis.forward[c*3]*components[0]+basis.forward[c*3+1]*components[1]+basis.forward[c*3+2]*components[2]):0;if(!std::isfinite(out[c]))return false;}
        return true;
    }
private:
    const SourceFrame &source;
    LensShading lens;
    bool active=false,usable=false;
    ChromaticBasis basis;
    QPointF centre;
    std::array<double,3> scales{1,1,1};
    int width=0,height=0;
    qsizetype stride=0;
    const uchar *data=nullptr;
};
