#pragma once
#include "core/document.h"
#include "core/cancellation.h"
FrameResult readSource(const QString &path,double rawWarmth=0,double rawTint=0,const CancellationToken &cancel={});
