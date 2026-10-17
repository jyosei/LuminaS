#include "io/source_metadata_codec.h"

#include <QJsonDocument>
#include <QJsonValue>
#include <QStringList>
#include <QtTest>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace {

struct StringField {
    QString SourceMetadata::*member;
    QString key;
};

const StringField kStringFields[] = {
    {&SourceMetadata::make, QStringLiteral("make")},
    {&SourceMetadata::model, QStringLiteral("model")},
    {&SourceMetadata::artist, QStringLiteral("artist")},
    {&SourceMetadata::copyright, QStringLiteral("copyright")},
    {&SourceMetadata::date, QStringLiteral("date")},
    {&SourceMetadata::lens, QStringLiteral("lens")},
};

const QStringList kNumberKeys = {
    QStringLiteral("exposure"), QStringLiteral("aperture"), QStringLiteral("focalLength"),
    QStringLiteral("iso"), QStringLiteral("latitude"), QStringLiteral("longitude"),
    QStringLiteral("altitude"),
};

SourceMetadata populatedGpsMetadata()
{
    SourceMetadata metadata;
    metadata.make = QStringLiteral("Lumna Imaging");
    metadata.model = QStringLiteral("Model X 100");
    metadata.artist = QString::fromUtf8("J\u00e9r\u00f4me \u65e5\u672c\U0001F4F7");
    metadata.copyright = QString::fromUtf8("\u00a9 2026 \u4f5c\u8005");
    metadata.date = QStringLiteral("2026:07:04 18:55:12");
    metadata.lens = QStringLiteral("RF 24-70mm F2.8 L IS USM");
    metadata.exposure = 0.008;
    metadata.aperture = 2.8;
    metadata.focalLength = 24.0;
    metadata.iso = 400;
    metadata.hasGps = true;
    metadata.latitude = 48.858370;
    metadata.longitude = 2.294481;
    metadata.altitude = -12.5;
    return metadata;
}

SourceMetadata sentinelMetadata()
{
    SourceMetadata metadata = populatedGpsMetadata();
    metadata.iso = 9876;
    metadata.artist = QStringLiteral("sentinel");
    metadata.latitude = 12.5;
    return metadata;
}

bool roundTrip(const SourceMetadata &input, SourceMetadata &output, QString &error)
{
    const QJsonObject json = sourceMetadataJson(input, error);
    if (!error.isEmpty()) {
        return false;
    }
    return sourceMetadataFromJson(json, output, error);
}

} // namespace

class SourceMetadataCodecTests : public QObject
{
    Q_OBJECT

private slots:
    void defaultAndPopulatedRoundTrip();
    void hasGpsFalseRequiresZeroCoordinates();
    void zeroCoordinatesArePreservedWhenHasGpsTrue();
    void coordinateBoundariesAccepted();
    void stringLengthBoundary();
    void canonicalJsonShape();
    void malformedInputsRejected_data();
    void malformedInputsRejected();
    void typedNonFiniteValuesRejected();
    void parsingFailurePreservesOutput();
};

void SourceMetadataCodecTests::defaultAndPopulatedRoundTrip()
{
    for (const SourceMetadata &input : {SourceMetadata{}, populatedGpsMetadata()}) {
        QString error = QStringLiteral("stale");
        SourceMetadata output;
        output.iso = 12345;
        QVERIFY(roundTrip(input, output, error));
        QVERIFY(error.isEmpty());
        QCOMPARE(output, input);
    }
}

void SourceMetadataCodecTests::hasGpsFalseRequiresZeroCoordinates()
{
    SourceMetadata input = populatedGpsMetadata();
    input.hasGps = false;
    input.latitude = 0.0;
    input.longitude = 0.0;
    input.altitude = 0.0;
    QString error;
    QVERIFY(validSourceMetadata(input, error));
    QVERIFY(error.isEmpty());


    for (int leak = 0; leak < 3; ++leak) {
        SourceMetadata leaked = input;
        if (leak == 0) {
            leaked.latitude = 1e-9;
        } else if (leak == 1) {
            leaked.longitude = -1.0;
        } else {
            leaked.altitude = 250.0;
        }
        QString rejected;
        QVERIFY2(!validSourceMetadata(leaked, rejected), "hidden GPS must be rejected");
        QVERIFY(!rejected.isEmpty());

        QString jsonError;
        QVERIFY(sourceMetadataJson(leaked, jsonError).isEmpty());
        QVERIFY(!jsonError.isEmpty());

        QString parseError;
        SourceMetadata untouched = sentinelMetadata();
        QVERIFY(!roundTrip(leaked, untouched, parseError));
        QVERIFY(!parseError.isEmpty());
        QCOMPARE(untouched, sentinelMetadata());
    }
}

void SourceMetadataCodecTests::zeroCoordinatesArePreservedWhenHasGpsTrue()
{
    SourceMetadata input = populatedGpsMetadata();
    input.latitude = 0.0;
    input.longitude = 0.0;
    input.altitude = 0.0;
    QString error;
    QVERIFY(validSourceMetadata(input, error));
    QVERIFY(error.isEmpty());

    const QJsonObject json = sourceMetadataJson(input, error);
    QVERIFY(error.isEmpty());
    QVERIFY(json.value(QStringLiteral("hasGps")).toBool());

    SourceMetadata output;
    QVERIFY(sourceMetadataFromJson(json, output, error));
    QVERIFY(error.isEmpty());
    QCOMPARE(output, input);
    QVERIFY(output.hasGps);
}

void SourceMetadataCodecTests::coordinateBoundariesAccepted()
{
    const std::pair<double, double> extremes[] = {
        {90.0, 180.0}, {-90.0, -180.0}, {89.9999999, -179.99999}, {0.0, 0.0},
    };
    for (const auto &[latitude, longitude] : extremes) {
        SourceMetadata input = populatedGpsMetadata();
        input.latitude = latitude;
        input.longitude = longitude;
        input.altitude = -420.75; // Below sea level is legitimate.
        QString error;
        QVERIFY2(validSourceMetadata(input, error), qPrintable(error));
        SourceMetadata output;
        QVERIFY2(roundTrip(input, output, error), qPrintable(error));
        QCOMPARE(output, input);
    }

    SourceMetadata zeroExposure = populatedGpsMetadata();
    zeroExposure.exposure = 0.0;
    zeroExposure.altitude = -9999.0;
    QString error;
    QVERIFY2(validSourceMetadata(zeroExposure, error), qPrintable(error));
    SourceMetadata output;
    QVERIFY2(roundTrip(zeroExposure, output, error), qPrintable(error));
    QCOMPARE(output.exposure, 0.0);
    QCOMPARE(output, zeroExposure);
}

void SourceMetadataCodecTests::stringLengthBoundary()
{
    for (const StringField &field : kStringFields) {
        SourceMetadata atLimit = populatedGpsMetadata();
        atLimit.*field.member = QString(2048, QChar('x'));
        QString error;
        QVERIFY2(validSourceMetadata(atLimit, error), qPrintable(error));
        SourceMetadata output;
        QVERIFY2(roundTrip(atLimit, output, error), qPrintable(error));
        QCOMPARE(output, atLimit);

        SourceMetadata overLimit = atLimit;
        overLimit.*field.member += QChar('y');
        QString overError;
        QVERIFY2(!validSourceMetadata(overLimit, overError), qPrintable(field.key));
        QVERIFY(!overError.isEmpty());

        SourceMetadata untouched = sentinelMetadata();
        QVERIFY2(!roundTrip(overLimit, untouched, overError), qPrintable(field.key));
        QCOMPARE(untouched, sentinelMetadata());

        SourceMetadata withNul = populatedGpsMetadata();
        withNul.*field.member = QStringLiteral("a\u0000b");
        QString nulError;
        QVERIFY2(!validSourceMetadata(withNul, nulError), qPrintable(field.key));
        QVERIFY(!nulError.isEmpty());
    }
}

void SourceMetadataCodecTests::canonicalJsonShape()
{
    const SourceMetadata input = populatedGpsMetadata();
    QString error;
    const QJsonObject json = sourceMetadataJson(input, error);
    QVERIFY(error.isEmpty());

    QCOMPARE(json.size(), 15);
    for (const StringField &field : kStringFields) {
        QCOMPARE(json.value(field.key).type(), QJsonValue::String);
        QCOMPARE(json.value(field.key).toString(), input.*field.member);
    }
    for (const QString &key : kNumberKeys) {
        QCOMPARE(json.value(key).type(), QJsonValue::Double);
    }
    QCOMPARE(json.value(QStringLiteral("hasGps")).type(), QJsonValue::Bool);
    QCOMPARE(json.value(QStringLiteral("schema")).type(), QJsonValue::Double);
    QCOMPARE(json.value(QStringLiteral("schema")).toDouble(), 1.0);
    QCOMPARE(json.value(QStringLiteral("iso")).toDouble(), 400.0);
    QCOMPARE(json.value(QStringLiteral("altitude")).toDouble(), -12.5);
    QCOMPARE(json.value(QStringLiteral("hasGps")).toBool(), true);

    for (const QString &key : json.keys()) {
        const bool known = key == QStringLiteral("schema") || key == QStringLiteral("hasGps")
            || kNumberKeys.contains(key)
            || std::any_of(std::begin(kStringFields), std::end(kStringFields),
                           [&key](const StringField &field) { return field.key == key; });
        QVERIFY2(known, qPrintable(key));
    }

    const QJsonDocument document(json);
    const QByteArray text = document.toJson(QJsonDocument::Compact);
    QJsonParseError parsed;
    const QJsonObject reparsed = QJsonDocument::fromJson(text, &parsed).object();
    QCOMPARE(parsed.error, QJsonParseError::NoError);
    SourceMetadata output;
    QVERIFY(sourceMetadataFromJson(reparsed, output, error));
    QCOMPARE(output, input);
}

void SourceMetadataCodecTests::malformedInputsRejected_data()
{
    QTest::addColumn<QJsonObject>("json");

    const QString schema = QStringLiteral("schema");
    const QString make = QStringLiteral("make");
    const QString iso = QStringLiteral("iso");
    const QString hasGps = QStringLiteral("hasGps");
    const QString latitude = QStringLiteral("latitude");
    const QString longitude = QStringLiteral("longitude");
    const QString altitude = QStringLiteral("altitude");
    const QString exposure = QStringLiteral("exposure");

    QString error;
    const QJsonObject reference = sourceMetadataJson(populatedGpsMetadata(), error);
    Q_ASSERT(error.isEmpty());

    const auto variant = [&reference](const QString &key, const QJsonValue &value) {
        QJsonObject copy = reference;
        copy.insert(key, value);
        return copy;
    };
    const auto without = [&reference](const QString &key) {
        QJsonObject copy = reference;
        copy.remove(key);
        return copy;
    };

    QTest::newRow("reference-canonical-ok") << reference;
    QTest::newRow("schema-plain-int-1-ok") << variant(schema, 1);
    QTest::newRow("missing-artist") << without(QStringLiteral("artist"));
    QTest::newRow("missing-exposure") << without(exposure);
    QTest::newRow("missing-hasGps") << without(hasGps);
    QTest::newRow("missing-altitude") << without(altitude);
    QTest::newRow("unknown-key") << variant(QStringLiteral("makerNote"), QStringLiteral("blob"));
    QTest::newRow("schema-string") << variant(schema, QStringLiteral("1"));
    QTest::newRow("schema-version-2") << variant(schema, 2);
    QTest::newRow("iso-1.5") << variant(iso, 1.5);
    QTest::newRow("iso-negative") << variant(iso, -1.0);
    QTest::newRow("iso-over-max") << variant(iso, 1000000.001);
    QTest::newRow("iso-bool") << variant(iso, true);
    QTest::newRow("make-number") << variant(make, 5);
    QTest::newRow("hasGps-string") << variant(hasGps, QStringLiteral("true"));
    QTest::newRow("hasGps-number") << variant(hasGps, 1);
    QTest::newRow("exposure-string") << variant(exposure, QStringLiteral("0.5"));
    QTest::newRow("exposure-null") << variant(exposure, QJsonValue::Null);
    QTest::newRow("exposure-negative") << variant(exposure, -0.25);
    QTest::newRow("latitude-over") << variant(latitude, 90.0001);
    QTest::newRow("latitude-under") << variant(latitude, -90.0001);
    QTest::newRow("longitude-over") << variant(longitude, 180.5);
    QTest::newRow("longitude-under") << variant(longitude, -180.5);
    QTest::newRow("gps-false-nonzero-coords") << variant(hasGps, false);
    QJsonObject zeroed = variant(hasGps, false);
    zeroed.insert(latitude, 0.0);
    zeroed.insert(longitude, 0.0);
    QTest::newRow("gps-false-nonzero-altitude") << zeroed;
    QJsonObject gpsFalseZero = zeroed;
    gpsFalseZero.insert(altitude, 0.0);
    QTest::newRow("gps-false-zero-coords-ok") << gpsFalseZero;
    QTest::newRow("string-nul") << variant(make, QStringLiteral("Canon\u0000EOS"));
    QTest::newRow("string-2049") << variant(QStringLiteral("lens"), QString(2049, QChar('z')));
    QJsonObject extraString;
    extraString.insert(QStringLiteral("extra"), QString());
    QTest::newRow("unknown-key-empty-string") << extraString;
}

void SourceMetadataCodecTests::malformedInputsRejected()
{
    QFETCH(QJsonObject, json);
    const QString name = QTest::currentDataTag();

    const bool isAccepted = name.endsWith(QStringLiteral("-ok"));
    const SourceMetadata before = sentinelMetadata();
    SourceMetadata output = before;
    QString error = QStringLiteral("stale");
    const bool accepted = sourceMetadataFromJson(json, output, error);

    if (isAccepted) {
        QVERIFY2(accepted, qPrintable(error));
        QVERIFY(error.isEmpty());
        return;
    }
    QVERIFY2(!accepted, qPrintable(name));
    QVERIFY2(!error.isEmpty(), qPrintable(name));
    QCOMPARE(output, before);
}

void SourceMetadataCodecTests::typedNonFiniteValuesRejected()
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    const double negativeInf = -std::numeric_limits<double>::infinity();

    for (double value : {nan, inf, negativeInf}) {
        const auto withValue = [value](auto setter) {
            SourceMetadata metadata = populatedGpsMetadata();
            setter(metadata, value);
            return metadata;
        };
        const SourceMetadata variants[] = {
            withValue([](SourceMetadata &m, double v) { m.exposure = v; }),
            withValue([](SourceMetadata &m, double v) { m.aperture = v; }),
            withValue([](SourceMetadata &m, double v) { m.focalLength = v; }),
            withValue([](SourceMetadata &m, double v) { m.latitude = v; }),
            withValue([](SourceMetadata &m, double v) { m.longitude = v; }),
            withValue([](SourceMetadata &m, double v) { m.altitude = v; }),
        };
        for (const SourceMetadata &metadata : variants) {
            QString error;
            QVERIFY(!validSourceMetadata(metadata, error));
            QVERIFY(!error.isEmpty());
            QString jsonError;
            QVERIFY(sourceMetadataJson(metadata, jsonError).isEmpty());
            QVERIFY(!jsonError.isEmpty());
            SourceMetadata untouched = sentinelMetadata();
            QVERIFY(!roundTrip(metadata, untouched, jsonError));
            QCOMPARE(untouched, sentinelMetadata());
        }
    }

    SourceMetadata invalidIso = populatedGpsMetadata();
    invalidIso.iso = 1000001;
    QString error;
    QVERIFY(!validSourceMetadata(invalidIso, error));
    invalidIso.iso = -5;
    QVERIFY(!validSourceMetadata(invalidIso, error));

    SourceMetadata maxIso = populatedGpsMetadata();
    maxIso.iso = 1000000;
    QVERIFY2(validSourceMetadata(maxIso, error), qPrintable(error));
}

void SourceMetadataCodecTests::parsingFailurePreservesOutput()
{
    QString error;
    const QJsonObject json = sourceMetadataJson(populatedGpsMetadata(), error);
    QVERIFY(error.isEmpty());

    const auto variant = [&json](const QString &key, const QJsonValue &value) {
        QJsonObject copy = json;
        copy.insert(key, value);
        return copy;
    };

    QJsonObject withNaNField = json;
    withNaNField.insert(QStringLiteral("altitude"), std::numeric_limits<double>::quiet_NaN());
    QJsonObject withInfField = json;
    withInfField.insert(QStringLiteral("longitude"), std::numeric_limits<double>::infinity());
    QJsonObject withNegativeAperture;
    withNegativeAperture = variant(QStringLiteral("aperture"), -2.0);
    QJsonObject withLongMake = variant(QStringLiteral("make"), QString(3000, QChar('a')));
    QJsonObject withHiddenGps = variant(QStringLiteral("hasGps"), false);
    QJsonObject withNulLens = variant(QStringLiteral("lens"), QStringLiteral("a\u0000b"));
    QJsonObject missingDate = json;
    missingDate.remove(QStringLiteral("date"));

    const QJsonObject brokenInputs[] = {
        withNaNField, withInfField, withNegativeAperture, withLongMake,
        withHiddenGps, withNulLens, missingDate,
    };
    for (const QJsonObject &broken : brokenInputs) {
        SourceMetadata output = sentinelMetadata();
        QString parseError;
        QVERIFY(!sourceMetadataFromJson(broken, output, parseError));
        QVERIFY2(!parseError.isEmpty(), qPrintable(QString::fromUtf8(QJsonDocument(broken).toJson(QJsonDocument::Compact))));
        QCOMPARE(output, sentinelMetadata());
    }
}

QTEST_GUILESS_MAIN(SourceMetadataCodecTests)
#include "source_metadata_codec_tests.moc"
