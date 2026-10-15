#include "app/export_dialog.h"
#include "app/output_inspector.h"
#include "app/window.h"
#include "app/canvas.h"
#include <QTest>
#include <QTemporaryDir>
#include <QColorSpace>
#include <QCryptographicHash>
#include <QComboBox>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QLineEdit>
#include <QLabel>
#include <QInputDialog>
#include <QSignalSpy>
#include <QTimer>
#include <QFile>
#include <QTableWidget>
#include <QScreen>
#include <QAction>
#include <QDockWidget>
using namespace Export;
class ExportUiTests:public QObject {
 Q_OBJECT
 QTemporaryDir dir;
 QList<Asset> samples(){QList<Asset> result;for(int i=0;i<2;++i){const auto path=dir.filePath(QString::number(i)+".jpg");QImage image(600,400,QImage::Format_RGB32);for(int y=0;y<400;++y)for(int x=0;x<600;++x)image.setPixelColor(x,y,QColor(x*255/600,y*255/400,150));image.setColorSpace(QColorSpace::SRgb);if(!image.save(path,"JPEG"))return {};QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};result.append({path,QString::fromLatin1(QCryptographicHash::hash(f.readAll(),QCryptographicHash::Sha256).toHex()),{},1,true,i==0});}return result;}
private slots:
 void initTestCase(){QVERIFY(dir.isValid());qputenv("LUMINA_EXPORT_DATA_DIR",dir.filePath("data").toUtf8());}
 void settingsAndNarrowReachability(){
    Queue queue(dir.filePath("layout-queue.json"));ExportDialog dialog(samples(),0,&queue);dialog.show();QVERIFY(QTest::qWaitForWindowExposed(&dialog));
    QVERIFY(dialog.frameGeometry().width()<=dialog.screen()->availableGeometry().width());QVERIFY(dialog.frameGeometry().height()<=dialog.screen()->availableGeometry().height());qInfo()<<"export native DPR"<<dialog.devicePixelRatioF()<<"startup"<<dialog.size();const auto startupSize=dialog.size();
    QCOMPARE(dialog.findChild<QComboBox *>("export-range")->currentIndex(),1);QCOMPARE(dialog.settings().quality,95);QVERIFY(!dialog.settings().gps);
    auto *profile=dialog.findChild<QComboBox *>("export-profile");profile->setCurrentText("ProPhoto RGB");QCOMPARE(dialog.settings().bits,16);QCOMPARE(dialog.settings().format,Format::Tiff);QVERIFY(dialog.findChild<QLabel *>("export-diagnostic")->text().contains(QStringLiteral("16")));
    dialog.findChild<QComboBox *>("export-mode")->setCurrentIndex(1);QCOMPARE(dialog.settings().mode,Mode::Cmyk);QCOMPARE(dialog.settings().format,Format::Tiff);QVERIFY(!dialog.findChild<QPushButton *>("export-start")->isEnabled());QVERIFY(dialog.settings().icc.isEmpty());
    Settings s;QString e;s.icc=builtinProfile("sRGB",e);dialog.setSettings(s);QVERIFY(dialog.findChild<QPushButton *>("export-start")->isEnabled());
    dialog.findChild<QPushButton *>("export-advanced-color")->click();dialog.resize(700,480);QTest::qWait(100);QCOMPARE(dialog.size(),QSize(700,480));auto *scroll=dialog.findChild<QScrollArea *>("export-settings-scroll");QVERIFY(scroll);
    for(auto *w:scroll->widget()->findChildren<QWidget *>())if(w->isEnabled()&&w->isVisible()&&(qobject_cast<QComboBox *>(w)||qobject_cast<QAbstractSpinBox *>(w)||qobject_cast<QLineEdit *>(w))){scroll->ensureWidgetVisible(w);QCoreApplication::processEvents();QVERIFY2(w->visibleRegion().contains(w->rect()),qPrintable(w->objectName()));}
    auto *start=dialog.findChild<QPushButton *>("export-start");QVERIFY(start->visibleRegion().contains(start->rect()));
    QSignalSpy paints(dialog.findChild<Canvas *>("export-preview"),&Canvas::framePainted);dialog.resize(startupSize);dialog.setSettings(s);QTRY_VERIFY_WITH_TIMEOUT(!paints.isEmpty(),30000);dialog.findChild<QPushButton *>("export-advanced-color")->setChecked(false);scroll->verticalScrollBar()->setValue(0);QCoreApplication::processEvents();const auto photo=qEnvironmentVariable("LUMINA_EXPORT_UI_SCREENSHOT");if(!photo.isEmpty())QVERIFY(dialog.grab().save(photo));
 }
 void presetsAndKeyboard(){
    Queue queue(dir.filePath("preset-queue.json"));ExportDialog dialog(samples(),0,&queue);dialog.show();QVERIFY(QTest::qWaitForWindowExposed(&dialog));
    auto *preset=dialog.findChild<QComboBox *>("export-preset");preset->setCurrentText(QStringLiteral("4K分享"));QMetaObject::invokeMethod(preset,"activated",Qt::DirectConnection,Q_ARG(int,preset->currentIndex()));QCOMPARE(dialog.settings().sizeMode,SizeMode::LongEdge);QCOMPARE(dialog.settings().width,3840.);
    auto respond=[&](QString text){QTimer::singleShot(50,&dialog,[&dialog,text]{if(auto *input=dialog.findChild<QInputDialog *>()){input->setTextValue(text);input->accept();}});};
    respond(QStringLiteral("测试导出预设"));dialog.findChild<QPushButton *>("export-preset-0")->click();QVERIFY(preset->findText(QStringLiteral("测试导出预设"))>=0);
    respond(QStringLiteral("测试预设改名"));dialog.findChild<QPushButton *>("export-preset-1")->click();QVERIFY(preset->findText(QStringLiteral("测试预设改名"))>=0);
    auto *range=dialog.findChild<QComboBox *>("export-range");range->setFocus();QTRY_VERIFY(range->hasFocus());auto *before=QApplication::focusWidget();QTest::keyClick(before,Qt::Key_Tab);QVERIFY(QApplication::focusWidget()!=before);QTest::keyClick(QApplication::focusWidget(),Qt::Key_Backtab);QCOMPARE(QApplication::focusWidget(),before);
    dialog.findChild<QPushButton *>("export-preset-2")->click();QVERIFY(preset->findText(QStringLiteral("测试预设改名"))<0);
 }
 void realQueueFromDialogAndInspector(){
    const auto inputs=samples();Queue queue(dir.filePath("run-queue.json"));auto *dialog=new ExportDialog(inputs,0,&queue);dialog->show();QVERIFY(QTest::qWaitForWindowExposed(dialog));Settings s;QString e;s.format=Format::Png;s.bits=16;s.icc=builtinProfile("sRGB",e);s.besideSource=false;s.directory=dir.filePath("results");dialog->setSettings(s);dialog->startExport();QTRY_VERIFY_WITH_TIMEOUT(queue.busy()||queue.jobs().size()==2,30000);delete dialog;QTRY_VERIFY_WITH_TIMEOUT(!queue.busy(),30000);QCOMPARE(queue.jobs().size(),2);for(const auto &j:queue.jobs())QVERIFY2(j.status==Status::Succeeded,qPrintable(j.error));
    OutputInspector inspector;inspector.resize(900,600);inspector.show();QSignalSpy inspected(&inspector,&OutputInspector::inspected);QVERIFY(inspector.inspect(queue.jobs()[0].path,e));QTRY_VERIFY_WITH_TIMEOUT(!inspected.isEmpty(),10000);QVERIFY(inspected.last()[1].toBool());inspector.findChild<QPushButton *>("output-inspector-actual")->click();QCOMPARE(inspector.canvas()->zoom()*inspector.canvas()->devicePixelRatioF(),1.0);QVERIFY(!inspector.inspect(dir.filePath("missing-file.tif"),e));QVERIFY(inspector.findChild<QLabel *>("output-inspector-status")->text().contains(QStringLiteral("不存在")));
 }
 void windowIntegrationFreezesLatestRecipe(){
    const auto inputs=samples();Window w(nullptr,QStringLiteral(""));w.show();w.addFiles({inputs[0].path});QTRY_VERIFY_WITH_TIMEOUT(w.neutralPickerAvailable(),30000);w.findChild<QAction *>("workspace-browse")->trigger();w.showExportDialog();auto *dialog=w.findChild<ExportDialog *>();QVERIFY(dialog);auto recipe=w.currentRecipe();recipe.exposure=.75;w.applyRecipe(recipe,QStringLiteral("后续修改"));Settings s;QString e;s.icc=builtinProfile("sRGB",e);s.besideSource=false;s.directory=dir.filePath("window-results");dialog->setSettings(s);dialog->startExport();auto *queue=w.findChild<Queue *>();QVERIFY(queue);QTRY_VERIFY_WITH_TIMEOUT(!queue->jobs().isEmpty(),30000);QCOMPARE(queue->jobs().last().recipe.exposure,.75);QTRY_VERIFY_WITH_TIMEOUT(!queue->busy(),30000);QCOMPARE(queue->jobs().last().status,Status::Succeeded);QCOMPARE(w.currentRecipe().exposure,.75);
    dialog->findChild<QPushButton *>("export-inspect")->click();auto *output=w.findChild<QDialog *>("export-output-dialog");QVERIFY(output);output->findChild<QPushButton *>("export-return-edit")->click();QVERIFY(dialog->isHidden());QVERIFY(w.findChild<QAction *>("workspace-edit")->isChecked());QVERIFY(w.findChild<QDockWidget *>("adjustment-dock")->isVisible());QCOMPARE(w.currentRecipe().exposure,.75);
 }
 void realSourceExportAndInspect(){
    const auto source=qEnvironmentVariable("LUMINA_EXPORT_UI_REAL_SOURCE");if(source.isEmpty())QSKIP("Opt-in real source export and native 100% output inspection");
    QFile f(source);QVERIFY(f.open(QIODevice::ReadOnly));QCryptographicHash hash(QCryptographicHash::Sha256);QVERIFY(hash.addData(&f));const auto before=hash.result().toHex();f.close();
    Recipe recipe;recipe.exposure=.25;Queue queue(dir.filePath("real-queue.json"));ExportDialog dialog({{source,QString::fromLatin1(before),recipe,1,true,true}},0,&queue);dialog.show();QVERIFY(QTest::qWaitForWindowExposed(&dialog));
    Settings s;QString e;s.format=Format::Tiff;s.bits=16;s.profileName="ProPhoto RGB";s.icc=builtinProfile(s.profileName,e);s.besideSource=false;s.directory=dir.filePath("real-output");dialog.setSettings(s);dialog.startExport();QTRY_VERIFY_WITH_TIMEOUT(!queue.jobs().isEmpty(),30000);QTRY_VERIFY_WITH_TIMEOUT(!queue.busy(),120000);QVERIFY2(queue.jobs()[0].status==Status::Succeeded,qPrintable(queue.jobs()[0].error));
    OutputInspector inspector(&dialog);inspector.setWindowFlags(Qt::Window);inspector.resize(1000,650);inspector.show();QSignalSpy loaded(&inspector,&OutputInspector::inspected),painted(inspector.canvas(),&Canvas::framePainted);QVERIFY(inspector.inspect(queue.jobs()[0].path,e));QTRY_VERIFY_WITH_TIMEOUT(!loaded.isEmpty(),30000);QVERIFY(loaded.last()[1].toBool());QTRY_VERIFY_WITH_TIMEOUT(!painted.isEmpty(),10000);inspector.findChild<QPushButton *>("output-inspector-actual")->click();QCOMPARE(inspector.canvas()->zoom()*inspector.canvas()->devicePixelRatioF(),1.0);QTest::qWait(100);
    const auto shot=qEnvironmentVariable("LUMINA_EXPORT_UI_INSPECT_SHOT");if(!shot.isEmpty())QVERIFY(inspector.grab().save(shot));QVERIFY(f.open(QIODevice::ReadOnly));QCryptographicHash after(QCryptographicHash::Sha256);QVERIFY(after.addData(&f));QCOMPARE(after.result().toHex(),before);qInfo()<<"real output native inspection"<<QFileInfo(source).fileName()<<"100% physical pixels";
 }
};
QTEST_MAIN(ExportUiTests)
#include "export_ui_tests.moc"
