#pragma once
#include "export/export_engine.h"
#include <future>
// Test-only synchronous wait. Real UI exports continue through Queue/watchers.
inline Export::Result executeTestJob(const Export::Job &job,const std::atomic_bool *cancel=nullptr,const Export::Progress &progress={}){
    return std::async(std::launch::async,[job,cancel,progress]{return Export::execute(job,cancel,progress);}).get();
}
