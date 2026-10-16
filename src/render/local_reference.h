#pragma once
#include "core/photo_document.h"
#include "core/cancellation.h"
#include <QRect>

namespace LocalEdit {
// Coordinates are in oriented original pixel-edge space: pixel (i,j) has
// center (i+.5,j+.5). A snapshot includes the current final endpoint once.
class BrushSampler {
public:
    explicit BrushSampler(double radius,double hardness=.6);
    bool append(QPointF point,QString &error);
    BrushGeometry snapshot() const;
    bool empty() const{return samples.isEmpty();}
    bool valid() const{return validSettings;}
private:
    QVector<QPointF> samples;
    QPointF last;
    double radius,hardness,step,next;
    bool validSettings=false;
};

struct MaskMapping {
    QSize sourceSize,outputSize;
    Recipe geometry; // original full recipe, even if the working frame is prepared
};
// Scalar brush/linear/radial reference; historical name kept for callers.
class BrushMaskReference {
public:
    explicit BrushMaskReference(Mask mask);
    bool valid() const{return problem.isEmpty();}
    QString error() const{return problem;}
    float sample(QPointF sourcePoint,float sourceAlpha=1) const;
    // Result is row-major for rect, using global output coordinates. No partial
    // output is published on cancellation or invalid alpha/input.
    bool rasterize(const MaskMapping &mapping,const QImage &alphaFrame,QRect rect,
        QVector<float> &output,QString &error,CancellationToken cancel={}) const;
private:
    Mask mask;
    QString problem;
};

class LocalPixelTransform {
public:
    explicit LocalPixelTransform(const LocalParameters &parameters);
    bool valid() const{return bool(impl);}
    bool identity() const;
    // Straight linear Rec.2020. RGB warm/tint always applies, including RAW.
    // Alpha is unchanged; a rejected pixel is unchanged as well.
    bool apply(float rgba[4],float coverage,float opacity) const;
private:
    struct Impl;
    std::shared_ptr<const Impl> impl;
};
}
