#include <QtTest>
#include <QDoubleSpinBox>
#include <QSlider>
#include <QCheckBox>
#include <QComboBox>
#include "app/local_mask_options.h"
using namespace LocalEdit;
class LocalMaskOptionsTests:public QObject {
    Q_OBJECT
private slots:
    void syncingAndBindingDoNotEdit(){
        LocalMaskOptions panel;Mask mask;mask.feather=12.3;mask.invert=true;
        QSignalSpy edits(&panel,&LocalMaskOptions::featherEdited),finish(&panel,&LocalMaskOptions::featherFinished),inverted(&panel,&LocalMaskOptions::invertRequested),colors(&panel,&LocalMaskOptions::colorSelected),bw(&panel,&LocalMaskOptions::blackWhiteToggled);
        panel.setMask(&mask,"asset/layer");panel.setAvailable(true);panel.setView(QColor(239,90,90),true);
        auto *spin=panel.findChild<QDoubleSpinBox *>("local-mask-feather");auto *slider=panel.findChild<QSlider *>("slider-local-mask-feather");QVERIFY(spin);QVERIFY(slider);
        QCOMPARE(spin->value(),12.3);QCOMPARE(spin->maximum(),16384.);QCOMPARE(edits.count(),0);QCOMPARE(inverted.count(),0);QCOMPARE(colors.count(),0);QCOMPARE(bw.count(),0);
        spin->setValue(4.2);QMetaObject::invokeMethod(spin,"editingFinished",Qt::DirectConnection);panel.setMask(&mask,"other/layer");QCoreApplication::processEvents();QCOMPARE(finish.count(),0);
        panel.setMask(nullptr);QVERIFY(!spin->isEnabled());QVERIFY(!slider->isEnabled());
    }
    void dragKeyboardAndStaleFinish(){
        LocalMaskOptions panel;Mask mask;panel.setMask(&mask,"a");panel.setAvailable(true);panel.show();QVERIFY(QTest::qWaitForWindowExposed(&panel));
        auto *spin=panel.findChild<QDoubleSpinBox *>("local-mask-feather");auto *slider=panel.findChild<QSlider *>("slider-local-mask-feather");
        QSignalSpy edits(&panel,&LocalMaskOptions::featherEdited),finish(&panel,&LocalMaskOptions::featherFinished);
        slider->setSliderDown(true);slider->setValue(200);slider->setValue(300);QVERIFY(edits.count()>0);
        mask.feather=spin->value();panel.setMask(&mask,"a");QCoreApplication::processEvents();QCOMPARE(finish.count(),0);
        slider->setSliderDown(false);QTRY_COMPARE(finish.count(),1);
        slider->setFocus();QTest::keyClick(slider,Qt::Key_Right);QTRY_COMPARE(finish.count(),2);
        spin->setValue(3.4);QMetaObject::invokeMethod(spin,"editingFinished",Qt::DirectConnection);
        slider->setSliderDown(true);slider->setValue(400);QCoreApplication::processEvents();QCOMPARE(finish.count(),2);
        slider->setSliderDown(false);QTRY_COMPARE(finish.count(),3);
        QCOMPARE(edits.last()[0].toDouble(),spin->value());
        spin->setValue(8.7);QMetaObject::invokeMethod(spin,"editingFinished",Qt::DirectConnection);panel.setAvailable(false);QCoreApplication::processEvents();QCOMPARE(finish.count(),3);
    }
};
QTEST_MAIN(LocalMaskOptionsTests)
#include "local_mask_options_tests.moc"
