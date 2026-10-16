#include "app/local_gradient_controls.h"
#include <QtTest>
#include <QComboBox>
#include <QPushButton>
using namespace LocalEdit;
namespace {
DocumentSnapshot document(){
    PhotoDocument doc;AdjustmentLayer layer;layer.id="layer";
    MaskComponent brush;brush.id="brush";
    MaskComponent linear;linear.id="linear";linear.geometry=LinearGeometry{};
    MaskComponent radial;radial.id="radial";radial.geometry=RadialGeometry{};radial.operation=MaskOperation::Subtract;
    layer.mask.components={brush,linear,radial};doc.layers={layer};
    return std::make_shared<const PhotoDocument>(doc);
}
}
class LocalGradientControlsTests:public QObject {
    Q_OBJECT
private slots:
    void refreshGatingIdsAndNoSignals(){
        LocalGradientControls panel;const auto doc=document();const auto frozen=*doc;
        auto *combo=panel.findChild<QComboBox *>("local-mask-components");
        auto *remove=panel.findChild<QPushButton *>("local-mask-remove");
        auto *linear=panel.findChild<QPushButton *>("local-gradient-linear");
        QSignalSpy tools(&panel,&LocalGradientControls::toolRequested),selection(&panel,&LocalGradientControls::componentSelected),removal(&panel,&LocalGradientControls::componentRemovalRequested);
        QVERIFY(combo);QVERIFY(remove);QVERIFY(linear);QVERIFY(!linear->isEnabled());
        panel.setDocument(doc,"layer","radial");QCOMPARE(combo->count(),4);QCOMPARE(combo->currentData().toString(),QString("radial"));
        QCOMPARE(combo->itemData(1).toString(),QString("brush"));QCOMPARE(combo->itemData(2).toString(),QString("linear"));
        QVERIFY(combo->itemText(3).contains(QStringLiteral("径向")));QVERIFY(combo->itemText(3).contains(QStringLiteral("减去")));
        QVERIFY(!remove->isEnabled());panel.setAvailable(true);QVERIFY(remove->isEnabled());
        panel.setActiveKind(2);QVERIFY(panel.findChild<QPushButton *>("local-gradient-radial")->isChecked());
        QCOMPARE(tools.count(),0);QCOMPARE(selection.count(),0);QCOMPARE(removal.count(),0);
        panel.setDocument(doc,"layer","gone");QCOMPARE(combo->currentIndex(),0);QVERIFY(!remove->isEnabled());
        panel.setDocument(doc,"gone","radial");QCOMPARE(combo->count(),1);QVERIFY(!linear->isEnabled());QCOMPARE(*doc,frozen);
    }
    void clickSelectionAndReentrantRefreshCopyIds(){
        LocalGradientControls panel;panel.setDocument(document(),"layer");panel.setAvailable(true);panel.show();QVERIFY(QTest::qWaitForWindowExposed(&panel));
        auto *linear=panel.findChild<QPushButton *>("local-gradient-linear");
        auto *radial=panel.findChild<QPushButton *>("local-gradient-radial");
        auto *combo=panel.findChild<QComboBox *>("local-mask-components");
        auto *remove=panel.findChild<QPushButton *>("local-mask-remove");
        QSignalSpy tools(&panel,&LocalGradientControls::toolRequested);
        QTest::mouseClick(linear,Qt::LeftButton);QCOMPARE(tools.last()[0].toInt(),1);QVERIFY(linear->isChecked());QVERIFY(!radial->isChecked());
        QTest::mouseClick(radial,Qt::LeftButton);QCOMPARE(tools.last()[0].toInt(),2);QVERIFY(!linear->isChecked());QVERIFY(radial->isChecked());
        QTest::mouseClick(radial,Qt::LeftButton);QCOMPARE(tools.last()[0].toInt(),0);QVERIFY(!radial->isChecked());
        connect(&panel,&LocalGradientControls::componentSelected,&panel,[&panel]{panel.setDocument(document(),"layer","linear");});
        QSignalSpy selection(&panel,&LocalGradientControls::componentSelected);
        combo->setCurrentIndex(3);QCOMPARE(selection.count(),1);QCOMPARE(selection[0][0].toString(),QString("radial"));QCOMPARE(combo->currentData().toString(),QString("linear"));
        connect(&panel,&LocalGradientControls::componentRemovalRequested,&panel,[&panel]{panel.setDocument({},{});});
        QSignalSpy removal(&panel,&LocalGradientControls::componentRemovalRequested);
        QTest::mouseClick(remove,Qt::LeftButton);QCOMPARE(removal.count(),1);QCOMPARE(removal[0][0].toString(),QString("linear"));QCOMPARE(combo->count(),1);
    }
};
QTEST_MAIN(LocalGradientControlsTests)
#include "local_gradient_controls_tests.moc"
