#pragma once
#include "core/document.h"

// Explicit geometry edit, not an implicit renderer change. Preserve crop centre,
// aspect and all other recipe fields. Conservatively contain the whole output
// footprint (including R/G/B bilinear support) in an opaque original source.
// No pixel buffers, decoding, display profile or preview cache are consumed.
// Failure leaves output unchanged. A valid already-contained crop is unchanged.
bool constrainedCrop(QSize sourceSize,const Recipe &recipe,QRectF &output,QString &error);
