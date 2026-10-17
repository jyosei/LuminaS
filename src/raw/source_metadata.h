#pragma once
#include "core/source_metadata.h"
namespace RawWorker {
// Worker source-kernel only. Bounded Canon CMT1 enrichment, no parent-side RAW
// reopen. Reuses the established whitelist parser and source hash verification.
void enrichCopyright(const QString &path,SourceMetadata &metadata);
}
