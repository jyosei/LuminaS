#pragma once
#include "io/source_reader.h"
#include <future>
// Test/reference setup only. Production UI must use asynchronous watchers, not
// wait for a future on its GUI thread. std::launch::async avoids Qt task stealing.
inline FrameResult readTestSource(const QString &path,double warmth=0,double tint=0,const CancellationToken &cancel={}){
    return std::async(std::launch::async,[path,warmth,tint,cancel]{return readSource(path,warmth,tint,cancel);}).get();
}
