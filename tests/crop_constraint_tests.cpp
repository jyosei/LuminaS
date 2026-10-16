#include <QtTest>
#include <QLineF>
#include <cmath>
#include <limits>
#include "render/crop_constraint.h"
#include "render/cpu_renderer.h"

namespace {
SourceFrame source(QSize size,bool raw=false){
    SourceFrame s;s.originalSize=size;s.raw=raw;s.cameraToWork={1,0,0,0,1,0,0,0,1};
    s.linear=QImage(size,QImage::Format_RGBA32FPx4);
    for(int y=0;y<size.height();++y){auto *p=reinterpret_cast<float *>(s.linear.scanLine(y));
        for(int x=0;x<size.width();++x){p[4*x]=.2f;p[4*x+1]=.3f;p[4*x+2]=.4f;p[4*x+3]=1;}}
    return s;
}
double minimumAlpha(const QImage &image){
    double value=1;for(int y=0;y<image.height();++y){const auto *p=reinterpret_cast<const float *>(image.constScanLine(y));
        for(int x=0;x<image.width();++x){if(!std::isfinite(p[x*4+3]))return -1;value=std::min(value,double(p[x*4+3]));}}
    return value;
}
}
class CropConstraintTests:public QObject {
    Q_OBJECT
private slots:
    void neutralAndContainedAreUnchanged(){
        Recipe r;r.crop={.1,.15,.7,.6};QRectF fitted;QString error;
        QVERIFY(constrainedCrop({600,400},r,fitted,error));QCOMPARE(fitted,r.crop);
        r.angle=7;r.crop={.2,.22,.6,.56};
        QVERIFY(constrainedCrop({600,400},r,fitted,error));QCOMPARE(fitted,r.crop);
    }
    void geometryAndChannelSupport(){
        int checks=0;double min=1;
        for(QSize size:{QSize(120,80),QSize(80,120),QSize(96,96)})for(bool raw:{false,true})
        for(int kind=0;kind<6;++kind)for(int flips=0;flips<4;++flips){
            const auto s=source(size,raw);Recipe r;r.angle=kind%2?-45:7;
            if(kind>=2)r.perspective={.3,-.3};
            if(kind>=3)r.distortion=kind%2?.4:-.4;
            if(kind>=4)r.chromaticAberration={10,-10};
            if(kind==5)r.crop={.08,.12,.84,.76};
            r.flipHorizontal=flips&1;r.flipVertical=flips&2;
            const auto original=r;QRectF fitted;QString error;
            QVERIFY2(constrainedCrop(size,r,fitted,error),qPrintable(error));
            QVERIFY(r==original);QVERIFY(fitted.width()<=r.crop.width());QVERIFY(fitted.height()<=r.crop.height());
            QVERIFY(QLineF(fitted.center(),r.crop.center()).length()<1e-12);
            QVERIFY(std::abs(fitted.width()/fitted.height()-r.crop.width()/r.crop.height())<1e-12);
            auto adjusted=r;adjusted.crop=fitted;QVERIFY(validRecipe(adjusted));
            QRectF again;QVERIFY(constrainedCrop(size,adjusted,again,error));QCOMPARE(again,fitted);
            for(int edge:{0,48}){
                const auto out=renderLinear(s,adjusted,edge,error);QVERIFY2(!out.isNull(),qPrintable(error));
                const auto alpha=minimumAlpha(out);
                QVERIFY2(alpha>.9999,qPrintable(QString("alpha %1 size %2x%3 raw %4 kind %5 flips %6 edge %7").arg(alpha).arg(size.width()).arg(size.height()).arg(raw).arg(kind).arg(flips).arg(edge)));
                min=std::min(min,alpha);++checks;
            }
        }
        QCOMPARE(checks,288);qInfo()<<"constrained geometry renders"<<checks<<"minimumAlpha"<<min;
    }
    void straightenIsTightWithoutOpticalCorrections(){
        Recipe r;r.angle=7;QRectF fitted;QString error;const QSize size(6960,4640);
        QVERIFY(constrainedCrop(size,r,fitted,error));
        const double a=7*3.14159265358979323846/180;
        const double expected=std::min((size.width()-1.)/(size.width()*std::cos(a)+size.height()*std::sin(a)),
            (size.height()-1.)/(size.width()*std::sin(a)+size.height()*std::cos(a)));
        QVERIFY(std::abs(fitted.width()-expected)<1e-8);QVERIFY(fitted.width()>.84);
    }
    void invalidAndImpossibleDoNotModifyOutput(){
        QRectF sentinel(.1,.2,.3,.4),out=sentinel;QString error;Recipe r;
        QVERIFY(!constrainedCrop({},r,out,error));QVERIFY(!error.isEmpty());QCOMPARE(out,sentinel);
        r.angle=std::numeric_limits<double>::quiet_NaN();QVERIFY(!constrainedCrop({600,400},r,out,error));QCOMPARE(out,sentinel);
        r={};r.angle=45;QVERIFY(!constrainedCrop({1,1},r,out,error));QCOMPARE(out,sentinel);
        r={};r.crop={.94,.94,.05,.05};r.perspective={-.3,-.3};
        QVERIFY(!constrainedCrop({600,400},r,out,error));QVERIFY(!error.isEmpty());QCOMPARE(out,sentinel);
    }
};
QTEST_GUILESS_MAIN(CropConstraintTests)
#include "crop_constraint_tests.moc"
