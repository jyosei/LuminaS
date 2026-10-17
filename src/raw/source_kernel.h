#pragma once
#include "core/document.h"
#include "core/cancellation.h"
#include <functional>

// Internal blocking source kernel for the local worker and equivalence probes.
// Shares the existing math; never call from an interactive GUI operation.
// Optional observer reports actual phase entry/completion, not an ETA.
using SourceReadProgress=std::function<void(const QString &,int,int)>;
FrameResult readSourceKernel(const QString &path,double rawWarmth=0,double rawTint=0,
    const CancellationToken &cancel={},const SourceReadProgress &progress={});
