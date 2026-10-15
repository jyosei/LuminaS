#pragma once
// Strict serialization for Recipe::chromaticAberration (the bounded red/blue
// corner displacement in original pixels). The helper owns only the object form
// of one QPointF; callers decide how a missing key maps to their default.
#include <QJsonObject>
#include <QJsonValue>
#include <QPointF>
#include <cmath>
inline constexpr double chromaticAberrationLimit=10;
inline QJsonObject chromaticAberrationJson(QPointF chromaticAberration){
    QJsonObject object;
    object["red"]=chromaticAberration.x();
    object["blue"]=chromaticAberration.y();
    return object;
}
inline bool validChromaticAberration(QPointF chromaticAberration){
    const double values[2]={chromaticAberration.x(),chromaticAberration.y()};
    for(double value:values)if(!std::isfinite(value)||std::abs(value)>chromaticAberrationLimit)return false;
    return true;
}
// Atomic: any structural, type, range or key-set failure leaves out unchanged.
inline bool chromaticAberrationFromJson(const QJsonValue &value,QPointF &out){
    if(!value.isObject())return false;
    const QJsonObject object=value.toObject();
    if(object.size()!=2)return false;
    for(auto it=object.begin();it!=object.end();++it)
        if(it.key()!="red"&&it.key()!="blue")return false;
    const QJsonValue red=object.value("red"),blue=object.value("blue");
    if(!red.isDouble()||!blue.isDouble())return false;
    const QPointF parsed(red.toDouble(),blue.toDouble());
    if(!validChromaticAberration(parsed))return false;
    out=parsed;
    return true;
}
