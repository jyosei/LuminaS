#pragma once
#include "core/defringe_settings.h"
#include <QImage>
#include <QString>
#include "core/document.h"
#include <QMutex>
#include <functional>
// Immutable, straight linear Rec.2020 input AFTER WB/geometry, before global
// tone. The standalone operator is the numerical foundation; UI/cache routing
// is a separate integration step, not implied by this API.
QImage applyDefringe(const QImage &before,const DefringeSettings &settings,double sourceScaleX,double sourceScaleY,QString &error);

// A single immutable 4K before/after pair. Full source images are never retained.
// Build outside the lock: a newer request prevents an older job's cache commit.
class DefringeCache {
public:
    QImage get(const SourceFrame &source,const Recipe &prefix,const DefringeSettings &settings,
               int edge,double scaleX,double scaleY,const std::function<QImage()> &build,QString &error);
    void clear();
private:
    QMutex mutex;
    quint64 generation=0;
    qint64 sourceKey=0;
    Recipe cachedPrefix;
    QSize originalSize;
    std::array<float,9> balance{},camera{};
    double pixelX=0,pixelY=0;
    int targetEdge=0;
    bool raw=false;
    QImage before,after;
    DefringeSettings processedSettings;
};
