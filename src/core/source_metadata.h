#pragma once
#include <QString>
// Read-only source delivery metadata. Separate from AssetIdentity/sourceFacts
// and editing recipes: GPS/artist data never becomes a project identity field.
struct SourceMetadata {
    QString make,model,artist,copyright,date,lens;
    double exposure=0,aperture=0,focalLength=0;
    int iso=0;
    bool hasGps=false;
    double latitude=0,longitude=0,altitude=0;
    bool operator==(const SourceMetadata &) const = default;
};
