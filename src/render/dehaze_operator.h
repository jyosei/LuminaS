#pragma once
#include <QImage>
#include <QString>
#include <array>
#include <vector>
#include <memory>
struct HazeBasis {
    QSize size;
    std::array<float,3> air{1,1,1};
    float airY=1;
    std::vector<float> a,b;
};
std::shared_ptr<const HazeBasis> prepareHaze(const QImage &image,double sourceScale,QSize originalSize,QString &error);
void applyHaze(const HazeBasis &basis,int x,int y,QSize imageSize,float *rgb,double strength);
class HazeSampler {
public:
    HazeSampler(const HazeBasis &basis,QSize size,double strength);
    void apply(int x,int y,float *rgb) const;
private:
    struct Axis {int lo,hi;float fraction;};
    const HazeBasis &basis;
    float strength,inverseAir;
    std::vector<Axis> columns,rows;
};
