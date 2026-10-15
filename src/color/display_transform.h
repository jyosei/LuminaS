#pragma once
#include <QImage>
#include <QByteArray>
#include <QString>
#include <atomic>
#include <memory>
struct DisplayTransformResult {QImage image;QString error;};
bool validDisplayProfile(const QByteArray &icc,QString &error);
// Display-only SDR conversion. Input is tagged encoded RGB (sRGB if untagged).
// Device RGB never enters SourceFrame, Recipe, histogram or output rendering.
DisplayTransformResult transformDisplay(const QImage &input,const QByteArray &monitorIcc,
    const std::shared_ptr<std::atomic<bool>> &cancel={});
