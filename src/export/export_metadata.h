#pragma once
#include "export_types.h"
#include <QMap>
namespace Export {
// Whitelist only; values are canonical strings, never copied opaque IFD blobs.
struct PhotoMetadata {
    QString make,model,artist,copyright,date,lens;
    double exposure=0,aperture=0,focalLength=0; int iso=0;
    bool hasGps=false; double latitude=0,longitude=0,altitude=0;
};
PhotoMetadata readMetadata(const QString &source,Metadata policy,bool gps,QString &error);
QByteArray exifBlock(const PhotoMetadata &metadata,QSize output,double ppi);
}
