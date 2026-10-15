#include "app/ui_primitives.h"
#include <QDoubleSpinBox>
#include <QStyleOptionSpinBox>
#include <QImage>
#include <QtTest>
#include <cmath>

class StyledSpin final:public QDoubleSpinBox {
public:
    using QDoubleSpinBox::QDoubleSpinBox;
    QRect button(QStyle::SubControl control) const {
        QStyleOptionSpinBox option;initStyleOption(&option);
        return style()->subControlRect(QStyle::CC_SpinBox,&option,control,this);
    }
};
class SpinIndicatorTests:public QObject {
    Q_OBJECT
private slots:
    void resourcesLoad();
    void actualButtons_data();void actualButtons();
    void visibleArrow_data();void visibleArrow();
};
void SpinIndicatorTests::resourcesLoad(){
    Ui::brandFont();
    for(const auto &theme:{"dark","light"})for(const auto &direction:{"up","down"})for(bool disabled:{false,true}){
        const QString path=QString(":/ui/spin-%1-%2%3.xpm").arg(direction,theme,disabled?"-disabled":"");
        const QImage image(path);QVERIFY2(!image.isNull(),qPrintable(path));QCOMPARE(image.size(),QSize(16,16));
        QCOMPARE(image.pixelColor(0,0).alpha(),0);int opaque=0;
        for(int y=0;y<16;++y)for(int x=0;x<16;++x)if(image.pixelColor(x,y).alpha()>0)++opaque;
        QVERIFY(opaque>=8);QVERIFY(opaque<200);
        int first=-1,last=-1,topWidth=0,bottomWidth=0;
        for(int y=0;y<16;++y){int count=0;for(int x=0;x<16;++x)count+=image.pixelColor(x,y).alpha()>0;if(count){if(first<0){first=y;topWidth=count;}last=y;bottomWidth=count;}}
        QVERIFY(first>=0&&last>first);
        if(QString::fromLatin1(direction)==QStringLiteral("up"))QVERIFY2(topWidth<bottomWidth,"up arrow must have its tip above its base");
        else QVERIFY2(topWidth>bottomWidth,"down arrow must have its tip below its base");
    }
}
void SpinIndicatorTests::actualButtons_data(){
    QTest::addColumn<bool>("dark");QTest::newRow("dark")<<true;QTest::newRow("light")<<false;
}
void SpinIndicatorTests::actualButtons(){
    QFETCH(bool,dark);QWidget host;StyledSpin spin(&host);host.resize(240,100);spin.setGeometry(20,20,160,32);
    spin.setRange(-1,1);spin.setSingleStep(.1);spin.setDecimals(2);spin.setAccessibleName("数值");
    Ui::applyTheme(&host,dark);host.show();QVERIFY(QTest::qWaitForWindowExposed(&host));QTest::qWait(20);
    const auto up=spin.button(QStyle::SC_SpinBoxUp),down=spin.button(QStyle::SC_SpinBoxDown);
    QVERIFY(up.isValid()&&down.isValid());QVERIFY(spin.rect().contains(up.center()));QVERIFY(spin.rect().contains(down.center()));
    QTest::mouseClick(&spin,Qt::LeftButton,Qt::NoModifier,up.center());QCOMPARE(spin.value(),.1);
    QTest::mouseClick(&spin,Qt::LeftButton,Qt::NoModifier,down.center());QCOMPARE(spin.value(),0.);
    spin.setValue(1);QTest::mouseClick(&spin,Qt::LeftButton,Qt::NoModifier,up.center());QCOMPARE(spin.value(),1.);
    spin.setValue(-1);QTest::mouseClick(&spin,Qt::LeftButton,Qt::NoModifier,down.center());QCOMPARE(spin.value(),-1.);
    spin.setValue(0);spin.setEnabled(false);QTest::mouseClick(&spin,Qt::LeftButton,Qt::NoModifier,up.center());QCOMPARE(spin.value(),0.);
    spin.setEnabled(true);spin.setFocus();spin.selectAll();QTest::keyClicks(&spin,"0.40");QTest::keyClick(&spin,Qt::Key_Return);
    QCOMPARE(spin.value(),.4);QCOMPARE(spin.accessibleName(),QString("数值"));
}
void SpinIndicatorTests::visibleArrow_data(){actualButtons_data();}
static double luminance(QColor c){
    auto linear=[](double v){v/=255;return v<=.04045?v/12.92:std::pow((v+.055)/1.055,2.4);};
    return .2126*linear(c.red())+.7152*linear(c.green())+.0722*linear(c.blue());
}
void SpinIndicatorTests::visibleArrow(){
    QFETCH(bool,dark);QWidget host;StyledSpin spin(&host);host.resize(240,100);spin.setGeometry(20,20,160,32);spin.setRange(-1,1);
    Ui::applyTheme(&host,dark);host.show();QVERIFY(QTest::qWaitForWindowExposed(&host));QTest::qWait(20);
    const auto image=spin.grab().toImage();const double dpr=image.devicePixelRatio();const double bg=luminance(QColor(dark?"#2c2d31":"#f8f8fa"));
    for(auto control:{QStyle::SC_SpinBoxUp,QStyle::SC_SpinBoxDown}){
        const auto logical=spin.button(control);
        const QRect pixels(int(std::floor(logical.x()*dpr)),int(std::floor(logical.y()*dpr)),int(std::ceil(logical.width()*dpr)),int(std::ceil(logical.height()*dpr)));
        const auto region=pixels.intersected(image.rect());QVERIFY(!region.isEmpty());int contrasting=0;
        for(int y=region.top();y<=region.bottom();++y)for(int x=region.left();x<=region.right();++x){
            const auto colour=image.pixelColor(x,y);if(colour.alpha()<16)continue;const double value=luminance(colour);
            if((std::max(value,bg)+.05)/(std::min(value,bg)+.05)>3)++contrasting;
        }
        QVERIFY2(contrasting>=4,qPrintable(QString("%1 theme control %2 has %3 contrasting pixels").arg(dark?"dark":"light").arg(int(control)).arg(contrasting)));
    }
}
QTEST_MAIN(SpinIndicatorTests)
#include "spin_indicator_tests.moc"
