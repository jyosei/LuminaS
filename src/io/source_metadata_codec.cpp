#include "io/source_metadata_codec.h"

#include <QJsonValue>
#include <QStringList>

#include <cmath>
#include <utility>

namespace {

constexpr int kSchemaVersion = 1;
constexpr int kMaxStringLength = 2048;
constexpr int kMaxIso = 1000000;

const QString kSchemaKey = QStringLiteral("schema");
const QString kMakeKey = QStringLiteral("make");
const QString kModelKey = QStringLiteral("model");
const QString kArtistKey = QStringLiteral("artist");
const QString kCopyrightKey = QStringLiteral("copyright");
const QString kDateKey = QStringLiteral("date");
const QString kLensKey = QStringLiteral("lens");
const QString kExposureKey = QStringLiteral("exposure");
const QString kApertureKey = QStringLiteral("aperture");
const QString kFocalLengthKey = QStringLiteral("focalLength");
const QString kIsoKey = QStringLiteral("iso");
const QString kHasGpsKey = QStringLiteral("hasGps");
const QString kLatitudeKey = QStringLiteral("latitude");
const QString kLongitudeKey = QStringLiteral("longitude");
const QString kAltitudeKey = QStringLiteral("altitude");

QStringList sourceMetadataStringKeys()
{
    return {kMakeKey, kModelKey, kArtistKey, kCopyrightKey, kDateKey, kLensKey};
}

QStringList sourceMetadataNumberKeys()
{
    return {kExposureKey, kApertureKey, kFocalLengthKey, kIsoKey,
            kLatitudeKey, kLongitudeKey, kAltitudeKey};
}

QStringList sourceMetadataAllKeys()
{
    QStringList keys;
    keys << kSchemaKey;
    keys << sourceMetadataStringKeys();
    keys << sourceMetadataNumberKeys();
    keys << kHasGpsKey;
    return keys;
}

bool finiteNonNegative(double value)
{
    return std::isfinite(value) && value >= 0.0;
}

bool validMetadataString(const QString &value, const QString &key, QString &error)
{
    if (value.size() > kMaxStringLength) {
        error = QStringLiteral("%1 exceeds %2 UTF-16 code units").arg(key).arg(kMaxStringLength);
        return false;
    }
    for (const QChar unit : value) {
        if (unit.unicode() == 0) {
            error = QStringLiteral("%1 contains a NUL character").arg(key);
            return false;
        }
    }
    return true;
}

// Validates the typed model: bounded strings, finite numbers, integer ISO,
// coordinate ranges and hidden-GPS leakage. Reused before serialization and
// after parsing.
bool validateTyped(const SourceMetadata &metadata, QString &error)
{
    const std::pair<const QString, const QString *> strings[] = {
        {kMakeKey, &metadata.make},
        {kModelKey, &metadata.model},
        {kArtistKey, &metadata.artist},
        {kCopyrightKey, &metadata.copyright},
        {kDateKey, &metadata.date},
        {kLensKey, &metadata.lens},
    };
    for (const auto &[key, value] : strings) {
        if (!validMetadataString(*value, key, error)) {
            return false;
        }
    }

    const std::pair<const QString, double> nonNegativeNumbers[] = {
        {kExposureKey, metadata.exposure},
        {kApertureKey, metadata.aperture},
        {kFocalLengthKey, metadata.focalLength},
    };
    for (const auto &[key, value] : nonNegativeNumbers) {
        if (!finiteNonNegative(value)) {
            error = QStringLiteral("%1 must be a finite non-negative number").arg(key);
            return false;
        }
    }

    if (metadata.iso < 0 || metadata.iso > kMaxIso) {
        error = QStringLiteral("iso must be an integer in [0, %1]").arg(kMaxIso);
        return false;
    }
    if (!std::isfinite(metadata.latitude) || metadata.latitude < -90.0 || metadata.latitude > 90.0) {
        error = QStringLiteral("latitude must be finite and in [-90, 90]");
        return false;
    }
    if (!std::isfinite(metadata.longitude) || metadata.longitude < -180.0 || metadata.longitude > 180.0) {
        error = QStringLiteral("longitude must be finite and in [-180, 180]");
        return false;
    }
    if (!std::isfinite(metadata.altitude)) {
        error = QStringLiteral("altitude must be finite");
        return false;
    }
    if (!metadata.hasGps
        && (metadata.latitude != 0.0 || metadata.longitude != 0.0 || metadata.altitude != 0.0)) {
        error = QStringLiteral("GPS coordinates must be zero when hasGps is false");
        return false;
    }
    return true;
}

// Strict JSON number: values must be JSON doubles that are finite. Booleans,
// strings, JSON null and non-numeric values are rejected.
bool jsonFiniteNumber(const QJsonValue &value, double &out)
{
    if (!value.isDouble()) {
        return false;
    }
    const double number = value.toDouble();
    if (!std::isfinite(number)) {
        return false;
    }
    out = number;
    return true;
}

bool parseInt(const QJsonObject &json, QString &error, int &out)
{
    double iso = 0.0;
    if (!jsonFiniteNumber(json.value(kIsoKey), iso)) {
        error = QStringLiteral("iso must be a finite JSON number");
        return false;
    }
    if (iso < 0.0 || iso > static_cast<double>(kMaxIso) || std::floor(iso) != iso) {
        error = QStringLiteral("iso must be an integer in [0, %1]").arg(kMaxIso);
        return false;
    }
    out = static_cast<int>(iso);
    return true;
}

} // namespace

bool validSourceMetadata(const SourceMetadata &metadata, QString &error)
{
    QString details;
    const bool valid = validateTyped(metadata, details);
    if (valid) {
        error.clear();
    } else {
        error = details;
    }
    return valid;
}

QJsonObject sourceMetadataJson(const SourceMetadata &metadata, QString &error)
{
    QString details;
    if (!validateTyped(metadata, details)) {
        error = details;
        return {};
    }
    QJsonObject json;
    json.insert(kSchemaKey, kSchemaVersion);
    json.insert(kMakeKey, metadata.make);
    json.insert(kModelKey, metadata.model);
    json.insert(kArtistKey, metadata.artist);
    json.insert(kCopyrightKey, metadata.copyright);
    json.insert(kDateKey, metadata.date);
    json.insert(kLensKey, metadata.lens);
    json.insert(kExposureKey, metadata.exposure);
    json.insert(kApertureKey, metadata.aperture);
    json.insert(kFocalLengthKey, metadata.focalLength);
    json.insert(kIsoKey, metadata.iso);
    json.insert(kHasGpsKey, metadata.hasGps);
    json.insert(kLatitudeKey, metadata.latitude);
    json.insert(kLongitudeKey, metadata.longitude);
    json.insert(kAltitudeKey, metadata.altitude);
    error.clear();
    return json;
}

bool sourceMetadataFromJson(const QJsonObject &json, SourceMetadata &metadata, QString &error)
{
    const QJsonValue schema = json.value(kSchemaKey);
    if (!schema.isDouble() || schema.toDouble() != static_cast<double>(kSchemaVersion)) {
        error = QStringLiteral("schema must be the integer %1").arg(kSchemaVersion);
        return false;
    }

    const QStringList expected = sourceMetadataAllKeys();
    for (auto it = json.constBegin(); it != json.constEnd(); ++it) {
        if (!expected.contains(it.key())) {
            error = QStringLiteral("unknown key: %1").arg(it.key());
            return false;
        }
    }
    if (json.size() != expected.size()) {
        error = QStringLiteral("missing fields: expected exactly %1 keys").arg(expected.size());
        return false;
    }

    SourceMetadata candidate;
    const QStringList stringKeys = sourceMetadataStringKeys();
    QString *const stringMembers[] = {
        &candidate.make, &candidate.model, &candidate.artist,
        &candidate.copyright, &candidate.date, &candidate.lens,
    };
    for (int index = 0; index < stringKeys.size(); ++index) {
        const QString &key = stringKeys.at(index);
        const QJsonValue value = json.value(key);
        if (!value.isString()) {
            error = QStringLiteral("%1 must be a string").arg(key);
            return false;
        }
        const QString text = value.toString();
        if (!validMetadataString(text, key, error)) {
            return false;
        }
        *stringMembers[index] = text;
    }

    const std::pair<const QString, double *> nonNegativeNumbers[] = {
        {kExposureKey, &candidate.exposure},
        {kApertureKey, &candidate.aperture},
        {kFocalLengthKey, &candidate.focalLength},
    };
    for (const auto &[key, target] : nonNegativeNumbers) {
        if (!jsonFiniteNumber(json.value(key), *target) || *target < 0.0) {
            error = QStringLiteral("%1 must be a finite non-negative JSON number").arg(key);
            return false;
        }
    }

    if (!parseInt(json, error, candidate.iso)) {
        return false;
    }

    const QJsonValue hasGps = json.value(kHasGpsKey);
    if (!hasGps.isBool()) {
        error = QStringLiteral("hasGps must be a boolean");
        return false;
    }
    candidate.hasGps = hasGps.toBool();

    const std::pair<const QString, double *> coordinates[] = {
        {kLatitudeKey, &candidate.latitude},
        {kLongitudeKey, &candidate.longitude},
        {kAltitudeKey, &candidate.altitude},
    };
    for (const auto &[key, target] : coordinates) {
        if (!jsonFiniteNumber(json.value(key), *target)) {
            error = QStringLiteral("%1 must be a finite JSON number").arg(key);
            return false;
        }
    }

    if (!validateTyped(candidate, error)) {
        return false;
    }

    metadata = candidate;
    error.clear();
    return true;
}
