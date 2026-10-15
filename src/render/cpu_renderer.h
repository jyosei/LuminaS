#pragma once
#include "core/document.h"
#include "core/cancellation.h"
class SpatialCache;
struct RenderView {int pointRange=-1;bool sharpenMask=false;bool operator==(const RenderView &) const = default;}; // transient; never serialized in Recipe
struct RenderResult { QImage image; QSize fullSize; QString error; double elapsedMs=0; };
QSize renderedSize(QSize source,const Recipe &recipe);
QPointF outputToSource(QPointF output,QSize source,const Recipe &recipe);
QImage renderLinear(const SourceFrame &source,const Recipe &recipe,int longEdge,QString &error,const RenderView &view={},const CancellationToken &cancel={});
RenderResult renderFrame(const SourceFrame &source,const Recipe &recipe,int longEdge=3840,SpatialCache *spatialCache=nullptr,const RenderView &view={},const CancellationToken &cancel={});
