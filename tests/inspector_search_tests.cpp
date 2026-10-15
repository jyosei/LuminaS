#include "app/window.h"
#include "app/ui_primitives.h"
#include <QApplication>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QSlider>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <QSvgRenderer>
#include <QFile>
#include <QPushButton>
#include <QSignalSpy>
#include <QComboBox>
#include <QScrollArea>
#include <QWheelEvent>
#include <QScrollBar>
#include <QFileInfo>
#include <QAction>
#include <QDockWidget>
#include "inspector_navigation_support.h"
#include <cmath>

namespace {
QFrame *card(Window &w,const QString &title){
    for(auto *c:w.findChildren<QFrame *>("adjustment-card"))
        if(auto *h=c->findChild<QToolButton *>("card-header");h&&h->text()==title)return c;
    return nullptr;
}
double contrast(QColor a,QColor b){
    auto luminance=[](QColor c){auto f=[](double v){return v<=.04045?v/12.92:std::pow((v+.055)/1.055,2.4);};return .2126*f(c.redF())+.7152*f(c.greenF())+.0722*f(c.blueF());};
    const double x=luminance(a),y=luminance(b);return (std::max(x,y)+.05)/(std::min(x,y)+.05);
}
}
class InspectorSearchTests:public QObject {
    Q_OBJECT
private slots:
    void wheelOverUnfocusedParameterScrollsPanel(){
        QTemporaryDir dir;QVERIFY(dir.isValid());QImage image(120,80,QImage::Format_RGB32);image.fill(Qt::gray);const auto path=dir.filePath("wheel.jpg");QVERIFY(image.save(path));
        Window w(nullptr,QStringLiteral(""));w.show();w.addFiles({path});QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),30000);QVERIFY(openAllInspectorSections(w));
        auto *spin=w.findChild<QDoubleSpinBox *>("exposure");auto *scroll=w.findChild<QScrollArea *>();QVERIFY(spin);QVERIFY(scroll);scroll->ensureWidgetVisible(spin);QTest::qWait(50);
        w.findChild<QLineEdit *>("adjustment-search")->setFocus();QVERIFY(!spin->hasFocus());const auto before=w.currentRecipe();const int position=scroll->verticalScrollBar()->value();
        QWheelEvent wheel(QPointF(spin->rect().center()),QPointF(spin->mapToGlobal(spin->rect().center())),QPoint(),QPoint(0,-120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
        QApplication::sendEvent(spin,&wheel);QTest::qWait(50);QCOMPARE(w.currentRecipe(),before);QVERIFY(scroll->verticalScrollBar()->value()>position);
        scroll->ensureWidgetVisible(spin);spin->setFocus();QTest::qWait(50);QVERIFY(spin->hasFocus());
        QWheelEvent focused(QPointF(spin->rect().center()),QPointF(spin->mapToGlobal(spin->rect().center())),QPoint(),QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
        QApplication::sendEvent(spin,&focused);QVERIFY(spin->value()>before.exposure);w.findChild<QLineEdit *>("adjustment-search")->setFocus();w.undoEdit();QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),30000);QCOMPARE(w.currentRecipe(),before);
    }
    void curveKeyboardRendersAndUsesSharedHistory(){
        QTemporaryDir dir;QVERIFY(dir.isValid());QImage image(240,160,QImage::Format_RGB32);image.fill(QColor(130,120,100));const auto path=dir.filePath("curve.jpg");QVERIFY(image.save(path));
        Window w(nullptr,QStringLiteral(""));w.show();w.addFiles({path});QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),30000);
        w.findChild<QTabBar *>("adjustment-groups")->setCurrentIndex(2);auto *section=card(w,QStringLiteral("点曲线"));QVERIFY(section);section->findChild<QToolButton *>("card-header")->setChecked(true);
        auto *curve=section->findChild<CurveEditor *>();QVERIFY(curve);w.findChild<QScrollArea *>()->ensureWidgetVisible(curve);QTest::qWait(50);curve->setFocus();QTRY_VERIFY(curve->hasFocus());
        QTest::keyClick(curve,Qt::Key_Insert);QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),30000);QCOMPARE(w.currentRecipe().curves[0].size(),3);QCOMPARE(w.currentRecipe().curves[0][1],QPointF(.5,.5));
        QTest::keyClick(curve,Qt::Key_Up);QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),30000);QVERIFY(qAbs(w.currentRecipe().curves[0][1].y()-.505)<1e-12);
        QTest::keyClick(curve,Qt::Key_Z,Qt::ControlModifier);QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),30000);QCOMPARE(w.currentRecipe().curves[0][1].y(),.5);
        w.redoEdit();QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),30000);QVERIFY(qAbs(w.currentRecipe().curves[0][1].y()-.505)<1e-12);
    }
    void canvasToolStateAndCropNavigation(){
        QTemporaryDir dir;QVERIFY(dir.isValid());QImage image(180,120,QImage::Format_RGB32);image.fill(QColor(140,115,100));const auto path=dir.filePath("tools.jpg");QVERIFY(image.save(path));
        Window w(nullptr,QStringLiteral(""));w.show();w.addFiles({path});QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),30000);
        auto *hand=w.findChild<QToolButton *>("tool-hand");auto *crop=w.findChild<QToolButton *>("tool-crop");auto *exit=w.findChild<QPushButton *>("exit-crop");auto *wb=w.findChild<QPushButton *>("wb-picker");auto *scroll=w.findChild<QScrollArea *>();
        QVERIFY(hand);QVERIFY(crop);QVERIFY(exit);QVERIFY(wb);QVERIFY(scroll);QVERIFY(hand->isChecked());QVERIFY(!crop->isChecked());QVERIFY(exit->isHidden());
        auto *white=card(w,QStringLiteral("白平衡"));QVERIFY(white);white->findChild<QToolButton *>("card-header")->setChecked(true);scroll->ensureWidgetVisible(wb);QTest::qWait(50);QTest::mouseClick(wb,Qt::LeftButton);
        QVERIFY(wb->isChecked());QVERIFY(!hand->isChecked());QVERIFY(!crop->isChecked());const auto recipe=w.currentRecipe();
        QSignalSpy ready(&w,&Window::previewChanged);QTest::mouseClick(crop,Qt::LeftButton);QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty(),30000);
        QVERIFY(!wb->isChecked());QVERIFY(crop->isChecked());QVERIFY(!hand->isChecked());QVERIFY(exit->isVisible());
        auto *aspect=w.findChild<QComboBox *>("crop-aspect");QVERIFY(aspect);QTRY_VERIFY(aspect->isVisible());QVERIFY(aspect->isEnabled());aspect->setCurrentIndex(2);QCOMPARE(w.currentRecipe(),recipe);
        crop->setFocus();QTest::keyClick(crop,Qt::Key_Escape);QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),30000);QVERIFY(hand->isChecked());QVERIFY(!crop->isChecked());QVERIFY(exit->isHidden());QCOMPARE(w.currentRecipe(),recipe);
        w.findChild<QAction *>("workspace-browse")->trigger();QVERIFY(w.findChild<QDockWidget *>("adjustment-dock")->isHidden());
        QTest::mouseClick(crop,Qt::LeftButton);QTRY_VERIFY(exit->isVisible());QVERIFY(w.findChild<QAction *>("workspace-edit")->isChecked());QVERIFY(!w.findChild<QDockWidget *>("adjustment-dock")->isHidden());QTest::mouseClick(exit,Qt::LeftButton);QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),30000);QVERIFY(hand->isChecked());QVERIFY(exit->isHidden());
    }
    void failedReadHasRecoverableAction(){
        QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=dir.filePath("repair.jpg");
        {QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write("bad jpeg"),qint64(8));}
        Window w(nullptr,QStringLiteral(""));w.show();w.addFiles({path});auto *retry=w.findChild<QPushButton *>("preview-retry");QVERIFY(retry);
        QTRY_VERIFY_WITH_TIMEOUT(retry->isVisible(),30000);QVERIFY(retry->isEnabled());QVERIFY(!w.neutralPickerAvailable());
        auto *cacheLabel=w.findChild<QLabel *>("preview-cache-progress-label");QVERIFY(cacheLabel);QTRY_VERIFY(cacheLabel->text().contains(QStringLiteral("失败")));
        const auto recipe=w.currentRecipe();QImage repaired(120,80,QImage::Format_RGB32);repaired.fill(QColor(130,110,90));QVERIFY(repaired.save(path));
        QTest::mouseClick(retry,Qt::LeftButton);QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),30000);QVERIFY(retry->isHidden());QCOMPARE(w.currentRecipe(),recipe);
        QTRY_VERIFY(!cacheLabel->text().contains(QStringLiteral("失败")));
    }
    void vectorAssetsAndThemeStates(){
        Window w(nullptr,QStringLiteral(""));
        for(int i=0;i<=int(Ui::Glyph::ChevronDown);++i){
            const auto icon=Ui::icon(Ui::Glyph(i));
            for(int size:{16,24,48}){const auto pixels=icon.pixmap({size,size}).toImage().convertToFormat(QImage::Format_ARGB32);QVERIFY(!pixels.isNull());int visible=0;
                for(int y=0;y<pixels.height();++y)for(int x=0;x<pixels.width();++x)visible+=qAlpha(pixels.pixel(x,y))>16;
                QVERIFY2(visible>8,qPrintable(QStringLiteral("empty glyph %1 at %2").arg(i).arg(size)));}
        }
        const auto active=Ui::icon(Ui::Glyph::Hand).pixmap({24,24},QIcon::Normal).toImage();
        const auto disabled=Ui::icon(Ui::Glyph::Hand).pixmap({24,24},QIcon::Disabled).toImage();QVERIFY(active!=disabled);
        Ui::applyTheme(&w,false);QVERIFY(Ui::icon(Ui::Glyph::Hand).pixmap({24,24}).toImage()!=active);
        QSvgRenderer mark(QStringLiteral(":/icons/brand-mark.svg"));QVERIFY(mark.isValid());QCOMPARE(mark.viewBox(),QRect(0,0,64,64));
    }
    void groupsSearchAndHistory(){
        Window w(nullptr,QStringLiteral(""));w.show();QVERIFY(QTest::qWaitForWindowExposed(&w));
        auto *tabs=w.findChild<QTabBar *>("adjustment-groups");auto *search=w.findChild<QLineEdit *>("adjustment-search");auto *empty=w.findChild<QLabel *>("adjustment-search-empty");
        QVERIFY(tabs);QVERIFY(search);QVERIFY(empty);QCOMPARE(tabs->count(),6);
        auto *light=card(w,QStringLiteral("光线"));auto *hsl=card(w,QStringLiteral("HSL 分色"));auto *detail=card(w,QStringLiteral("细节"));QVERIFY(light);QVERIFY(hsl);QVERIFY(detail);
        const auto recipe=w.currentRecipe();tabs->setCurrentIndex(2);QVERIFY(light->isHidden());QVERIFY(!hsl->isHidden());QVERIFY(detail->isHidden());
        auto *header=light->findChild<QToolButton *>("card-header");header->setChecked(false);
        search->setText(QStringLiteral("曝光"));QVERIFY(!light->isHidden());QVERIFY(header->isChecked());QVERIFY(hsl->isHidden());QVERIFY(empty->isHidden());
        search->clear();QCOMPARE(tabs->currentIndex(),2);QVERIFY(light->isHidden());QVERIFY(!header->isChecked());QVERIFY(!hsl->isHidden());
        search->setText(QStringLiteral("no-matching-control-918"));QVERIFY(!empty->isHidden());
        search->setFocus();QTRY_VERIFY(search->hasFocus());QTest::keyClick(search,Qt::Key_Escape);QTRY_VERIFY(search->text().isEmpty());QVERIFY(empty->isHidden());
        QCOMPARE(w.currentRecipe(),recipe);QVERIFY(!w.findChild<QDoubleSpinBox *>("exposure")->isEnabled());
    }
    void namesAndContrast(){
        Window w(nullptr,QStringLiteral(""));
        for(const auto &id:{QStringLiteral("exposure"),QStringLiteral("sharpen-amount")}){
            auto *spin=w.findChild<QDoubleSpinBox *>(id);auto *slider=w.findChild<QSlider *>("slider-"+id);auto *label=w.findChild<QLabel *>("label-"+id);
            QVERIFY(spin);QVERIFY(slider);QVERIFY(label);QVERIFY(!spin->accessibleName().isEmpty());QCOMPARE(slider->accessibleName(),spin->accessibleName());QCOMPARE(label->buddy(),spin);
        }
        for(bool dark:{true,false}){
            Ui::applyTheme(&w,dark);const auto p=w.palette();
            qInfo()<<"theme"<<dark<<"text"<<p.color(QPalette::Text)<<"base"<<p.color(QPalette::Base)<<"contrast"<<contrast(p.color(QPalette::Text),p.color(QPalette::Base));
            QVERIFY(contrast(p.color(QPalette::Text),p.color(QPalette::Base))>=4.5);
            QVERIFY(contrast(p.color(QPalette::WindowText),p.color(QPalette::Window))>=4.5);
            QVERIFY(contrast(p.color(QPalette::Disabled,QPalette::Text),p.color(QPalette::Base))>=4.5);
            QVERIFY(contrast(p.color(QPalette::HighlightedText),p.color(QPalette::Highlight))>=4.5);
            // QSS selected controls use a separate, quieter surface; validate that pair too.
            QVERIFY(contrast(dark?QColor("#ffffff"):QColor("#2a2035"),dark?QColor("#3c3448"):QColor("#e9e0f3"))>=4.5);
        }
    }
    void nativeSyntheticPreview(){
        QTemporaryDir dir;QVERIFY(dir.isValid());QImage image(1200,800,QImage::Format_RGB32);
        for(int y=0;y<image.height();++y){auto *row=reinterpret_cast<QRgb *>(image.scanLine(y));for(int x=0;x<image.width();++x)row[x]=qRgb(45+x*180/image.width(),65+y*135/image.height(),100+x*70/image.width());}
        const auto external=qEnvironmentVariable("LUMINA_UI_TEST_PHOTO");const auto path=external.isEmpty()?dir.filePath("study.jpg"):QFileInfo(external).canonicalFilePath();QVERIFY(!path.isEmpty());if(external.isEmpty())QVERIFY(image.save(path));Window w(nullptr,QStringLiteral(""));w.show();w.addFiles({path});QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),90000);
        auto *tabs=w.findChild<QTabBar *>("adjustment-groups");QVERIFY(tabs);tabs->setCurrentIndex(1);QTest::qWait(100);
        auto *exposure=w.findChild<QDoubleSpinBox *>("exposure");QVERIFY(exposure->isVisible());QVERIFY(exposure->isEnabled());QVERIFY2(exposure->visibleRegion().contains(exposure->rect()),"initial exposure field is clipped");qInfo()<<"nativeDPR"<<w.devicePixelRatioF()<<"logicalWindow"<<w.size();exposure->setValue(.25);QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),30000);QCOMPARE(w.currentRecipe().exposure,.25);
        // Tab navigation must reach both controls and reverse without editing.
        const auto beforeTab=w.currentRecipe();
        auto *slider=w.findChild<QSlider *>("slider-exposure");QVERIFY(slider);
        auto *reset=w.findChild<QPushButton *>("reset-exposure");QVERIFY(reset);
        slider->setFocus();QTRY_VERIFY(slider->hasFocus());
        QTest::keyClick(slider,Qt::Key_Tab);QVERIFY(exposure->hasFocus());
        QTest::keyClick(exposure,Qt::Key_Tab);QVERIFY(reset->hasFocus());
        QTest::keyClick(reset,Qt::Key_Backtab);QVERIFY(exposure->hasFocus());
        QTest::keyClick(exposure,Qt::Key_Backtab);QVERIFY(slider->hasFocus());
        QCOMPARE(w.currentRecipe(),beforeTab);
        const auto shot=qEnvironmentVariable("LUMINA_INSPECTOR_SCREENSHOT");if(!shot.isEmpty())QVERIFY(w.grab().save(shot));
    }
};
QTEST_MAIN(InspectorSearchTests)
#include "inspector_search_tests.moc"
