#include "app/window.h"
#include <QGuiApplication>
#include <QLabel>
#include <QListWidget>
#include <QScreen>
#include <QTest>
#include <QToolButton>

// Production startup sizing: the Window constructor resizes to the available screen and must
// resolve responsive visibility (filmstrip / navigation hint) for that size before the first
// show, so the initial layout cannot inflate the frame beyond the available geometry.
class StartupLayoutTests : public QObject {
    Q_OBJECT
private slots:
    void productionStartupRespectsAvailableScreen();
};

void StartupLayoutTests::productionStartupRespectsAvailableScreen(){
    auto *screen=QGuiApplication::primaryScreen();
    QVERIFY(screen);
    const auto available=screen->availableGeometry();
    Window window(nullptr,QStringLiteral(""));
    // No test-side clamp: only the production constructor sizing under test.
    window.show();
    QTRY_VERIFY(window.isVisible());
    QTRY_VERIFY(window.frameGeometry().isValid());
    qInfo()<<"scaleFactor"<<screen->devicePixelRatio()<<"available"<<available
           <<"frame"<<window.frameGeometry()<<"size"<<window.size()
           <<"minimumSizeHint"<<window.minimumSizeHint();
    QTRY_VERIFY2(available.contains(window.frameGeometry()),
                 qPrintable(QStringLiteral("frame %1 outside available %2")
                                .arg(QStringLiteral("(%1,%2 %3x%4)")
                                         .arg(window.frameGeometry().x())
                                         .arg(window.frameGeometry().y())
                                         .arg(window.frameGeometry().width())
                                         .arg(window.frameGeometry().height()))
                                .arg(QStringLiteral("(%1,%2 %3x%4)")
                                         .arg(available.x())
                                         .arg(available.y())
                                         .arg(available.width())
                                         .arg(available.height()))));
    // Top-level non-client size: frame, not client size, must fit the available height/width.
    QVERIFY(window.frameGeometry().height()<=available.height());
    QVERIFY(window.frameGeometry().width()<=available.width());

    auto *strip=window.findChild<QListWidget *>("filmstrip");
    auto *toggle=window.findChild<QToolButton *>("filmstrip-toggle");
    auto *hint=window.findChild<QLabel *>("navigation-hint");
    QVERIFY(strip);
    QVERIFY(toggle);
    QVERIFY(hint);
    QVERIFY(toggle->isChecked());
    const int height=window.height(),width=window.width();
    QCOMPARE(strip->isVisible(),height>=500);
    QCOMPARE(toggle->isEnabled(),height>=500);
    QCOMPARE(hint->isVisible(),width>=1000);

    // Responsive thresholds still hold when resized after startup.
    window.resize(640,480);
    QTRY_VERIFY(!strip->isVisible());
    QVERIFY(!toggle->isEnabled());
    QVERIFY(toggle->isChecked());
    window.resize(900,700);
    QTRY_VERIFY(strip->isVisible());
    QVERIFY(toggle->isEnabled());
    QVERIFY(!hint->isVisible());
    window.resize(1100,700);
    QTRY_VERIFY(hint->isVisible());
    QVERIFY(strip->isVisible());
    QVERIFY(toggle->isChecked());
}

QTEST_MAIN(StartupLayoutTests)
#include "startup_layout_tests.moc"
