#pragma once
#include "core/document.h"
#include <vector>
struct NoiseBasis {
    std::vector<float> luminance,redChroma,blueChroma;
};
std::shared_ptr<const NoiseBasis> prepareNoise(const QImage &image,double scale,bool luminance,bool chroma,QString &error,int stripRows=128);
bool applyNoiseInPlace(QImage &image,double scale,const Recipe &recipe,QString &error,int stripRows=128);
