#pragma once
#include "core/document.h"
#include <QMutex>
#include <functional>
#include <vector>
#include <cstdint>
#include "dehaze_operator.h"
#include "denoise_operator.h"
#include "defringe_operator.h"
struct SpatialBasis {
    QImage before;
    std::vector<float> texture,clarity,sharpen;
    std::vector<std::uint16_t> sharpenEdges; // normalized coefficient, not image data
    double sharpenRadius=0;
    std::shared_ptr<const HazeBasis> haze;
    std::shared_ptr<const NoiseBasis> noise;
    double sourceScale=1;
    bool noiseLuminancePrepared=false,noiseChromaPrepared=false;
};
std::shared_ptr<SpatialBasis> prepareSpatial(QImage before,double sourceScale,QSize originalSize,QString &error,bool haze=false,bool detail=true,double sharpenRadius=0,bool noiseLuminance=false,bool noiseChroma=false,bool edgeMask=false);
std::vector<float> spatialGaussianMean(const std::vector<float> &input,const std::vector<float> &alpha,int width,int height,double sigma);
std::vector<float> spatialGaussian(std::vector<float> values,int width,int height,double sigma,std::vector<float> &scratch);
std::vector<float> spatialMean(std::vector<float> input,int width,int height,int radius);
QImage applySpatial(const SpatialBasis &basis,const Recipe &recipe,QString &error);
QImage applySpatialOwned(std::shared_ptr<SpatialBasis> basis,const Recipe &recipe,QString &error);
float sharpenMaskWeight(std::uint16_t edge,double strength);
QImage renderSharpenMask(const SpatialBasis &basis,double strength,QString &error);
// Exactly one immutable basis, shared by current preparation and interactive lanes.
class SpatialCache {
public:
    std::shared_ptr<const SpatialBasis> get(const SourceFrame &source,const Recipe &recipe,int longEdge,double scale,const std::function<QImage()> &build,QString &error,bool haze=false,bool detail=true,double sharpenRadius=0,bool noiseLuminance=false,bool noiseChroma=false,bool edgeMask=false);
    void clear();
    void clearSpatial();
    DefringeCache defringe;
private:
    QMutex mutex;
    qint64 imageKey=0;
    Recipe prefix;
    std::array<float,9> balance{};
    double sourceScale=0;
    QSize originalSize;
    int targetEdge=0;
    bool raw=false;
    std::shared_ptr<const SpatialBasis> basis;
};
