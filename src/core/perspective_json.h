#pragma once
// Strict serialization for Recipe::perspective (the bounded inverse projective
// tilt of the normalized full canvas). The helper owns only the object form of
// one QPointF; callers decide how a missing key maps to their default.
#include <QJsonObject>
#include <QJsonValue>
#include <QPointF>
#include <cmath>
inline constexpr double perspectiveLimit=.3;
inline QJsonObject perspectiveJson(QPointF perspective){
    QJsonObject object;
    object["horizontal"]=perspective.x();
    object["vertical"]=perspective.y();
    return object;
}
inline bool validPerspective(QPointF perspective){
    const double values[2]={perspective.x(),perspective.y()};
    for(double value:values)if(!std::isfinite(value)||std::abs(value)>perspectiveLimit)return false;
    return true;
}
// Atomic: any structural, type, range or key-set failure leaves out unchanged.
inline bool perspectiveFromJson(const QJsonValue &value,QPointF &out){
    if(!value.isObject())return false;
    const QJsonObject object=value.toObject();
    if(object.size()!=2)return false;
    for(auto it=object.begin();it!=object.end();++it)
        if(it.key()!="horizontal"&&it.key()!="vertical")return false;
    const QJsonValue horizontal=object.value("horizontal"),vertical=object.value("vertical");
    if(!horizontal.isDouble()||!vertical.isDouble())return false;
    const QPointF parsed(horizontal.toDouble(),vertical.toDouble());
    if(!validPerspective(parsed))return false;
    out=parsed;
    return true;
}
