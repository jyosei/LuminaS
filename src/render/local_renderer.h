#pragma once
#include "core/photo_document.h"
#include "cpu_renderer.h"

namespace LocalEdit {
// Background calls only. processingRecipe describes the actual working source;
// document.global always retains the original geometry for mask coordinates.
// A prepared preview must never be passed to the full-source entry point.
QImage renderDocumentPreviewLinear(const SourceFrame &source,const PhotoDocument &document,
    const Recipe &processingRecipe,int longEdge,QString &error,const CancellationToken &cancel={});
RenderResult renderDocumentPreview(const SourceFrame &source,const PhotoDocument &document,
    const Recipe &processingRecipe,int longEdge=3840,SpatialCache *cache=nullptr,
    const RenderView &view={},const CancellationToken &cancel={});
QImage renderDocumentFullLinear(const SourceFrame &source,const PhotoDocument &document,
    QString &error,const CancellationToken &cancel={});
}
