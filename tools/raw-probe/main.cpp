#include "raw/raw_decoder.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QJsonDocument>
#include <QJsonArray>
#include <QSaveFile>
#include <QFileInfo>
#include <QColorSpace>
#include <QImageReader>
#include <QPainter>
#include <QTextStream>
#include <cmath>

static bool saveJson(const QString &path, const QJsonObject &object) {
    if (QFileInfo::exists(path)) return false;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    const auto data = QJsonDocument(object).toJson();
    return file.write(data)==data.size() && file.commit();
}
static bool savePng(const QString &path, const QImage &image) {
    if (QFileInfo::exists(path)) return false;
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && image.save(&file,"PNG") && file.commit();
}
int main(int argc, char **argv) {
    QCoreApplication app(argc,argv); app.setApplicationName("LuminaS raw-probe S1");
    QCommandLineParser parser; parser.addHelpOption(); parser.addPositionalArgument("input","Local CR3 input (read-only)");
    parser.addOptions({{{"a","algorithm"},"Built-in AHD or DCB","algorithm","ahd"},
        {"linear","Experimental RGB16 linear PPM output (must not exist)","path"},
        {"preview","Encoded-sRGB 4K PNG preview (must not exist)","path"},
        {"report","JSON metadata, configuration, timings, process peak and numeric boundaries","path"},
        {"reference","Camera JPEG for descriptive comparison; not a colorimetric ground truth","path"},
        {"comparison","Side-by-side PNG: developed RAW left / camera JPEG right","path"}});
    parser.process(app);
    if (parser.positionalArguments().size()!=1 || !parser.isSet("report")) parser.showHelp(2);
    const auto algorithm=parser.value("algorithm").toLower();
    if (algorithm!="ahd" && algorithm!="dcb") { QTextStream(stderr)<<"Unsupported algorithm\n"; return 2; }
    const auto input=parser.positionalArguments().first();
    for (const auto &option : {"linear","preview","report","comparison"}) {
        if (parser.isSet(option) && QFileInfo::exists(parser.value(option))) {
            QTextStream(stderr)<<"Refusing existing output: "<<parser.value(option)<<"\n"; return 2;
        }
    }
    RawOptions options; options.quality=algorithm=="ahd"?3:4; options.linearOutput=parser.value("linear");
    auto result=developRaw(input,options);
    if (!result.error.isEmpty()) {
        saveJson(parser.value("report"),result.report);
        QTextStream(stderr)<<result.error<<"\n"; return 1;
    }
    if (parser.isSet("preview") && !savePng(parser.value("preview"),result.preview)) { QTextStream(stderr)<<"Cannot save preview\n"; return 1; }
    if (parser.isSet("reference")) {
        QImageReader reader(parser.value("reference")); reader.setAutoTransform(true);
        auto reference=reader.read();
        if (reference.isNull()) { QTextStream(stderr)<<"Cannot decode reference JPEG\n"; return 1; }
        if (!reference.colorSpace().isValid()) reference.setColorSpace(QColorSpace::SRgb);
        reference=reference.convertedToColorSpace(QColorSpace::SRgb,QImage::Format_RGB32);
        const auto referenceSize=reference.size();
        if (referenceSize!=result.originalSize) { QTextStream(stderr)<<"Reference geometry differs; refusing unregistered metrics\n"; return 1; }
        reference=reference.scaled(result.preview.size(),Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
        double sumAbs=0,sumSquared=0;
        for (int y=0;y<reference.height();++y) {
            const auto *r=reinterpret_cast<const QRgb *>(reference.constScanLine(y));
            const auto *d=reinterpret_cast<const QRgb *>(result.preview.constScanLine(y));
            for(int x=0;x<reference.width();++x) for (const auto shift : {0,8,16}) {
                const int difference=int((r[x]>>shift)&255)-int((d[x]>>shift)&255);
                sumAbs+=std::abs(difference); sumSquared+=double(difference)*difference;
            }
        }
        const double count=double(reference.width())*reference.height()*3;
        result.report.insert("cameraJpegComparison",QJsonObject{{"referenceFile",QFileInfo(parser.value("reference")).fileName()},
            {"width",reference.width()},{"height",reference.height()},
            {"maeEncodedRgb8",sumAbs/count},{"rmseEncodedRgb8",std::sqrt(sumSquared/count)},
            {"meaning","Descriptive pixel differences only: camera picture style/exposure/tone/denoise differ; not DeltaE or RAW acceptance"}});
        if (parser.isSet("comparison")) {
            QImage comparison(reference.width()*2,reference.height(),QImage::Format_RGB32);
            QPainter painter(&comparison); painter.drawImage(0,0,result.preview); painter.drawImage(reference.width(),0,reference); painter.end();
            comparison.setColorSpace(QColorSpace::SRgb);
            if (!savePng(parser.value("comparison"),comparison)) { QTextStream(stderr)<<"Cannot save comparison\n"; return 1; }
        }
    }
    if (!saveJson(parser.value("report"),result.report)) { QTextStream(stderr)<<"Cannot save report\n"; return 1; }
    QTextStream(stdout)<<algorithm<<" "<<result.originalSize.width()<<"x"<<result.originalSize.height()
        <<" preview "<<result.preview.width()<<"x"<<result.preview.height()<<" "<<result.report["elapsedMs"].toDouble()<<"ms\n";
    return 0;
}
