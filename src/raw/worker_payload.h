#pragma once
#include "worker_protocol.h"
#include "core/cancellation.h"

namespace RawWorker {
constexpr qint64 maximumPayloadBytes=45000000LL*16;
// Background-only, immutable frame. Private existing directory, fixed
// <request.token>.frame target; no overwrite. All outputs change only on success.
// File: 32-byte LE header: magic[8]="LUMRAW1\0", version u32 at8,
// compact metadata byte count u32 at12, pixel byte count u64 at16,
// width u32 at24, height u32 at28; metadata JSON then packed RGBA32F rows.
// Done keys: type,protocol,id,token,fileBytes(decimal string),pixelSha256,
// fileSha256,frame(metadata). frame includes pixelSha256 and requestHash.
bool validFrame(const Request &request,const SourceFrame &frame,QString &error);
bool writePayload(const QString &directory,const Request &request,const SourceFrame &frame,
    QJsonObject &done,QString &error,CancellationToken cancel={});
FrameResult readPayload(const QString &directory,const Request &request,const QJsonObject &done,
    const CancellationToken &cancel={});
}
