#include "photo_document.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QSet>
#include <limits>
#include <cmath>

// S3 A2 bounded data serialization for LocalEdit::PhotoDocument. Pure record
// validation plus strict JSON boilerplate. Global (Recipe) fields are delegated
// to the existing recipeJson/recipeFromJson/validRecipe helpers; no colour or
// geometrical algorithm is re-implemented here and no QImage or file system is
// touched. Parsing builds a temporary and assigns the caller output only after
// the whole document is valid.

namespace {
using namespace LocalEdit;

constexpr int maximumIdLength=128;
constexpr int maximumSourcePathLength=32767;
constexpr int maximumNameLength=4096;
constexpr qint64 maximumSourceFactsBytes=262144;
constexpr double coordinateLimit=1e8;
constexpr double geometryEpsilon=1e-12;

bool fail(QString &error,const QString &message){error=message;return false;}
bool finite(double value){return std::isfinite(value);}
bool failFinite(QString &error,const QString &what){return fail(error,QStringLiteral("%1 must be finite").arg(what));}
bool failRange(QString &error,const QString &what){return fail(error,QStringLiteral("%1 is outside the permitted range").arg(what));}

bool validIdentifier(const QString &id){return !id.isEmpty()&&id.size()<=maximumIdLength;}
bool validName(const QString &name){return name.size()<=maximumNameLength;}
bool validCoordinate(double value){return finite(value)&&std::abs(value)<=coordinateLimit;}

QJsonArray pointArray(QPointF point){return QJsonArray{point.x(),point.y()};}

bool readPoint(const QJsonValue &value,QPointF &point){
    if(!value.isArray())return false;
    const QJsonArray array=value.toArray();
    if(array.size()!=2)return false;
    if(!array[0].isDouble()||!array[1].isDouble())return false;
    const QPointF parsed(array[0].toDouble(),array[1].toDouble());
    if(!validCoordinate(parsed.x())||!validCoordinate(parsed.y()))return false;
    point=parsed;
    return true;
}

bool validLocalParameters(const LocalParameters &p,QString &error){
    const double exposure=p.exposure,contrast=p.contrast,highlights=p.highlights,shadows=p.shadows;
    const double whites=p.whites,blacks=p.blacks,warmth=p.warmth,tint=p.tint,saturation=p.saturation;
    if(!finite(exposure))return failFinite(error,QStringLiteral("parameters.exposure"));
    if(!finite(contrast))return failFinite(error,QStringLiteral("parameters.contrast"));
    if(!finite(highlights))return failFinite(error,QStringLiteral("parameters.highlights"));
    if(!finite(shadows))return failFinite(error,QStringLiteral("parameters.shadows"));
    if(!finite(whites))return failFinite(error,QStringLiteral("parameters.whites"));
    if(!finite(blacks))return failFinite(error,QStringLiteral("parameters.blacks"));
    if(!finite(warmth))return failFinite(error,QStringLiteral("parameters.warmth"));
    if(!finite(tint))return failFinite(error,QStringLiteral("parameters.tint"));
    if(!finite(saturation))return failFinite(error,QStringLiteral("parameters.saturation"));
    if(std::abs(exposure)>5)return failRange(error,QStringLiteral("parameters.exposure [-5,5]"));
    if(std::abs(contrast)>1)return failRange(error,QStringLiteral("parameters.contrast [-1,1]"));
    if(std::abs(highlights)>1||std::abs(shadows)>1||std::abs(whites)>1||std::abs(blacks)>1)
        return failRange(error,QStringLiteral("parameters highlights/shadows/whites/blacks [-1,1]"));
    if(std::abs(warmth)>1||std::abs(tint)>1)return failRange(error,QStringLiteral("parameters warmth/tint [-1,1]"));
    if(saturation<0||saturation>2)return failRange(error,QStringLiteral("parameters.saturation [0,2]"));
    return true;
}

const QStringList &parameterKeys(){
    static const QStringList keys{QStringLiteral("exposure"),QStringLiteral("contrast"),QStringLiteral("highlights"),
        QStringLiteral("shadows"),QStringLiteral("whites"),QStringLiteral("blacks"),QStringLiteral("warmth"),
        QStringLiteral("tint"),QStringLiteral("saturation")};
    return keys;
}

QJsonObject localParametersJson(const LocalParameters &p){
    return QJsonObject{{QStringLiteral("exposure"),p.exposure},{QStringLiteral("contrast"),p.contrast},
        {QStringLiteral("highlights"),p.highlights},{QStringLiteral("shadows"),p.shadows},
        {QStringLiteral("whites"),p.whites},{QStringLiteral("blacks"),p.blacks},
        {QStringLiteral("warmth"),p.warmth},{QStringLiteral("tint"),p.tint},
        {QStringLiteral("saturation"),p.saturation}};
}

bool localParametersFromJson(const QJsonValue &value,LocalParameters &out){
    if(!value.isObject())return false;
    const QJsonObject object=value.toObject();
    if(object.size()!=parameterKeys().size())return false;
    for(auto it=object.begin();it!=object.end();++it)if(!parameterKeys().contains(it.key()))return false;
    for(const QString &key:parameterKeys())if(!object.value(key).isDouble())return false;
    LocalParameters parsed;
    parsed.exposure=object.value(QStringLiteral("exposure")).toDouble();
    parsed.contrast=object.value(QStringLiteral("contrast")).toDouble();
    parsed.highlights=object.value(QStringLiteral("highlights")).toDouble();
    parsed.shadows=object.value(QStringLiteral("shadows")).toDouble();
    parsed.whites=object.value(QStringLiteral("whites")).toDouble();
    parsed.blacks=object.value(QStringLiteral("blacks")).toDouble();
    parsed.warmth=object.value(QStringLiteral("warmth")).toDouble();
    parsed.tint=object.value(QStringLiteral("tint")).toDouble();
    parsed.saturation=object.value(QStringLiteral("saturation")).toDouble();
    QString error;
    if(!validLocalParameters(parsed,error))return false;
    out=parsed;
    return true;
}

bool validBrush(const BrushGeometry &brush,QString &error){
    if(brush.points.isEmpty())return fail(error,QStringLiteral("brush geometry requires at least one point"));
    if(brush.points.size()>maximumPointsPerStroke)return fail(error,QStringLiteral("brush points exceed the per-stroke limit"));
    for(const QPointF &point:brush.points)
        if(!validCoordinate(point.x())||!validCoordinate(point.y()))
            return fail(error,QStringLiteral("brush point coordinates are not finite or exceed 1e8"));
    if(!finite(brush.radius)||brush.radius<=0||brush.radius>coordinateLimit)
        return fail(error,QStringLiteral("brush radius must be finite, positive and at most 1e8"));
    if(!finite(brush.hardness)||brush.hardness<0||brush.hardness>1)
        return fail(error,QStringLiteral("brush hardness is outside [0,1]"));
    return true;
}

bool validLinear(const LinearGeometry &linear,QString &error){
    if(!validCoordinate(linear.start.x())||!validCoordinate(linear.start.y())||
       !validCoordinate(linear.end.x())||!validCoordinate(linear.end.y()))
        return fail(error,QStringLiteral("linear endpoints are not finite or exceed 1e8"));
    const double dx=linear.end.x()-linear.start.x(),dy=linear.end.y()-linear.start.y();
    const double squared=dx*dx+dy*dy;
    if(!finite(squared)||squared<=geometryEpsilon)
        return fail(error,QStringLiteral("linear endpoints must not coincide"));
    return true;
}

bool validRadial(const RadialGeometry &radial,QString &error){
    if(!validCoordinate(radial.center.x())||!validCoordinate(radial.center.y()))
        return fail(error,QStringLiteral("radial center is not finite or exceeds 1e8"));
    if(!finite(radial.radiusX)||radial.radiusX<=0||radial.radiusX>coordinateLimit)
        return fail(error,QStringLiteral("radial radiusX must be finite, positive and at most 1e8"));
    if(!finite(radial.radiusY)||radial.radiusY<=0||radial.radiusY>coordinateLimit)
        return fail(error,QStringLiteral("radial radiusY must be finite, positive and at most 1e8"));
    if(!finite(radial.angleDegrees)||std::abs(radial.angleDegrees)>coordinateLimit)
        return fail(error,QStringLiteral("radial angleDegrees is not finite or exceeds 1e8"));
    return true;
}

bool validMaskGeometry(const MaskGeometry &geometry,QString &error){
    if(const auto *brush=std::get_if<BrushGeometry>(&geometry))return validBrush(*brush,error);
    if(const auto *linear=std::get_if<LinearGeometry>(&geometry))return validLinear(*linear,error);
    if(const auto *radial=std::get_if<RadialGeometry>(&geometry))return validRadial(*radial,error);
    return fail(error,QStringLiteral("unsupported mask geometry"));
}

QJsonObject maskGeometryJson(const MaskGeometry &geometry){
    QJsonObject object;
    if(const auto *brush=std::get_if<BrushGeometry>(&geometry)){
        QJsonArray points;
        for(const QPointF &point:brush->points)points.append(pointArray(point));
        object.insert(QStringLiteral("kind"),QStringLiteral("brush"));
        object.insert(QStringLiteral("points"),points);
        object.insert(QStringLiteral("radius"),brush->radius);
        object.insert(QStringLiteral("hardness"),brush->hardness);
    } else if(const auto *linear=std::get_if<LinearGeometry>(&geometry)){
        object.insert(QStringLiteral("kind"),QStringLiteral("linear"));
        object.insert(QStringLiteral("start"),pointArray(linear->start));
        object.insert(QStringLiteral("end"),pointArray(linear->end));
    } else if(const auto *radial=std::get_if<RadialGeometry>(&geometry)){
        object.insert(QStringLiteral("kind"),QStringLiteral("radial"));
        object.insert(QStringLiteral("center"),pointArray(radial->center));
        object.insert(QStringLiteral("radiusX"),radial->radiusX);
        object.insert(QStringLiteral("radiusY"),radial->radiusY);
        object.insert(QStringLiteral("angleDegrees"),radial->angleDegrees);
    }
    return object;
}

bool exactKeys(const QJsonObject &object,const QStringList &keys){
    if(object.size()!=keys.size())return false;
    for(auto it=object.begin();it!=object.end();++it)if(!keys.contains(it.key()))return false;
    return true;
}

bool maskGeometryFromJson(const QJsonValue &value,MaskGeometry &out){
    if(!value.isObject())return false;
    const QJsonObject object=value.toObject();
    const QJsonValue kindValue=object.value(QStringLiteral("kind"));
    if(!kindValue.isString())return false;
    const QString kind=kindValue.toString();
    if(kind==QStringLiteral("brush")){
        if(!exactKeys(object,{QStringLiteral("kind"),QStringLiteral("points"),QStringLiteral("radius"),QStringLiteral("hardness")}))return false;
        const QJsonValue pointsValue=object.value(QStringLiteral("points"));
        if(!pointsValue.isArray())return false;
        const QJsonArray pointsArray=pointsValue.toArray();
        if(pointsArray.isEmpty()||pointsArray.size()>maximumPointsPerStroke)return false;
        if(!object.value(QStringLiteral("radius")).isDouble()||!object.value(QStringLiteral("hardness")).isDouble())return false;
        BrushGeometry parsed;
        parsed.points.reserve(pointsArray.size());
        for(const QJsonValue &point:pointsArray){
            QPointF parsedPoint;
            if(!readPoint(point,parsedPoint))return false;
            parsed.points.append(parsedPoint);
        }
        parsed.radius=object.value(QStringLiteral("radius")).toDouble();
        parsed.hardness=object.value(QStringLiteral("hardness")).toDouble();
        QString error;
        if(!validBrush(parsed,error))return false;
        out=parsed;
        return true;
    }
    if(kind==QStringLiteral("linear")){
        if(!exactKeys(object,{QStringLiteral("kind"),QStringLiteral("start"),QStringLiteral("end")}))return false;
        LinearGeometry parsed;
        if(!readPoint(object.value(QStringLiteral("start")),parsed.start))return false;
        if(!readPoint(object.value(QStringLiteral("end")),parsed.end))return false;
        QString error;
        if(!validLinear(parsed,error))return false;
        out=parsed;
        return true;
    }
    if(kind==QStringLiteral("radial")){
        if(!exactKeys(object,{QStringLiteral("kind"),QStringLiteral("center"),QStringLiteral("radiusX"),
                              QStringLiteral("radiusY"),QStringLiteral("angleDegrees")}))return false;
        for(const auto &key:{QStringLiteral("radiusX"),QStringLiteral("radiusY"),QStringLiteral("angleDegrees")})
            if(!object.value(key).isDouble())return false;
        RadialGeometry parsed;
        if(!readPoint(object.value(QStringLiteral("center")),parsed.center))return false;
        parsed.radiusX=object.value(QStringLiteral("radiusX")).toDouble();
        parsed.radiusY=object.value(QStringLiteral("radiusY")).toDouble();
        parsed.angleDegrees=object.value(QStringLiteral("angleDegrees")).toDouble();
        QString error;
        if(!validRadial(parsed,error))return false;
        out=parsed;
        return true;
    }
    return false;
}

bool validMaskComponent(const MaskComponent &component,QString &error){
    if(!validIdentifier(component.id))return fail(error,QStringLiteral("mask component id must be 1..128 characters"));
    if(component.operation!=MaskOperation::Add&&component.operation!=MaskOperation::Subtract)
        return fail(error,QStringLiteral("mask component operation is not Add or Subtract"));
    if(!finite(component.strength)||component.strength<0||component.strength>1)
        return fail(error,QStringLiteral("mask component strength is outside [0,1]"));
    return validMaskGeometry(component.geometry,error);
}

QJsonObject maskComponentJson(const MaskComponent &component){
    QJsonObject object;
    object.insert(QStringLiteral("id"),component.id);
    object.insert(QStringLiteral("operation"),component.operation==MaskOperation::Subtract?
        QStringLiteral("subtract"):QStringLiteral("add"));
    object.insert(QStringLiteral("strength"),component.strength);
    object.insert(QStringLiteral("geometry"),maskGeometryJson(component.geometry));
    return object;
}

bool maskComponentFromJson(const QJsonValue &value,MaskComponent &out){
    if(!value.isObject())return false;
    const QJsonObject object=value.toObject();
    if(!exactKeys(object,{QStringLiteral("id"),QStringLiteral("operation"),QStringLiteral("strength"),QStringLiteral("geometry")}))return false;
    const QJsonValue idValue=object.value(QStringLiteral("id"));
    const QJsonValue operationValue=object.value(QStringLiteral("operation"));
    if(!idValue.isString()||!operationValue.isString())return false;
    if(!object.value(QStringLiteral("strength")).isDouble())return false;
    MaskComponent parsed;
    parsed.id=idValue.toString();
    const QString operation=operationValue.toString();
    if(operation==QStringLiteral("add"))parsed.operation=MaskOperation::Add;
    else if(operation==QStringLiteral("subtract"))parsed.operation=MaskOperation::Subtract;
    else return false;
    parsed.strength=object.value(QStringLiteral("strength")).toDouble();
    if(!maskGeometryFromJson(object.value(QStringLiteral("geometry")),parsed.geometry))return false;
    QString error;
    if(!validMaskComponent(parsed,error))return false;
    out=parsed;
    return true;
}

bool parseUint64(const QJsonValue &value,quint64 minimum,quint64 &out){
    if(!value.isString())return false;
    const QString text=value.toString();
    if(text.isEmpty()||text.size()>20)return false;
    for(const QChar c:text)if(c<QChar('0')||c>QChar('9'))return false;
    if(text.size()>1&&text.at(0)==QChar('0'))return false;
    if(text.size()==20&&text>QStringLiteral("18446744073709551615"))return false;
    bool ok=false;
    const quint64 parsed=text.toULongLong(&ok);
    if(!ok||parsed<minimum)return false;
    out=parsed;
    return true;
}

bool validMask(const Mask &mask,QString &error){
    if(mask.components.size()>maximumComponentsPerLayer)
        return fail(error,QStringLiteral("mask components exceed the per-layer limit"));
    if(!finite(mask.feather)||mask.feather<0||mask.feather>16384)
        return fail(error,QStringLiteral("mask feather is outside [0,16384]"));
    if(mask.version<1)return fail(error,QStringLiteral("mask version must be at least 1"));
    for(const auto &component:mask.components)if(!validMaskComponent(component,error))return false;
    return true;
}

QJsonObject maskJson(const Mask &mask){
    QJsonArray components;
    for(const MaskComponent &component:mask.components)components.append(maskComponentJson(component));
    return QJsonObject{{QStringLiteral("components"),components},{QStringLiteral("feather"),mask.feather},
        {QStringLiteral("invert"),mask.invert},{QStringLiteral("version"),QString::number(mask.version)}};
}

bool maskFromJson(const QJsonValue &value,Mask &out){
    if(!value.isObject())return false;
    const QJsonObject object=value.toObject();
    if(!exactKeys(object,{QStringLiteral("components"),QStringLiteral("feather"),QStringLiteral("invert"),QStringLiteral("version")}))return false;
    if(!object.value(QStringLiteral("components")).isArray())return false;
    if(!object.value(QStringLiteral("feather")).isDouble())return false;
    if(!object.value(QStringLiteral("invert")).isBool())return false;
    quint64 version=1;
    if(!parseUint64(object.value(QStringLiteral("version")),1,version))return false;
    const QJsonArray componentArray=object.value(QStringLiteral("components")).toArray();
    if(componentArray.size()>maximumComponentsPerLayer)return false;
    Mask parsed;
    parsed.components.reserve(componentArray.size());
    for(const QJsonValue &component:componentArray){
        MaskComponent entry;
        if(!maskComponentFromJson(component,entry))return false;
        parsed.components.append(entry);
    }
    parsed.feather=object.value(QStringLiteral("feather")).toDouble();
    parsed.invert=object.value(QStringLiteral("invert")).toBool();
    parsed.version=version;
    QString error;
    if(!validMask(parsed,error))return false;
    out=parsed;
    return true;
}

bool validLayer(const AdjustmentLayer &layer,QString &error){
    if(!validIdentifier(layer.id))return fail(error,QStringLiteral("layer id must be 1..128 characters"));
    if(!validName(layer.name))return fail(error,QStringLiteral("layer name exceeds 4096 characters"));
    if(!finite(layer.opacity)||layer.opacity<0||layer.opacity>1)
        return fail(error,QStringLiteral("layer opacity is outside [0,1]"));
    if(layer.version<1)return fail(error,QStringLiteral("layer version must be at least 1"));
    if(!validLocalParameters(layer.parameters,error))return false;
    return validMask(layer.mask,error);
}

QJsonObject layerJson(const AdjustmentLayer &layer){
    return QJsonObject{{QStringLiteral("id"),layer.id},{QStringLiteral("name"),layer.name},
        {QStringLiteral("enabled"),layer.enabled},{QStringLiteral("opacity"),layer.opacity},
        {QStringLiteral("parameters"),localParametersJson(layer.parameters)},
        {QStringLiteral("mask"),maskJson(layer.mask)},
        {QStringLiteral("version"),QString::number(layer.version)}};
}

bool layerFromJson(const QJsonValue &value,AdjustmentLayer &out){
    if(!value.isObject())return false;
    const QJsonObject object=value.toObject();
    if(!exactKeys(object,{QStringLiteral("id"),QStringLiteral("name"),QStringLiteral("enabled"),QStringLiteral("opacity"),
                          QStringLiteral("parameters"),QStringLiteral("mask"),QStringLiteral("version")}))return false;
    const QJsonValue idValue=object.value(QStringLiteral("id"));
    const QJsonValue nameValue=object.value(QStringLiteral("name"));
    if(!idValue.isString()||!nameValue.isString())return false;
    if(!object.value(QStringLiteral("enabled")).isBool())return false;
    if(!object.value(QStringLiteral("opacity")).isDouble())return false;
    quint64 version=1;
    if(!parseUint64(object.value(QStringLiteral("version")),1,version))return false;
    AdjustmentLayer parsed;
    parsed.id=idValue.toString();
    parsed.name=nameValue.toString();
    parsed.enabled=object.value(QStringLiteral("enabled")).toBool();
    parsed.opacity=object.value(QStringLiteral("opacity")).toDouble();
    if(!localParametersFromJson(object.value(QStringLiteral("parameters")),parsed.parameters))return false;
    if(!maskFromJson(object.value(QStringLiteral("mask")),parsed.mask))return false;
    parsed.version=version;
    QString error;
    if(!validLayer(parsed,error))return false;
    out=parsed;
    return true;
}

// Canonical global template: a Recipe emitted by the existing serializer with one
// default point colour sample, so the nested point-colour key set is known without
// re-deriving any colour semantics here.
QJsonObject canonicalGlobalTemplate(){
    Recipe recipe;
    recipe.pointColors.append(PointColorAdjustment{});
    return recipeJson(recipe);
}

bool canonicalKeysMatch(const QJsonValue &provided,const QJsonValue &canonical,const QString &label){
    if(provided.isObject()&&canonical.isObject()){
        const QJsonObject providedObject=provided.toObject(),canonicalObject=canonical.toObject();
        for(auto it=providedObject.begin();it!=providedObject.end();++it){
            const auto reference=canonicalObject.constFind(it.key());
            if(reference==canonicalObject.constEnd())return false;
            if(!canonicalKeysMatch(it.value(),reference.value(),label+QChar('.')+it.key()))return false;
        }
        return true;
    }
    if(provided.isArray()&&canonical.isArray()){
        const QJsonArray providedArray=provided.toArray(),canonicalArray=canonical.toArray();
        if(providedArray.isEmpty()||canonicalArray.isEmpty())return true;
        for(const QJsonValue &entry:providedArray)
            if(!canonicalKeysMatch(entry,canonicalArray.at(0),label+QStringLiteral("[]")))return false;
        return true;
    }
    return true;
}
bool globalKeysRecognized(const QJsonObject &object){
    if(object.isEmpty())return true;
    const QJsonObject canonical=canonicalGlobalTemplate();
    for(auto it=object.begin();it!=object.end();++it){
        const auto reference=canonical.constFind(it.key());
        if(reference==canonical.constEnd())return false;
        if(!canonicalKeysMatch(it.value(),reference.value(),it.key()))return false;
    }
    return true;
}

bool globalFromJson(const QJsonValue &value,Recipe &out){
    if(!value.isObject())return false;
    const QJsonObject object=value.toObject();
    if(!globalKeysRecognized(object))return false;
    const Recipe parsed=recipeFromJson(object);
    if(!validRecipe(parsed))return false;
    out=parsed;
    return true;
}

bool assetShapeValid(const AssetIdentity &asset,bool requireSourceIdentity,QString &error){
    if(!validIdentifier(asset.id))return fail(error,QStringLiteral("asset id must be 1..128 characters"));
    if(asset.sourcePath.isEmpty())return fail(error,QStringLiteral("asset sourcePath must not be empty"));
    if(asset.sourcePath.contains(QChar(0)))return fail(error,QStringLiteral("asset sourcePath contains a null character"));
    if(asset.sourcePath.size()>maximumSourcePathLength)
        return fail(error,QStringLiteral("asset sourcePath exceeds 32767 characters"));
    if(!asset.sourceHash.isEmpty()){
        if(asset.sourceHash.size()!=64)return fail(error,QStringLiteral("asset sourceHash must be empty or 64 hex digits"));
        for(const QChar c:asset.sourceHash){
            const ushort code=c.unicode();
            const bool hex=(code>=ushort('0')&&code<=ushort('9'))||(code>=ushort('a')&&code<=ushort('f'))||
                           (code>=ushort('A')&&code<=ushort('F'));
            if(!hex)return fail(error,QStringLiteral("asset sourceHash must be empty or 64 hex digits"));
        }
    }
    const int width=asset.normalizedSize.width(),height=asset.normalizedSize.height();
    const bool unknown=width==0&&height==0;
    if(!unknown){
        if(width<=0||height<=0)
            return fail(error,QStringLiteral("asset normalizedSize must be fully unknown (0x0) or a positive width and height"));
        const qint64 pixels=qint64(width)*qint64(height);
        if(pixels>45000000)return fail(error,QStringLiteral("asset normalizedSize exceeds the 45MP limit"));
    }
    if(requireSourceIdentity&&(asset.sourceHash.isEmpty()||unknown))
        return fail(error,QStringLiteral("persistence requires a 64 hex digit sourceHash and a positive normalizedSize"));
    if(QJsonDocument(asset.sourceFacts).toJson(QJsonDocument::Compact).size()>maximumSourceFactsBytes)
        return fail(error,QStringLiteral("asset sourceFacts exceed the 256KiB encoding limit"));
    return true;
}

bool validDocumentBody(const PhotoDocument &document,bool requireSourceIdentity,QString &error){
    if(!assetShapeValid(document.asset,requireSourceIdentity,error))return false;
    if(document.algorithmVersion!=localAlgorithmVersion)
        return fail(error,QStringLiteral("algorithmVersion must be 1"));
    if(document.revision<1)return fail(error,QStringLiteral("revision must be at least 1"));
    if(document.layers.size()>maximumLayers)return fail(error,QStringLiteral("document layers exceed the 64 layer limit"));
    if(!validRecipe(document.global))return fail(error,QStringLiteral("global recipe is invalid"));
    QSet<QString> identifiers;
    identifiers.insert(document.asset.id);
    qint64 points=0;
    for(const AdjustmentLayer &layer:document.layers){
        if(!validLayer(layer,error))return false;
        if(identifiers.contains(layer.id))return fail(error,QStringLiteral("duplicate layer id in document"));
        identifiers.insert(layer.id);
        for(const MaskComponent &component:layer.mask.components){
            if(identifiers.contains(component.id))return fail(error,QStringLiteral("duplicate mask component id in document"));
            identifiers.insert(component.id);
            if(const auto *brush=std::get_if<BrushGeometry>(&component.geometry))points+=brush->points.size();
        }
    }
    if(points>maximumPointsPerDocument)return fail(error,QStringLiteral("document brush points exceed the limit"));
    return true;
}

QJsonObject assetJson(const AssetIdentity &asset){
    return QJsonObject{{QStringLiteral("id"),asset.id},{QStringLiteral("sourcePath"),asset.sourcePath},
        {QStringLiteral("sourceHash"),asset.sourceHash},
        {QStringLiteral("normalizedSize"),QJsonArray{asset.normalizedSize.width(),asset.normalizedSize.height()}},
        {QStringLiteral("raw"),asset.raw},{QStringLiteral("sourceFacts"),asset.sourceFacts}};
}

bool assetFromJson(const QJsonValue &value,AssetIdentity &out){
    if(!value.isObject())return false;
    const QJsonObject object=value.toObject();
    if(!exactKeys(object,{QStringLiteral("id"),QStringLiteral("sourcePath"),QStringLiteral("sourceHash"),
                          QStringLiteral("normalizedSize"),QStringLiteral("raw"),QStringLiteral("sourceFacts")}))return false;
    const QJsonValue idValue=object.value(QStringLiteral("id"));
    const QJsonValue pathValue=object.value(QStringLiteral("sourcePath"));
    const QJsonValue hashValue=object.value(QStringLiteral("sourceHash"));
    const QJsonValue sizeValue=object.value(QStringLiteral("normalizedSize"));
    const QJsonValue factsValue=object.value(QStringLiteral("sourceFacts"));
    if(!idValue.isString()||!pathValue.isString()||!hashValue.isString())return false;
    if(!sizeValue.isArray())return false;
    if(!object.value(QStringLiteral("raw")).isBool())return false;
    if(!factsValue.isObject())return false;
    const QJsonArray size=sizeValue.toArray();
    if(size.size()!=2)return false;
    if(!size[0].isDouble()||!size[1].isDouble())return false;
    const double width=size[0].toDouble(),height=size[1].toDouble();
    if(!finite(width)||!finite(height))return false;
    if(std::floor(width)!=width||std::floor(height)!=height)return false;
    if(width<std::numeric_limits<int>::min()||width>std::numeric_limits<int>::max())return false;
    if(height<std::numeric_limits<int>::min()||height>std::numeric_limits<int>::max())return false;
    AssetIdentity parsed;
    parsed.id=idValue.toString();
    parsed.sourcePath=pathValue.toString();
    parsed.sourceHash=hashValue.toString();
    parsed.normalizedSize=QSize(int(width),int(height));
    parsed.raw=object.value(QStringLiteral("raw")).toBool();
    parsed.sourceFacts=factsValue.toObject();
    out=parsed;
    return true;
}

bool documentFromJsonBody(const QJsonObject &json,PhotoDocument &document,bool requireSourceIdentity){
    if(!exactKeys(json,{QStringLiteral("format"),QStringLiteral("formatVersion"),QStringLiteral("algorithmVersion"),
                        QStringLiteral("revision"),QStringLiteral("localIdentity"),QStringLiteral("asset"),
                        QStringLiteral("global"),QStringLiteral("layers")}))return false;
    const QJsonValue formatValue=json.value(QStringLiteral("format"));
    if(!formatValue.isString()||formatValue.toString()!=QStringLiteral("LuminaS.photo-document"))return false;
    const QJsonValue formatVersionValue=json.value(QStringLiteral("formatVersion"));
    if(!formatVersionValue.isDouble()||formatVersionValue.toDouble()!=double(documentFormatVersion))return false;
    const QJsonValue algorithmVersionValue=json.value(QStringLiteral("algorithmVersion"));
    if(!algorithmVersionValue.isDouble()||algorithmVersionValue.toDouble()!=double(localAlgorithmVersion))return false;
    quint64 revision=1,localIdentity=0;
    if(!parseUint64(json.value(QStringLiteral("revision")),1,revision))return false;
    if(!parseUint64(json.value(QStringLiteral("localIdentity")),0,localIdentity))return false;
    if(!json.value(QStringLiteral("layers")).isArray())return false;
    const QJsonArray layerArray=json.value(QStringLiteral("layers")).toArray();
    if(layerArray.size()>maximumLayers)return false;
    // Bound the aggregate before copying point arrays into the editable record
    // tree. Per-stroke limits alone do not bound a many-stroke document.
    qint64 totalPoints=0;
    for(const auto &layerValue:layerArray){
        const auto components=layerValue.toObject().value("mask").toObject().value("components").toArray();
        if(components.size()>maximumComponentsPerLayer)return false;
        for(const auto &componentValue:components){
            const auto geometry=componentValue.toObject().value("geometry").toObject();
            if(geometry.value("kind").toString()=="brush"){
                const auto count=geometry.value("points").toArray().size();
                if(count>maximumPointsPerStroke)return false;
                totalPoints+=count;if(totalPoints>maximumPointsPerDocument)return false;
            }
        }
    }
    PhotoDocument parsed;
    parsed.revision=revision;
    parsed.localIdentity=localIdentity;
    parsed.algorithmVersion=localAlgorithmVersion;
    if(!assetFromJson(json.value(QStringLiteral("asset")),parsed.asset))return false;
    if(!globalFromJson(json.value(QStringLiteral("global")),parsed.global))return false;
    parsed.layers.reserve(layerArray.size());
    for(const QJsonValue &layer:layerArray){
        AdjustmentLayer entry;
        if(!layerFromJson(layer,entry))return false;
        parsed.layers.append(entry);
    }
    QString reason;
    if(!validDocumentBody(parsed,requireSourceIdentity,reason))return false;
    document=parsed;
    return true;
}

}

bool LocalEdit::validDocument(const PhotoDocument &document,QString &error,bool requireSourceIdentity){
    error.clear();
    QString reason;
    if(!validDocumentBody(document,requireSourceIdentity,reason))return fail(error,reason);
    return true;
}

QJsonObject LocalEdit::documentJson(const PhotoDocument &document,QString &error){
    error.clear();
    QString reason;
    if(!validDocument(document,reason,false)){
        error=QStringLiteral("cannot serialize an invalid document: %1").arg(reason);
        return {};
    }
    QJsonArray layers;
    for(const AdjustmentLayer &layer:document.layers)layers.append(layerJson(layer));
    return QJsonObject{{QStringLiteral("format"),QStringLiteral("LuminaS.photo-document")},
        {QStringLiteral("formatVersion"),documentFormatVersion},
        {QStringLiteral("algorithmVersion"),document.algorithmVersion},
        {QStringLiteral("revision"),QString::number(document.revision)},
        {QStringLiteral("localIdentity"),QString::number(document.localIdentity)},
        {QStringLiteral("asset"),assetJson(document.asset)},
        {QStringLiteral("global"),recipeJson(document.global)},
        {QStringLiteral("layers"),layers}};
}

bool LocalEdit::documentFromJson(const QJsonObject &json,PhotoDocument &document,QString &error,bool requireSourceIdentity){
    error.clear();
    PhotoDocument parsed;
    if(!documentFromJsonBody(json,parsed,requireSourceIdentity))
        return fail(error,QStringLiteral("invalid photo document JSON"));
    document=parsed;
    return true;
}

bool LocalEdit::documentFromLegacyRecipe(const AssetIdentity &asset,const QJsonObject &recipe,PhotoDocument &document,QString &error){
    error.clear();
    QString reason;
    if(!assetShapeValid(asset,false,reason))
        return fail(error,QStringLiteral("invalid asset identity: %1").arg(reason));
    Recipe global;
    if(!globalFromJson(recipe,global))
        return fail(error,QStringLiteral("invalid legacy recipe object"));
    PhotoDocument candidate;
    candidate.asset=asset;
    candidate.global=global;
    candidate.layers.clear();
    candidate.revision=1;
    candidate.localIdentity=0;
    candidate.algorithmVersion=localAlgorithmVersion;
    if(!validDocumentBody(candidate,false,reason)){
        error=QStringLiteral("invalid legacy recipe document: %1").arg(reason);
        return false;
    }
    document=candidate;
    return true;
}
