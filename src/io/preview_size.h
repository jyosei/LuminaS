#pragma once
#include <QSize>
// 3840 on the long edge, preserving aspect ratio; never upscale a small source.
inline QSize previewSize(QSize source, int longEdge = 3840) {
    if (longEdge <= 0 || source.width() <= 0 || source.height() <= 0) return {};
    if (qMax(source.width(), source.height()) <= longEdge) return source;
    const auto scaled=source.scaled(longEdge, longEdge, Qt::KeepAspectRatio);
    return {qMax(1,scaled.width()),qMax(1,scaled.height())};
}
inline bool acceptedImageSize(QSize size) {
    return size.width() > 0 && size.height() > 0 && qint64(size.width()) * size.height() <= 45000000;
}
