#include "window.h"
#include <QFileDialog>
#include <QDir>
#include <QFileInfo>
#include <QToolBar>
#include <QVBoxLayout>
#include <QStatusBar>
#include <QSignalBlocker>
#include <QDockWidget>
#include <QFormLayout>
#include <QApplication>
#include <QLineEdit>
#include <QShortcut>
#include <atomic>
#include <QSlider>
#include <QHBoxLayout>
#include <cmath>
#include <QGroupBox>
#include <QScrollArea>
#include <QActionGroup>
#include <QPalette>
#include <QToolButton>
#include <QScreen>
#include <QMenu>
#include <QButtonGroup>
#include <QMessageBox>
#include <QStandardPaths>
#include <QInputDialog>
#include <QProgressBar>
#include "ui_primitives.h"
#include "adjustment_navigation.h"
#include "parameter_wheel_guard.h"
#include "core/parametric_curve.h"
#include "display_presenter.h"
#include "export_dialog.h"
#include "local_adjustment_panel.h"
#include <QTabWidget>
#include "render/crop_constraint.h"

Window::Window(QWidget *parent,const QString &libraryDirectory,const QString &projectDirectory) : QMainWindow(parent) {
    editor=new EditLoader(this);
    static std::atomic<quint64> sessions{1};session=sessions.fetch_add(1);
    presetLibrary=libraryDirectory.isNull()?QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+QStringLiteral("/presets"):libraryDirectory;
    setWindowTitle(QStringLiteral("LuminaS"));setWindowIcon(Ui::brandIcon());
    const auto available=QGuiApplication::primaryScreen()?QGuiApplication::primaryScreen()->availableGeometry().size():QSize(1406,930);
    resize(std::min(1366,std::max(640,available.width()-40)),std::min(850,std::max(320,available.height()-60)));
    auto theme=[this](bool dark){Ui::applyTheme(this,dark);};theme(true);
    auto *tools=addToolBar(QStringLiteral("工作阶段"));tools->setObjectName("stage-toolbar");tools->setMovable(false);tools->setIconSize({18,18});tools->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    auto *exportAction=new QAction(QStringLiteral("导出"),this);exportAction->setObjectName("export-action");exportAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+E")));addAction(exportAction);connect(exportAction,&QAction::triggered,this,&Window::showExportDialog);
    auto *brandMark=new QLabel;brandMark->setObjectName("brand-mark");brandMark->setPixmap(Ui::brandIcon().pixmap({26,26}));brandMark->setAccessibleName(QStringLiteral("LuminaS 标志"));tools->addWidget(brandMark);
    auto *brand=new QLabel("LuminaS");brand->setObjectName("brand");brand->setFont(Ui::brandFont());tools->addWidget(brand);
    auto *workspaces=new QActionGroup(this);workspaces->setExclusive(true);
    auto *browse=tools->addAction(Ui::icon(Ui::Glyph::Browse),QStringLiteral("选片"));browse->setObjectName("workspace-browse");browse->setCheckable(true);workspaces->addAction(browse);
    auto *edit=tools->addAction(Ui::icon(Ui::Glyph::Edit),QStringLiteral("编辑"));edit->setObjectName("workspace-edit");edit->setCheckable(true);edit->setChecked(true);workspaces->addAction(edit);tools->addSeparator();
    for(auto *a:{browse,edit})if(auto *b=qobject_cast<QToolButton *>(tools->widgetForAction(a))){b->setProperty("workspaceTab",true);b->setAccessibleName(a->text());}
    auto *importButton=new QToolButton;importButton->setObjectName("import-button");importButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);importButton->setPopupMode(QToolButton::MenuButtonPopup);
    auto *importMenu=new QMenu(importButton);auto *open=importMenu->addAction(Ui::icon(Ui::Glyph::Import),QStringLiteral("选择照片…"));open->setObjectName("import-files");connect(open,&QAction::triggered,this,[this]{addFiles(QFileDialog::getOpenFileNames(this,QStringLiteral("选择 JPEG / CR3"),{},"照片 (*.jpg *.jpeg *.cr3 *.JPG *.JPEG *.CR3)"));});importButton->setDefaultAction(open);importButton->setText(QStringLiteral("导入照片"));
    open->setShortcut(QKeySequence::Open);browse->setShortcut(QKeySequence(QStringLiteral("Ctrl+1")));edit->setShortcut(QKeySequence(QStringLiteral("Ctrl+2")));
    importButton->setText(QStringLiteral("导入照片"));
    auto *folder=importMenu->addAction(Ui::icon(Ui::Glyph::Folder),QStringLiteral("选择文件夹…"));folder->setObjectName("import-folder");connect(folder,&QAction::triggered,this,[this]{
        const auto path=QFileDialog::getExistingDirectory(this,QStringLiteral("选择照片文件夹（不递归）"));if(path.isEmpty())return;QStringList files;
        for(const auto &entry:QDir(path).entryInfoList(QDir::Files,QDir::Name))if(entry.suffix().compare("jpg",Qt::CaseInsensitive)==0||entry.suffix().compare("jpeg",Qt::CaseInsensitive)==0||entry.suffix().compare("cr3",Qt::CaseInsensitive)==0)files<<entry.absoluteFilePath();addFiles(files);
    });importButton->setMenu(importMenu);tools->addWidget(importButton);
    auto *spacer=new QWidget;spacer->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred);tools->addWidget(spacer);
    auto action=[tools](const QString &text,Ui::Glyph glyph,auto fn){auto *a=tools->addAction(Ui::icon(glyph),text);QObject::connect(a,&QAction::triggered,tools,fn);auto *b=qobject_cast<QToolButton *>(tools->widgetForAction(a));b->setToolButtonStyle(Qt::ToolButtonIconOnly);b->setToolTip(text);b->setAccessibleName(text);return a;};
    undoAction=action(QStringLiteral("撤销"),Ui::Glyph::Undo,[this]{undoEdit();});redoAction=action(QStringLiteral("重做"),Ui::Glyph::Redo,[this]{redoEdit();});tools->addSeparator();
    auto *themeAction=action(QStringLiteral("切换浅色主题"),Ui::Glyph::Theme,[]{});themeAction->setObjectName("theme-toggle");themeAction->setCheckable(true);connect(themeAction,&QAction::toggled,this,[theme,themeAction](bool light){theme(!light);themeAction->setToolTip(light?QStringLiteral("切换深色主题"):QStringLiteral("切换浅色主题"));});
    tools->addSeparator();auto *exportButton=new QToolButton;exportButton->setObjectName("export-button");exportButton->setDefaultAction(exportAction);exportButton->setToolButtonStyle(Qt::ToolButtonTextOnly);exportButton->setAccessibleName(QStringLiteral("导出照片"));tools->addWidget(exportButton);
    auto *central = new QWidget; auto *layout = new QVBoxLayout(central);
    central->setObjectName("photo-workspace");
    layout->setContentsMargins(12,8,12,6);layout->setSpacing(7);
    auto tool=[this](QHBoxLayout *line,const QString &name,const QString &id,Ui::Glyph glyph,auto callback){auto *b=new QToolButton;b->setObjectName(id);b->setIcon(Ui::icon(glyph));b->setIconSize({18,18});b->setToolTip(name);b->setAccessibleName(name);line->addWidget(b);connect(b,&QToolButton::clicked,this,callback);return b;};
    auto *context=new QFrame;context->setObjectName("context-bar");auto *contextLine=new QHBoxLayout(context);contextLine->setContentsMargins(0,0,0,0);contextLine->setSpacing(5);
    auto *hand=tool(contextLine,QStringLiteral("小手平移 · 滚轮缩放"),"tool-hand",Ui::Glyph::Hand,[this]{const bool render=cropSelecting||pointRangeActive||sharpenMaskActive;clearPointColorModes();if(auto *b=findChild<QPushButton *>("wb-picker"))b->setChecked(false);if(cropSelecting)markViewEpochChange();cropSelecting=false;canvas->setCropMode(false);if(render)requestEdit();else updatePickerState();});hand->setCheckable(true);hand->setChecked(true);
    auto *crop=tool(contextLine,QStringLiteral("裁剪"),"tool-crop",Ui::Glyph::Crop,[this]{beginCropSelection();});crop->setCheckable(true);
    auto *canvasTools=new QButtonGroup(this);canvasTools->setExclusive(false);canvasTools->addButton(hand);canvasTools->addButton(crop);
    tool(contextLine,QStringLiteral("适合窗口 · 双击画布"),"view-fit",Ui::Glyph::Fit,[this]{canvas->fit();});
    compareButton=new QPushButton(Ui::icon(Ui::Glyph::Compare),QStringLiteral("原图比较"));compareButton->setObjectName("compare-original");compareButton->setCheckable(true);compareButton->setToolTip(QStringLiteral("对比默认显影，不改变当前参数"));connect(compareButton,&QPushButton::clicked,this,&Window::compareOriginal);contextLine->addWidget(compareButton);contextLine->addStretch();
    auto *exitCrop=new QPushButton(QStringLiteral("退出裁剪"));exitCrop->setObjectName("exit-crop");exitCrop->setToolTip(QStringLiteral("回到小手浏览；拖选后已应用的裁剪可撤销。Esc同样退出。"));exitCrop->hide();contextLine->addWidget(exitCrop);connect(exitCrop,&QPushButton::clicked,this,[this]{if(cropSelecting)markViewEpochChange();cropSelecting=false;canvas->setCropMode(false);requestEdit();});
    auto *retry=new QPushButton(Ui::icon(Ui::Glyph::Reset),QStringLiteral("重新读取"));retry->setObjectName("preview-retry");retry->setAccessibleName(QStringLiteral("重新读取当前照片"));retry->hide();contextLine->addWidget(retry);connect(retry,&QPushButton::clicked,this,[this]{if(current>=0){previewError.clear();findChild<QPushButton *>("preview-retry")->hide();canvas->setMessage(QStringLiteral("正在重新读取照片…"));requestEdit();}});
    auto *viewHint=new QLabel(QStringLiteral("滚轮缩放 · 小手拖动"));viewHint->setObjectName("navigation-hint");viewHint->setProperty("role","muted");contextLine->addWidget(viewHint);layout->addWidget(context);
    canvas=new Canvas;canvas->setObjectName("photo-canvas");canvas->setMinimumSize(280,120);layout->addWidget(canvas,1);
    details=new QLabel(QStringLiteral("导入照片开始选片"));details->setWordWrap(true);details->setMinimumHeight(22);details->setProperty("role","muted");layout->addWidget(details);
    auto *filmHeader=new QFrame;filmHeader->setObjectName("filmstrip-header");auto *filmLine=new QHBoxLayout(filmHeader);filmLine->setContentsMargins(0,0,0,0);filmLine->setSpacing(4);
    tool(filmLine,QStringLiteral("上一张"),"photo-previous",Ui::Glyph::Previous,[this]{step(-1);});tool(filmLine,QStringLiteral("下一张"),"photo-next",Ui::Glyph::Next,[this]{step(1);});
    tool(filmLine,QStringLiteral("未标记"),"pick-unmarked",Ui::Glyph::Unmarked,[this]{mark(Pick::Unmarked);});tool(filmLine,QStringLiteral("保留"),"pick-keep",Ui::Glyph::Keep,[this]{mark(Pick::Keep);});tool(filmLine,QStringLiteral("排除 · 不删除文件"),"pick-reject",Ui::Glyph::Reject,[this]{mark(Pick::Reject);});filmLine->addStretch();
    filters=new QComboBox;filters->setObjectName("pick-filter");filters->addItems({QStringLiteral("全部照片"),QStringLiteral("未标记"),QStringLiteral("保留"),QStringLiteral("排除")});filters->setAccessibleName(QStringLiteral("选片筛选"));filmLine->addWidget(filters);connect(filters,&QComboBox::currentIndexChanged,this,&Window::filter);
    auto *stripToggle=tool(filmLine,QStringLiteral("显示 / 收起胶片条"),"filmstrip-toggle",Ui::Glyph::Filmstrip,[this](bool checked){strip->setVisible(checked&&height()>=500);});stripToggle->setCheckable(true);stripToggle->setChecked(true);layout->addWidget(filmHeader);
    strip=new QListWidget;strip->setObjectName("filmstrip");strip->setViewMode(QListView::IconMode);strip->setFlow(QListView::LeftToRight);strip->setWrapping(false);strip->setMovement(QListView::Static);strip->setSelectionMode(QAbstractItemView::ExtendedSelection);
    strip->setIconSize({120,85});strip->setGridSize({156,124});strip->setFixedHeight(142);Ui::installFilmstripDelegate(strip);layout->addWidget(strip);setCentralWidget(central);
    auto *dock=new QDockWidget(QStringLiteral("调整"),this);dock->setObjectName("adjustment-dock");dock->setProperty("initialAdjustmentGroup",1);dock->setProperty("compactAdjustmentNavigation",true);dock->setAllowedAreas(Qt::RightDockWidgetArea);dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    auto *inspectorTitle=new QWidget(dock);inspectorTitle->setFixedHeight(0);dock->setTitleBarWidget(inspectorTitle);dock->setAccessibleName(QStringLiteral("照片调整面板"));
    editPanel=new QWidget;editPanel->setObjectName("adjustment-panel");editPanel->setMinimumWidth(300);auto *sections=new QVBoxLayout(editPanel);sections->setContentsMargins(8,4,8,8);sections->setSpacing(9);QFormLayout *form=nullptr;
    histogram=new PreviewHistogram;sections->addWidget(histogram);
    auto section=[&](const QString &title,Ui::Glyph glyph,const QString &description=QString()){sections->addWidget(Ui::card(title,description,glyph,form));};
    auto spin=[this,&form](const QString &name,const QString &object,double minimum,double maximum,double step,double initial){
        auto *s=new QDoubleSpinBox;s->setObjectName(object);s->setRange(minimum,maximum);s->setSingleStep(step);s->setDecimals(2);s->setValue(initial);s->setKeyboardTracking(false);s->setMinimumWidth(76);s->setMaximumWidth(94);
        auto *label=new QLabel(name);label->setObjectName("label-"+object);label->setBuddy(s);s->setAccessibleName(name);
        auto *row=new QWidget;auto *line=new QHBoxLayout(row);line->setContentsMargins(0,0,0,0);
        auto *slider=new QSlider(Qt::Horizontal);slider->setObjectName("slider-"+object);slider->setAccessibleName(name);slider->setRange(int(minimum*100),int(maximum*100));slider->setValue(int(initial*100));
        line->addWidget(slider,1);line->addWidget(s);auto *reset=new QPushButton;reset->setIcon(Ui::icon(Ui::Glyph::Reset));reset->setIconSize({14,14});reset->setFixedWidth(25);reset->setAccessibleName(name+QStringLiteral("复位"));reset->setToolTip(QStringLiteral("复位此项"));reset->setObjectName("reset-"+object);line->addWidget(reset);QWidget::setTabOrder(slider,s);QWidget::setTabOrder(s,reset);connect(reset,&QPushButton::clicked,this,[this,s,initial]{endControlEdit();s->setValue(initial);endControlEdit();});form->addRow(label,row);
        connect(slider,&QSlider::valueChanged,this,[s](int v){s->setValue(v/100.);});
        connect(slider,&QSlider::sliderReleased,this,&Window::endControlEdit);
        connect(s,&QDoubleSpinBox::valueChanged,this,[this,slider,s](double value){const QSignalBlocker block(slider);slider->setValue(int(std::lround(value*100)));editControls(s);});
        connect(s,&QDoubleSpinBox::editingFinished,this,[this]{for(auto *bar:editPanel->findChildren<QSlider *>())if(bar->isSliderDown())return;endControlEdit();});return s;
    };
    section(QStringLiteral("风格预设"),Ui::Glyph::Color,QStringLiteral("替换全局调色与细节；保留白平衡、裁剪、校直和翻转。"));
    presets=builtinPresets();presetChoice=new QComboBox;presetChoice->setObjectName("preset-choice");presetChoiceCount=presets.size();for(const auto &p:presets)presetChoice->addItem(p.name);
    QStringList libraryWarnings;const auto library=loadPresetLibrary(presetLibrary,libraryWarnings);
    for(const auto &p:library){bool found=false;for(int i=presetChoiceCount;i<presets.size();++i)if(presets[i].name==p.name){presets[i]=p;found=true;break;}if(!found){presets.append(p);++customPresetCount;}}
    form->addRow(QStringLiteral("预设"),presetChoice);auto *libraryHint=new QLabel;presetLibraryHint=libraryHint;libraryHint->setObjectName("preset-library-hint");libraryHint->setProperty("role","muted");libraryHint->setWordWrap(true);form->addRow(libraryHint);
    refreshPresetChoice(0,QStringLiteral("内置 %1 · 自定义 %2%3").arg(presetChoiceCount).arg(customPresetCount).arg(presetLibrary.isEmpty()?QStringLiteral(" · 本次会话"):QStringLiteral(" · 本机预设库"))+(libraryWarnings.isEmpty()?QString():QStringLiteral(" · 已忽略 %1 项无效/超限文件").arg(libraryWarnings.size())));
    auto *presetActions=new QWidget;auto *presetLine=new QHBoxLayout(presetActions);presetLine->setContentsMargins(0,0,0,0);auto *presetApply=new QPushButton(QStringLiteral("应用"));presetApply->setObjectName("preset-apply");auto *presetSave=new QPushButton(Ui::icon(Ui::Glyph::Folder),QStringLiteral("保存…"));presetSave->setObjectName("preset-save");auto *presetLoad=new QPushButton(Ui::icon(Ui::Glyph::Import),QStringLiteral("读取…"));presetLoad->setObjectName("preset-load");for(auto *b:{presetApply,presetSave,presetLoad})presetLine->addWidget(b);form->addRow(presetActions);
    connect(presetApply,&QPushButton::clicked,this,[this]{if(current<0||comparing||cropSelecting)return;QString error;if(!beginPresetStrength(error)){presetStrengthHint->setText(error);statusBar()->showMessage(error,6000);}});
    connect(presetSave,&QPushButton::clicked,this,[this]{if(current<0||comparing||cropSelecting)return;auto path=QFileDialog::getSaveFileName(this,QStringLiteral("保存 LuminaS 调整预设"),"Custom.luminas-preset.json","LuminaS 预设 (*.luminas-preset.json)");if(path.isEmpty())return;if(!path.endsWith(".json",Qt::CaseInsensitive))path+=".luminas-preset.json";QString error;if(!saveAdjustmentPreset(path,error))QMessageBox::warning(this,QStringLiteral("预设保存失败"),error);else statusBar()->showMessage(QStringLiteral("已保存预设：")+QFileInfo(path).fileName(),5000);});
    connect(presetLoad,&QPushButton::clicked,this,[this]{if(current<0||comparing||cropSelecting)return;const auto path=QFileDialog::getOpenFileName(this,QStringLiteral("读取并应用 LuminaS 调整预设"),{},"LuminaS 预设 (*.luminas-preset.json *.json)");if(path.isEmpty())return;QString error;if(!loadAdjustmentPreset(path,error))QMessageBox::warning(this,QStringLiteral("预设读取失败"),error);});
    presetManage=new QPushButton(Ui::icon(Ui::Glyph::Edit),QStringLiteral("管理…"));presetManage->setObjectName("preset-manage");presetManage->setAccessibleName(QStringLiteral("管理选中的自定义预设"));presetManage->setToolTip(QStringLiteral("重命名或删除选中的自定义预设；不改变当前照片调整。"));form->addRow(presetManage);
    auto *presetMenu=new QMenu(presetManage);presetManage->setMenu(presetMenu);auto *renameAction=presetMenu->addAction(QStringLiteral("重命名…"));renameAction->setObjectName("preset-rename");auto *removeAction=presetMenu->addAction(QStringLiteral("删除…"));removeAction->setObjectName("preset-remove");
    auto showLibraryError=[this](const QString &error){presetLibraryHint->setText(error);statusBar()->showMessage(error,6000);};
    connect(renameAction,&QAction::triggered,this,[this,showLibraryError]{const int index=presetChoice->currentIndex();if(index<presetChoiceCount||index>=presets.size())return;bool accepted=false;const auto name=QInputDialog::getText(this,QStringLiteral("重命名预设"),QStringLiteral("预设名称"),QLineEdit::Normal,presets[index].name,&accepted);if(!accepted)return;QString error;if(!renameSelectedPreset(name,error))showLibraryError(error);});
    connect(removeAction,&QAction::triggered,this,[this,showLibraryError]{const int index=presetChoice->currentIndex();if(index<presetChoiceCount||index>=presets.size())return;const auto reply=QMessageBox::question(this,QStringLiteral("删除自定义预设"),QStringLiteral("删除“%1”？\n只删除本机预设，现有照片调整不受影响。").arg(presets[index].name),QMessageBox::Yes|QMessageBox::No,QMessageBox::No);if(reply!=QMessageBox::Yes)return;QString error;if(!removeSelectedPreset(error))showLibraryError(error);});
    connect(presetChoice,&QComboBox::currentIndexChanged,this,[this]{updatePresetManagement();});updatePresetManagement();
    auto *strengthRow=new QWidget;auto *strengthLine=new QHBoxLayout(strengthRow);strengthLine->setContentsMargins(0,0,0,0);presetStrengthSlider=new QSlider(Qt::Horizontal);presetStrengthSlider->setObjectName("slider-preset-strength");presetStrengthSlider->setRange(0,100);presetStrengthSlider->setValue(100);presetStrengthSlider->setEnabled(false);presetStrengthSlider->setAccessibleName(QStringLiteral("预设强度"));auto *strengthValue=new QLabel(QStringLiteral("100%"));strengthValue->setObjectName("preset-strength-value");strengthLine->addWidget(presetStrengthSlider,1);strengthLine->addWidget(strengthValue);form->addRow(QStringLiteral("强度"),strengthRow);presetStrengthHint=new QLabel(QStringLiteral("应用预设后可调整强度；手动调整后需重新应用。"));presetStrengthHint->setObjectName("preset-strength-hint");presetStrengthHint->setProperty("role","muted");presetStrengthHint->setWordWrap(true);form->addRow(presetStrengthHint);
    connect(presetStrengthSlider,&QSlider::valueChanged,this,[this,strengthValue](int value){strengthValue->setText(QString::number(value)+"%");if(!restoring)updatePresetStrength(value);});connect(presetStrengthSlider,&QSlider::sliderReleased,this,&Window::finishPresetStrength);connect(presetChoice,&QComboBox::currentIndexChanged,this,[this]{invalidatePresetStrength();});
    section(QStringLiteral("白平衡"),Ui::Glyph::Balance);
    auto *neutralPicker=new QPushButton(QStringLiteral("白平衡吸管"));neutralPicker->setObjectName("wb-picker");neutralPicker->setCheckable(true);neutralPicker->setToolTip(QStringLiteral("点击明亮、未过曝的中性区域；采样调整前的浮点预览。Esc取消。"));
    auto *wbAuto=new QPushButton(Ui::icon(Ui::Glyph::Balance),QStringLiteral("自动"));wbAuto->setObjectName("wb-auto");wbAuto->setAccessibleName(QStringLiteral("自动白平衡"));wbAuto->setToolTip(QStringLiteral("通过画面统计估计中性色；主体色彩可能影响结果，可撤销后手动微调。"));wbAuto->setEnabled(false);auto *wbActions=new QWidget;auto *wbActionLine=new QHBoxLayout(wbActions);wbActionLine->setContentsMargins(0,0,0,0);wbActionLine->addWidget(neutralPicker,1);wbActionLine->addWidget(wbAuto);form->addRow(wbActions);
    neutralPicker->setAccessibleName(QStringLiteral("白平衡吸管"));neutralPicker->setEnabled(false);
    pickerHint=new QLabel(QStringLiteral("打开照片后可取样白平衡"));pickerHint->setObjectName("wb-picker-hint");pickerHint->setProperty("role","muted");pickerHint->setWordWrap(true);form->addRow(pickerHint);
    connect(neutralPicker,&QPushButton::toggled,this,[this](bool enabled){if(enabled){endControlEdit();if(pointPickerActive){pointPickerActive=false;editor->cancelPointColorSample();if(pointColorPanel)pointColorPanel->setPickActive(false);}if(sharpenMaskActive){sharpenMaskActive=false;if(sharpenMaskPreview){const QSignalBlocker block(sharpenMaskPreview);sharpenMaskPreview->setChecked(false);}requestEdit();}}updatePickerState();});
    connect(canvas,&Canvas::neutralPicked,this,[this](QPointF uv){QString error;if(pointPickerActive){if(!pickPointColor(uv,error))statusBar()->showMessage(error,6000);return;}if(!pickNeutralBalance(uv,error)){pickerHint->setText(error);statusBar()->showMessage(error,6000);}});
    connect(wbAuto,&QPushButton::clicked,this,[this]{QString error;if(!autoWhiteBalance(error)){pickerHint->setText(error);statusBar()->showMessage(error,6000);}});
    warmth=spin(QStringLiteral("暖冷 EV"),"warmth",-1,1,.05,0);warmthLabel=editPanel->findChild<QLabel *>("label-warmth");
    tint=spin(QStringLiteral("色调 EV"),"tint",-1,1,.05,0);tintLabel=editPanel->findChild<QLabel *>("label-tint");
    auto *wbOriginal=new QPushButton(Ui::icon(Ui::Glyph::Reset),QStringLiteral("恢复原照白平衡"));wbOriginal->setObjectName("wb-original");wbOriginal->setAccessibleName(QStringLiteral("恢复原照白平衡"));wbOriginal->setEnabled(false);form->addRow(wbOriginal);
    connect(wbOriginal,&QPushButton::clicked,this,[this]{restoreOriginalWhiteBalance();});
    section(QStringLiteral("光线"),Ui::Glyph::Light);
    exposure=spin(QStringLiteral("曝光 EV"),"exposure",-5,5,.1,0);
    contrast=spin(QStringLiteral("对比度"),"contrast",-1,1,.05,0);
    highlights=spin(QStringLiteral("高光"),"highlights",-1,1,.05,0);shadows=spin(QStringLiteral("阴影"),"shadows",-1,1,.05,0);
    whites=spin(QStringLiteral("白色"),"whites",-1,1,.05,0);blacks=spin(QStringLiteral("黑色"),"blacks",-1,1,.05,0);
    section(QStringLiteral("色彩"),Ui::Glyph::Color);
    vibrance=spin(QStringLiteral("鲜艳度"),"vibrance",-1,1,.05,0);saturation=spin(QStringLiteral("饱和度"),"saturation",0,2,.05,1);
    section(QStringLiteral("质感"),Ui::Glyph::Edit);texture=spin(QStringLiteral("纹理"),"texture",-1,1,.05,0);clarity=spin(QStringLiteral("清晰度"),"clarity",-1,1,.05,0);texture->setToolTip(QStringLiteral("细小纹理的局部反差；负值柔化"));clarity->setToolTip(QStringLiteral("中等尺度的局部反差；负值降低反差"));
    dehaze=spin(QStringLiteral("去雾"),"dehaze",-1,1,.05,0);dehaze->setToolTip(QStringLiteral("正值减弱雾化，负值增加雾感；明亮天空和肤色需结合实际画面检查"));
    section(QStringLiteral("细节"),Ui::Glyph::Edit);sharpenAmount=spin(QStringLiteral("锐化强度"),"sharpen-amount",0,2,.05,0);sharpenRadius=spin(QStringLiteral("半径 px"),"sharpen-radius",.5,4,.1,1);sharpenThreshold=spin(QStringLiteral("边缘阈值"),"sharpen-threshold",0,.1,.01,0);sharpenAmount->setToolTip(QStringLiteral("亮度反差锐化；0关闭。过强可能强化噪声或产生边缘光晕"));sharpenRadius->setToolTip(QStringLiteral("方向归一原图像素中的模糊标准差，4K按采样尺度换算；不是屏幕像素"));sharpenThreshold->setToolTip(QStringLiteral("线性亮度差的软阈值，抑制微小反差；不是独立边缘蒙版"));
    sharpenMask=spin(QStringLiteral("边缘蒙版"),"sharpen-mask",0,1,.05,0);sharpenMask->setToolTip(QStringLiteral("0对全图锐化；增大后抑制平坦区域，只保留较强边缘。与边缘阈值独立。"));
    // Diagnostic preview only: renders the gray effective sharpen coefficient through the
    // transient RenderView. It never touches Recipe/history/presets and cannot be saved.
    sharpenMaskPreview=new QPushButton(QStringLiteral("显示锐化蒙版"));sharpenMaskPreview->setObjectName("sharpen-mask-preview");sharpenMaskPreview->setCheckable(true);
    sharpenMaskPreview->setAccessibleName(QStringLiteral("显示锐化蒙版"));sharpenMaskPreview->setToolTip(QStringLiteral("诊断显示：白色=受锐化影响，黑色=被保护；临时预览，不会保存到调整。与边缘蒙版数值独立。"));
    form->addRow(sharpenMaskPreview);
    connect(sharpenMaskPreview,&QPushButton::toggled,this,[this](bool enabled){
        endControlEdit(); // finalize the in-flight gesture before switching display mode
        const bool usable=current>=0&&!comparing&&!cropSelecting;
        if(enabled&&usable){
            // Never simultaneous with point-color modes: the canvas has one transient view.
            clearPointColorModes();
            if(auto *b=findChild<QPushButton *>("wb-picker")){const QSignalBlocker block(b);b->setChecked(false);}
            sharpenMaskActive=true;
        } else {
            sharpenMaskActive=false;
        }
        {const QSignalBlocker block(sharpenMaskPreview);sharpenMaskPreview->setChecked(sharpenMaskActive);}
        requestEdit();
    });
    noiseLuminance=spin(QStringLiteral("亮度降噪"),"noise-luminance",0,1,.05,0);noiseChroma=spin(QStringLiteral("色彩降噪"),"noise-chroma",0,1,.05,0);noiseLuminance->setToolTip(QStringLiteral("保边局部平滑亮度；高强度可能损失细小纹理，不是AI细节重建"));noiseChroma->setToolTip(QStringLiteral("分别平滑亮度之外的色差；保持原亮度，减弱彩色杂点"));
    section(QStringLiteral("颗粒"),Ui::Glyph::Light,QStringLiteral("以原图像素为尺度添加颗粒；预览与完整源共用配方。"));grainAmount=spin(QStringLiteral("强度"),"grain-amount",0,1,.05,0);grainSize=spin(QStringLiteral("尺寸（原图像素）"),"grain-size",1,12,.5,3);grainRoughness=spin(QStringLiteral("粗糙度"),"grain-roughness",0,1,.05,.5);
    section(QStringLiteral("裁剪后暗角"),Ui::Glyph::Light,QStringLiteral("按最终构图收暗或提亮边缘；不是镜头配置校正。"));
    vignetteAmount=spin(QStringLiteral("强度 EV"),"vignette-amount",-2,2,.05,0);vignetteMidpoint=spin(QStringLiteral("中点"),"vignette-midpoint",0,1,.05,.5);vignetteFeather=spin(QStringLiteral("羽化"),"vignette-feather",0,1,.05,.5);
    vignetteAmount->setToolTip(QStringLiteral("负值收暗、正值提亮；0关闭，复位保留其他调整。"));
    section(QStringLiteral("点曲线"),Ui::Glyph::Curve,QStringLiteral("亮度曲线统一缩放RGB；RGB与单色曲线分别映射通道。"));curveChannel=new QComboBox;curveChannel->setObjectName("curve-channel");curveChannel->addItems({"RGB",QStringLiteral("红"),QStringLiteral("绿"),QStringLiteral("蓝"),QStringLiteral("亮度")});form->addRow(QStringLiteral("通道"),curveChannel);curveEditor=new CurveEditor;form->addRow(curveEditor);
    auto *resetCurve=new QPushButton(QStringLiteral("复位此通道曲线"));form->addRow(resetCurve);connect(resetCurve,&QPushButton::clicked,this,[this]{endControlEdit();auto r=currentRecipe();if(curveChannel->currentIndex()==4)r.luminanceCurve={{0,0},{1,1}};else r.curves[curveChannel->currentIndex()]={{0,0},{1,1}};if(curveChannel->currentIndex()==4)r.luminanceTangents.clear();else r.curveTangents[curveChannel->currentIndex()].clear();applyRecipe(r,QStringLiteral("曲线复位"));});
    connect(curveEditor,&CurveEditor::curveEdited,this,[this](const Curve &curve){if(restoring||current<0||comparing||cropSelecting)return;auto candidate=live;if(curveChannel->currentIndex()==4){candidate.luminanceCurve=curve;candidate.luminanceTangents.clear();}else{candidate.curves[curveChannel->currentIndex()]=curve;candidate.curveTangents[curveChannel->currentIndex()].clear();}if(!validRecipe(candidate)){curveEditor->setCurve(curveChannel->currentIndex()==4?live.luminanceCurve:live.curves[curveChannel->currentIndex()],curveChannel->currentIndex()==4?live.luminanceTangents:live.curveTangents[curveChannel->currentIndex()]);return;}live=candidate;requestEdit();});connect(curveEditor,&CurveEditor::curveFinished,this,&Window::endControlEdit);
    connect(curveChannel,&QComboBox::currentIndexChanged,this,[this]{endControlEdit();restoreControls();});
    section(QStringLiteral("参数曲线"),Ui::Glyph::Curve,QStringLiteral("调节四个影调区域与分界；曲线保持平滑、单调，黑白端点固定。"));parametricEditor=new CurveEditor;parametricEditor->setObjectName("parametric-curve-graph");parametricEditor->setReadOnly(true);parametricEditor->setToolTip(QStringLiteral("下方滑杆调节四个影调区域和分界；图表显示当前参数曲线。"));form->addRow(parametricEditor);
    const QString regionLabels[]{QStringLiteral("阴影"),QStringLiteral("暗调"),QStringLiteral("亮调"),QStringLiteral("高光")};const QString regionNames[]{"param-shadows","param-darks","param-lights","param-highlights"};for(int i=0;i<4;++i)parametricRegions[i]=spin(regionLabels[i],regionNames[i],-1,1,.05,0);
    const QString splitLabels[]{QStringLiteral("暗部分界"),QStringLiteral("中间分界"),QStringLiteral("亮部分界")};const QString splitNames[]{"param-shadow-split","param-mid-split","param-highlight-split"};for(int i=0;i<3;++i)parametricSplits[i]=spin(splitLabels[i],splitNames[i],.05,.95,.01,(i+1)*.25);
    auto *resetParametric=new QPushButton(QStringLiteral("复位参数曲线"));resetParametric->setObjectName("reset-parametric-curve");form->addRow(resetParametric);connect(resetParametric,&QPushButton::clicked,this,[this]{endControlEdit();auto r=currentRecipe();r.parametricCurve={};applyRecipe(r,QStringLiteral("参数曲线复位"));});
    section(QStringLiteral("HSL 分色"),Ui::Glyph::Color);hslBand=new QComboBox;hslBand->setObjectName("hsl-band");hslBand->addItems({QStringLiteral("红"),QStringLiteral("橙"),QStringLiteral("黄"),QStringLiteral("绿"),QStringLiteral("青"),QStringLiteral("蓝"),QStringLiteral("紫"),QStringLiteral("洋红")});form->addRow(QStringLiteral("色段"),hslBand);
    hslHue=spin(QStringLiteral("色相 °"),"hsl-hue",-30,30,1,0);hslSaturation=spin(QStringLiteral("饱和度"),"hsl-saturation",-1,1,.05,0);hslLuminance=spin(QStringLiteral("明度 EV"),"hsl-luminance",-1,1,.05,0);
    connect(hslBand,&QComboBox::currentIndexChanged,this,[this]{endControlEdit();restoreControls();});
    section(QStringLiteral("精确分色"),Ui::Glyph::Color,QStringLiteral("取色后调整范围与羽化；范围预览将未选中的颜色转灰。"));setupPointColorPanel(form);
    section(QStringLiteral("颜色分级"),Ui::Glyph::Color);gradeRange=new QComboBox;gradeRange->setObjectName("grade-range");gradeRange->addItems({QStringLiteral("阴影"),QStringLiteral("中间调"),QStringLiteral("高光")});form->addRow(QStringLiteral("范围"),gradeRange);
    gradeHue=spin(QStringLiteral("色相 °"),"grade-hue",0,360,1,0);gradeSaturation=spin(QStringLiteral("着色强度"),"grade-saturation",0,1,.05,0);gradeLuminance=spin(QStringLiteral("明度 EV"),"grade-luminance",-1,1,.05,0);
    gradeBlend=spin(QStringLiteral("混合"),"grade-blend",0,1,.05,.5);gradeBalance=spin(QStringLiteral("平衡"),"grade-balance",-1,1,.05,0);gradeBalance->setToolTip(QStringLiteral("负值偏向阴影，正值偏向高光"));connect(gradeRange,&QComboBox::currentIndexChanged,this,[this]{endControlEdit();restoreControls();});
    section(QStringLiteral("黑白混色"),Ui::Glyph::Light);monochromeEnabled=new QCheckBox(QStringLiteral("启用黑白"));monochromeEnabled->setObjectName("monochrome-enabled");form->addRow(monochromeEnabled);monochromeStrength=spin(QStringLiteral("混合比例"),"monochrome-strength",0,1,.05,1);monochromeStrength->setToolTip(QStringLiteral("黑白与彩色之间连续混合；关闭黑白时不生效。"));
    monochromeBand=new QComboBox;monochromeBand->setObjectName("monochrome-band");monochromeBand->addItems({QStringLiteral("红"),QStringLiteral("橙"),QStringLiteral("黄"),QStringLiteral("绿"),QStringLiteral("青"),QStringLiteral("蓝"),QStringLiteral("紫"),QStringLiteral("洋红")});form->addRow(QStringLiteral("原色段"),monochromeBand);
    monochromeMix=spin(QStringLiteral("明度 EV"),"monochrome-mix",-1,1,.05,0);monochromeMix->setToolTip(QStringLiteral("按转黑白之前的颜色选择明度"));connect(monochromeBand,&QComboBox::currentIndexChanged,this,[this]{endControlEdit();restoreControls();});
    connect(monochromeEnabled,&QCheckBox::toggled,this,[this](bool enabled){if(restoring||current<0||comparing||cropSelecting)return;endControlEdit();auto r=currentRecipe();r.monochrome=enabled;applyRecipe(r,QStringLiteral("黑白模式"));});
    section(QStringLiteral("原色调整"),Ui::Glyph::Color,QStringLiteral("调整原色风格；相机配置文件与色卡标定尚未提供。"));primaryChannel=new QComboBox;primaryChannel->setObjectName("primary-channel");primaryChannel->addItems({QStringLiteral("红原色"),QStringLiteral("绿原色"),QStringLiteral("蓝原色")});form->addRow(QStringLiteral("原色"),primaryChannel);primaryHue=spin(QStringLiteral("色相 °"),"primary-hue",-30,30,1,0);primarySaturation=spin(QStringLiteral("饱和度"),"primary-saturation",-1,1,.05,0);connect(primaryChannel,&QComboBox::currentIndexChanged,this,[this]{endControlEdit();restoreControls();});
    section(QStringLiteral("手动横向色差"),Ui::Glyph::Color,QStringLiteral("分别配准红/蓝边缘；原图像素尺度，尚无自动镜头标定。"));setupChromaticAberrationControls(form);
    section(QStringLiteral("紫绿边去色"),Ui::Glyph::Color,QStringLiteral("手动抑制高反差边缘的紫绿杂色；真实细色边可能受影响。"));setupDefringeControls(form);
    section(QStringLiteral("畸变校正"),Ui::Glyph::Crop,QStringLiteral("手动修正径向变形；透明边缘可通过裁剪去除，尚无镜头标定。"));setupDistortionControls(form);
    section(QStringLiteral("镜头暗角校正"),Ui::Glyph::Light,QStringLiteral("按原图中心补偿镜头边缘亮度；与裁剪后风格暗角分开。"));setupLensShadingControls(form);
    section(QStringLiteral("透视"),Ui::Glyph::Crop,QStringLiteral("校正画面的水平/垂直汇聚；透明边缘可通过裁剪去除。"));setupPerspectiveControls(form);
    section(QStringLiteral("构图"),Ui::Glyph::Crop);
    angle=spin(QStringLiteral("校直角度 °"),"angle",-45,45,.1,0);
    cropAspect=new QComboBox;cropAspect->setObjectName("crop-aspect");cropAspect->addItems({QStringLiteral("自由"),QStringLiteral("原图"),"1:1","4:3","3:2","16:9"});form->addRow(QStringLiteral("裁剪比例"),cropAspect);connect(cropAspect,&QComboBox::currentIndexChanged,this,[this]{updateCropAspect();});
    auto *flips=new QWidget;auto *flipLine=new QHBoxLayout(flips);flipLine->setContentsMargins(0,0,0,0);flipHorizontal=new QPushButton(QStringLiteral("水平翻转"));flipHorizontal->setObjectName("flip-horizontal");flipVertical=new QPushButton(QStringLiteral("垂直翻转"));flipVertical->setObjectName("flip-vertical");for(auto *b:{flipHorizontal,flipVertical}){b->setCheckable(true);flipLine->addWidget(b);}form->addRow(flips);connect(flipHorizontal,&QPushButton::clicked,this,[this](bool enabled){auto r=currentRecipe();r.flipHorizontal=enabled;applyRecipe(r,QStringLiteral("水平翻转"));});connect(flipVertical,&QPushButton::clicked,this,[this](bool enabled){auto r=currentRecipe();r.flipVertical=enabled;applyRecipe(r,QStringLiteral("垂直翻转"));});
    auto button=[this,form](const QString &label,auto callback){auto *b=new QPushButton(label);form->addRow(b);connect(b,&QPushButton::clicked,this,callback);return b;};
    auto *selectCrop=button(QStringLiteral("在照片上裁剪"),[this]{beginCropSelection();});selectCrop->setObjectName("begin-crop");selectCrop->setToolTip(QStringLiteral("显示完整校正画面，拖选后应用裁剪；Esc退出。"));
    auto *constrainCrop=button(QStringLiteral("去除空白边缘"),[this]{constrainCurrentCrop();});constrainCrop->setObjectName("constrain-crop");constrainCrop->setToolTip(QStringLiteral("在校正后的有效源范围内按当前比例收紧裁剪框，保守去除旋转/透视等产生的透明边缘；保持裁框中心与比例，可能比最小所需多裁一点，不会拉伸或填充，可撤销。"));constrainCrop->setAccessibleName(QStringLiteral("去除空白边缘，按当前比例保守收紧裁剪框"));
    auto *resetGeometry=button(QStringLiteral("复位构图"),[this]{auto r=currentRecipe();r.crop={0,0,1,1};r.angle=0;r.perspective={0,0};r.distortion=0;r.chromaticAberration={0,0};r.flipHorizontal=r.flipVertical=false;applyRecipe(r,QStringLiteral("复位构图"));});resetGeometry->setObjectName("reset-geometry");resetGeometry->setToolTip(QStringLiteral("恢复完整画面，清除透视、畸变校正、横向色差、校直和水平/垂直翻转；保留调色。"));
    button(QStringLiteral("重置所有调整"),[this]{applyRecipe({},QStringLiteral("重置"));});
    auto *hint=new QLabel(QStringLiteral("调整只作用当前照片。可从导入菜单保存项目或恢复会话。旋转、透视和光学校正可能露出透明边缘，可用“去除空白边缘”保守收紧裁框。"));hint->setWordWrap(true);form->addRow(hint);
    for(auto *value:editPanel->findChildren<QDoubleSpinBox *>()){value->setMinimumWidth(76);value->setMaximumWidth(94);}
    new Ui::ParameterWheelGuard(editPanel);
    sections->addStretch();auto *scroll=new QScrollArea;scroll->setObjectName("global-adjustment-scroll");scroll->setWidgetResizable(true);scroll->setWidget(editPanel);dock->setWidget(scroll);Ui::installAdjustmentNavigation(dock,scroll);addDockWidget(Qt::RightDockWidgetArea,dock);resizeDocks({dock},{350},Qt::Horizontal);editPanel->setEnabled(false);
    setupLocalEditing(dock);
    connect(browse,&QAction::triggered,this,[this,dock]{clearPointColorModes();if(auto *b=findChild<QPushButton *>("wb-picker"))b->setChecked(false);endControlEdit();if(cropSelecting){markViewEpochChange();cropSelecting=false;canvas->setCropMode(false);requestEdit();}dock->hide();});connect(edit,&QAction::triggered,dock,&QWidget::show);
    connect(canvas,&Canvas::cropPicked,this,[this](QRectF crop){auto r=currentRecipe();r.crop=crop;if(cropSelecting)markViewEpochChange();cropSelecting=false;canvas->setCropMode(false);applyRecipe(r,QStringLiteral("裁剪"));});
    auto shortcut=[this](const QKeySequence &key,auto action){auto *s=new QShortcut(key,this);connect(s,&QShortcut::activated,this,[action]{if(qobject_cast<QLineEdit *>(QApplication::focusWidget()))return;action();});};
    shortcut(QKeySequence::Undo,[this]{undoEdit();});shortcut(QKeySequence::Redo,[this]{redoEdit();});
    shortcut(QKeySequence(Qt::Key_Escape),[this]{if(cancelLocalEdits())return;if(pointPickerActive){clearPointColorModes();updatePickerState();return;}if(sharpenMaskActive){clearPointColorModes();requestEdit();return;}if(auto *b=findChild<QPushButton *>("wb-picker");b&&b->isChecked()){b->setChecked(false);return;}if(cropSelecting)markViewEpochChange();cropSelecting=false;canvas->setCropMode(false);restoreControls();requestEdit();});
    connect(editor,&EditLoader::ready,this,[this](const EditResult &result){presentResult(result);});
    connect(editor,&EditLoader::prefetchProgress,this,[this](int done,int total,int failed){cacheCompleted=done;cacheTotal=total;cacheFailures=failed;refreshLabels();});
    connect(editor,&EditLoader::prefetched,this,[this](const EditResult &result){
        if(projectLoading||projectApplying||sourceBlocks.contains(result.path))return;
        for(auto &photo:photos)if(photo.path==result.path){photo.cacheState=result.error.isEmpty()?1:2;
            if(result.error.isEmpty()){importErrors.remove(photo.path);bindPreparedAsset(result);}else importErrors[photo.path]=result.error;
            if(result.error.isEmpty()&&history.value(photo.path)==Recipe{})photo.item->setIcon(QPixmap::fromImage(result.image.scaled(120,85,Qt::KeepAspectRatio,Qt::SmoothTransformation)));
            if(!result.error.isEmpty())photo.item->setToolTip(photo.path+"\n"+result.error);break;}
        // A canonical default cache entry can serve a newly selected default
        // document. Revalidate the cache identity; do not relabel a stale edit job.
        if(current>=0&&displayed.isEmpty()&&photos[current].path==result.path&&live==Recipe{}&&localIdentity==0&&!comparing&&!cropSelecting&&!pointRangeActive&&!sharpenMaskActive&&result.error.isEmpty()){
            if(auto frame=editor->preview(result.path,Recipe{})){frame->tag=expected;presentResult(*frame,true);}
        }
    });
    loader = new JpegLoader(this);
    auto *summary=new QLabel;summary->setObjectName("photo-summary");summary->setProperty("role","muted");summary->setAccessibleName(QStringLiteral("照片数量与选择"));statusBar()->addWidget(summary,1);
    auto *cacheLabel=new QLabel;cacheLabel->setObjectName("preview-cache-progress-label");cacheLabel->setProperty("role","muted");statusBar()->addPermanentWidget(cacheLabel);
    auto *cacheProgress=new QProgressBar;cacheProgress->setObjectName("preview-cache-progress");cacheProgress->setAccessibleName(QStringLiteral("后台预览准备进度"));cacheProgress->setFixedWidth(80);cacheProgress->setFixedHeight(5);cacheProgress->setTextVisible(false);statusBar()->addPermanentWidget(cacheProgress);
    projectStatus=new QLabel(QStringLiteral("本次会话"));projectStatus->setObjectName("project-status");projectStatus->setProperty("role","muted");statusBar()->addPermanentWidget(projectStatus);
    connect(strip, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
        if (item) activate(item->data(Qt::UserRole).toInt());
    });
    connect(strip, &QListWidget::itemSelectionChanged, this, [this] { refreshLabels(); });
    connect(loader, &JpegLoader::ready, this, [this](const JpegResult &result, quint64 resultToken, bool preview) {
        for (auto &photo : photos) if (photo.path == result.path) {
            if (!result.image.isNull()&&photo.cacheState==0) photo.item->setIcon(QPixmap::fromImage(result.image.scaled(120,85,Qt::KeepAspectRatio,Qt::SmoothTransformation)));
            if (!result.error.isEmpty()) photo.item->setToolTip(result.path + "\n" + result.error);
            break;
        }
        Q_UNUSED(preview);Q_UNUSED(resultToken);
    });
    refreshLabels();
    updateResponsiveLayout();
    connect(canvas->presenter(),&DisplayPresenter::displayProfileChanged,this,[this](const QByteArray &icc){Ui::setFilmstripDisplayProfile(strip,icc);});
    Ui::setFilmstripDisplayProfile(strip,canvas->presenter()->monitorIcc());
    setupProjectPersistence(importMenu,projectDirectory.isNull()?(libraryDirectory.isNull()?QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+QStringLiteral("/recovery"):QStringLiteral("")):projectDirectory);
}
Window::~Window(){
    projectClosing=true;if(projectPoll)projectPoll->stop();
    finishLocalEdits();
    restoring=true;current=-1;
    if(editor)editor->invalidate();
    // Child focus-out signals may fire from QWidget's base destructor, after history dies.
    for(auto *child:findChildren<QObject *>())QObject::disconnect(child,nullptr,this,nullptr);
    QCoreApplication::removePostedEvents(this,QEvent::MetaCall);
}
void Window::updateResponsiveLayout(){
    if(histogram){histogram->setMinimumHeight(height()<600?60:96);histogram->setMaximumHeight(height()<600?60:110);}
    if(strip){const bool compact=height()<650;strip->setGridSize({156,compact?90:124});strip->setFixedHeight(compact?108:142);auto *toggle=findChild<QToolButton *>("filmstrip-toggle");strip->setVisible(height()>=500&&toggle->isChecked());toggle->setEnabled(height()>=500);toggle->setToolTip(height()>=500?QStringLiteral("显示 / 收起胶片条"):QStringLiteral("窗口高度较小，胶片条自动收起；扩大窗口恢复"));}
    if(auto *hint=findChild<QLabel *>("navigation-hint"))hint->setVisible(width()>=1000);
}
void Window::resizeEvent(QResizeEvent *event){
    QMainWindow::resizeEvent(event);
    updateResponsiveLayout();
}
void Window::markViewEpochChange(){
    // Monotonic view identity: any requested RenderView or compare/crop role change closes the
    // previous view generation, so a view opened again can never accept the earlier frame.
    ++viewEpoch;
}
// Single gate for every result that may reach the canvas or lastBase: path, exact RenderView
// and same-generation identity (interactive feedback may omit draft/job, never identity).
bool Window::acceptResult(const EditResult &result) const{
    return !projectLoading&&!projectApplying&&current>=0&&!sourceBlocks.contains(result.path)&&result.path==photos[current].path&&acceptsEditTag(result.tag,expected);
}
void Window::presentResult(const EditResult &result,bool immediate){
    const int range=pointRangeActive&&!comparing&&!cropSelecting&&pointColorPanel?pointColorPanel->selectedIndex():-1;
    // Validate the entire transient view: a preview rendered for another display mode must never be shown.
    if(result.view!=requestedView||result.view.pointRange!=range||result.view.sharpenMask!=(sharpenMaskActive&&!comparing&&!cropSelecting))return;
        const bool liveFeedback=acceptResult(result);
        if(!liveFeedback||result.tag.job<presentedJob)return;
        presentedJob=result.tag.job;
        displayedTag=result.tag;
        QString error=result.error;
        if(error.isEmpty()&&!photos[current].sha256.isEmpty()&&photos[current].sha256!=result.sourceHash)error=QStringLiteral("源文件内容已变化，请关闭重开；不继续应用旧参数");
        previewError=error;
        const int previousCacheState=photos[current].cacheState;
        if(!error.isEmpty()){localSourceReady=false;photos[current].cacheState=2;displayed.clear();canvas->setMessage(QStringLiteral("无法读取这张照片\n")+error);histogram->setImage({});details->setText(error);editPanel->setEnabled(false);
            if(auto *retry=findChild<QPushButton *>("preview-retry")){retry->setVisible(true);retry->setEnabled(!error.contains(QStringLiteral("源文件内容已变化")));retry->setToolTip(error);}}
        else {
            photos[current].cacheState=1;
            if(auto *retry=findChild<QPushButton *>("preview-retry"))retry->hide();
            currentOriginalSize=result.originalSize;updateCropAspect();
            photos[current].sha256=result.sourceHash;displayed=result.path;canvas->setImage(result.image,QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8").arg(result.path).arg(result.tag.session).arg(result.tag.revision).arg(result.tag.geometry).arg(result.tag.localIdentity).arg(result.tag.viewEpoch).arg(result.view.pointRange).arg(result.view.sharpenMask?1:0));
            canvas->setLocalMask(result.view.localMaskVisible?result.localMask:QImage{});bindLocalAsset(result);
            // The diagnostic mask is not a photograph: keep the real histogram and filmstrip icon.
            if(!result.view.sharpenMask)histogram->setImage(result.image);
            displayedAngle=result.angle;
            canvas->setLiveAngle(((comparing||cropSelecting?0:live.angle)-result.angle)*((live.flipHorizontal!=live.flipVertical&&!comparing&&!cropSelecting)?-1:1));
            if(!result.tag.interactive&&result.view.pointRange<0&&!result.view.sharpenMask)photos[current].item->setIcon(QPixmap::fromImage(result.image.scaled(120,85,Qt::KeepAspectRatio,Qt::SmoothTransformation)));
            details->setText(QStringLiteral("%1 · 原图（方向归一）%2 × %3 · 输出 %4 × %5 · 预览 %6 × %7 · %8 · %9")
                .arg(QFileInfo(result.path).fileName()).arg(result.originalSize.width()).arg(result.originalSize.height()).arg(result.fullSize.width()).arg(result.fullSize.height())
                .arg(result.image.width()).arg(result.image.height()).arg(comparing?QStringLiteral("原图 / 默认显影"):QStringLiteral("长边4K（小图原尺寸），非原图100%检查")).arg(result.colorNote));
            details->setToolTip(details->text());
            details->setText(QStringLiteral("%1  ·  %2 × %3  ·  %4").arg(QFileInfo(result.path).fileName()).arg(result.image.width()).arg(result.image.height()).arg(result.view.sharpenMask?QStringLiteral("锐化蒙版 · 白色作用 / 黑色保护"):comparing?QStringLiteral("原图比较"):qMax(result.image.width(),result.image.height())<3840?QStringLiteral("原尺寸预览"):QStringLiteral("4K预览")));
            editPanel->setEnabled(true);for(auto *spin:editPanel->findChildren<QDoubleSpinBox *>())spin->setEnabled(!comparing&&!cropSelecting);curveEditor->setEnabled(!comparing&&!cropSelecting);
            if(immediate){editPanel->setEnabled(false);details->setText(details->text()+QStringLiteral(" · 已缓存显示，编辑数据准备中…"));}
            for(auto *slider:editPanel->findChildren<QSlider *>())slider->setEnabled(!comparing&&!cropSelecting);
            monochromeEnabled->setEnabled(!comparing&&!cropSelecting);monochromeBand->setEnabled(live.monochrome&&!comparing&&!cropSelecting);monochromeMix->setEnabled(live.monochrome&&!comparing&&!cropSelecting);editPanel->findChild<QSlider *>("slider-monochrome-mix")->setEnabled(live.monochrome&&!comparing&&!cropSelecting);monochromeStrength->setEnabled(live.monochrome&&!comparing&&!cropSelecting);editPanel->findChild<QSlider *>("slider-monochrome-strength")->setEnabled(live.monochrome&&!comparing&&!cropSelecting);
            flipHorizontal->setEnabled(!comparing&&!cropSelecting);flipVertical->setEnabled(!comparing&&!cropSelecting);
            presetChoice->setEnabled(!comparing&&!cropSelecting);for(const auto &name:{"preset-apply","preset-save","preset-load"})editPanel->findChild<QPushButton *>(name)->setEnabled(!comparing&&!cropSelecting);
            canvas->setCropMode(cropSelecting);
        }
        refreshLocalPanel();updatePickerState();refreshPresetStrengthState();
        if(previousCacheState!=photos[current].cacheState)refreshLabels();
        const bool stillLive=acceptResult(result);
        if(!immediate&&stillLive)emit previewChanged(result.path,error.isEmpty());
}
void Window::addFiles(const QStringList &paths) {
    if(projectLoading||projectApplying||projectClosing)return;
    const QSignalBlocker block(strip);
    QStringList added;
    for (const auto &path : paths) {
        if(photos.size()>=LocalEdit::maximumProjectPhotos){statusBar()->showMessage(QStringLiteral("每个项目最多 2000 张照片"),6000);break;}
        QFileInfo info(path);
        const QString absolute = info.canonicalFilePath().isEmpty() ? info.absoluteFilePath() : info.canonicalFilePath();
        if (known.contains(absolute)) continue;
        known.insert(absolute);
        auto *item = new QListWidgetItem(strip); item->setData(Qt::UserRole, photos.size()); item->setToolTip(absolute);
        photos.append({absolute, Pick::Unmarked, item});
        added.append(absolute);
        // RAW thumbnail is derived from the genuine developed frame when selected;
        // do not start redundant full RAW jobs for filmstrip placeholders.
        if (info.suffix().compare("cr3",Qt::CaseInsensitive)!=0) loader->thumbnail(absolute);
    }
    refreshLabels(); filter(filters->currentIndex());editor->prefetch(added);
}
void Window::activate(int index) {
    if (index < 0 || index >= photos.size() || photos[index].item->isHidden()) return;
    finishLocalEdits();localSourceReady=false;
    if(current>=0&&index!=current&&!comparing&&!cropSelecting&&history.commit(photos[current].path,live,QStringLiteral("调整")))++revision;
    current = index; ++token; markViewEpochChange(); displayed.clear();
    currentOriginalSize={};
    if(comparing||cropSelecting)markViewEpochChange();comparing=false;cropSelecting=false;canvas->setCropMode(false);compareButton->setChecked(false);restoreControls();
    { const QSignalBlocker block(strip); strip->setCurrentItem(photos[index].item, QItemSelectionModel::NoUpdate); }
    if(auto *retry=findChild<QPushButton *>("preview-retry"))retry->hide();
    canvas->setMessage(QStringLiteral("正在准备照片预览…"));details->setText(QFileInfo(photos[index].path).fileName()+QStringLiteral("   正在准备预览"));details->setToolTip(photos[index].path);
    histogram->setImage({});
    editPanel->setEnabled(false);refreshLabels();requestEdit();
    if(auto frame=editor->preview(photos[current].path,live)){frame->tag=expected;presentResult(*frame,true);}
}
void Window::mark(Pick pick) {
    if (current < 0) return;
    photos[current].pick = pick; refreshLabels(); filter(filters->currentIndex());
}
void Window::filter(int value) {
    finishLocalEdits();
    endControlEdit();
    const QSignalBlocker block(strip);
    { const QSignalBlocker comboBlock(filters); filters->setCurrentIndex(value); }
    int first = -1;
    for (int i = 0; i < photos.size(); ++i) {
        const bool visible = value == 0 || int(photos[i].pick) == value - 1;
        if (!visible) photos[i].item->setSelected(false);
        photos[i].item->setHidden(!visible);
        if (visible && first < 0) first = i;
    }
    if (current < 0 || photos[current].item->isHidden()) {
        current = -1;localSourceReady=false; ++token; displayed.clear();
        ++draft;expected={session,revision,draft,token,++geometry,false,localIdentity,++viewEpoch};editPanel->setEnabled(false);
        if (first >= 0) activate(first);
        else { editor->invalidate();canvas->setMessage(QStringLiteral("当前筛选没有照片"));histogram->setImage({});details->setText(QStringLiteral("更换筛选或打开更多 JPEG")); }
    }
    refreshLabels();refreshLocalPanel();
}
void Window::step(int delta) {
    for (int i = current + delta; i >= 0 && i < photos.size(); i += delta) if (!photos[i].item->isHidden()) { activate(i); return; }
}
void Window::refreshLabels() {
    for(const auto &name:{"tool-hand","tool-crop","photo-previous","photo-next","pick-unmarked","pick-keep","pick-reject","view-fit"})if(auto *b=findChild<QToolButton *>(name))b->setEnabled(current>=0);
    compareButton->setEnabled(current>=0);
    auto historyLabel=[](const QString &verb,const EditCommand *cmd){return cmd?verb+QStringLiteral("：")+QFileInfo(cmd->path).fileName()+" / "+cmd->name:verb;};
    undoAction->setToolTip(historyLabel(QStringLiteral("撤销"),history.nextUndo()));undoAction->setEnabled(history.canUndo());
    redoAction->setToolTip(historyLabel(QStringLiteral("重做"),history.nextRedo()));redoAction->setEnabled(history.canRedo());
    const QStringList marks{QStringLiteral("未标记"),QStringLiteral("保留"),QStringLiteral("排除")};
    int visible = 0,actualFailures=0;
    for (int i = 0; i < photos.size(); ++i) {
        const auto cache=photos[i].cacheState==1?QStringLiteral(" · 4K就绪"):photos[i].cacheState==2?QStringLiteral(" · 解析失败"):QStringLiteral(" · 排队解析");
        photos[i].item->setData(Qt::UserRole+1,i==current);photos[i].item->setData(Qt::UserRole+2,int(photos[i].pick));photos[i].item->setData(Qt::UserRole+3,photos[i].cacheState);photos[i].item->setData(Qt::UserRole+4,QFileInfo(photos[i].path).fileName());
        photos[i].item->setText((i == current ? QStringLiteral("▶ ") : QString()) + QFileInfo(photos[i].path).fileName() + "\n" + marks[int(photos[i].pick)]+cache);
        if (!photos[i].item->isHidden()) ++visible;
        if(photos[i].cacheState==2)++actualFailures;
    }
    if(auto *summary=findChild<QLabel *>("photo-summary")){
        summary->setText(photos.isEmpty()?QStringLiteral("导入照片开始选片"):QStringLiteral("%1 张照片   当前 %2%3").arg(visible).arg(current<0?QStringLiteral("无"):QString::number(current+1)).arg(strip->selectedItems().isEmpty()?QString():QStringLiteral("   多选 %1").arg(strip->selectedItems().size())));
        summary->setToolTip(QStringLiteral("共 %1 张，筛选可见 %2 张；参数仅作用当前照片。标记不删除原文件。").arg(photos.size()).arg(visible));
    }
    const bool preparing=cacheTotal>0&&cacheCompleted<cacheTotal;
    if(auto *label=findChild<QLabel *>("preview-cache-progress-label")){
        label->setText(preparing?QStringLiteral("准备预览 %1/%2").arg(cacheCompleted).arg(cacheTotal):actualFailures?QStringLiteral("%1 张预览失败").arg(actualFailures):QString());label->setVisible(preparing||actualFailures>0);
        label->setToolTip(QStringLiteral("导入后后台准备4K预览。解析失败 %1 张；点击失败照片查看原因。").arg(actualFailures));
    }
    if(auto *progress=findChild<QProgressBar *>("preview-cache-progress")){progress->setRange(0,qMax(1,cacheTotal));progress->setValue(cacheCompleted);progress->setVisible(preparing);}
}
Recipe Window::currentRecipe() const {return current<0?Recipe{}:live;}
void Window::updateCropAspect(){
    double ratio=0;switch(cropAspect->currentIndex()){case 1:if(currentOriginalSize.isValid())ratio=double(currentOriginalSize.width())/currentOriginalSize.height();break;case 2:ratio=1;break;case 3:ratio=4./3;break;case 4:ratio=3./2;break;case 5:ratio=16./9;break;default:break;}canvas->setCropAspect(ratio);
}
void Window::restoreControls(){
    clearPointColorModes();
    if(presetStrengthActive&&presetStrengthDirty){presetStrengthActive=false;presetStrengthDirty=false;if(presetStrengthSlider)presetStrengthSlider->setSliderDown(false);}
    live=current<0?Recipe{}:history.value(photos[current].path);syncControlsFromLive();refreshLocalPanel();
}
void Window::syncControlsFromLive(){
    restoring=true;
    exposure->setValue(live.exposure);contrast->setValue(live.contrast);saturation->setValue(live.saturation);
    highlights->setValue(live.highlights);shadows->setValue(live.shadows);whites->setValue(live.whites);blacks->setValue(live.blacks);vibrance->setValue(live.vibrance);
    texture->setValue(live.texture);clarity->setValue(live.clarity);dehaze->setValue(live.dehaze);
    noiseLuminance->setValue(live.noiseLuminance);noiseChroma->setValue(live.noiseChroma);
    grainAmount->setValue(live.grainAmount);grainSize->setValue(live.grainSize);grainRoughness->setValue(live.grainRoughness);
    vignetteAmount->setValue(live.vignetteAmount);vignetteMidpoint->setValue(live.vignetteMidpoint);vignetteFeather->setValue(live.vignetteFeather);
    const auto &primary=live.primaries[primaryChannel->currentIndex()];primaryHue->setValue(primary.hue*30);primarySaturation->setValue(primary.saturation);
    sharpenAmount->setValue(live.sharpenAmount);sharpenRadius->setValue(live.sharpenRadius);sharpenThreshold->setValue(live.sharpenThreshold);
    sharpenMask->setValue(live.sharpenMask);
    flipHorizontal->setChecked(live.flipHorizontal);flipVertical->setChecked(live.flipVertical);
    curveEditor->setCurve(curveChannel->currentIndex()==4?live.luminanceCurve:live.curves[curveChannel->currentIndex()],curveChannel->currentIndex()==4?live.luminanceTangents:live.curveTangents[curveChannel->currentIndex()]);parametricEditor->setCurve(parametricPoints(live.parametricCurve));parametricEditor->setRegionBoundaries(live.parametricCurve.splits);for(int i=0;i<4;++i)parametricRegions[i]->setValue(live.parametricCurve.regions[i]);for(int i=0;i<3;++i)parametricSplits[i]->setValue(live.parametricCurve.splits[i]);const auto &hsl=live.hsl[hslBand->currentIndex()];hslHue->setValue(hsl.hue*30);hslSaturation->setValue(hsl.saturation);hslLuminance->setValue(hsl.luminance);
    const auto &grade=live.grading[gradeRange->currentIndex()];gradeHue->setValue(grade.hue);gradeSaturation->setValue(grade.saturation);gradeLuminance->setValue(grade.luminance);gradeBlend->setValue(live.gradingBlend);gradeBalance->setValue(live.gradingBalance);
    monochromeEnabled->setChecked(live.monochrome);monochromeMix->setValue(live.monochromeMix[monochromeBand->currentIndex()]);monochromeStrength->setValue(live.monochromeStrength);monochromeBand->setEnabled(live.monochrome&&!comparing&&!cropSelecting);monochromeMix->setEnabled(live.monochrome&&!comparing&&!cropSelecting);editPanel->findChild<QSlider *>("slider-monochrome-mix")->setEnabled(live.monochrome&&!comparing&&!cropSelecting);monochromeStrength->setEnabled(live.monochrome&&!comparing&&!cropSelecting);editPanel->findChild<QSlider *>("slider-monochrome-strength")->setEnabled(live.monochrome&&!comparing&&!cropSelecting);
    warmth->setValue(live.warmth);tint->setValue(live.tint);angle->setValue(live.angle);canvas->setLiveAngle(0);
    const bool raw=current>=0&&QFileInfo(photos[current].path).suffix().compare("cr3",Qt::CaseInsensitive)==0;
    findChild<QPushButton *>("wb-original")->setToolTip(raw?QStringLiteral("恢复RAW记录的相机白平衡，仅复位暖冷和色调；其他调整保留，可撤销。"):QStringLiteral("移除额外暖冷和色调，保留JPEG已有的机内白平衡；其他调整保留，可撤销。"));
    warmthLabel->setText(QStringLiteral("暖冷 EV"));tintLabel->setText(QStringLiteral("色调 EV"));
    warmthLabel->setToolTip(raw?QStringLiteral("修改RAW红/蓝白平衡增益；松手从源精算"):QStringLiteral("调整JPEG已渲染RGB的红/蓝平衡"));
    syncChromaticAberrationControls();syncDefringeControls();syncDistortionControls();syncLensShadingControls();syncPerspectiveControls();if(pointColorPanel)pointColorPanel->setPoints(live.pointColors);
    tintLabel->setToolTip(raw?QStringLiteral("修改RAW绿色白平衡增益；松手从源精算"):QStringLiteral("调整JPEG已渲染RGB的绿色平衡"));restoring=false;
}
void Window::requestEdit(){
    if(projectLoading||projectApplying||projectClosing)return;
    if(current>=0&&sourceBlocks.contains(photos[current].path)){
        localSourceReady=false;displayed.clear();editPanel->setEnabled(false);canvas->setMessage(sourceBlocks.value(photos[current].path));
        if(auto *retry=findChild<QPushButton *>("preview-retry"))retry->hide();return;
    }
    if(pointPickerActive){pointPickerActive=false;editor->cancelPointColorSample();if(pointColorPanel)pointColorPanel->setPickActive(false);}
    if(presetStrengthActive&&(current<0||photos[current].path!=presetStrengthPath||live!=presetStrengthLast))invalidatePresetStrength();refreshPresetStrengthState();
    if(auto *b=findChild<QPushButton *>("wb-picker"))b->setChecked(false);
    if(auto *b=findChild<QToolButton *>("tool-hand"))b->setChecked(!cropSelecting);
    if(auto *b=findChild<QToolButton *>("tool-crop"))b->setChecked(cropSelecting);
    if(current<0)return;auto recipe=comparing?Recipe{}:live;
    if(cropSelecting){recipe.crop={0,0,1,1};recipe.angle=0;recipe.flipHorizontal=recipe.flipVertical=false;}
    expected={session,revision,++draft,++token,geometry,false,localIdentity,viewEpoch};
    if(!comparing&&!cropSelecting&&curveEditor->isDragging())expected.interactive=true;
    if(!comparing&&!cropSelecting&&(brushTransaction.active()||gradientTransaction.active()||localParameterDraft))expected.interactive=true;
    if(!comparing&&!cropSelecting)for(auto *slider:editPanel->findChildren<QSlider *>())if(slider->isSliderDown())expected.interactive=true;
    RenderView view;if(pointRangeActive&&!comparing&&!cropSelecting&&pointColorPanel&&pointColorPanel->selectedIndex()>=0&&pointColorPanel->selectedIndex()<recipe.pointColors.size())view.pointRange=pointColorPanel->selectedIndex();else if(pointRangeActive){pointRangeActive=false;pointColorPanel->setRangeActive(false);}
    // Compare and crop replace the whole frame; the diagnostic mask is not meaningful there.
    if(sharpenMaskActive&&(comparing||cropSelecting)){sharpenMaskActive=false;if(sharpenMaskPreview){const QSignalBlocker block(sharpenMaskPreview);sharpenMaskPreview->setChecked(false);}}
    view.sharpenMask=sharpenMaskActive&&!comparing&&!cropSelecting;
    if(!comparing&&!cropSelecting&&!view.sharpenMask&&view.pointRange<0&&adjustmentScope&&adjustmentScope->currentIndex()==1&&localPanel->overlayVisible()&&!selectedLocalLayer.isEmpty()){
        view.localMaskVisible=true;view.localMaskLayer=selectedLocalLayer;
    }
    const int role=comparing?1:cropSelecting?2:0;
    if(view!=requestedView||role!=requestedViewRole)markViewEpochChange();
    requestedView=view;requestedViewRole=role;expected.viewEpoch=viewEpoch;
    const auto document=currentPhotoDocument();
    if(!comparing&&!cropSelecting&&document&&(!document->layers.isEmpty()||view.localMaskVisible)){
        QString error;if(!editor->requestDocument(photos[current].path,document,expected,error,view))statusBar()->showMessage(error,6000);
    }else editor->request(photos[current].path,recipe,expected,view);
    previewError.clear();
    updatePickerState();refreshPresetStrengthState();
}
bool Window::neutralPickerAvailable() const{
    return current>=0&&!comparing&&!cropSelecting&&!pointRangeActive&&!sharpenMaskActive&&displayed==photos[current].path&&displayedTag==expected&&editPanel->isEnabled()&&editor->canPickNeutral(photos[current].path,live,expected);
}
void Window::updatePickerState(){
    if(auto *button=findChild<QPushButton *>("constrain-crop"))button->setEnabled(current>=0&&!comparing&&!cropSelecting&&editPanel->isEnabled());
    auto *button=findChild<QPushButton *>("wb-picker");const bool available=neutralPickerAvailable();
    findChild<QPushButton *>("wb-original")->setEnabled(available&&(live.warmth!=0||live.tint!=0));findChild<QPushButton *>("wb-auto")->setEnabled(available);
    if(!available)button->setChecked(false);button->setEnabled(available);canvas->setPickerMode(available&&button->isChecked());
    button->setText(button->isChecked()?QStringLiteral("取样中 · Esc取消"):QStringLiteral("白平衡吸管"));
    const auto message=current<0?QStringLiteral("打开照片后可取样白平衡"):!previewError.isEmpty()?QStringLiteral("当前预览失败，请检查图片"):comparing?QStringLiteral("退出原图比较后可取样"):cropSelecting?QStringLiteral("完成裁剪后可取样"):sharpenMaskActive?QStringLiteral("关闭锐化蒙版显示后可取样"):!available?QStringLiteral("正在准备当前预览，完成后可取样"):button->isChecked()?QStringLiteral("点击明亮、未过曝的中性区域；Esc取消"):QStringLiteral("选择中性区域取样，结果可撤销");
    pickerHint->setText(message);button->setAccessibleDescription(message);updatePointPickerState(available);syncCanvasTools();
}
void Window::showAdjustmentSection(const QString &title){
    if(adjustmentScope)adjustmentScope->setCurrentIndex(0);
    if(auto *edit=findChild<QAction *>("workspace-edit"))edit->setChecked(true);
    if(auto *dock=findChild<QDockWidget *>("adjustment-dock")){
        dock->show();if(auto *choice=dock->findChild<QComboBox *>("adjustment-section-choice")){
            const int index=choice->findText(title);if(index>=0){choice->setCurrentIndex(index);QMetaObject::invokeMethod(choice,"activated",Qt::DirectConnection,Q_ARG(int,index));}
        }
    }
}
void Window::beginCropSelection(){
    if(current<0)return;endControlEdit();clearPointColorModes();
    if(auto *button=findChild<QPushButton *>("wb-picker"))button->setChecked(false);
    comparing=false;compareButton->setChecked(false);markViewEpochChange();cropSelecting=true;canvas->setCropMode(false);
    showAdjustmentSection(QStringLiteral("构图"));requestEdit();
}
void Window::constrainCurrentCrop(){
    if(current<0||comparing||cropSelecting||!editPanel->isEnabled())return;
    endControlEdit();
    const auto document=currentPhotoDocument();
    if(!document||!document->asset.normalizedSize.isValid()){statusBar()->showMessage(QStringLiteral("尚无可用的源尺寸，无法约束裁切"),6000);return;}
    auto recipe=currentRecipe();QRectF output=recipe.crop;QString error;
    if(!constrainedCrop(document->asset.normalizedSize,recipe,output,error)){
        statusBar()->showMessage(error.isEmpty()?QStringLiteral("无法约束裁切"):error,6000);return;
    }
    if(output==recipe.crop){statusBar()->showMessage(QStringLiteral("当前裁切已在有效源范围内"),6000);return;}
    recipe.crop=output;applyRecipe(recipe,QStringLiteral("去除空白边缘"));
    statusBar()->showMessage(QStringLiteral("约束裁切已应用，可撤销"),6000);
}
void Window::syncCanvasTools(){
    syncLocalTool();
    const auto *neutral=findChild<QPushButton *>("wb-picker");const bool picking=pointPickerActive||(neutral&&neutral->isChecked());
    if(auto *hand=findChild<QToolButton *>("tool-hand")){QSignalBlocker block(hand);hand->setChecked(!cropSelecting&&!picking&&!canvas->brushMode());}
    if(auto *crop=findChild<QToolButton *>("tool-crop")){QSignalBlocker block(crop);crop->setChecked(cropSelecting);}
    if(auto *exit=findChild<QPushButton *>("exit-crop"))exit->setVisible(cropSelecting);
    if(auto *hint=findChild<QLabel *>("navigation-hint"))hint->setText(cropSelecting?QStringLiteral("拖选后应用裁剪，Esc退出"):picking?QStringLiteral("点击照片取样，Esc取消"):canvas->brushMode()?(localGradientKind?QStringLiteral("拖动渐变或控制点 · Alt 减去 · Space 平移"):QStringLiteral("笔刷涂抹 · Alt 擦除 · Space 平移")):QStringLiteral("滚轮缩放 · 小手拖动"));
}
bool Window::autoWhiteBalance(QString &error){
    if(!neutralPickerAvailable()){error=pickerHint->text();return false;}
    Recipe estimated;if(!editor->autoBalance(photos[current].path,live,expected,estimated,error))return false;
    if(auto *b=findChild<QPushButton *>("wb-picker"))b->setChecked(false);
    if(estimated==live){statusBar()->showMessage(QStringLiteral("白平衡已是当前统计结果"),4000);return true;}
    applyRecipe(estimated,QStringLiteral("自动白平衡"));return true;
}
bool Window::restoreOriginalWhiteBalance(){
    if(!neutralPickerAvailable()||(live.warmth==0&&live.tint==0))return false;
    endControlEdit();auto recipe=live;recipe.warmth=recipe.tint=0;
    applyRecipe(recipe,QStringLiteral("恢复原照白平衡"));return true;
}
bool Window::pickNeutralBalance(QPointF uv,QString &error){
    if(!neutralPickerAvailable()){error=pickerHint->text();return false;}
    if(current<0||comparing||cropSelecting){error=QStringLiteral("请在正常编辑模式取样");return false;}
    if(presentedJob!=expected.job){error=QStringLiteral("请等待最新调整预览完成后取样");return false;}
    if(displayed!=photos[current].path){error=QStringLiteral("当前图片没有有效预览");return false;}
    if(!editPanel->isEnabled()){error=QStringLiteral("请等待浮点编辑数据准备完成后取样");return false;}
    Recipe picked;if(!editor->pickNeutral(photos[current].path,live,expected,uv,picked,error))return false;
    applyRecipe(picked,QStringLiteral("白平衡吸管"));return true;
}
void Window::editControls(QDoubleSpinBox *changed){
    if(restoring||current<0||comparing||cropSelecting)return;
    for(int i=0;i<3;++i)if(changed==parametricSplits[i]){const auto &b=live.parametricCurve.splits;const double value=std::clamp(changed->value(),i==0?.05:b[i-1]+.05,i==2?.95:b[i+1]-.05);live.parametricCurve.splits[i]=value;restoring=true;changed->setValue(value);restoring=false;parametricEditor->setCurve(parametricPoints(live.parametricCurve));parametricEditor->setRegionBoundaries(live.parametricCurve.splits);requestEdit();return;}
    double *target=nullptr;for(int i=0;i<4;++i)if(changed==parametricRegions[i])target=&live.parametricCurve.regions[i];
    if(changed==exposure)target=&live.exposure;else if(changed==contrast)target=&live.contrast;else if(changed==saturation)target=&live.saturation;
    else if(changed==highlights)target=&live.highlights;else if(changed==shadows)target=&live.shadows;else if(changed==whites)target=&live.whites;else if(changed==blacks)target=&live.blacks;else if(changed==vibrance)target=&live.vibrance;
    else if(changed==texture)target=&live.texture;else if(changed==clarity)target=&live.clarity;
    else if(changed==noiseLuminance)target=&live.noiseLuminance;else if(changed==noiseChroma)target=&live.noiseChroma;
    else if(changed==grainAmount)target=&live.grainAmount;else if(changed==grainSize)target=&live.grainSize;else if(changed==grainRoughness)target=&live.grainRoughness;
    else if(changed==vignetteAmount)target=&live.vignetteAmount;else if(changed==vignetteMidpoint)target=&live.vignetteMidpoint;else if(changed==vignetteFeather)target=&live.vignetteFeather;
    else if(changed==primaryHue)target=&live.primaries[primaryChannel->currentIndex()].hue;else if(changed==primarySaturation)target=&live.primaries[primaryChannel->currentIndex()].saturation;
    else if(changed==dehaze)target=&live.dehaze;
    else if(changed==sharpenAmount)target=&live.sharpenAmount;else if(changed==sharpenRadius)target=&live.sharpenRadius;else if(changed==sharpenThreshold)target=&live.sharpenThreshold;
    else if(changed==sharpenMask)target=&live.sharpenMask;
    else if(changed==warmth)target=&live.warmth;else if(changed==tint)target=&live.tint;
    else if(changed==hslHue)target=&live.hsl[hslBand->currentIndex()].hue;else if(changed==hslSaturation)target=&live.hsl[hslBand->currentIndex()].saturation;else if(changed==hslLuminance)target=&live.hsl[hslBand->currentIndex()].luminance;
    else if(changed==gradeHue)target=&live.grading[gradeRange->currentIndex()].hue;else if(changed==gradeSaturation)target=&live.grading[gradeRange->currentIndex()].saturation;else if(changed==gradeLuminance)target=&live.grading[gradeRange->currentIndex()].luminance;else if(changed==gradeBlend)target=&live.gradingBlend;else if(changed==gradeBalance)target=&live.gradingBalance;else if(changed==monochromeMix)target=&live.monochromeMix[monochromeBand->currentIndex()];else if(changed==monochromeStrength)target=&live.monochromeStrength;
    if(target){const double value=(changed==hslHue||changed==primaryHue)?changed->value()/30:changed->value();if(std::abs(*target-value)<1e-12)return;*target=value;parametricEditor->setCurve(parametricPoints(live.parametricCurve));parametricEditor->setRegionBoundaries(live.parametricCurve.splits);requestEdit();return;}
    auto *angleSlider=editPanel->findChild<QSlider *>("slider-angle");
    if(angleSlider->isSliderDown()){live.angle=angle->value();expected={session,revision,++draft,++token,geometry,true,localIdentity,viewEpoch};canvas->setLiveAngle((live.angle-displayedAngle)*(live.flipHorizontal!=live.flipVertical?-1:1));return;}
    if(live.angle!=angle->value())++geometry;live.angle=angle->value();requestEdit();
}
void Window::endControlEdit(){
    finishLocalEdits();
    if(restoring||current<0||comparing||cropSelecting)return;
    if(presetStrengthActive&&presetStrengthDirty&&live==presetStrengthLast){finishPresetStrength();return;}
    if(history.commit(photos[current].path,live,QStringLiteral("调整"))){++revision;++geometry;refreshLabels();requestEdit();}
    else if(expected.interactive)requestEdit();
}
void Window::applyRecipe(const Recipe &recipe,const QString &name){
    finishLocalEdits();
    if(current<0||!validRecipe(recipe))return;
    invalidatePresetStrength();
    if(comparing||cropSelecting)markViewEpochChange();comparing=false;cropSelecting=false;canvas->setCropMode(false);compareButton->setChecked(false);
    if(history.commit(photos[current].path,recipe,name))++revision;
    ++geometry;restoreControls();refreshLabels();requestEdit();
}
void Window::undoEdit(){
    finishLocalEdits();++localIdentity;
    if(current<0)return;endControlEdit();const auto path=history.undo();if(path.isEmpty())return;
    restoreControls();++revision;++geometry;filter(0);for(int i=0;i<photos.size();++i)if(photos[i].path==path){activate(i);return;}
}
void Window::redoEdit(){
    finishLocalEdits();++localIdentity;
    if(current<0)return;endControlEdit();const auto path=history.redo();if(path.isEmpty())return;
    restoreControls();++revision;++geometry;filter(0);for(int i=0;i<photos.size();++i)if(photos[i].path==path){activate(i);return;}
}
void Window::compareOriginal(bool enabled){
    if(current<0)return;endControlEdit();if(comparing!=enabled)markViewEpochChange();comparing=enabled;cropSelecting=false;canvas->setCropMode(false);compareButton->setChecked(enabled);restoreControls();requestEdit();
}
bool Window::saveAdjustmentPreset(const QString &path,QString &error){
    if(current<0||comparing||cropSelecting){error=QStringLiteral("请在正常编辑模式保存预设");return false;}endControlEdit();auto name=QFileInfo(path).completeBaseName();if(name.endsWith(".luminas-preset",Qt::CaseInsensitive))name.chop(QString(".luminas-preset").size());
    AdjustmentPreset preset{name.trimmed(),applyPreset(Recipe{},AdjustmentPreset{QString(),currentRecipe()})};
    if(!savePresetFile(path,preset,error))return false;
    QString stored;
    if(!storeAdjustmentPreset(preset,stored)){error=QStringLiteral("外部 JSON 已保存，但预设库保存失败：")+stored;return false;}
    return true;
}
bool Window::loadAdjustmentPreset(const QString &path,QString &error){
    if(current<0||comparing||cropSelecting){error=QStringLiteral("请在正常编辑模式读取预设");return false;}AdjustmentPreset preset;if(!loadPresetFile(path,preset,error))return false;
    preset={preset.name.trimmed(),applyPreset(Recipe{},preset)};
    QString stored;
    if(!storeAdjustmentPreset(preset,stored)){error=QStringLiteral("预设未能写入本机预设库：")+stored;return false;}
    endControlEdit();applyRecipe(applyPreset(currentRecipe(),preset),QStringLiteral("预设：")+preset.name);return true;
}
void Window::refreshPresetChoice(int selected,const QString &hint){
    const QSignalBlocker block(presetChoice);presetChoice->clear();
    for(int i=0;i<presets.size();++i)presetChoice->addItem(QStringLiteral("%1 · %2").arg(presets[i].name,i<presetChoiceCount?QStringLiteral("内置"):QStringLiteral("自定义")));
    presetChoice->setCurrentIndex(selected>=0&&selected<presets.size()?selected:0);
    if(presetLibraryHint&&!hint.isEmpty())presetLibraryHint->setText(hint);
    updatePresetManagement();
}
void Window::updatePresetManagement(){if(presetManage){const int index=presetChoice->currentIndex();presetManage->setEnabled(index>=presetChoiceCount&&index<presets.size());}}
void Window::refreshPresetStrengthState(){if(presetStrengthSlider){const bool active=presetStrengthActive&&current>=0&&photos[current].path==presetStrengthPath; presetStrengthSlider->setEnabled(active&&!comparing&&!cropSelecting);presetStrengthHint->setText(active?QStringLiteral("相对应用前调整 · 0%保留原调整，100%为预设 · 拖动后可撤销"):QStringLiteral("应用预设后可调整强度；手动调整后需重新应用。"));}}
void Window::invalidatePresetStrength(){
    if(presetStrengthActive&&presetStrengthDirty){if(history.commit(presetStrengthPath,presetStrengthLast,QStringLiteral("预设强度"))){++revision;++geometry;refreshLabels();}}
    presetStrengthDirty=false;presetStrengthActive=false;refreshPresetStrengthState();
}
bool Window::beginPresetStrength(QString &error){
    error.clear();const int index=presetChoice->currentIndex();if(current<0||comparing||cropSelecting||!editPanel->isEnabled()||index<0||index>=presets.size()){error=QStringLiteral("请等待当前编辑预览就绪后应用预设");return false;}
    endControlEdit();const auto base=live;const auto snapshot=presets[index];const auto target=applyPreset(base,snapshot);applyRecipe(target,QStringLiteral("预设：")+snapshot.name);
    presetStrengthBase=base;presetStrengthSnapshot=snapshot;presetStrengthLast=target;presetStrengthPath=photos[current].path;presetStrengthActive=true;presetStrengthDirty=false;{const QSignalBlocker block(presetStrengthSlider);presetStrengthSlider->setValue(100);presetStrengthSlider->setProperty("lastPercent",100);}findChild<QLabel *>("preset-strength-value")->setText("100%");refreshPresetStrengthState();return true;
}
void Window::updatePresetStrength(int percentage){
    if(!presetStrengthActive||current<0||photos[current].path!=presetStrengthPath||comparing||cropSelecting)return;
    Recipe result;QString error;if(!blendPreset(presetStrengthBase,presetStrengthSnapshot,percentage/100.,result,error)){const QSignalBlocker block(presetStrengthSlider);presetStrengthSlider->setValue(presetStrengthSlider->property("lastPercent").isValid()?presetStrengthSlider->property("lastPercent").toInt():100);findChild<QLabel *>("preset-strength-value")->setText(QString::number(presetStrengthSlider->value())+"%");presetStrengthHint->setText(error);statusBar()->showMessage(error,6000);return;}
    presetStrengthSlider->setProperty("lastPercent",percentage);if(result==live)return;live=result;presetStrengthLast=result;presetStrengthDirty=true;syncControlsFromLive();requestEdit();if(!presetStrengthSlider->isSliderDown())finishPresetStrength();
}
void Window::finishPresetStrength(){
    if(!presetStrengthActive||current<0||photos[current].path!=presetStrengthPath)return;
    const bool changed=presetStrengthDirty;
    if(presetStrengthDirty){if(history.commit(presetStrengthPath,live,QStringLiteral("预设强度"))){++revision;++geometry;refreshLabels();}presetStrengthDirty=false;}
    if(changed||expected.interactive)requestEdit();
}
bool Window::renameSelectedPreset(const QString &name,QString &error){
    error.clear();const int index=presetChoice->currentIndex();if(index<presetChoiceCount||index>=presets.size()){error=QStringLiteral("请选择自定义预设；内置预设不能重命名");return false;}
    const auto trimmed=name.trimmed();bool valid=!trimmed.isEmpty()&&trimmed.size()<=80;for(auto c:trimmed)if(c.category()==QChar::Other_Control)valid=false;if(!valid){error=QStringLiteral("预设名称应为1–80字符，且不能含控制字符");return false;}
    for(int i=presetChoiceCount;i<presets.size();++i)if(i!=index&&presets[i].name==trimmed){error=QStringLiteral("已有同名自定义预设");return false;}
    if(trimmed==presets[index].name)return true;
    if(!presetLibrary.isEmpty()&&!renamePresetLibrary(presetLibrary,presets[index].name,trimmed,error))return false;
    invalidatePresetStrength();presets[index].name=trimmed;refreshPresetChoice(index,QStringLiteral("已重命名：%1 · 自定义 %2").arg(trimmed).arg(customPresetCount));return true;
}
bool Window::removeSelectedPreset(QString &error){
    error.clear();const int index=presetChoice->currentIndex();if(index<presetChoiceCount||index>=presets.size()){error=QStringLiteral("请选择自定义预设；内置预设不能删除");return false;}
    if(!presetLibrary.isEmpty()&&!removePresetLibrary(presetLibrary,presets[index].name,error))return false;
    invalidatePresetStrength();const auto name=presets[index].name;presets.removeAt(index);--customPresetCount;refreshPresetChoice(qMin(index,int(presets.size())-1),QStringLiteral("已删除：%1 · 自定义 %2").arg(name).arg(customPresetCount));return true;
}
bool Window::storeAdjustmentPreset(const AdjustmentPreset &preset,QString &error){
    error.clear();
    if(!presetLibrary.isEmpty()&&!storePresetLibrary(presetLibrary,preset,error))return false;
    invalidatePresetStrength();
    for(int i=presetChoiceCount;i<presets.size();++i)if(presets[i].name==preset.name){presets[i]=preset;refreshPresetChoice(i);return true;}
    presets.append(preset);++customPresetCount;refreshPresetChoice(presets.size()-1,presetLibrary.isEmpty()?QString()
        :QStringLiteral("内置 %1 · 自定义 %2 · 本机预设库").arg(presetChoiceCount).arg(customPresetCount));return true;
}





