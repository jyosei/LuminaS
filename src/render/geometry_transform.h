#pragma once
#include "core/document.h"
#include "distortion_operator.h"
#include <cmath>
#include <limits>
inline bool hasPerspective(const Recipe &r){return r.perspective.x()!=0||r.perspective.y()!=0;}
inline bool hasGeometricWarp(const Recipe &r){return hasPerspective(r)||r.distortion!=0;}
inline bool hasChromaticAberration(const Recipe &r){return !r.chromaticAberration.isNull();}
inline bool hasPreparedAdjustment(const Recipe &r){return hasGeometricWarp(r)||hasChromaticAberration(r)||r.lensShading!=0;}
inline bool samePreparedGeometry(const Recipe &a,const Recipe &b){return a.crop==b.crop&&a.angle==b.angle&&a.perspective==b.perspective&&a.distortion==b.distortion&&a.chromaticAberration==b.chromaticAberration&&a.lensShading==b.lensShading;}
// The corrected canvas keeps the original aspect/dimensions. Crop and angle are
// canvas operations; this inverse homography then locates the source sample.
inline QPointF perspectiveSource(QPointF point,QSize size,QPointF tilt){
    if(tilt.x()==0&&tilt.y()==0)return point;
    const double x=2*point.x()/size.width()-1,y=2*point.y()/size.height()-1;
    const double denominator=1+tilt.x()*x+tilt.y()*y;
    if(!std::isfinite(denominator)||denominator<=1e-8){const auto nan=std::numeric_limits<double>::quiet_NaN();return {nan,nan};}
    return {(x/denominator+1)*size.width()/2,(y/denominator+1)*size.height()/2};
}
inline QPointF perspectiveSource(QPointF point,QSize size,const Recipe &r){return perspectiveSource(point,size,r.perspective);}
inline QPointF geometrySource(QPointF point,QSize size,const Recipe &r){return RadialDistortion(size,r.distortion).source(perspectiveSource(point,size,r));}
// Shared scalar map with per-frame constants cached. Preserve the multiplication
// order of outputToSource so tool and tile coordinates use identical arithmetic.
class OutputSourceMapping {
public:
    OutputSourceMapping(QSize source,const Recipe &r):size(source),tilt(r.perspective),distortion(source,r.distortion),
        cropWidth(r.crop.width()),cropHeight(r.crop.height()),
        cx((r.crop.x()+r.crop.width()/2)*source.width()),cy((r.crop.y()+r.crop.height()/2)*source.height()),
        co(std::cos(r.angle*3.14159265358979323846/180)),si(std::sin(r.angle*3.14159265358979323846/180)),
        flipH(r.flipHorizontal),flipV(r.flipVertical){}
    QPointF map(QPointF uv) const {
        if(flipH)uv.setX(1-uv.x());if(flipV)uv.setY(1-uv.y());
        const double dx=(uv.x()-.5)*cropWidth*size.width(),dy=(uv.y()-.5)*cropHeight*size.height();
        return distortion.source(perspectiveSource({cx+dx*co+dy*si,cy-dx*si+dy*co},size,tilt));
    }
private:
    QSize size;QPointF tilt;RadialDistortion distortion;
    double cropWidth,cropHeight,cx,cy,co,si;
    bool flipH,flipV;
};
inline Recipe preparedGeometry(const Recipe &r){Recipe result;result.crop=r.crop;result.angle=r.angle;result.perspective=r.perspective;result.distortion=r.distortion;result.chromaticAberration=r.chromaticAberration;result.lensShading=r.lensShading;return result;}
inline void clearPreparedGeometry(Recipe &r){r.crop={0,0,1,1};r.angle=0;r.perspective={0,0};r.distortion=0;r.chromaticAberration={0,0};r.lensShading=0;}

