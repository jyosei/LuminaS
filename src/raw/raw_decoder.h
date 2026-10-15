#pragma once
#include <QImage>
#include <QJsonObject>
#include <QString>
struct RawOptions {
    int quality = 3; // AHD=3, DCB=4; built-in implementations only.
    int longEdge = 3840;
    QString linearOutput; // Experimental RGB16 PPM, not a product export recipe.
    bool collectLinearDiagnostics = true; // Probe-only full RGB16 scan/hash; no pixel processing changes.
    bool directLinearPreview = false; // Experimental copy_mem_image route; reference path remains default.
};
struct RawResult {
    QString error;
    QSize originalSize;
    QImage preview;
    QJsonObject report;
};
RawResult developRaw(const QString &path, const RawOptions &options = {});
