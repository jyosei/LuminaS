#pragma once
#include "document.h"
#include <variant>
#include <memory>

namespace LocalEdit {
constexpr int documentFormatVersion=1;
constexpr int localAlgorithmVersion=1;
constexpr int maximumLayers=64;
constexpr int maximumComponentsPerLayer=4096;
constexpr int maximumPointsPerStroke=262144;
constexpr int maximumPointsPerDocument=1048576;

struct AssetIdentity {
    QString id,sourcePath,sourceHash;
    QSize normalizedSize{0,0}; // unknown until source preparation
    bool raw=false;
    QJsonObject sourceFacts;
    bool operator==(const AssetIdentity &) const = default;
};
struct LocalParameters {
    double exposure=0,contrast=0,highlights=0,shadows=0,whites=0,blacks=0;
    double warmth=0,tint=0,saturation=1;
    bool operator==(const LocalParameters &) const = default;
};
enum class MaskOperation {Add,Subtract};
struct BrushGeometry {
    QVector<QPointF> points;
    double radius=100,hardness=.6;
    bool operator==(const BrushGeometry &) const = default;
};
struct LinearGeometry {
    QPointF start,end{1,0};
    bool operator==(const LinearGeometry &) const = default;
};
struct RadialGeometry {
    QPointF center;
    double radiusX=100,radiusY=100,angleDegrees=0;
    bool operator==(const RadialGeometry &) const = default;
};
using MaskGeometry=std::variant<BrushGeometry,LinearGeometry,RadialGeometry>;
struct MaskComponent {
    QString id;
    MaskOperation operation=MaskOperation::Add;
    double strength=1;
    MaskGeometry geometry;
    bool operator==(const MaskComponent &) const = default;
};
struct Mask {
    QVector<MaskComponent> components;
    double feather=0;
    bool invert=false;
    quint64 version=1;
    bool operator==(const Mask &) const = default;
};
struct AdjustmentLayer {
    QString id,name;
    bool enabled=true;
    double opacity=1;
    LocalParameters parameters;
    Mask mask;
    quint64 version=1;
    bool operator==(const AdjustmentLayer &) const = default;
};
struct PhotoDocument {
    AssetIdentity asset;
    Recipe global;
    QVector<AdjustmentLayer> layers; // bottom to top; global is outside this list
    quint64 revision=1,localIdentity=0;
    int algorithmVersion=localAlgorithmVersion;
    bool operator==(const PhotoDocument &) const = default;
};
using DocumentSnapshot=std::shared_ptr<const PhotoDocument>;

bool validDocument(const PhotoDocument &document,QString &error,bool requireSourceIdentity=false);
QJsonObject documentJson(const PhotoDocument &document,QString &error);
bool documentFromJson(const QJsonObject &json,PhotoDocument &document,QString &error,bool requireSourceIdentity=false);
bool documentFromLegacyRecipe(const AssetIdentity &asset,const QJsonObject &recipe,PhotoDocument &document,QString &error);
}
