#pragma once
#include "io/source_reader.h"
#include <future>
// Synchronous command-line setup, explicitly outside QCoreApplication's thread.
// Measured sourceRead/readTotal includes worker IPC, not merely LibRaw decode.
inline FrameResult readProbeSource(const QString &path,double warmth=0,double tint=0){
    return std::async(std::launch::async,[path,warmth,tint]{return readSource(path,warmth,tint);}).get();
}
