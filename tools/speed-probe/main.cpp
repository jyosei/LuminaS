#include "io/jpeg_loader.h"
#include "raw/raw_decoder.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonArray>
#include <QSaveFile>
#include <QTextStream>
#include <QElapsedTimer>
#include <QFileInfo>
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    const auto args=app.arguments();
    if(args.size()!=5) { QTextStream(stderr)<<"speed_probe input mode(jpeg|audit|preview|direct) report repetitions\n"; return 2; }
    if(QFileInfo::exists(args[3])) return 2;
    bool valid=false; const int repetitions=args[4].toInt(&valid);
    if(!valid || repetitions<1 || repetitions>10) return 2;
    QJsonArray runs; QByteArray expected;
    for(int i=0;i<repetitions;++i) {
        QImage frame; QJsonObject run; QElapsedTimer timer; timer.start();
        if(args[2]=="jpeg") {
            const auto result=decodeJpeg(args[1]); if(!result.error.isEmpty()) return 1;
            frame=result.image; run.insert("timings",result.timings);
            run.insert("normalizedSourceSize",QJsonArray{result.originalSize.width(),result.originalSize.height()});
            run.insert("colorNote",result.colorNote);
        } else if(args[2]=="audit" || args[2]=="preview" || args[2]=="direct") {
            RawOptions options; options.collectLinearDiagnostics=args[2]=="audit";
            options.directLinearPreview=args[2]=="direct";
            const auto result=developRaw(args[1],options); if(!result.error.isEmpty()) { QTextStream(stderr)<<result.error<<"\n"; return 1; }
            frame=result.preview; run=result.report;
        } else return 2;
        run.insert("wallDecodeToBufferMs",double(timer.elapsed()));
        frame=frame.convertToFormat(QImage::Format_RGB32);
        QCryptographicHash hash(QCryptographicHash::Sha256);
        for(int y=0;y<frame.height();++y) hash.addData(QByteArrayView(reinterpret_cast<const char *>(frame.constScanLine(y)),frame.width()*4));
        const auto digest=hash.result().toHex();
        if(i>0 && digest!=expected) return 1;
        expected=digest;
        run.insert("previewRgb32Sha256",QString::fromLatin1(digest));
        run.insert("width",frame.width()); run.insert("height",frame.height()); run.insert("iteration",i+1);
        runs.append(run); QTextStream(stdout)<<args[2]<<" "<<i+1<<" "<<run["wallDecodeToBufferMs"].toDouble()<<"ms\n";
    }
    QJsonObject report{{"mode",args[2]},{"runs",runs},{"scope","decode to 4K buffer; no painting or application cache; filesystem cache uncontrolled; process-lifetime memory peaks"}};
    QSaveFile file(args[3]); if(!file.open(QIODevice::WriteOnly)) return 1;
    const auto bytes=QJsonDocument(report).toJson();
    return file.write(bytes)==bytes.size() && file.commit() ? 0 : 1;
}
