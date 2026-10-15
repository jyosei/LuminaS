#pragma once
#include "export_engine.h"
#include "export_metadata.h"
class QIODevice;
namespace Export {
// Seekable output stream boundary also permits deterministic short-write tests.
bool writeTiff(QIODevice &device,const Pixels &pixels,const Settings &settings,
               const PhotoMetadata &metadata,QString &error,const std::atomic_bool *cancel=nullptr);
}
