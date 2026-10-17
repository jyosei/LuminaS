#include "../source_read_support.h"
#include "app/window.h"
#include "io/source_reader.h"
#include "io/preview_cache.h"
#include "render/cpu_renderer.h"
#include <QApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTimer>
#include <algorithm>

static QString imageHash(const QImage &image){return QString::fromLatin1(QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char *>(image.constBits()),image.sizeInBytes()),QCryptographicHash::Sha256).toHex());}
int main(int argc,char **argv){
    if(qEnvironmentVariableIsEmpty("OMP_NUM_THREADS"))qputenv("OMP_NUM_THREADS","4");
    if(qEnvironmentVariableIsEmpty("OMP_WAIT_POLICY"))qputenv("OMP_WAIT_POLICY","PASSIVE");
    QElapsedTimer wall;wall.start();QApplication app(argc,argv);const auto appInitMs=wall.elapsed();QImageReader::setAllocationLimit(256);
    const auto args=app.arguments();if(args.size()!=4||QFileInfo::exists(args[2]))return 2;
    const auto path=QFileInfo(args[1]).canonicalFilePath();if(path.isEmpty())return 2;
    QJsonObject report{{"mode",args[3]},{"sourceName",QFileInfo(path).fileName()},{"filesystemCache","uncontrolled"},{"appInitMs",double(appInitMs)}};
    bool ok=false;
    if(args[3]=="pipeline"){
        QElapsedTimer phase;phase.start();SourceFrame proxy;QString error;
        {
            const auto read=readProbeSource(path);report.insert("readTotalMs",double(phase.elapsed()));
            if(!read.source||!read.error.isEmpty()){report.insert("error",read.error);}
            else{
                report.insert("readStages",read.source->facts.value("readStages"));phase.restart();proxy=*read.source;
                proxy.linear=renderLinear(*read.source,{},3840,error);report.insert("renderBaseMs",double(phase.elapsed()));
                if(!proxy.linear.isNull()){
                    proxy.sourcePixelsPerPixelX=double(read.source->linear.width())/proxy.linear.width();proxy.sourcePixelsPerPixelY=double(read.source->linear.height())/proxy.linear.height();
                    proxy.sourcePixelsPerPixel=std::max(proxy.pixelScaleX(),proxy.pixelScaleY());
                }
            }
        }
        if(!proxy.linear.isNull()&&error.isEmpty()){
            PreviewCache cache;QJsonObject cacheStages;phase.restart();const auto cached=cache.put(proxy,&cacheStages);report.insert("cacheWriteStages",cacheStages);report.insert("cacheWriteMs",double(phase.elapsed()));report.insert("cacheWriteOk",cached);
            phase.restart();const auto rendered=renderFrame(proxy,{},0);report.insert("sdrMs",double(phase.elapsed()));report.insert("coreReadyMs",double(wall.elapsed()));
            ok=cached&&rendered.error.isEmpty()&&!rendered.image.isNull();if(!rendered.error.isEmpty())report.insert("error",rendered.error);
            if(ok){report.insert("width",rendered.image.width());report.insert("height",rendered.image.height());report.insert("linearSha256",imageHash(proxy.linear));report.insert("displaySha256",imageHash(rendered.image));}
        }else if(!error.isEmpty())report.insert("error",error);
    }else if(args[3]=="window"){
        const auto beforeWindow=wall.elapsed();Window window;report.insert("windowInitMs",double(wall.elapsed()-beforeWindow));
        auto *canvas=window.findChild<Canvas *>();auto *loader=window.findChild<EditLoader *>();if(!canvas||!loader)return 1;
        QEventLoop loop;bool committed=false;QTimer deadline;deadline.setSingleShot(true);
        QObject::connect(&deadline,&QTimer::timeout,&loop,[&]{report.insert("error","photo paint timeout");loop.quit();});
        QObject::connect(&window,&Window::previewChanged,&loop,[&](const QString &,bool success){committed=success;report.insert("previewReadyMs",double(wall.elapsed()));if(!success){report.insert("error","preview failed");loop.quit();}});
        QObject::connect(loader,&EditLoader::ready,&loop,[&](const EditResult &result){report.insert("workerMs",result.elapsedMs);report.insert("width",result.image.width());report.insert("height",result.image.height());});
        QObject::connect(canvas,&Canvas::framePainted,&loop,[&]{if(committed&&!ok){ok=true;report.insert("firstPhotoPaintMs",double(wall.elapsed()));loop.quit();}});
        window.show();report.insert("importStartedMs",double(wall.elapsed()));window.addFiles({path});deadline.start(90000);loop.exec();
        // Window/cache teardown happens after the recorded first paint, not within it.
    }else return 2;
    report.insert("ok",ok);report.insert("totalIncludingProbeHashAndTeardownMs",double(wall.elapsed()));
    QSaveFile file(args[2]);if(!file.open(QIODevice::WriteOnly))return 1;const auto bytes=QJsonDocument(report).toJson();if(file.write(bytes)!=bytes.size()||!file.commit())return 1;
    return ok?0:1;
}
