#pragma once
#include "core/photo_document.h"

namespace LocalEdit {
enum class GradientKind {Linear,Radial};
// Handle order: linear start/end/translation; radial centre/X radius/Y radius/
// rotation (the latter at 1.25 X radius). Points are in original source space.
QVector<QPointF> gradientHandlePoints(const MaskGeometry &geometry);
class GradientTransaction {
public:
    GradientTransaction();~GradientTransaction();
    GradientTransaction(const GradientTransaction &)=delete;
    GradientTransaction &operator=(const GradientTransaction &)=delete;
    bool beginCreate(DocumentSnapshot document,const QString &layerId,const QString &componentId,
        GradientKind kind,QPointF uv,double strength,MaskOperation operation,QString &error);
    bool beginEdit(DocumentSnapshot document,const QString &layerId,const QString &componentId,
        int handle,QPointF uv,QString &error);
    bool append(QPointF uv,QString &error);
    bool active() const;
    DocumentSnapshot preview() const;
    DocumentSnapshot finish(DocumentSnapshot current,QString &error); // always closes
    void cancel();
private:
    struct State;std::unique_ptr<State> state;
};
}
