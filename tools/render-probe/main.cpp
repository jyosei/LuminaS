#include "../source_read_support.h"
#include "io/source_reader.h"
#include "render/cpu_renderer.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QImageReader>
#include <QTextStream>
#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif
static QString hashFile(const QString &path){QFile f(path);QCryptographicHash h(QCryptographicHash::Sha256);if(!f.open(QIODevice::ReadOnly)||!h.addData(&f))return {};return QString::fromLatin1(h.result().toHex());}
int main(int argc,char **argv){
    if(qEnvironmentVariableIsEmpty("OMP_NUM_THREADS"))qputenv("OMP_NUM_THREADS","4");
    if(qEnvironmentVariableIsEmpty("OMP_WAIT_POLICY"))qputenv("OMP_WAIT_POLICY","PASSIVE");
    QCoreApplication app(argc,argv);const auto args=app.arguments();
    QImageReader::setAllocationLimit(256);
    if(args.size()<3||args.size()>4){qCritical("Usage: render_probe source output-prefix [recipe.json]");return 2;}
    Recipe recipe;if(args.size()==4){QFile f(args[3]);if(!f.open(QIODevice::ReadOnly))return 2;QJsonParseError error;auto json=QJsonDocument::fromJson(f.readAll(),&error);if(error.error!=QJsonParseError::NoError||!json.isObject())return 2;recipe=recipeFromJson(json.object());}
    if(!validRecipe(recipe))return 2;
    const auto prefix=args[2];for(const auto &suffix:{"-preview.png","-full.png",".json"})if(QFile::exists(prefix+suffix)){qCritical("Output already exists");return 2;}
    QElapsedTimer timer;timer.start();auto source=readProbeSource(args[1],recipe.warmth,recipe.tint);if(!source.error.isEmpty()){qCritical().noquote()<<source.error;return 1;}
    QJsonObject report{{"sourceSha256",source.source->sha256},{"facts",source.source->facts},{"recipe",recipeJson(recipe)},{"sourceReadMs",double(timer.elapsed())}};
    for(int edge:{3840,0}){
        const auto result=renderFrame(*source.source,recipe,edge);if(!result.error.isEmpty()){qCritical().noquote()<<result.error;return 1;}
        const auto name=edge==0?QString("full"):QString("preview");const auto path=prefix+"-"+name+".png";
        if(!result.image.save(path))return 1;
        report.insert(name,QJsonObject{{"width",result.image.width()},{"height",result.image.height()},{"renderMs",result.elapsedMs},{"pixelSha256",QString::fromLatin1(QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char *>(result.image.constBits()),result.image.sizeInBytes()),QCryptographicHash::Sha256).toHex())}});
    }
    report.insert("sourceUnchanged",hashFile(args[1])==source.source->sha256);
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX counters{};counters.cb=sizeof(counters);GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters),sizeof(counters));
    report.insert("peakWorkingSetMiB",double(counters.PeakWorkingSetSize)/1048576);report.insert("peakCommitMiB",double(counters.PeakPagefileUsage)/1048576);
#endif
    report.insert("totalIncludingPngMs",double(timer.elapsed()));QFile file(prefix+".json");if(!file.open(QIODevice::WriteOnly|QIODevice::NewOnly))return 1;file.write(QJsonDocument(report).toJson());
    QTextStream(stdout)<<QJsonDocument(report).toJson(QJsonDocument::Compact)<<Qt::endl;return report.value("sourceUnchanged").toBool()?0:1;
}
