#pragma once
#include "photo_document.h"
#include <QStringList>

namespace LocalEdit {
enum class LayerChangeKind {Rename,Visibility,Opacity,Duplicate,Remove,Reorder,RemoveComponent};
struct LayerChange {
    LayerChangeKind kind=LayerChangeKind::Rename;
    QString layerId,name,duplicateId,componentId;
    QStringList componentIds;
    QStringList order; // complete bottom-to-top layer-id permutation
    bool visible=true;
    double opacity=1;
};
// Value-only operation. Null + empty error is a no-op. No pixel buffers or
// history counters are created here; EditHistory owns commit/Undo revisions.
// Duplicate ids are supplied by the caller and validated across the document.
DocumentSnapshot changedLayers(const PhotoDocument &base,const LayerChange &change,QString &error);
}
