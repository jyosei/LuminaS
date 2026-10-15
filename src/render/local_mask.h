#pragma once
#include "local_reference.h"

namespace LocalEdit {
// One mutable scratch owner per executing thread; never share it across jobs.
// Storage is bounded by the largest accepted 512x512 tile, not the full image.
class MaskRasterScratch {
public:
    MaskRasterScratch()=default;
    MaskRasterScratch(const MaskRasterScratch &)=delete;
    MaskRasterScratch &operator=(const MaskRasterScratch &)=delete;
    qsizetype storageBytes() const{return points.capacity()*qsizetype(sizeof(QPointF))+
        active.capacity()+values.capacity()*qsizetype(sizeof(float));}
    qint64 mappingNs=0,samplingNs=0;
private:
    friend class PreparedBrushMask;
    QVector<QPointF> points;
    QVector<quint8> active;
    QVector<float> values;
};
// Immutable balanced point index. A brush maximum is the head weight at the
// nearest stamp; composition order still follows the original mask record.
class PreparedBrushMask {
public:
    explicit PreparedBrushMask(const Mask &mask);
    bool valid() const{return bool(impl);}
    QString error() const{return problem;}
    float sample(QPointF point,float alpha=1) const;
    // B2 is pointwise, so halo=0. Whole-mask spatial feather arrives in C4.
    // A tile is at most 512x512; production traversal defaults to 256.
    bool rasterize(const MaskMapping &mapping,const QImage &alphaFrame,QRect rect,
        QVector<float> &output,QString &error,CancellationToken cancel={},
        MaskRasterScratch *scratch=nullptr) const;
    qsizetype indexBytes() const;
private:
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
