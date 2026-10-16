#pragma once
#include "core/cancellation.h"
#include <QPointF>
#include <QVector>
#include <QString>
#include <functional>

namespace LocalEdit::Detail {
using CoverageSampler=std::function<float(QPointF)>;
// Whole-mask source-plane low pass. A normalized separable raised-cosine
// window samples original pixel centres; radius=max(2,feather). Below 2 source
// pixels blend the original analytic coverage with the filtered field.
// No alpha is convolved, no source image boundary is imposed, no inversion here.
float featherSample(QPointF point,double feather,const CoverageSampler &raw,
    const CancellationToken &cancel={});
// Bounded source-grid groups and streaming nine-moment integral queries.
// Scratch scales with point count + (256+2*radius), never radius squared.
// Inactive points publish zero; invalid active points fail. Errors/cancellation
// preserve output.
bool featherPoints(const QVector<QPointF> &points,const QVector<quint8> &active,
    double feather,const CoverageSampler &raw,QVector<float> &output,QString &error,
    const CancellationToken &cancel={},qsizetype *peakScratchBytes=nullptr);
}
