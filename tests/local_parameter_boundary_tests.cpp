#include <QtTest>
#include <bit>
#include <cmath>
#include "render/local_renderer.h"

using namespace LocalEdit;
namespace {
struct Field {const char *name;double LocalParameters::*local;double Recipe::*global;double low,high;};
constexpr Field fields[]{
    {"exposure",&LocalParameters::exposure,&Recipe::exposure,-5,5},
    {"contrast",&LocalParameters::contrast,&Recipe::contrast,-1,1},
    {"highlights",&LocalParameters::highlights,&Recipe::highlights,-1,1},
    {"shadows",&LocalParameters::shadows,&Recipe::shadows,-1,1},
    {"whites",&LocalParameters::whites,&Recipe::whites,-1,1},
    {"blacks",&LocalParameters::blacks,&Recipe::blacks,-1,1},
    {"warmth",&LocalParameters::warmth,&Recipe::warmth,-1,1},
    {"tint",&LocalParameters::tint,&Recipe::tint,-1,1},
    {"saturation",&LocalParameters::saturation,&Recipe::saturation,0,2}
};
SourceFrame frame(bool raw){
    SourceFrame result;result.path="parameter-boundary";result.sha256=QString(64,'a');
    result.originalSize={8,6};result.raw=raw;result.linear=QImage(result.originalSize,QImage::Format_RGBA32FPx4);
    for(int y=0;y<6;++y)for(int x=0;x<8;++x){
        auto *p=reinterpret_cast<float *>(result.linear.scanLine(y))+4*x;
        p[0]=-.5f+.23f*x;p[1]=.04f+.07f*y;p[2]=1.8f-.17f*x;
        p[3]=x==0?0.f:x==1?.4f:1.f;
    }return result;
}
}
class LocalParameterBoundaryTests:public QObject {
    Q_OBJECT
private slots:
    void eachBoundaryMatchesExistingGlobalRgbMath_data(){
        QTest::addColumn<int>("field");QTest::addColumn<double>("value");QTest::addColumn<bool>("raw");
        for(int i=0;i<9;++i)for(bool raw:{false,true})for(bool high:{false,true})
            QTest::newRow(qPrintable(QString("%1-%2-%3").arg(fields[i].name).arg(raw?"raw":"rgb").arg(high?"high":"low")))
                <<i<<(high?fields[i].high:fields[i].low)<<raw;
    }
    void eachBoundaryMatchesExistingGlobalRgbMath(){
        QFETCH(int,field);QFETCH(double,value);QFETCH(bool,raw);
        const auto source=frame(raw);PhotoDocument doc;
        doc.asset.id="asset";doc.asset.sourcePath=source.path;doc.asset.sourceHash=source.sha256;
        doc.asset.normalizedSize=source.originalSize;doc.asset.raw=raw;
        AdjustmentLayer layer;layer.id="layer";layer.parameters.*(fields[field].local)=value;
        MaskComponent component;component.id="whole";BrushGeometry brush;brush.points={{4,3}};brush.radius=100;brush.hardness=1;
        component.geometry=brush;layer.mask.components={component};doc.layers={layer};
        QString error;const auto actual=renderDocumentFullLinear(source,doc,error);QVERIFY2(!actual.isNull(),qPrintable(error));
        // Local warmth/tint is intentionally a working-RGB adjustment for RAW
        // too. Compare with the existing RGB global math, not camera WB gains.
        auto rgb=source;rgb.raw=false;Recipe equivalent;equivalent.*(fields[field].global)=value;
        const auto expected=renderLinear(rgb,equivalent,0,error);QVERIFY2(!expected.isNull(),qPrintable(error));
        const auto base=renderLinear(source,{},0,error);QVERIFY2(!base.isNull(),qPrintable(error));
        bool changed=false;
        for(int y=0;y<6;++y)for(int x=0;x<8;++x){
            const auto *input=reinterpret_cast<const float *>(source.linear.constScanLine(y))+4*x;
            const auto *a=reinterpret_cast<const float *>(actual.constScanLine(y))+4*x;
            const auto *b=reinterpret_cast<const float *>(expected.constScanLine(y))+4*x;
            const auto *prefix=reinterpret_cast<const float *>(base.constScanLine(y))+4*x;
            QCOMPARE(std::bit_cast<quint32>(a[3]),std::bit_cast<quint32>(input[3]));
            for(int c=0;c<3;++c){
                QVERIFY(std::isfinite(a[c]));
                // The global source/geometry stage canonicalizes transparent
                // RGB to zero. Locals preserve that stage, not invisible source
                // garbage that has already been removed before composition.
                if(input[3]==0){QCOMPARE(std::bit_cast<quint32>(a[c]),std::bit_cast<quint32>(prefix[c]));continue;}
                QVERIFY2(std::abs(a[c]-b[c])<=2e-6f+2e-6f*std::abs(b[c]),qPrintable(QString("pixel %1,%2 channel %3").arg(x).arg(y).arg(c)));
                changed|=a[c]!=input[c];
            }
        }
        QVERIFY(changed);
    }
};
QTEST_MAIN(LocalParameterBoundaryTests)
#include "local_parameter_boundary_tests.moc"
