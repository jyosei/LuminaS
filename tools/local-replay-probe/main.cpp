// LuminaS local-replay-probe: standalone bounded verification probe.
// Reports exactly one JSON object on stdout. Writes no files.
#include "core/document.h"
#include "core/photo_document.h"
#include "io/source_reader.h"
#include "render/cpu_renderer.h"
#include "render/geometry_transform.h"
#include "render/local_reference.h"
#include "render/local_renderer.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QByteArrayView>
#include <QSet>
#include <QString>
#include <QTextStream>

#include <bit>
#include <cmath>
#include <algorithm>

#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#endif

using namespace LocalEdit;

namespace {

constexpr double kToleranceAbsolute = 2e-6;
constexpr double kToleranceRelative = 2e-6;

QString sha256File(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) return {};
    return QString::fromLatin1(hash.result().toHex());
}

QString sha256Image(const QImage &image) {
    if (image.isNull()) return {};
    return QString::fromLatin1(QCryptographicHash::hash(
        QByteArrayView(reinterpret_cast<const char *>(image.constBits()), image.sizeInBytes()),
        QCryptographicHash::Sha256).toHex());
}

QJsonObject fail(const QString &message) {
    QJsonObject report;
    report.insert(QStringLiteral("ok"), false);
    report.insert(QStringLiteral("error"), message);
    return report;
}

void emitReport(const QJsonObject &report) {
    QTextStream stream(stdout);
    stream << QString::fromUtf8(QJsonDocument(report).toJson(QJsonDocument::Compact)) << Qt::endl;
}

// 9000x5000 straight RGBA32FPx4, alpha 1, constant RGB (.18,.25,.4).
QImage syntheticFrame() {
    QImage image(9000, 5000, QImage::Format_RGBA32FPx4);
    if (image.isNull()) return image;
    for (int y = 0; y < image.height(); ++y) {
        float *row = reinterpret_cast<float *>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            float *pixel = row + 4 * x;
            pixel[0] = .18f; pixel[1] = .25f; pixel[2] = .4f; pixel[3] = 1.f;
        }
    }
    return image;
}

struct EdgeReport {
    int edge = 0;
    int width = 0, height = 0;
    QString format;
    QString baseSha, actualSha;
    double baseMs = 0, actualMs = 0;
    int sampleCount = 0, mismatchCount = 0, changedSamples = 0, maskPositiveSamples = 0;
    double maxAbsError = 0, elapsedMs = 0;
    bool alphaExact = true;
    QString error;
};

// GPT-specified fixed 17x17 numeric check against the scalar reference path.
bool verifyEdge(const QImage &base, const QImage &actual, const PhotoDocument &document,
                const AdjustmentLayer &layer, QSize originalSize, EdgeReport &report) {
    if (base.isNull() || actual.isNull()) { report.error = QStringLiteral("null image"); return false; }
    if (base.size() != actual.size() || base.format() != QImage::Format_RGBA32FPx4 || actual.format() != QImage::Format_RGBA32FPx4) {
        report.error = QStringLiteral("base/actual shape mismatch"); return false;
    }
    const BrushMaskReference reference(layer.mask);
    if (!reference.valid()) { report.error = reference.error(); return false; }
    const LocalPixelTransform transform(layer.parameters);
    if (!transform.valid()) { report.error = QStringLiteral("invalid local transform"); return false; }

    const int w = base.width(), h = base.height();
    QSet<qint64> visited; // tiny frames clamp several grid points onto one pixel
    for (int iy = 0; iy < 17; ++iy) {
        for (int ix = 0; ix < 17; ++ix) {
            const int x = std::min(w - 1, int(std::floor((ix + .5) * w / 17.0)));
            const int y = std::min(h - 1, int(std::floor((iy + .5) * h / 17.0)));
            if (visited.contains(qint64(y) * w + x)) continue;
            visited.insert(qint64(y) * w + x);
            const QPointF sourcePoint = outputToSource(
                QPointF((x + .5) / double(w), (y + .5) / double(h)), originalSize, document.global);
            const float *basePixel = reinterpret_cast<const float *>(base.constScanLine(y)) + 4 * x;
            const float mask = reference.sample(sourcePoint, basePixel[3]);
            float expected[4] = {basePixel[0], basePixel[1], basePixel[2], basePixel[3]};
            if (!transform.apply(expected, mask, float(layer.opacity))) {
                report.error = QStringLiteral("transform rejected a sampled pixel"); return false;
            }
            const float *actualPixel = reinterpret_cast<const float *>(actual.constScanLine(y)) + 4 * x;
            ++report.sampleCount;
            if (mask > 0) ++report.maskPositiveSamples;
            bool changed = false;
            for (int c = 0; c < 3; ++c) {
                if(!std::isfinite(actualPixel[c])||!std::isfinite(expected[c])){report.error="Non-finite sampled RGB";return false;}
                const double difference = std::abs(double(actualPixel[c]) - double(expected[c]));
                const double tolerance = kToleranceAbsolute + kToleranceRelative * std::abs(double(expected[c]));
                report.maxAbsError = std::max(report.maxAbsError, difference);
                if (difference > tolerance) ++report.mismatchCount;
            }
            for (int c = 0; c < 3; ++c)
                if (std::bit_cast<quint32>(actualPixel[c]) != std::bit_cast<quint32>(basePixel[c])) changed = true;
            if (changed) ++report.changedSamples;
            if (std::bit_cast<quint32>(actualPixel[3]) != std::bit_cast<quint32>(expected[3])) report.alphaExact = false;
        }
    }
    return true;
}

bool runCase(const SourceFrame &frame, const PhotoDocument &document, QJsonObject &out) {
    const QSize originalSize = frame.originalSize;
    QJsonArray edges;
    bool allPass = true;
    for (int edge : {3840, 0}) {
        EdgeReport report;
        report.edge = edge;
        QElapsedTimer timer; timer.start();
        QString error;
        // Base first, then actual; no other case/edge frame is retained.
        const QImage base = renderLinear(frame, document.global, edge, error);
        if (base.isNull()) { report.error = error.isEmpty() ? QStringLiteral("base render failed") : error; out.insert(QStringLiteral("error"), report.error); return false; }
        report.baseMs = timer.restart();
        const QImage actual = edge == 0
            ? renderDocumentFullLinear(frame, document, error)
            : renderDocumentPreviewLinear(frame, document, document.global, edge, error);
        if (actual.isNull()) { report.error = error.isEmpty() ? QStringLiteral("document render failed") : error; out.insert(QStringLiteral("error"), report.error); return false; }
        report.actualMs = timer.restart();

        report.width = actual.width();
        report.height = actual.height();
        report.format = QString::number(int(actual.format()));
        report.baseSha = sha256Image(base);
        report.actualSha = sha256Image(actual);

        const QSize expectedSize = renderedSize(originalSize, document.global);
        const int fullLongEdge = std::max(expectedSize.width(), expectedSize.height());
        const int actualLongEdge = std::max(actual.width(), actual.height());
        // Full edge must equal renderedSize. Preview edge must equal it when the source
        // already fits within the bound (never upscaled), otherwise be exactly the bound.
        const bool sizeOk = edge == 0
            ? actual.size() == expectedSize
            : (fullLongEdge <= edge ? actual.size() == expectedSize
                                    : (actual.size() != expectedSize && actualLongEdge == edge));

        bool edgePass = sizeOk;
        for (const AdjustmentLayer &layer : document.layers) {
            EdgeReport layerReport = report;
            const bool verified = verifyEdge(base, actual, document, layer, originalSize, layerReport);
            layerReport.elapsedMs = timer.elapsed();
            QJsonObject entry;
            entry.insert(QStringLiteral("edge"), edge);
            entry.insert(QStringLiteral("layerId"), layer.id);
            entry.insert(QStringLiteral("width"), layerReport.width);
            entry.insert(QStringLiteral("height"), layerReport.height);
            entry.insert(QStringLiteral("format"), layerReport.format);
            entry.insert(QStringLiteral("baseSha256"), layerReport.baseSha);
            entry.insert(QStringLiteral("pixelSha256"), layerReport.actualSha);
            entry.insert(QStringLiteral("baseRenderMs"), layerReport.baseMs);
            entry.insert(QStringLiteral("renderMs"), layerReport.actualMs);
            entry.insert(QStringLiteral("sampleCount"), layerReport.sampleCount);
            entry.insert(QStringLiteral("mismatchCount"), layerReport.mismatchCount);
            entry.insert(QStringLiteral("changedSamples"), layerReport.changedSamples);
            entry.insert(QStringLiteral("maskPositiveSamples"), layerReport.maskPositiveSamples);
            entry.insert(QStringLiteral("maxAbsError"), layerReport.maxAbsError);
            entry.insert(QStringLiteral("alphaExact"), layerReport.alphaExact);
            entry.insert(QStringLiteral("elapsedMs"), layerReport.elapsedMs);
            const bool pass = verified && sizeOk && layerReport.mismatchCount == 0 &&
                layerReport.maskPositiveSamples > 0 && layerReport.changedSamples > 0 &&
                layerReport.alphaExact && layerReport.error.isEmpty();
            entry.insert(QStringLiteral("pass"), pass);
            if (!layerReport.error.isEmpty()) entry.insert(QStringLiteral("error"), layerReport.error);
            if (!verified || !sizeOk || !pass) edgePass = false;
            edges.append(entry);
        }
        if (!edgePass) allPass = false;
        if (!sizeOk) { out.insert(QStringLiteral("error"), QStringLiteral("unexpected rendered size")); }
    }
    out.insert(QStringLiteral("edges"), edges);
    out.insert(QStringLiteral("pass"), allPass);
    return allPass;
}

AdjustmentLayer makeLayer(const SourceFrame &frame, const QString &id = QStringLiteral("brush")) {
    AdjustmentLayer layer;
    layer.id = id;
    layer.name = QStringLiteral("Replay");
    layer.parameters.exposure = .7;
    layer.parameters.saturation = 1.15;
    layer.opacity = .65;

    const double longEdge = std::max(frame.originalSize.width(), frame.originalSize.height());
    const double radius = longEdge * .06;
    QString error;
    BrushSampler sampler(radius, .6);
    if (!sampler.valid()) return AdjustmentLayer{};
    for (const QPointF uv : {QPointF(.3, .4), QPointF(.5, .5), QPointF(.7, .6)}) {
        const QPointF point = outputToSource(uv, frame.originalSize, Recipe{});
        if (!sampler.append(point, error)) return AdjustmentLayer{};
    }
    MaskComponent add;
    add.id = id + QStringLiteral("-add");
    add.operation = MaskOperation::Add;
    add.strength = .8;
    add.geometry = sampler.snapshot();

    MaskComponent subtract;
    subtract.id = id + QStringLiteral("-subtract");
    subtract.operation = MaskOperation::Subtract;
    subtract.strength = .45;
    BrushSampler sub(radius * .45, .3);
    if (!sub.valid()) return AdjustmentLayer{};
    const QPointF center = outputToSource(QPointF(.5, .5), frame.originalSize, Recipe{});
    if (!sub.append(center, error)) return AdjustmentLayer{};
    subtract.geometry = sub.snapshot();

    layer.mask.components = {add, subtract};
    return layer;
}

PhotoDocument makeDocument(const SourceFrame &frame,bool gradients=false) {
    PhotoDocument document;
    document.asset.id = QStringLiteral("replay-asset");
    document.asset.sourcePath = frame.path;
    document.asset.sourceHash = frame.sha256;
    document.asset.normalizedSize = frame.originalSize;
    document.asset.raw = frame.raw;
    document.asset.sourceFacts = frame.facts;
    document.global = Recipe{};
    document.layers = {makeLayer(frame)};
    if(gradients){
        const double w=frame.originalSize.width(),h=frame.originalSize.height();
        MaskComponent line;line.id="probe-linear";line.strength=.35;
        line.geometry=LinearGeometry{{w*.2,h*.25},{w*.8,h*.7}};
        MaskComponent ellipse;ellipse.id="probe-radial";ellipse.strength=.2;ellipse.operation=MaskOperation::Subtract;
        ellipse.geometry=RadialGeometry{{w*.55,h*.5},w*.28,h*.18,27};
        document.layers[0].mask.components.append(line);document.layers[0].mask.components.append(ellipse);
    }
    return document;
}

}

int main(int argc, char **argv) {
    if(qEnvironmentVariableIsEmpty("OMP_NUM_THREADS"))qputenv("OMP_NUM_THREADS","4");
    if(qEnvironmentVariableIsEmpty("OMP_WAIT_POLICY"))qputenv("OMP_WAIT_POLICY","PASSIVE");
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    QImageReader::setAllocationLimit(256);

    if ((args.size()!=2&&args.size()!=3)||(args.size()==3&&args[2]!="--gradients")) {
        emitReport(fail(QStringLiteral("usage: local-replay-probe <source|--synthetic45> [--gradients]")));
        return 2;
    }
    const QString argument = args[1];
    const bool gradients=args.size()==3;
    const bool synthetic = argument == QStringLiteral("--synthetic45");
    if (!synthetic && argument.startsWith(QLatin1Char('-'))) {
        emitReport(fail(QStringLiteral("unknown option")));
        return 2;
    }

    QJsonObject report;
    if (synthetic) {
        SourceFrame frame;
        frame.path = QStringLiteral("synthetic45");
        frame.sha256 = QString(64, QLatin1Char('a'));
        frame.originalSize = QSize(9000, 5000);
        frame.raw = false;
        frame.linear = syntheticFrame();
        if (frame.linear.isNull() || frame.linear.size() != frame.originalSize) {
            emitReport(fail(QStringLiteral("synthetic frame allocation failed"))); return 1;
        }
        report = QJsonObject{{QStringLiteral("mode"), QStringLiteral("synthetic45")},
                             {QStringLiteral("sourceSha256"), frame.sha256}};
        QJsonObject caseResult;
        const bool pass = runCase(frame, makeDocument(frame,gradients), caseResult);
        report.insert(QStringLiteral("cases"), QJsonArray{caseResult});
        report.insert(QStringLiteral("pass"), pass);
        if (caseResult.contains(QStringLiteral("error")))
            report.insert(QStringLiteral("error"), caseResult.value(QStringLiteral("error")));
    } else {
        const QString before = sha256File(argument);
        if (before.isEmpty()) { emitReport(fail(QStringLiteral("cannot hash source"))); return 1; }
        const FrameResult loaded = readSource(argument, 0, 0);
        if (!loaded.source) { emitReport(fail(loaded.error.isEmpty() ? QStringLiteral("source read failed") : loaded.error)); return 1; }
        const SourceFrame &frame = *loaded.source;
        if(frame.sha256!=before){emitReport(fail("Source changed during decode"));return 1;}
        report = QJsonObject{{QStringLiteral("mode"), QStringLiteral("source")},
                             {QStringLiteral("sourceSha256"), frame.sha256},
                             {QStringLiteral("facts"), frame.facts},
                             {QStringLiteral("width"), frame.linear.width()},
                             {QStringLiteral("height"), frame.linear.height()},
                             {QStringLiteral("format"), QString::number(int(frame.linear.format()))}};
        QJsonObject identity, combined;
        const bool identityPass = runCase(frame, makeDocument(frame,gradients), identity);
        PhotoDocument combinedDocument = makeDocument(frame,gradients);
        combinedDocument.global.crop = QRectF(.08, .07, .84, .86);
        combinedDocument.global.angle = 9;
        combinedDocument.global.perspective = QPointF(.025, -.02);
        combinedDocument.global.distortion = .04;
        combinedDocument.global.flipHorizontal = true;
        combinedDocument.global.flipVertical = true;
        const bool combinedPass = runCase(frame, combinedDocument, combined);
        report.insert(QStringLiteral("cases"), QJsonArray{identity, combined});
        const bool unchanged = sha256File(argument) == before;
        report.insert(QStringLiteral("sourceUnchanged"), unchanged);
        report.insert(QStringLiteral("pass"), identityPass && combinedPass && unchanged);
        if (identity.contains(QStringLiteral("error")))
            report.insert(QStringLiteral("error"), identity.value(QStringLiteral("error")));
        else if (combined.contains(QStringLiteral("error")))
            report.insert(QStringLiteral("error"), combined.value(QStringLiteral("error")));
    }

#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters), sizeof(counters))) {
        report.insert(QStringLiteral("peakWorkingSetMiB"), double(counters.PeakWorkingSetSize) / 1048576.0);
        report.insert(QStringLiteral("peakCommitMiB"), double(counters.PeakPagefileUsage) / 1048576.0);
    }
#endif

    const bool ok = report.value(QStringLiteral("pass")).toBool();
    report.insert("gradients",gradients);
    if(!ok&&!report.contains("error"))report.insert("error","Replay numerical or dimension check failed");
    report.insert(QStringLiteral("ok"), ok);
    emitReport(report);
    return ok ? 0 : 1;
}
