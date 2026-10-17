#pragma once
#include "core/document.h"
#include "core/cancellation.h"
#include "source_progress.h"
// Full source. RAW uses the fixed same-version worker and requires a background
// caller; never falls back to in-process LibRaw. expectedHash freezes identity.
FrameResult readSource(const QString &path,double rawWarmth=0,double rawTint=0,const CancellationToken &cancel={},const QString &expectedHash={},const SourceReadProgress &progress={});
// Exact 4K prefix only (long side3840, small sources unchanged). Grading fields
// are rejected; callers apply their remaining recipe exactly once afterwards.
FrameResult prepareSource(const QString &path,const Recipe &geometry={},double rawWarmth=0,double rawTint=0,const CancellationToken &cancel={},const QString &expectedHash={},const SourceReadProgress &progress={});
