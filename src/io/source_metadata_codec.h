#pragma once
#include "core/source_metadata.h"
#include <QJsonObject>
// Strict bounded whitelist, schema=1. No opaque EXIF/MakerNote/thumbnail blobs.
// All output parameters change only on success.
bool validSourceMetadata(const SourceMetadata &metadata,QString &error);
QJsonObject sourceMetadataJson(const SourceMetadata &metadata,QString &error);
bool sourceMetadataFromJson(const QJsonObject &json,SourceMetadata &metadata,QString &error);
