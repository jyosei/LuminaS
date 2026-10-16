#pragma once
#include "local_reference.h"

namespace LocalEdit {
// One mutable scratch owner per executing thread; never share it across jobs.
// Storage is bounded by tile query count and the source feather support row,
// never a full-source mask. Temporary feather payload is reported as a peak.
class MaskRasterScratch {
public:
    MaskRasterScratch()=default;
    MaskRasterScratch(const MaskRasterScratch &)=delete;
    MaskRasterScratch &operator=(const MaskRasterScratch &)=delete;
    qsizetype storageBytes() const{return points.capacity()*qsizetype(sizeof(QPointF))+
        active.capacity()+values.capacity()*qsizetype(sizeof(float))+featherScratchBytes;}
    qint64 mappingNs=0,samplingNs=0;
    qsizetype featherScratchBytes=0; // peak temporary payload, not retained capacity
private:
    friend class PreparedBrushMask;
    QVector<QPointF> points;
    QVector<quint8> active;
    QVector<float> values;
};
// Historical class name retained. Brushes use an immutable balanced point
// index; C3 gradients use analytic coverage and radial support bounds. A brush
// maximum is the nearest-stamp weight; all component order remains unchanged.
class PreparedBrushMask {
public:
    explicit PreparedBrushMask(const Mask &mask);
    bool valid() const{return bool(impl);}
    QString error() const{return problem;}
    float sample(QPointF point,float alpha=1) const;
    // Feather evaluates source-plane support through bounded streaming groups;
    // the output tile never requests a full-source coverage buffer.
    // A tile is at most 512x512; production traversal defaults to 256.
    bool rasterize(const MaskMapping &mapping,const QImage &alphaFrame,QRect rect,
        QVector<float> &output,QString &error,CancellationToken cancel={},
        MaskRasterScratch *scratch=nullptr) const;
    qsizetype indexBytes() const;
private:
    float rawSample(QPointF point) const;
    struct Impl;
    std::shared_ptr<const Impl> impl;
    QString problem;
};
struct MaskPreviewStats {
    int tiles=0,workers=0;
    qsizetype maximumScratchBytes=0;
    double mappingAccumulatedMs=0,samplingAccumulatedMs=0;
};
// Background-only preview assembly. Four bounded workers, disjoint output rows;
// full-source consumers use individual tiles instead of this full-mask buffer.
bool rasterizeMaskPreview(const PreparedBrushMask &mask,const MaskMapping &mapping,
    const QImage &alpha,QVector<float> &output,QString &error,CancellationToken cancel={},
    int tileSize=256,MaskPreviewStats *stats=nullptr);
}
