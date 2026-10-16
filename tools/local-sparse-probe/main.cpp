// Synthetic mask-only timing and complete comparison with an unpruned sweep.
// No photo/file input, RGB render, image output, native paint or FPS measurement.
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTextStream>
#include <algorithm>
#include <cmath>
#include "render/local_mask.h"
#include "render/local_mask_feather.h"
#include "render/geometry_transform.h"
using namespace LocalEdit;
int main(int argc,char **argv){
    QCoreApplication app(argc,argv);const QSize source(6000,4000),size(3840,2560);
    QImage alpha(size,QImage::Format_RGBA32FPx4);if(alpha.isNull())return 2;
    for(int y=0;y<size.height();++y){auto *row=reinterpret_cast<float *>(alpha.scanLine(y));for(int x=0;x<size.width();++x){row[4*x]=row[4*x+1]=row[4*x+2]=.18f;row[4*x+3]=1;}}
    Mask mask;mask.feather=12;mask.invert=app.arguments().contains("--invert");
    MaskComponent add;add.id="add";add.strength=.8;add.geometry=RadialGeometry{{3000,2000},600,350,27};
    MaskComponent sub;sub.id="sub";sub.operation=MaskOperation::Subtract;sub.strength=.7;
    BrushGeometry brush;brush.points={{3100,2100}};brush.radius=180;brush.hardness=.4;sub.geometry=brush;mask.components={add,sub};
    const PreparedBrushMask prepared(mask);if(!prepared.valid()){qCritical().noquote()<<prepared.error();return 2;}
    const MaskMapping mapping{source,size,{}};QVector<float> values;QString error;QJsonArray rounds;MaskPreviewStats stats;
    for(int round=0;round<3;++round){
        QElapsedTimer timer;timer.start();if(!rasterizeMaskPreview(prepared,mapping,alpha,values,error,{},256,&stats)){qCritical().noquote()<<error;return 2;}
        rounds.append(timer.nsecsElapsed()/1e6);
    }
    // Reproduce the original unpruned source-grid algorithm outside timing.
    auto plain=mask;plain.feather=0;plain.invert=false;const PreparedBrushMask raw(plain);
    const OutputSourceMapping transform(source,{});double maximum=0;qint64 mismatches=0;
    for(int ty=0;ty<size.height();ty+=256)for(int tx=0;tx<size.width();tx+=256){
        const int width=std::min(256,size.width()-tx),height=std::min(256,size.height()-ty);
        QVector<QPointF> points;points.reserve(width*height);QVector<quint8> active(width*height,1);
        for(int y=0;y<height;++y)for(int x=0;x<width;++x)points.append(transform.map({(tx+x+.5)/size.width(),(ty+y+.5)/size.height()}));
        QVector<float> reference;if(!Detail::featherPoints(points,active,mask.feather,[&raw](QPointF p){return raw.sample(p);},reference,error)){qCritical().noquote()<<error;return 2;}
        for(int y=0;y<height;++y)for(int x=0;x<width;++x){
            const float expected=mask.invert?1-reference[y*width+x]:reference[y*width+x];
            const double difference=std::abs(double(values[(ty+y)*size.width()+tx+x])-expected);
            maximum=std::max(maximum,difference);if(difference>2e-6)++mismatches;
        }
    }
    const auto sha=QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char *>(values.constData()),values.size()*qsizetype(sizeof(float))),QCryptographicHash::Sha256).toHex();
    const QJsonObject report{{"width",size.width()},{"height",size.height()},{"sourceWidth",source.width()},{"sourceHeight",source.height()},
        {"components",2},{"feather",mask.feather},{"invert",mask.invert},{"roundMs",rounds},{"workers",stats.workers},
        {"scratchBytes",double(stats.maximumScratchBytes)},{"maxCoverageError",maximum},{"mismatchCount",double(mismatches)},
        {"maskSha256",QString::fromLatin1(sha)},{"oracle","unpruned source sweep, every output pixel"},{"meaning","mask-only; no RGB/import/paint"}};
    QTextStream(stdout)<<QJsonDocument(report).toJson(QJsonDocument::Compact)<<Qt::endl;
    return mismatches?1:0;
}
