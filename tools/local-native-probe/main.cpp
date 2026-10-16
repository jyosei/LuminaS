// Native, opt-in QA. Reads only the explicitly supplied original and writes no
// screenshots, rendered images or caches of its own. The ordinary application
// session cache retains its normal ownership/budget.
#include "app/window.h"
#include "app/canvas.h"
#include "app/display_presenter.h"
#include "app/edit_loader.h"
#include "core/photo_document.h"
#include "render/cpu_renderer.h"
#include <QApplication>
#include <QCryptographicHash>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineF>
#include <QMap>
#include <QMouseEvent>
#include <QPushButton>
#include <QSlider>
#include <QTabWidget>
#include <QTest>
#include <QTextStream>
#include <algorithm>
#include <cmath>
#include <functional>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#endif

namespace {
QString fileHash(const QString &path){
    QFile file(path);QCryptographicHash hash(QCryptographicHash::Sha256);
    if(!file.open(QIODevice::ReadOnly)||!hash.addData(&file))return {};
    return QString::fromLatin1(hash.result().toHex());
}
bool waitFor(const std::function<bool()> &predicate,int timeout=90000){
    QElapsedTimer timer;timer.start();
    while(!predicate()){if(timer.elapsed()>=timeout)return false;QTest::qWait(10);}return true;
}
QPoint position(Canvas *canvas,double u,double v){
    const auto size=canvas->presenter()->canonicalImage().size();
    return {int(std::lround(canvas->width()/2.+(u-.5)*size.width()*canvas->zoom())),
        int(std::lround(canvas->height()/2.+(v-.5)*size.height()*canvas->zoom()))};
}
struct Entry {
    QString phase;EditTag tag;qint64 queued=0,photoKey=0,maskKey=0;
    double readyMs=-1,photoMs=-1,maskMs=-1,effectMs=0,maskComputeMs=0;
};
QJsonObject distribution(QVector<double> values){
    if(values.isEmpty())return {{"count",0}};
    std::sort(values.begin(),values.end());
    return {{"count",int(values.size())},{"minMs",values.first()},
        {"medianMs",values[(values.size()-1)/2]},
        {"p95Ms",values[qsizetype(std::ceil(values.size()*.95))-1]},
        {"maxMs",values.last()}};
}
}

int main(int argc,char **argv){
    if(qEnvironmentVariableIsEmpty("OMP_NUM_THREADS"))qputenv("OMP_NUM_THREADS","4");
    if(qEnvironmentVariableIsEmpty("OMP_WAIT_POLICY"))qputenv("OMP_WAIT_POLICY","PASSIVE");
    QApplication app(argc,argv);
    if(app.arguments().size()!=2){qCritical("Usage: local_native_probe source");return 2;}
    const QString path=QFileInfo(app.arguments()[1]).canonicalFilePath();
    const QString before=fileHash(path);QJsonObject report{{"sourceSha256",before},
        {"measurement","request enqueue to actual native paint; separate photo/mask provenance"}};
    QString error;bool ok=false;QElapsedTimer wall;wall.start();
    {
        Window window(nullptr,QStringLiteral(""));
        auto *canvas=window.findChild<Canvas *>("photo-canvas");
        auto *loader=window.findChild<EditLoader *>();
        QMap<quint64,Entry> entries;QString phase="import";
        quint64 latestQueued=0,latestReady=0; qint64 lastPhoto=0,latestCanonical=0;
        QPointF pressedUv,lastMovedUv;bool sawPress=false,sawMove=false;
        if(!canvas||!loader||path.isEmpty()||before.isEmpty())error="Invalid source or missing native components";
        else {
            QObject::connect(canvas,&Canvas::brushPressed,&window,[&](QPointF uv,bool){pressedUv=uv;sawPress=true;});
            QObject::connect(canvas,&Canvas::brushMoved,&window,[&](QPointF uv){lastMovedUv=uv;sawMove=true;});
            QObject::connect(loader,&EditLoader::requestQueued,&window,[&](const QString &source,EditTag tag){
                if(source!=path)return;
                latestQueued=tag.job;
                while(entries.size()>=128)entries.erase(entries.begin());
                Entry entry;entry.phase=phase;entry.tag=tag;entry.queued=wall.nsecsElapsed();entries.insert(tag.job,entry);
            });
            QObject::connect(loader,&EditLoader::ready,&window,[&](const EditResult &result){
                if(result.path!=path||!result.error.isEmpty()||!entries.contains(result.tag.job))return;
                // Window's earlier ready connection already applied its delivery gate.
                // A rejected result is not eligible for paint attribution.
                if(canvas->presenter()->canonicalImage().cacheKey()!=result.image.cacheKey())return;
                auto &entry=entries[result.tag.job];entry.readyMs=(wall.nsecsElapsed()-entry.queued)/1e6;
                entry.photoKey=result.image.cacheKey();entry.maskKey=result.localMask.cacheKey();
                entry.effectMs=result.effectElapsedMs;entry.maskComputeMs=result.maskElapsedMs;
                latestReady=result.tag.job;latestCanonical=entry.photoKey;
            });
            QObject::connect(canvas,&Canvas::canonicalFramePainted,&window,[&](qint64 key){
                lastPhoto=key;
                // One actual frame counts once; never attach the newest request tag
                // to an older display-conversion result.
                for(auto i=entries.end();i!=entries.begin();){--i;
                    if(i->photoKey==key&&i->photoMs<0){i->photoMs=(wall.nsecsElapsed()-i->queued)/1e6;break;}}
            });
            QObject::connect(canvas,&Canvas::localMaskPainted,&window,[&](qint64 key){
                for(auto i=entries.end();i!=entries.begin();){--i;
                    if(i->maskKey==key&&i->maskMs<0){i->maskMs=(wall.nsecsElapsed()-i->queued)/1e6;break;}}
            });
            const auto idle=[&]{return latestQueued!=0&&latestReady==latestQueued&&lastPhoto==latestCanonical;};
            const auto settle=[&]{return waitFor(idle);};
            window.resize(1366,768);window.show();window.addFiles({path});
            if(!waitFor([&]{return idle()&&window.currentPhotoDocument()&&window.neutralPickerAvailable();}))error="Initial native photo paint timeout";
            else {
                report.insert("importToPaintMs",double(wall.elapsed()));
                report.insert("previewWidth",canvas->presenter()->canonicalImage().width());
                report.insert("previewHeight",canvas->presenter()->canonicalImage().height());
                report.insert("dpr",canvas->devicePixelRatioF());report.insert("displayProfile",canvas->displayProfileNote());
                auto *scope=window.findChild<QTabWidget *>("adjustment-scope-tabs");
                auto *create=window.findChild<QPushButton *>("local-new-brush");
                auto *slider=window.findChild<QSlider *>("slider-local-exposure");
                auto *radius=window.findChild<QDoubleSpinBox *>("local-radius");
                if(!scope||!create||!slider||!radius)error="Missing local controls";
                else {
                    phase="setup";scope->setCurrentIndex(1);QTest::mouseClick(create,Qt::LeftButton);
                    if(!settle()||!canvas->brushMode())error="Local brush setup timeout";
                    else {
                        const auto size=window.currentPhotoDocument()->asset.normalizedSize;
                        radius->setValue(std::max(size.width(),size.height())*.06);
                        slider->setValue(70);if(!settle())error="Initial local exposure timeout";
                        if(error.isEmpty()){
                            phase="brushOverlay";
                            QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,position(canvas,.3,.4));
                            for(int i=1;i<=12;++i){
                                const auto p=position(canvas,.3+.4*i/12.,.4+.2*i/12.);
                                QMouseEvent event(QEvent::MouseMove,QPointF(p),canvas->mapToGlobal(p),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
                                QApplication::sendEvent(canvas,&event);QTest::qWait(120);
                            }
                            QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,position(canvas,.7,.6));
                            if(!settle())error="Brush native paint timeout";
                        }
                        for(bool overlay:{true,false})if(error.isEmpty()){
                            phase="setup";
                            if(!overlay){canvas->setFocus();QTest::keyClick(canvas,Qt::Key_O);if(!settle()){error="Overlay toggle timeout";break;}}
                            phase=overlay?"parameterOverlay":"parameterPhotoOnly";
                            slider->setSliderDown(true);
                            for(int i=1;i<=12;++i){slider->setValue(overlay?30+i*10:180-i*10);QTest::qWait(120);}
                            slider->setSliderDown(false);if(!settle())error="Parameter native paint timeout";
                        }
                        const auto document=window.currentPhotoDocument();
                        if(document){
                            report.insert("raw",document->asset.raw);report.insert("sourceWidth",size.width());report.insert("sourceHeight",size.height());
                            report.insert("layerCount",int(document->layers.size()));
                            if(document->layers.size()!=1||document->layers[0].mask.components.size()!=1)error="Native stroke did not commit exactly once";
                            else {const auto &brush=std::get<LocalEdit::BrushGeometry>(document->layers[0].mask.components[0].geometry);
                                report.insert("strokePointCount",int(brush.points.size()));
                                if(brush.points.size()<3||!sawPress||!sawMove)error="Missing sampled native stroke";
                                else {
                                    const double startError=QLineF(brush.points.first(),outputToSource(pressedUv,size,document->global)).length();
                                    const double endError=QLineF(brush.points.last(),outputToSource(lastMovedUv,size,document->global)).length();
                                    report.insert("sourceAnchorStartErrorPx",startError);report.insert("sourceAnchorEndErrorPx",endError);
                                    if(startError>1e-8||endError>1e-8)error="Native source anchor drift";
                                }}
                        }
                        else error="Native document missing";
                    }
                }
            }
        }
        QJsonArray records;QJsonObject phases;
        for(const auto &name:{QString("brushOverlay"),QString("parameterOverlay"),QString("parameterPhotoOnly")}){
            QVector<double> photos,masks,readies,effects,maskCompute;int requested=0;
            for(const auto &entry:entries)if(entry.phase==name){
                ++requested;if(entry.photoMs>=0)photos.append(entry.photoMs);if(entry.maskMs>=0)masks.append(entry.maskMs);
                if(entry.readyMs>=0){readies.append(entry.readyMs);effects.append(entry.effectMs);maskCompute.append(entry.maskComputeMs);}
            }
            phases.insert(name,QJsonObject{{"requests",requested},{"notDelivered",requested-int(readies.size())},
                {"deliveredNotPainted",int(readies.size()-photos.size())},{"photoPaint",distribution(photos)},
                {"maskPaint",distribution(masks)},{"workerDelivery",distribution(readies)},
                {"effectCompute",distribution(effects)},{"maskCompute",distribution(maskCompute)}});
            if(error.isEmpty()&&photos.isEmpty())error="No measured actual photo paint in "+name;
            if(error.isEmpty()&&name!="parameterPhotoOnly"&&masks.isEmpty())error="No measured actual mask paint in "+name;
        }
        for(const auto &entry:entries)records.append(QJsonObject{{"job",QString::number(entry.tag.job)},
            {"phase",entry.phase},{"interactive",entry.tag.interactive},{"readyMs",entry.readyMs},
            {"photoMs",entry.photoMs},{"maskMs",entry.maskMs},{"photoKey",QString::number(entry.photoKey)},
            {"maskKey",QString::number(entry.maskKey)}});
        report.insert("phases",phases);report.insert("jobs",records);
        report.insert("sourceUnchanged",fileHash(path)==before&&!before.isEmpty());
        if(error.isEmpty()&&!report.value("sourceUnchanged").toBool())error="Source changed";
        ok=error.isEmpty();
    }
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX counters{};counters.cb=sizeof(counters);
    if(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters),sizeof(counters))){
        report.insert("peakWorkingSetMiB",double(counters.PeakWorkingSetSize)/1048576);
        report.insert("peakCommitMiB",double(counters.PeakPagefileUsage)/1048576);
    }
#endif
    report.insert("ok",ok);report.insert("error",error);report.insert("totalMs",double(wall.elapsed()));
    QTextStream(stdout)<<QJsonDocument(report).toJson(QJsonDocument::Compact)<<Qt::endl;
    return ok?0:1;
}
