#pragma once
// Strict serialization for Recipe::defringe (the bounded purple/green fringe
// suppression settings). The helper owns only the object form of one
// DefringeSettings; callers decide how a missing key maps to their default.
#include <QJsonObject>
#include <QJsonValue>
#include "defringe_settings.h"
inline QJsonObject defringeJson(const DefringeSettings &defringe){
    QJsonObject object;
    object["purpleAmount"]=defringe.purpleAmount;
    object["purpleHue"]=defringe.purpleHue;
    object["purpleWidth"]=defringe.purpleWidth;
    object["greenAmount"]=defringe.greenAmount;
    object["greenHue"]=defringe.greenHue;
    object["greenWidth"]=defringe.greenWidth;
    object["radius"]=defringe.radius;
    return object;
}
// Atomic: any structural, type, range or key-set failure leaves out unchanged.
inline bool defringeFromJson(const QJsonValue &value,DefringeSettings &out){
    if(!value.isObject())return false;
    const QJsonObject object=value.toObject();
    if(object.size()!=7)return false;
    for(auto it=object.begin();it!=object.end();++it)
        if(it.key()!="purpleAmount"&&it.key()!="purpleHue"&&it.key()!="purpleWidth"&&
           it.key()!="greenAmount"&&it.key()!="greenHue"&&it.key()!="greenWidth"&&it.key()!="radius")return false;
    for(const auto &key:{"purpleAmount","purpleHue","purpleWidth","greenAmount","greenHue","greenWidth","radius"})
        if(!object.value(key).isDouble())return false;
    DefringeSettings parsed;
    parsed.purpleAmount=object.value("purpleAmount").toDouble();
    parsed.purpleHue=object.value("purpleHue").toDouble();
    parsed.purpleWidth=object.value("purpleWidth").toDouble();
    parsed.greenAmount=object.value("greenAmount").toDouble();
    parsed.greenHue=object.value("greenHue").toDouble();
    parsed.greenWidth=object.value("greenWidth").toDouble();
    parsed.radius=object.value("radius").toDouble();
    if(!validDefringe(parsed))return false;
    out=parsed;
    return true;
}