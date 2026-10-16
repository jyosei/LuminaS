#pragma once
#include "core/photo_document.h"
#include <memory>

namespace LocalEdit {
// One source-coordinate brush gesture, with no pixels or history ownership.
// The host commits finish() once through EditHistory, or discards via cancel().
class BrushTransaction {
public:
    BrushTransaction();
    ~BrushTransaction();
    BrushTransaction(const BrushTransaction &)=delete;
    BrushTransaction &operator=(const BrushTransaction &)=delete;
    bool begin(DocumentSnapshot document,const QString &layerId,const QString &componentId,
        QPointF outputUv,double radius,double hardness,double strength,MaskOperation operation,QString &error);
    bool append(QPointF outputUv,QString &error);
    bool active() const;
    DocumentSnapshot preview() const;
    // Empty + no error means no meaningful stroke. Always closes the gesture.
    // Fresh metadata is retained; incompatible edits are rejected, not replaced.
    DocumentSnapshot finish(DocumentSnapshot current,QString &error);
    void cancel();
private:
    struct State;
    std::unique_ptr<State> state;
};
}
