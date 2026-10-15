// Comprehensive inspector reachability. Draft only: GPT owns CMake wiring and
// execution. Reveals every adjustment card through the public six-tab group bar
// and user-facing section headers, then proves the QDoubleSpinBox/QSlider set
// inside is genuinely on screen and that scrolling never disturbs the Recipe.
#include "app/window.h"
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QImage>
#include <QLineEdit>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <array>

namespace {
// Cards are reachable only through their own header, so identify by header text.
QFrame *cardByName(Window &w,const QString &title){
    for(auto *c:w.findChildren<QFrame *>(QStringLiteral("adjustment-card")))
        if(auto *h=c->findChild<QToolButton *>(QStringLiteral("card-header"));h&&h->text()==title)return c;
    return nullptr;
}
// Navigation recreates the dock payload; the live scroll area is the one whose
// widget is the adjustment panel.
QScrollArea *panelScroll(Window &w){
    for(auto *a:w.findChildren<QScrollArea *>()){
        auto *inner=a->widget();
        if(inner&&inner->objectName()==QStringLiteral("adjustment-panel"))return a;
    }
    return nullptr;
}
void showForInspection(Window &w){const auto size=qEnvironmentVariable("LUMINA_INSPECTOR_SIZE").split('x');if(size.size()==2)w.resize(size[0].toInt(),size[1].toInt());w.show();qInfo()<<"inspectionWindow"<<w.size()<<"DPR"<<w.devicePixelRatioF();}
void settle(){QCoreApplication::processEvents();QTest::qWait(5);QCoreApplication::processEvents();}
// Reveal one card using only public affordances: owning group tab, header
// expansion, then the containing viewport.
QToolButton *revealSection(Window &w,int group,const QString &title){
    auto *tabs=w.findChild<QTabBar *>(QStringLiteral("adjustment-groups"));
    if(!tabs)return nullptr;
    tabs->setCurrentIndex(group);settle();
    auto *card=cardByName(w,title);
    if(!card)return nullptr;
    auto *header=card->findChild<QToolButton *>(QStringLiteral("card-header"));
    if(!header)return nullptr;
    if(!header->isChecked())header->setChecked(true);
    settle();
    if(auto *scroll=panelScroll(w))scroll->ensureWidgetVisible(card);
    settle();
    return header;
}
} // namespace

class InspectorReachabilityTests:public QObject {
    Q_OBJECT
private slots:
    void everyCardControlIsReachable(){
        QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=dir.filePath(QStringLiteral("reach.jpg"));
        {QImage image(640,420,QImage::Format_RGB32);
         for(int y=0;y<image.height();++y){auto *row=reinterpret_cast<QRgb *>(image.scanLine(y));
             for(int x=0;x<image.width();++x)row[x]=qRgb(40+x*150/image.width(),70+y*120/image.height(),120);}
         QVERIFY(image.save(path));}
        Window w(nullptr,QStringLiteral(""));showForInspection(w);QVERIFY(QTest::qWaitForWindowExposed(&w));
        w.addFiles({path});QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),30000);
        auto *tabs=w.findChild<QTabBar *>(QStringLiteral("adjustment-groups"));
        QVERIFY(tabs);QCOMPARE(tabs->count(),6);QVERIFY(panelScroll(w));
        // Section title -> owning group tab. Missing titles are skipped; the
        // revealed/checked floors below still require broad coverage.
        const std::array<QPair<const char *,int>,21> sections{{
            {"风格预设",5},{"白平衡",1},{"光线",1},{"色彩",1},{"质感",1},
            {"细节",3},{"颗粒",5},{"裁剪后暗角",5},{"点曲线",2},{"参数曲线",2},
            {"HSL 分色",2},{"精确分色",2},{"颜色分级",2},{"黑白混色",2},{"原色调整",2},
            {"手动横向色差",4},{"紫绿边去色",4},{"畸变校正",4},{"镜头暗角校正",4},{"透视",4},{"构图",4}}};
        Recipe configured=w.currentRecipe();configured.monochrome=true;PointColorAdjustment point;point.hue=30;point.saturation=.5;point.luminance=.5;configured.pointColors.append(point);w.applyRecipe(configured);QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),30000);
        const auto untouched=w.currentRecipe();int revealed=0,checked=0;
        for(const auto &entry:sections){
            const QString title=QString::fromUtf8(entry.first);
            auto *header=revealSection(w,entry.second,title);
            QVERIFY2(header,qPrintable(title+QStringLiteral(" missing")));
            QVERIFY2(header->isChecked(),qPrintable(title+QStringLiteral(" header not expanded")));
            ++revealed;auto *card=header->parentWidget();QVERIFY(card);
            // Blackwhite mix is intentionally disabled until 黑白 is enabled, so
            // visibility is always required but enabled/clipping only when enabled.
            for(auto *spin:card->findChildren<QDoubleSpinBox *>()){
                if(auto *scroll=panelScroll(w))scroll->ensureWidgetVisible(spin);
                settle();
                QVERIFY2(!spin->isHidden(),qPrintable(QStringLiteral("%1/%2 hidden").arg(title,spin->objectName())));
                QVERIFY2(spin->visibleRegion().contains(spin->rect()),
                    qPrintable(QStringLiteral("%1/%2 clipped").arg(title,spin->objectName())));
                ++checked;}
            for(auto *slider:card->findChildren<QSlider *>()){
                if(auto *scroll=panelScroll(w))scroll->ensureWidgetVisible(slider);
                settle();
                QVERIFY2(!slider->isHidden(),qPrintable(QStringLiteral("%1/%2 slider hidden").arg(title,slider->objectName())));
                QVERIFY2(slider->visibleRegion().contains(slider->rect()),
                    qPrintable(QStringLiteral("%1/%2 slider clipped").arg(title,slider->objectName())));
                ++checked;}
        }
        QVERIFY2(revealed==21,qPrintable(QStringLiteral("revealed %1").arg(revealed)));
        QVERIFY2(checked>=100,qPrintable(QStringLiteral("inspected %1 controls").arg(checked)));QCOMPARE(w.currentRecipe(),untouched);qInfo()<<"reachableSections"<<revealed<<"controls"<<checked;
    }
    void scrollingDoesNotMutateRecipe(){
        QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=dir.filePath(QStringLiteral("scroll.jpg"));
        {QImage image(480,320,QImage::Format_RGB32);image.fill(QColor(90,110,140));QVERIFY(image.save(path));}
        Window w(nullptr,QStringLiteral(""));showForInspection(w);QVERIFY(QTest::qWaitForWindowExposed(&w));
        w.addFiles({path});QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),30000);
        auto *tabs=w.findChild<QTabBar *>(QStringLiteral("adjustment-groups"));QVERIFY(tabs);
        tabs->setCurrentIndex(1);settle();
        auto *scroll=panelScroll(w);QVERIFY(scroll);QVERIFY(scroll->widget());
        const auto recipe=w.currentRecipe();
        auto *exposure=w.findChild<QDoubleSpinBox *>(QStringLiteral("exposure"));
        QVERIFY(exposure);QVERIFY(exposure->isEnabled());
        auto *bar=scroll->verticalScrollBar();QVERIFY(bar);const int top=bar->value();
        bar->setValue(bar->maximum());settle();bar->setValue(bar->minimum());settle();
        bar->setValue(top);settle();
        QCOMPARE(w.currentRecipe(),recipe);QCOMPARE(exposure->value(),0.0);
    }
    void sectionsExpandAndCollapseWithoutRecipeChange(){
        QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=dir.filePath(QStringLiteral("toggle.jpg"));
        {QImage image(320,240,QImage::Format_RGB32);image.fill(QColor(120,90,60));QVERIFY(image.save(path));}
        Window w(nullptr,QStringLiteral(""));showForInspection(w);QVERIFY(QTest::qWaitForWindowExposed(&w));
        w.addFiles({path});QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),30000);
        const auto recipe=w.currentRecipe();
        auto *header=revealSection(w,1,QStringLiteral("光线"));QVERIFY(header);QVERIFY(header->isChecked());
        header->setChecked(false);settle();
        auto *body=header->parentWidget()->findChild<QWidget *>(QStringLiteral("card-body"));
        QVERIFY(body);QVERIFY(body->isHidden());
        header->setChecked(true);settle();QVERIFY(!body->isHidden());
        QCOMPARE(w.currentRecipe(),recipe);
    }
    void keyboardGroupNavigationAndExactSearch(){
        QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=dir.filePath(QStringLiteral("navigate.jpg"));
        {QImage image(400,300,QImage::Format_RGB32);image.fill(QColor(70,130,110));QVERIFY(image.save(path));}
        Window w(nullptr,QStringLiteral(""));showForInspection(w);QVERIFY(QTest::qWaitForWindowExposed(&w));
        w.addFiles({path});QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),30000);
        auto *tabs=w.findChild<QTabBar *>(QStringLiteral("adjustment-groups"));QVERIFY(tabs);
        tabs->setFocus();QTRY_VERIFY(tabs->hasFocus());
        const int start=tabs->currentIndex();
        QTest::keyClick(tabs,Qt::Key_Right);settle();QVERIFY(tabs->currentIndex()!=start||tabs->count()==1);
        QTest::keyClick(tabs,Qt::Key_Left);settle();QCOMPARE(tabs->currentIndex(),start);
        // Exact parameter search reveals the owning card across groups and must
        // leave the Recipe untouched.
        const auto recipe=w.currentRecipe();
        auto *search=w.findChild<QLineEdit *>(QStringLiteral("adjustment-search"));QVERIFY(search);
        tabs->setCurrentIndex(1);settle();
        auto *detail=cardByName(w,QStringLiteral("细节"));QVERIFY(detail);QVERIFY(detail->isHidden());
        search->setText(QStringLiteral("锐化"));settle();QVERIFY(!detail->isHidden());
        auto *sharpen=detail->findChild<QDoubleSpinBox *>(QStringLiteral("sharpen-amount"));
        QVERIFY(sharpen);QVERIFY(sharpen->isEnabled());
        if(auto *scroll=panelScroll(w))scroll->ensureWidgetVisible(sharpen);
        settle();QVERIFY(sharpen->visibleRegion().contains(sharpen->rect()));
        QCOMPARE(w.currentRecipe(),recipe);
        search->clear();settle();QCOMPARE(w.currentRecipe(),recipe);
    }
    void curveEditorsAndPointColorPanelShow(){
        QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=dir.filePath(QStringLiteral("curves.jpg"));
        {QImage image(300,300,QImage::Format_RGB32);image.fill(QColor(100,100,140));QVERIFY(image.save(path));}
        Window w(nullptr,QStringLiteral(""));showForInspection(w);QVERIFY(QTest::qWaitForWindowExposed(&w));
        w.addFiles({path});QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),30000);
        for(const auto &title:{QStringLiteral("点曲线"),QStringLiteral("参数曲线"),QStringLiteral("精确分色")}){
            auto *header=revealSection(w,2,title);
            QVERIFY2(header,qPrintable(title+QStringLiteral(" missing")));
            auto *card=header->parentWidget();QVERIFY(card);
            for(auto *editor:card->findChildren<CurveEditor *>()){
                if(auto *scroll=panelScroll(w))scroll->ensureWidgetVisible(editor);
                settle();
                QVERIFY2(!editor->isHidden(),qPrintable(title+QStringLiteral(" curve editor hidden")));
                QVERIFY(editor->isVisible());}
        }
        auto *panel=w.findChild<PointColorPanel *>(QStringLiteral("point-color-panel"));
        QVERIFY(panel);QVERIFY(panel->isVisible());
        if(auto *scroll=panelScroll(w))scroll->ensureWidgetVisible(panel);
        settle();QVERIFY(!panel->isHidden());
    }
    void groupScreenshotsForGptReview(){
        const auto outDir=qEnvironmentVariable("LUMINA_INSPECTOR_GROUP_SHOTS");
        if(outDir.isEmpty())QSKIP("LUMINA_INSPECTOR_GROUP_SHOTS not set");
        QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=dir.filePath(QStringLiteral("shots.jpg"));
        {QImage image(512,384,QImage::Format_RGB32);
         for(int y=0;y<image.height();++y){auto *row=reinterpret_cast<QRgb *>(image.scanLine(y));
             for(int x=0;x<image.width();++x)row[x]=qRgb(60+x/3,50+y/3,140);}
         QVERIFY(image.save(path));}
        QVERIFY(QDir().mkpath(outDir));
        Window w(nullptr,QStringLiteral(""));showForInspection(w);QVERIFY(QTest::qWaitForWindowExposed(&w));
        w.addFiles({path});QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),30000);
        auto *tabs=w.findChild<QTabBar *>(QStringLiteral("adjustment-groups"));QVERIFY(tabs);
        // Opt-in GPT inspection aid: writes group0..group5 once each. The worker
        // never reads the produced images.
        for(int group=0;group<6;++group){
            tabs->setCurrentIndex(group);settle();
            const auto file=QDir(outDir).filePath(QStringLiteral("group%1.png").arg(group));
            QVERIFY2(w.grab().save(file),qPrintable(file));}
    }
};

QTEST_MAIN(InspectorReachabilityTests)
#include "inspector_reachability_tests.moc"
