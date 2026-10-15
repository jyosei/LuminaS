#pragma once
#include <cmath>
#include <initializer_list>
struct DefringeSettings {
    double purpleAmount=0,purpleHue=300,purpleWidth=50;
    double greenAmount=0,greenHue=115,greenWidth=45;
    double radius=3; // nominal direction-normalized original pixel support
    bool operator==(const DefringeSettings &) const = default;
};
inline bool validDefringe(const DefringeSettings &s){
    for(double v:{s.purpleAmount,s.purpleHue,s.purpleWidth,s.greenAmount,s.greenHue,s.greenWidth,s.radius})if(!std::isfinite(v))return false;
    return s.purpleAmount>=0&&s.purpleAmount<=1&&s.greenAmount>=0&&s.greenAmount<=1&&
        s.purpleHue>=0&&s.purpleHue<=360&&s.greenHue>=0&&s.greenHue<=360&&
        s.purpleWidth>=5&&s.purpleWidth<=90&&s.greenWidth>=5&&s.greenWidth<=90&&s.radius>=1&&s.radius<=20;
}
inline bool hasDefringe(const DefringeSettings &s){return s.purpleAmount!=0||s.greenAmount!=0;}
