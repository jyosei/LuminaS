#pragma once
#include "core/document.h"

// Control-point presentation only. Inverts the same source mapping as the pixel
// renderer; failure leaves uv unchanged. Off-crop finite UV values are allowed.
bool sourceToOutput(QPointF sourcePoint,QSize original,const Recipe &recipe,QPointF &uv);
