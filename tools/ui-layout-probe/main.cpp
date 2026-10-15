// Bounded native UI layout diagnostic for the shipped Window. This tool is a
// probe: it creates one QApplication and one production Window, generates its
// own abstract synthetic JPEG inside a caller-provided fresh output directory,
// drives the shipped adjustment-navigation combobox to the defringe card, then
// reports widget geometry, enabled state and clipping state as JSON text plus
// one screenshot. It reads no private photo, no historical screenshot and no
// other project artifact; it never writes outside output-dir.
//
// The screenshot shows the window as the process renders it. Clipping
// (visibleRegion().isEmpty()) is reported per widget as a diagnostic, not as a
// rule: the adjustment dock is a scroll area and every card cannot fit at once.
#include "app/window.h"
#include "app/ui_primitives.h"
#include "app/canvas.h"
#include "app/display_presenter.h"
#include <QApplication>
#include <QDockWidget>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QScreen>
#include <QSlider>
#include <QTextStream>
#include <QTimer>
#include <QWidget>
#include <QPixmap>

namespace {
constexpr int kDeadlineMs=90000;
constexpr int kPreviewTimeoutMs=60000;
constexpr int kSettleMs=500;
constexpr int kSliderWaitMs=8000;
const QString kDefringeCard=QString::fromUtf8("紫绿边去色");
const QString kNavigation=QStringLiteral("adjustment-navigation");
const QString kSectionChoice=QStringLiteral("adjustment-section-choice");
const QString kAdjustmentDock=QStringLiteral("adjustment-dock");

const QStringList kSpinNames{
    QStringLiteral("defringe-purple-amount"),
    QStringLiteral("defringe-purple-hue"),
    QStringLiteral("defringe-purple-width"),
    QStringLiteral("defringe-green-amount"),
    QStringLiteral("defringe-green-hue"),
    QStringLiteral("defringe-green-width"),
    QStringLiteral("defringe-radius")};

// One deadline for the whole run: waits are shortened so the process cannot
// outlive it, and the caller checks it before writing results.
qint64 elapsedMs(){
    static const QElapsedTimer timer=[] { QElapsedTimer started; started.start(); return started; }();
    return timer.elapsed();
}
bool deadlineHit(){return elapsedMs()>kDeadlineMs;}

int fail(const QString &message){
    QTextStream(stderr)<<message<<"\n";
    return 1;
}

// Wait for a signal without exiting the timeout. The hard deadline is checked
// by the caller after every wait, so a stalled render cannot outlive the tool.
bool awaitSignal(Window *sender,int timeoutMs,bool &delivered){
    delivered=false;
    QEventLoop loop;
    QObject::connect(sender,&Window::previewChanged,&loop,[&loop,&delivered](const QString &,bool success){delivered=success;loop.quit();});
    QTimer::singleShot(timeoutMs,&loop,&QEventLoop::quit);
    loop.exec();
    return delivered;
}

bool awaitEditReady(Window *root,int timeoutMs){
    QEventLoop loop;
    const auto sweep=[root]{
        return root->neutralPickerAvailable();
    };
    if(sweep())return true;
    QTimer poll;poll.setInterval(25);
    QObject::connect(&poll,&QTimer::timeout,&loop,[&loop,&sweep]{if(sweep())loop.quit();});
    poll.start();
    QTimer::singleShot(timeoutMs,&loop,&QEventLoop::quit);
    loop.exec();
    return sweep();
}

// Abstract diagnostic sheet: a neutral black/white edge carrying a thin purple
// stripe plus a few large flat colour blocks. Generated into output-dir only.
QImage syntheticSheet(){
    QImage sheet(1920,1280,QImage::Format_RGB32);
    sheet.fill(QColor(128,128,128));
    QPainter painter(&sheet);
    painter.fillRect(0,0,960,640,QColor(20,20,20));
    painter.fillRect(960,0,960,640,QColor(240,240,240));
    painter.fillRect(956,120,8,400,QColor(200,40,200));
    painter.fillRect(60,700,420,240,QColor(210,60,40));
    painter.fillRect(520,700,420,240,QColor(40,90,200));
    painter.fillRect(980,700,420,240,QColor(230,220,120));
    painter.fillRect(1440,700,420,240,QColor(30,150,90));
    painter.fillRect(60,980,1800,240,QColor(32,32,32));
    painter.setPen(QPen(QColor(250,250,250),4));
    for(int x=0;x<sheet.width();x+=320)painter.drawLine(x,980,x,1219);
    painter.end();
    return sheet;
}

QString copyOf(const QString &text){return text;}

QJsonObject rectJson(const QRect &rect){
    return QJsonObject{{QStringLiteral("x"),rect.x()},{QStringLiteral("y"),rect.y()},
                       {QStringLiteral("width"),rect.width()},{QStringLiteral("height"),rect.height()}};
}

QJsonObject widgetJson(QWidget *widget,const QWidget *window){
    if(!widget)
        return QJsonObject{{QStringLiteral("present"),false}};
    const QRect global(widget->mapTo(window,QPoint(0,0)),widget->size());
    const bool visible=widget->isVisible();
    const bool clipped=visible&&widget->visibleRegion().isEmpty();
    return QJsonObject{
        {QStringLiteral("present"),true},
        {QStringLiteral("objectName"),widget->objectName()},
        {QStringLiteral("enabled"),widget->isEnabled()},
        {QStringLiteral("visible"),visible},
        {QStringLiteral("hidden"),widget->isHidden()},
        {QStringLiteral("logicalSize"),QJsonObject{{QStringLiteral("width"),widget->width()},
                                                   {QStringLiteral("height"),widget->height()}}},
        {QStringLiteral("rectMappedToWindow"),rectJson(global)},
        {QStringLiteral("accessibleName"),widget->accessibleName()},
        {QStringLiteral("visibleRegionEmpty"),clipped},
        {QStringLiteral("meaning"),clipped
            ? QStringLiteral("diagnostic only: no pixels of this widget currently visible; scrolling/clipping is expected in the adjustment dock")
            : QStringLiteral("pixels of this widget are currently visible")}};
}

QJsonObject insideJson(const QRect &rect,const QRect &container){
    return QJsonObject{{QStringLiteral("rect"),rectJson(rect)},
                       {QStringLiteral("container"),rectJson(container)},
                       {QStringLiteral("inside"),container.contains(rect)}};
}
}

int main(int argc,char **argv){
    QApplication app(argc,argv);
    app.setApplicationName(QStringLiteral("LuminaS ui-layout-probe"));

    QStringList positional;
    for(int i=1;i<argc;++i)positional.append(QString::fromLocal8Bit(argv[i]));
    if(positional.isEmpty()||positional.size()>3){
        QTextStream(stderr)<<"usage: ui_layout_probe output-dir [logical-width logical-height]  (defaults 1360x900)\n";
        return 2;
    }
    const QString outputDirectory=QDir(positional[0]).absolutePath();
    int logicalWidth=1360,logicalHeight=900;
    if(positional.size()==3){
        bool okWidth=false,okHeight=false;
        logicalWidth=positional[1].toInt(&okWidth);
        logicalHeight=positional[2].toInt(&okHeight);
        if(!okWidth||!okHeight||logicalWidth<640||logicalHeight<480)
            return 2+fail(QStringLiteral("invalid logical size; expected integers >= 640x480"));
    }
    // The caller (GPT) chooses a fresh directory; refuse to reuse one so a stale
    // screenshot or report can never be mistaken for this run.
    if(QFileInfo::exists(outputDirectory))
        return 2+fail(QStringLiteral("refusing existing output directory: ")+outputDirectory);
    QDir().mkpath(outputDirectory);
    if(!QFileInfo(outputDirectory).isDir())
        return 1+fail(QStringLiteral("cannot create output directory: ")+outputDirectory);

    const QString sourcePng=QDir(outputDirectory).filePath(QStringLiteral("synthetic-source.png"));
    const QString sourceJpeg=QDir(outputDirectory).filePath(QStringLiteral("synthetic-source.jpg"));
    const QString screenshotPath=QDir(outputDirectory).filePath(QStringLiteral("window.png"));
    const QString reportPath=QDir(outputDirectory).filePath(QStringLiteral("ui-layout-report.json"));

    const QImage sheet=syntheticSheet();
    if(sheet.isNull()||!sheet.save(sourcePng,"PNG")||!sheet.save(sourceJpeg,"JPG",95))
        return 1+fail(QStringLiteral("cannot write the generated synthetic source into ")+outputDirectory);

    QJsonObject report;
    report.insert(QStringLiteral("tool"),QStringLiteral("ui_layout_probe"));
    report.insert(QStringLiteral("purpose"),QStringLiteral("bounded native UI layout diagnostic; no production UI or algorithm code is exercised beyond the shipped Window surface"));
    report.insert(QStringLiteral("outputDirectory"),outputDirectory);
    report.insert(QStringLiteral("generatedSourceJpeg"),sourceJpeg);
    report.insert(QStringLiteral("generatedSourcePixels"),QJsonObject{{QStringLiteral("width"),sheet.width()},{QStringLiteral("height"),sheet.height()}});
    report.insert(QStringLiteral("qtScaleFactorEnvironment"),qEnvironmentVariable("QT_SCALE_FACTOR"));
    report.insert(QStringLiteral("noteNotEveryCardFitsAtOnce"),QStringLiteral("the adjustment dock is a scroll area; cards below the viewport are legitimately clipped and visibleRegionEmpty is diagnostic, not a failure"));

    Window window(nullptr,QStringLiteral(""));
    const bool useAppSize=qEnvironmentVariableIsSet("LUMINA_UI_PROBE_USE_APP_SIZE");
    if(!useAppSize)window.resize(logicalWidth,logicalHeight);
    report.insert(QStringLiteral("usedAppStartupSize"),useAppSize);
    window.show();
    // The window is mapped by now, so every widget below has a real geometry.

    window.addFiles(QStringList{sourceJpeg});
    if(window.currentIndex()<0)
        return 1+fail(QStringLiteral("the generated synthetic JPEG did not become the current photo"));
    // addFiles/activate post the render request; wait for its result now.
    bool previewDelivered=false;
    const int previewBudget=qMax(1,qMin(kPreviewTimeoutMs,kDeadlineMs-int(elapsedMs())));
    const bool previewArrived=awaitSignal(&window,previewBudget,previewDelivered);
    if(!previewArrived){
        return 1+fail(QStringLiteral("generated image preview did not load successfully"));
    }
    if(!awaitEditReady(&window,qMax(1,kDeadlineMs-int(elapsedMs())))){
        return 1+fail(QStringLiteral("generated image edit preview did not become ready"));
    }

    // Production navigation: the combobox installed by installAdjustmentNavigation.
    auto *choice=window.findChild<QComboBox *>(kSectionChoice);
    if(!choice)
        return 1+fail(QStringLiteral("missing production navigation combobox ")+kSectionChoice);
    int index=-1;
    for(int i=0;i<choice->count();++i)if(choice->itemText(i)==kDefringeCard){index=i;break;}
    if(index<0)
        return 1+fail(QStringLiteral("action: navigation combobox does not name ")+kDefringeCard);
    // Queued selection: post the index change so the navigation timers and the
    // resulting layout run before the screenshot is taken.
    QTimer::singleShot(0,choice,[choice,index]{choice->setCurrentIndex(index);});

    bool settled=false;
    {
        QEventLoop loop;
        QTimer::singleShot(qMax(1,qMin(kSettleMs,kDeadlineMs-int(elapsedMs()))),&loop,[&loop,&settled]{settled=true;loop.quit();});
        loop.exec();
    }

    const QScreen *screen=window.screen();
    if(screen){
        report.insert(QStringLiteral("availableScreenRect"),rectJson(screen->availableGeometry()));
        report.insert(QStringLiteral("windowFrameRect"),rectJson(window.frameGeometry()));
        report.insert(QStringLiteral("frameInsideAvailableScreen"),screen->availableGeometry().contains(window.frameGeometry()));
    }
    report.insert(QStringLiteral("screen"),QJsonObject{
        {QStringLiteral("name"),screen?screen->name():QString()},
        {QStringLiteral("devicePixelRatio"),screen?screen->devicePixelRatio():0.0},
        {QStringLiteral("logicalDotsPerInch"),screen?screen->logicalDotsPerInch():0.0}});
    report.insert(QStringLiteral("window"),QJsonObject{
        {QStringLiteral("devicePixelRatio"),window.devicePixelRatioF()},
        {QStringLiteral("logicalSize"),QJsonObject{{QStringLiteral("width"),window.width()},{QStringLiteral("height"),window.height()}}},
        {QStringLiteral("clientLogicalSize"),QJsonObject{{QStringLiteral("width"),window.centralWidget()?window.centralWidget()->width():0},
                                                         {QStringLiteral("height"),window.centralWidget()?window.centralWidget()->height():0}}},
        {QStringLiteral("devicePixels"),QJsonObject{{QStringLiteral("width"),window.width()*window.devicePixelRatioF()},
                                                    {QStringLiteral("height"),window.height()*window.devicePixelRatioF()}}}});

    QJsonObject navigation;
    navigation.insert(QStringLiteral("defringeCardLabel"),kDefringeCard);
    navigation.insert(QStringLiteral("comboBoxIndex"),index);
    navigation.insert(QStringLiteral("selectedIndexAfterSelection"),choice->currentIndex());
    navigation.insert(QStringLiteral("selectedTextAfterSelection"),choice->currentText());
    navigation.insert(QStringLiteral("widget"),widgetJson(window.findChild<QWidget *>(kNavigation),&window));
    navigation.insert(QStringLiteral("comboBox"),widgetJson(choice,&window));
    navigation.insert(QStringLiteral("expandAll"),widgetJson(window.findChild<QWidget *>(QStringLiteral("adjustment-expand-all")),&window));
    navigation.insert(QStringLiteral("collapseAll"),widgetJson(window.findChild<QWidget *>(QStringLiteral("adjustment-collapse-all")),&window));
    report.insert(QStringLiteral("navigation"),navigation);

    QWidget *field0=window.findChild<QDoubleSpinBox *>(kSpinNames.value(0));
    if(!field0)
        return 1+fail(QStringLiteral("missing defringe spin ")+kSpinNames.value(0));
    QWidget *card=nullptr;
    for(QWidget *walk=field0;walk;walk=walk->parentWidget())
        if(walk->objectName()==QStringLiteral("adjustment-card")){card=walk;break;}
    if(!card)
        return 1+fail(QStringLiteral("the defringe card is not reachable from its fields"));
    auto *scrollArea=window.findChild<QScrollArea *>();
    if(!scrollArea)
        return 1+fail(QStringLiteral("the adjustment scroll area is not reachable from the window"));
    QWidget *viewport=scrollArea->viewport();
    if(!viewport)
        return 1+fail(QStringLiteral("the adjustment scroll area has no viewport"));
    const bool revealLast=qEnvironmentVariableIsSet("LUMINA_UI_PROBE_REVEAL_LAST_FIELD");
    if(revealLast){
        auto *last=window.findChild<QDoubleSpinBox *>(kSpinNames.last());
        if(!last)return 1+fail(QStringLiteral("missing final defringe control"));
        scrollArea->ensureWidgetVisible(last,0,12);
        QCoreApplication::processEvents(QEventLoop::AllEvents,20);
    }
    report.insert(QStringLiteral("revealedLastField"),revealLast);
    auto *dock=window.findChild<QDockWidget *>(kAdjustmentDock);
    const QRect cardWindowRect=card?QRect(card->mapTo(&window,QPoint(0,0)),card->size()):QRect();
    const QRect viewportWindowRect=viewport?QRect(viewport->mapTo(&window,QPoint(0,0)),viewport->size()):QRect();
    QJsonObject cardJson=widgetJson(card,&window);
    cardJson.insert(QStringLiteral("insideOwnScrollViewport"),insideJson(cardWindowRect,viewportWindowRect));
    cardJson.insert(QStringLiteral("insideWindow"),QJsonObject{{QStringLiteral("inside"),window.rect().contains(cardWindowRect)}});
    report.insert(QStringLiteral("defringeCard"),cardJson);

    QJsonArray spins;
    QJsonArray sliders;
    QJsonArray resets;
    for(const QString &name:kSpinNames){
        auto *field=window.findChild<QDoubleSpinBox *>(name);
        if(!field)return 1+fail(QStringLiteral("missing defringe spin ")+name);
        QJsonObject entry=widgetJson(field,&window);
        entry.insert(QStringLiteral("objectName"),name);
        entry.insert(QStringLiteral("decimals"),field->decimals());
        entry.insert(QStringLiteral("minimum"),field->minimum());
        entry.insert(QStringLiteral("maximum"),field->maximum());
        entry.insert(QStringLiteral("singleStep"),field->singleStep());
        entry.insert(QStringLiteral("value"),field->value());
        entry.insert(QStringLiteral("rectInsideOwnScrollViewport"),insideJson(QRect(field->mapTo(&window,QPoint(0,0)),field->size()),viewportWindowRect).value(QStringLiteral("inside")));
        entry.insert(QStringLiteral("rectInsideWindow"),window.rect().contains(QRect(field->mapTo(&window,QPoint(0,0)),field->size())));
        spins.append(entry);

        const QString sliderName=QStringLiteral("slider-")+name;
        auto *slider=window.findChild<QSlider *>(sliderName);
        if(!slider)return 1+fail(QStringLiteral("missing defringe slider ")+sliderName);
        QJsonObject bar=widgetJson(slider,&window);
        bar.insert(QStringLiteral("objectName"),sliderName);
        bar.insert(QStringLiteral("minimum"),slider->minimum());
        bar.insert(QStringLiteral("maximum"),slider->maximum());
        bar.insert(QStringLiteral("value"),slider->value());
        sliders.append(bar);

        const QString resetName=QStringLiteral("reset-")+name;
        auto *reset=window.findChild<QPushButton *>(resetName);
        if(!reset)return 1+fail(QStringLiteral("missing defringe reset ")+resetName);
        QJsonObject button=widgetJson(reset,&window);
        button.insert(QStringLiteral("objectName"),resetName);
        resets.append(button);
    }
    report.insert(QStringLiteral("defringeSpins"),spins);
    report.insert(QStringLiteral("defringeSliders"),sliders);
    report.insert(QStringLiteral("defringeResets"),resets);

    QJsonObject dockJson=widgetJson(dock,&window);
    dockJson.insert(QStringLiteral("width"),dock?dock->width():0);
    report.insert(QStringLiteral("adjustmentDock"),dockJson);
    report.insert(QStringLiteral("scrollViewport"),widgetJson(viewport,&window));

    const bool deadlineExceeded=deadlineHit();
    const QPixmap snapshot=window.grab();
    const QImage snapshotImage=snapshot.toImage();
    report.insert(QStringLiteral("previewSuccess"),previewDelivered);
    report.insert(QStringLiteral("previewRenderReady"),window.neutralPickerAvailable());
    if(auto *canvas=window.findChild<Canvas *>()){
        report.insert(QStringLiteral("displayPolicy"),canvas->displaySnapshot());
        report.insert(QStringLiteral("displayProfileBytes"),canvas->presenter()->monitorIcc().size());
        report.insert(QStringLiteral("displayProfilePending"),canvas->presenter()->pendingProfile());
    }
    report.insert(QStringLiteral("previewDisplayedPathIsGenerated"),window.displayedPath()==QFileInfo(sourceJpeg).canonicalFilePath()
                                                                    ||window.displayedPath()==sourceJpeg);
    report.insert(QStringLiteral("screenshotPath"),screenshotPath);
    report.insert(QStringLiteral("screenshotPixels"),QJsonObject{{QStringLiteral("width"),snapshotImage.width()},
                                                                 {QStringLiteral("height"),snapshotImage.height()}});
    report.insert(QStringLiteral("deadlineMs"),kDeadlineMs);
    report.insert(QStringLiteral("deadlineExceededBeforeSnapshot"),deadlineExceeded);
    report.insert(QStringLiteral("scaleFactorUsed"),QStringLiteral("observed window.devicePixelRatioF(); QT_SCALE_FACTOR is read from the environment and never forced"));

    if(snapshotImage.isNull())return 1+fail(QStringLiteral("window grab produced no image"));
    if(qEnvironmentVariableIsSet("LUMINA_UI_PROBE_BRAND_ICON")){
        const auto iconPath=QDir(outputDirectory).filePath(QStringLiteral("LuminaS.ico"));
        if(!Ui::brandIcon().pixmap({256,256}).toImage().save(iconPath,"ICO"))
            return 1+fail(QStringLiteral("cannot export the existing native brand icon"));
        report.insert(QStringLiteral("brandIcon"),iconPath);
    }
    {
        QSaveFile file(screenshotPath);
        if(!file.open(QIODevice::WriteOnly)||!snapshotImage.save(&file,"PNG")||!file.commit())
            return 1+fail(QStringLiteral("cannot save ")+screenshotPath);
    }
    {
        QSaveFile file(reportPath);
        if(!file.open(QIODevice::WriteOnly))
            return 1+fail(QStringLiteral("cannot save ")+reportPath);
        const QByteArray payload=QJsonDocument(report).toJson(QJsonDocument::Indented);
        if(file.write(payload)!=payload.size()||!file.commit())
            return 1+fail(QStringLiteral("cannot save ")+reportPath);
    }
    if(deadlineExceeded)
        return 1+fail(QStringLiteral("the run exceeded the bounded deadline of ")+QString::number(kDeadlineMs)+QStringLiteral(" ms"));
    QTextStream(stdout)<<"ui_layout_probe "<<logicalWidth<<"x"<<logicalHeight
                       <<" dpr="<<window.devicePixelRatioF()
                       <<" preview="<<(previewDelivered?"ok":"missing")
                       <<" screenshot="<<screenshotPath<<"\n";

    return 0;

}




