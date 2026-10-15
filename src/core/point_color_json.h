#pragma once
// Serialization for Recipe::pointColors. Strict, atomic: a failed parse leaves
// the destination vector untouched, and every present object must carry all ten
// double fields with no unknown keys.
#include "core/document.h"
#include <QJsonArray>
#include <QJsonValue>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <cmath>

inline QJsonArray pointColorsJson(const QVector<PointColorAdjustment> &points){
    QJsonArray array;
    for(const PointColorAdjustment &p:points){
        QJsonObject o;
        o["hue"]=p.hue;o["saturation"]=p.saturation;o["luminance"]=p.luminance;
        o["hueWidth"]=p.hueWidth;o["saturationWidth"]=p.saturationWidth;o["luminanceWidth"]=p.luminanceWidth;
        o["feather"]=p.feather;
        o["hueShift"]=p.hueShift;o["saturationShift"]=p.saturationShift;o["luminanceShift"]=p.luminanceShift;
        array.append(o);
    }
    return array;
}

inline bool validPointColors(const QVector<PointColorAdjustment> &points){
    if(points.size()>maximumPointColors)return false;
    for(const PointColorAdjustment &p:points){
        const double values[10]={p.hue,p.saturation,p.luminance,p.hueWidth,p.saturationWidth,
            p.luminanceWidth,p.feather,p.hueShift,p.saturationShift,p.luminanceShift};
        for(double v:values)if(!std::isfinite(v))return false;
        if(p.hue<0||p.hue>=360)return false;
        if(p.saturation<0||p.saturation>1)return false;
        if(p.luminance<0||p.luminance>1)return false;
        if(p.hueWidth<1||p.hueWidth>180)return false;
        if(p.saturationWidth<.01||p.saturationWidth>1)return false;
        if(p.luminanceWidth<.01||p.luminanceWidth>1)return false;
        if(p.feather<.01||p.feather>1)return false;
        if(p.hueShift<-1||p.hueShift>1)return false;
        if(p.saturationShift<-1||p.saturationShift>1)return false;
        if(p.luminanceShift<-1||p.luminanceShift>1)return false;
    }
    return true;
}

inline bool pointColorsFromJson(const QJsonValue &value,QVector<PointColorAdjustment> &out){
    if(!value.isArray())return false;
    const QJsonArray array=value.toArray();
    if(array.size()>maximumPointColors)return false;
    static const QStringList keys{"hue","saturation","luminance","hueWidth","saturationWidth",
        "luminanceWidth","feather","hueShift","saturationShift","luminanceShift"};
    QVector<PointColorAdjustment> parsed;
    parsed.reserve(array.size());
    for(const QJsonValue &entry:array){
        if(!entry.isObject())return false;
        const QJsonObject object=entry.toObject();
        if(object.size()!=keys.size())return false;
        double fields[10]{};
        for(int i=0;i<keys.size();++i){
            const QJsonValue field=object.value(keys[i]);
            if(!field.isDouble())return false;
            fields[i]=field.toDouble();
            if(!std::isfinite(fields[i]))return false;
        }
        PointColorAdjustment p;
        p.hue=fields[0];p.saturation=fields[1];p.luminance=fields[2];
        p.hueWidth=fields[3];p.saturationWidth=fields[4];p.luminanceWidth=fields[5];
        p.feather=fields[6];
        p.hueShift=fields[7];p.saturationShift=fields[8];p.luminanceShift=fields[9];
        parsed.append(p);
    }
    if(!validPointColors(parsed))return false;
    out=parsed;
    return true;
}
