#include <QtTest>
#include <bit>
#include <limits>
#include "render/local_mask.h"

using namespace LocalEdit;
namespace {
QImage frame(QSize size){
    QImage image(size,QImage::Format_RGBA32FPx4);
    for(int y=0;y<size.height();++y){
        auto *row=reinterpret_cast<float *>(image.scanLine(y));
        for(int x=0;x<size.width();++x){
            row[4*x]=row[4*x+1]=row[4*x+2]=.18f;
            row[4*x+3]=(x%31==0)?0.f:.5f;
        }
    }
    return image;
}
Mask strokes(){
    Mask mask;
    for(int i=0;i<2;++i){
        MaskComponent component;component.id=QString::number(i);
        component.operation=i?MaskOperation::Subtract:MaskOperation::Add;
        component.strength=i?.6:.8;
        BrushGeometry brush;brush.radius=170;brush.hardness=.55;
        brush.points={{400.,220.},{500.,270.},{600.,300.}};
        if(i)for(auto &point:brush.points)point+=QPointF(50,10);
        component.geometry=brush;mask.components.append(component);
    }
    return mask;
}
MaskMapping mapping(){
    MaskMapping map;map.sourceSize={1026,514};map.outputSize={513,257};
    map.geometry.crop={.1,.08,.8,.84};map.geometry.angle=11;
    map.geometry.flipHorizontal=true;map.geometry.perspective={.04,-.03};
    map.geometry.distortion=.08;return map;
}
}
class LocalMaskPreviewTests:public QObject {
    Q_OBJECT
private slots:
    void exactReplay(){
        const auto map=mapping();const auto alpha=frame(map.outputSize);
        auto mask=strokes();const BrushMaskReference reference(mask);
        const PreparedBrushMask prepared(mask);
        QVERIFY2(prepared.valid(),qPrintable(prepared.error()));
        mask.components.clear(); // Prepared owns the frozen geometry.
        QString error;QVector<float> expected;
        QVERIFY2(reference.rasterize(map,alpha,QRect(QPoint(),map.outputSize),expected,error),qPrintable(error));
        int nonzero=0,fractional=0;
        for(float value:expected){nonzero+=value>0;fractional+=value>0&&value<1;}
        QVERIFY(nonzero>0);QVERIFY(fractional>0);
        for(int tileSize:{256,512}){
            QVector<float> actual;MaskPreviewStats stats;
            QVERIFY2(rasterizeMaskPreview(prepared,map,alpha,actual,error,{},tileSize,&stats),qPrintable(error));
            QCOMPARE(actual.size(),expected.size());
            for(qsizetype i=0;i<actual.size();++i)
                QCOMPARE(std::bit_cast<quint32>(actual[i]),std::bit_cast<quint32>(expected[i]));
            QCOMPARE(stats.tiles,tileSize==256?6:2);QCOMPARE(stats.workers,tileSize==256?4:2);
            QVERIFY(stats.maximumScratchBytes>0);
            QVERIFY(stats.maximumScratchBytes<=512*512*25);
        }
    }
    void failuresNeverPublish(){
        auto map=mapping();auto alpha=frame(map.outputSize);
        const PreparedBrushMask prepared(strokes());
        QString error;const QVector<float> sentinel{17,23,42};auto actual=sentinel;
        CancellationSource cancellation;cancellation.cancel();
        QVERIFY(!rasterizeMaskPreview(prepared,map,alpha,actual,error,cancellation.token()));
        QCOMPARE(actual,sentinel);
        QVERIFY(!rasterizeMaskPreview(prepared,map,alpha,actual,error,{},128));
        QCOMPARE(actual,sentinel);
        auto bad=map;bad.outputSize={3841,1};
        QVERIFY(!rasterizeMaskPreview(prepared,bad,alpha,actual,error));QCOMPARE(actual,sentinel);
        QVERIFY(!rasterizeMaskPreview(prepared,map,frame({1,1}),actual,error));QCOMPARE(actual,sentinel);
        reinterpret_cast<float *>(alpha.scanLine(256))[4*512+3]=std::numeric_limits<float>::quiet_NaN();
        QVERIFY(!rasterizeMaskPreview(prepared,map,alpha,actual,error));QCOMPARE(actual,sentinel);
    }
};
QTEST_GUILESS_MAIN(LocalMaskPreviewTests)
#include "local_mask_preview_tests.moc"
