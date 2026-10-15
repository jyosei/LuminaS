#pragma once
#include "core/document.h"
#include <array>
#include <cmath>
class PrimaryOperator {
public:
    explicit PrimaryOperator(const Recipe &r){
        constexpr double weights[]{.2627,.6780,.0593};
        for(int i=0;i<3;++i){const auto &p=r.primaries[i];if(p.hue==0&&p.saturation==0)continue;enabled=true;double hue=120*i+30*p.hue;if(hue<0)hue+=360;if(hue>=360)hue-=360;const int sector=int(hue/60);const double f=hue/60-sector,x=sector%2?1-f:f;double rgb[3]{};
            switch(sector){case 0:rgb[0]=1;rgb[1]=x;break;case 1:rgb[0]=x;rgb[1]=1;break;case 2:rgb[1]=1;rgb[2]=x;break;case 3:rgb[1]=x;rgb[2]=1;break;case 4:rgb[0]=x;rgb[2]=1;break;default:rgb[0]=1;rgb[2]=x;}
            const double y=weights[0]*rgb[0]+weights[1]*rgb[1]+weights[2]*rgb[2];for(int c=0;c<3;++c)delta[c*3+i]=float(weights[i]+(1+p.saturation)*(rgb[c]-y)-(c==i?1:0));
        }
    }
    void apply(float *v) const {
        if(!enabled||(v[0]==v[1]&&v[1]==v[2]))return;const float y=.2627f*v[0]+.6780f*v[1]+.0593f*v[2],a=v[0]-y,b=v[1]-y,c=v[2]-y;for(int i=0;i<3;++i)v[i]+=delta[i*3]*a+delta[i*3+1]*b+delta[i*3+2]*c;
    }
private:
    std::array<float,9> delta{};bool enabled=false;
};
