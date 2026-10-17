#pragma once
#include "export_types.h"
#include "core/source_metadata.h"
#include <QMap>
namespace Export {
// Whitelist only; values are canonical strings, never copied opaque IFD blobs.
using PhotoMetadata=SourceMetadata;
struct SourceMetadataSnapshot {
    QString sourcePath,sourceHash;
    PhotoMetadata values;
};
PhotoMetadata readMetadata(const QString &source,Metadata policy,bool gps,QString &error,const SourceMetadataSnapshot *rawSnapshot=nullptr);
QByteArray exifBlock(const PhotoMetadata &metadata,QSize output,double ppi);
}
