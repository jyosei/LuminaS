#include "export_dialog.h"
#include "canvas.h"
#include "parameter_wheel_guard.h"
#include "ui_primitives.h"
#include "io/source_reader.h"
#include "render/cpu_renderer.h"
#include "render/local_renderer.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QScreen>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QCryptographicHash>
#include <QCloseEvent>
#include <QtConcurrentRun>
using namespace Export;
namespace {
bool sameDocument(const LocalEdit::DocumentSnapshot &a,const LocalEdit::DocumentSnapshot &b){return a==b||(a&&b&&*a==*b);}
}
QString ExportDialog::dataDir() const{const auto override=qEnvironmentVariable("LUMINA_EXPORT_DATA_DIR");return override.isEmpty()?QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/export":override;}
ExportDialog::ExportDialog(QList<Asset> input,int index,Queue *queue,QWidget *parent):ExportDialog(std::move(input),index,queue,parent,{}){}
ExportDialog::ExportDialog(QList<Asset> input,int index,Queue *queue,QWidget *parent,PreviewReader reader):QDialog(parent),exportQueue(queue),assets(std::move(input)),currentIndex(index),previewReader(std::move(reader)){
 if(!previewReader)previewReader=[](const QString &path,double warmth,double tint,const CancellationToken &token,const QString &hash){return readSource(path,warmth,tint,token,hash);};
 setObjectName("export-dialog");if(!parent)Ui::applyTheme(this,true);setWindowTitle(QStringLiteral("导出照片"));setAttribute(Qt::WA_DeleteOnClose);setWindowModality(Qt::NonModal);setMinimumSize(620,400);const auto available=screen()?screen()->availableGeometry().size():QSize(1120,800);resize(qMax(620,qMin(1080,available.width()-40)),qMax(400,qMin(740,available.height()-60)));pool.setMaxThreadCount(1);
 QString error;for(const auto &name:{"sRGB","Display P3","Adobe RGB","ProPhoto RGB"})profiles.append({QString::fromLatin1(name),{},builtinProfile(name,error),Mode::Rgb});profiles.append(installedProfiles());state.icc=profiles[0].icc;
 auto *layout=new QVBoxLayout(this);auto *body=new QHBoxLayout;layout->addLayout(body,1);left=new QWidget;auto *previewLayout=new QVBoxLayout(left);previewLayout->setContentsMargins(0,0,12,0);previewLabel=new QLabel(QStringLiteral("目标色彩预览"));previewLayout->addWidget(previewLabel);canvas=new Canvas;canvas->setObjectName("export-preview");canvas->setMessage(QStringLiteral("正在准备目标色彩预览…"));canvas->setMinimumSize(220,150);previewLayout->addWidget(canvas,1);summary=new QLabel;summary->setWordWrap(true);summary->setObjectName("export-summary");previewLayout->addWidget(summary);body->addWidget(left,1);
 auto *scroll=new QScrollArea;scroll->setObjectName("export-settings-scroll");scroll->setWidgetResizable(true);scroll->setMinimumWidth(350);auto *panel=new QWidget;auto *sections=new QVBoxLayout(panel);sections->setContentsMargins(12,4,12,4);scroll->setWidget(panel);body->addWidget(scroll,1);
 auto section=[&](const QString &title){auto *heading=new QLabel(title);QFont f=heading->font();f.setBold(true);heading->setFont(f);sections->addWidget(heading);auto *form=new QFormLayout;form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);form->setRowWrapPolicy(QFormLayout::WrapLongRows);sections->addLayout(form);return form;};
 auto combo=[&](QFormLayout *form,const QString &label,const char *id,QStringList values){auto *c=new QComboBox;c->setObjectName(id);c->setAccessibleName(label);c->addItems(values);form->addRow(label,c);connect(c,&QComboBox::currentIndexChanged,this,[this]{changed();});return c;};
 auto line=[&](QFormLayout *form,const QString &label,const char *id){auto *c=new QLineEdit;c->setObjectName(id);c->setAccessibleName(label);form->addRow(label,c);connect(c,&QLineEdit::textChanged,this,[this]{changed();});return c;};
 auto check=[&](QFormLayout *form,const QString &label,const char *id){auto *c=new QCheckBox(label);c->setObjectName(id);form->addRow(c);connect(c,&QCheckBox::toggled,this,[this]{changed();});return c;};
 auto real=[&](QFormLayout *form,const QString &label,const char *id,double maximum){auto *c=new QDoubleSpinBox;c->setRange(1,maximum);c->setDecimals(2);c->setObjectName(id);c->setAccessibleName(label);form->addRow(label,c);connect(c,&QDoubleSpinBox::valueChanged,this,[this]{changed();});return c;};
 auto integer=[&](QFormLayout *form,const QString &label,const char *id,int min,int max){auto *c=new QSpinBox;c->setRange(min,max);c->setObjectName(id);c->setAccessibleName(label);form->addRow(label,c);connect(c,&QSpinBox::valueChanged,this,[this]{changed();});return c;};
 auto *f=section(QStringLiteral("导出对象与预设"));range=combo(f,QStringLiteral("照片范围"),"export-range",{QStringLiteral("当前照片"),QStringLiteral("多选照片"),QStringLiteral("全部保留照片")});int selected=0;for(const auto &a:assets)selected+=a.selected;range->setCurrentIndex(selected>=2?1:0);
 preset=new QComboBox;preset->setObjectName("export-preset");preset->setAccessibleName(QStringLiteral("导出预设"));f->addRow(QStringLiteral("预设"),preset);auto *presetRow=new QHBoxLayout;f->addRow(presetRow);for(const auto &entry:{qMakePair(QStringLiteral("保存"),0),qMakePair(QStringLiteral("重命名"),1),qMakePair(QStringLiteral("删除"),2)}){auto *b=new QPushButton(entry.first);b->setObjectName("export-preset-"+QString::number(entry.second));presetRow->addWidget(b);connect(b,&QPushButton::clicked,this,[this,n=entry.second]{if(n==0)savePreset();else if(n==1)renamePreset();else deletePreset();});}
 f=section(QStringLiteral("文件与颜色"));format=combo(f,QStringLiteral("文件格式"),"export-format",{"JPEG","PNG","TIFF"});mode=combo(f,QStringLiteral("色彩模式"),"export-mode",{"RGB","CMYK"});bits=combo(f,QStringLiteral("位深 / 通道"),"export-bits",{QStringLiteral("8位"),QStringLiteral("16位")});quality=integer(f,QStringLiteral("JPEG品质"),"export-quality",1,100);zip=check(f,QStringLiteral("TIFF ZIP无损压缩"),"export-zip");profile=combo(f,QStringLiteral("目标ICC"),"export-profile",{});
 auto *import=new QPushButton(QStringLiteral("导入ICC…"));import->setObjectName("export-import-icc");f->addRow(import);connect(import,&QPushButton::clicked,this,[this]{auto p=QFileDialog::getOpenFileName(this,QStringLiteral("选择目标ICC"),{},"ICC (*.icc *.icm)");if(p.isEmpty())return;QFile file(p);QString e;if(!file.open(QIODevice::ReadOnly)||file.size()>4*1024*1024){diagnostic->setText(QStringLiteral("ICC无法读取或超过4MiB"));return;}auto bytes=file.readAll();auto m=Mode(mode->currentIndex());if(!validProfile(bytes,m,e)){diagnostic->setText(e);return;}profiles.append({QFileInfo(p).completeBaseName(),p,bytes,m});state.profileName=profiles.last().name;state.icc=bytes;profilesChanged();changed();});
 auto *advanced=new QWidget;auto *af=new QFormLayout(advanced);af->setContentsMargins(0,0,0,0);intent=combo(af,QStringLiteral("转换意图"),"export-intent",{QStringLiteral("相对色度"),QStringLiteral("感知")});bpc=check(af,QStringLiteral("黑点补偿"),"export-blackpoint");auto *toggle=new QPushButton(QStringLiteral("高级色彩选项"));toggle->setObjectName("export-advanced-color");toggle->setCheckable(true);f->addRow(toggle);f->addRow(advanced);advanced->hide();connect(toggle,&QPushButton::toggled,advanced,&QWidget::setVisible);
 auto *iccNote=new QLabel(QStringLiteral("始终嵌入目标ICC · 透明边缘合并为白色"));iccNote->setWordWrap(true);f->addRow(iccNote);
 f=section(QStringLiteral("图像大小"));sizeModeBox=combo(f,QStringLiteral("尺寸方式"),"export-size",{QStringLiteral("裁剪后完整尺寸"),QStringLiteral("指定长边"),QStringLiteral("指定短边"),QStringLiteral("适配宽高框"),QStringLiteral("百分比"),QStringLiteral("打印尺寸")});widthValue=real(f,QStringLiteral("宽 / 长边"),"export-width",1000000);heightValue=real(f,QStringLiteral("高 / 短边"),"export-height",1000000);percent=real(f,QStringLiteral("百分比 %"),"export-percent",1600);ppi=real(f,QStringLiteral("分辨率 PPI"),"export-ppi",100000);cm=check(f,QStringLiteral("打印尺寸使用厘米（未选为毫米）"),"export-cm");dontEnlarge=check(f,QStringLiteral("不放大小图"),"export-no-enlarge");
 f=section(QStringLiteral("位置与命名"));beside=check(f,QStringLiteral("保存到各原图目录下的子文件夹"),"export-beside");subfolder=line(f,QStringLiteral("子文件夹"),"export-subfolder");directory=line(f,QStringLiteral("指定文件夹"),"export-directory");auto *browse=new QPushButton(QStringLiteral("浏览…"));f->addRow(browse);connect(browse,&QPushButton::clicked,this,[this]{auto p=QFileDialog::getExistingDirectory(this,QStringLiteral("导出到"),directory->text());if(!p.isEmpty()){directory->setText(p);beside->setChecked(false);}});sequence=check(f,QStringLiteral("前缀＋序号（未选用原文件名）"),"export-sequence");prefix=line(f,QStringLiteral("前缀"),"export-prefix");sequenceStart=integer(f,QStringLiteral("起始序号"),"export-sequence-start",0,1000000000);suffix=line(f,QStringLiteral("后缀"),"export-suffix");collision=combo(f,QStringLiteral("同名文件"),"export-collision",{QStringLiteral("自动编号"),QStringLiteral("跳过")});
 f=section(QStringLiteral("元数据与完成操作"));metadata=combo(f,QStringLiteral("保留信息"),"export-metadata",{QStringLiteral("无拍摄/作者信息"),QStringLiteral("版权信息"),QStringLiteral("常用拍摄信息")});gps=check(f,QStringLiteral("保留GPS位置信息"),"export-gps");after=combo(f,QStringLiteral("完成后"),"export-after",{QStringLiteral("显示结果"),QStringLiteral("打开文件夹"),QStringLiteral("检查成片")});sections->addStretch();new Ui::ParameterWheelGuard(panel);
 diagnostic=new QLabel;diagnostic->setObjectName("export-diagnostic");diagnostic->setWordWrap(true);layout->addWidget(diagnostic);
 table=new QTableWidget(0,4);table->setObjectName("export-results");table->setHorizontalHeaderLabels({QStringLiteral("文件"),QStringLiteral("状态 / 阶段"),QStringLiteral("原因"),QStringLiteral("输出路径")});table->setEditTriggers(QAbstractItemView::NoEditTriggers);table->setSelectionBehavior(QAbstractItemView::SelectRows);table->setSelectionMode(QAbstractItemView::SingleSelection);table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);table->horizontalHeader()->setStretchLastSection(true);table->setMaximumHeight(150);layout->addWidget(table);connect(table,&QTableWidget::cellDoubleClicked,this,[this]{inspectSelected();});
 auto *footer=new QHBoxLayout;layout->addLayout(footer);counts=new QLabel;counts->setObjectName("export-counts");footer->addWidget(counts,1);auto button=[&](QString label,const char *id){auto *b=new QPushButton(label);b->setObjectName(id);footer->addWidget(b);return b;};cancel=button(QStringLiteral("取消任务"),"export-cancel");retry=button(QStringLiteral("重试未完成"),"export-retry");folder=button(QStringLiteral("文件夹"),"export-open-folder");inspect=button(QStringLiteral("检查成片"),"export-inspect");start=button(QStringLiteral("开始导出"),"export-start");start->setDefault(true);start->setStyleSheet(QStringLiteral("QPushButton#export-start{background:palette(highlight);color:palette(highlighted-text);border:1px solid palette(highlight);font-weight:600;} QPushButton#export-start:hover{border-color:palette(text);} QPushButton#export-start:pressed{border:2px solid palette(text);} QPushButton#export-start:focus{border:2px solid palette(text);} QPushButton#export-start:disabled{background:palette(button);color:palette(mid);border-color:palette(mid);}"));connect(start,&QPushButton::clicked,this,&ExportDialog::startExport);connect(cancel,&QPushButton::clicked,exportQueue,&Queue::cancel);connect(retry,&QPushButton::clicked,this,[this]{QString e;if(!exportQueue->retry(e)||!exportQueue->start(e))diagnostic->setText(e);});connect(folder,&QPushButton::clicked,this,&ExportDialog::openFolder);connect(inspect,&QPushButton::clicked,this,&ExportDialog::inspectSelected);
 debounce.setSingleShot(true);debounce.setInterval(250);connect(&debounce,&QTimer::timeout,this,&ExportDialog::previewNow);
 connect(&previewWatcher,&QFutureWatcher<Preview>::finished,this,[this]{auto r=previewWatcher.result();
  const bool current=r.sourceGeneration==sourceGeneration&&r.index>=0&&r.index<assets.size()&&assets[r.index].path==r.path&&assets[r.index].recipe==r.recipe&&sameDocument(assets[r.index].document,r.document);
  if(current&&!r.linear.isNull()){cachedIndex=r.index;cachedLinear=r.linear;cachedFull=r.full;assets[r.index].hash=r.hash;emit previewSourceCached(r.path,r.hash,r.sourceGeneration);}
  if(current&&r.generation==generation){if(r.image.isNull())canvas->setMessage(r.error);else canvas->setImage(r.image,QStringLiteral("export-preview"));}
  if(previewPending){previewPending=false;previewNow();}updateSummary();});
 connect(&preparationWatcher,&QFutureWatcher<Preparation>::finished,this,[this]{preparing=false;auto result=preparationWatcher.result();QString e=result.error;if(e.isEmpty()&&exportQueue->append(result.jobs,e)){persist(e);ownBatch=true;if(!exportQueue->start(e))ownBatch=false;}diagnostic->setText(e);updateResults();});
 connect(exportQueue,&Queue::changed,this,&ExportDialog::updateResults);connect(exportQueue,&Queue::finished,this,[this]{updateResults();if(ownBatch){ownBatch=false;if(state.after==1)openFolder();else if(state.after==2)inspectSelected();}});
 loadPresets();connect(preset,&QComboBox::activated,this,[this](int){auto n=preset->currentText();if(presets.contains(n))setSettings(presets[n]);});setSettings(state);updateResults();
}
ExportDialog::~ExportDialog(){previewCancel.cancel();debounce.stop();disconnect(&previewWatcher,nullptr,this,nullptr);disconnect(&preparationWatcher,nullptr,this,nullptr);pool.clear();pool.waitForDone();}
Settings ExportDialog::settings() const{
 auto s=state;s.format=Format(format->currentIndex());s.mode=Mode(mode->currentIndex());s.bits=bits->currentIndex()==1?16:8;s.quality=quality->value();s.zip=zip->isChecked();s.blackPoint=bpc->isChecked();s.intent=intent->currentIndex()==0?1:0;const int p=profile->currentData().toInt();if(p>=0&&p<profiles.size()){s.profileName=profiles[p].name;s.icc=profiles[p].icc;}else{s.profileName.clear();s.icc.clear();}s.sizeMode=SizeMode(sizeModeBox->currentIndex());s.width=widthValue->value();s.height=heightValue->value();s.percent=percent->value();s.ppi=ppi->value();s.dontEnlarge=dontEnlarge->isChecked();s.printCm=cm->isChecked();s.besideSource=beside->isChecked();s.subfolder=subfolder->text();s.directory=directory->text();s.sequence=sequence->isChecked();s.prefix=prefix->text();s.suffix=suffix->text();s.start=sequenceStart->value();s.collision=Collision(collision->currentIndex());s.metadata=Metadata(metadata->currentIndex());s.gps=gps->isChecked();s.after=after->currentIndex();return s;
}
void ExportDialog::setSettings(const Settings &s){state=s;if(state.icc.isEmpty()&&state.mode==Mode::Rgb){QString e;state.icc=builtinProfile(state.profileName,e);}bool found=false;for(const auto &p:profiles)found|=p.icc==state.icc;if(!found&&!state.icc.isEmpty())profiles.append({state.profileName,{},state.icc,state.mode});sync();changed();}
void ExportDialog::profilesChanged(){QSignalBlocker b(profile);profile->clear();profile->addItem(QStringLiteral("请选择目标配置"),-1);int selected=-1;for(int i=0;i<profiles.size();++i)if(profiles[i].mode==state.mode){profile->addItem(profiles[i].name,i);if(profiles[i].icc==state.icc)selected=profile->count()-1;}profile->setCurrentIndex(selected>=0?selected:0);}
void ExportDialog::sync(){syncing=true;format->setCurrentIndex(int(state.format));mode->setCurrentIndex(int(state.mode));bits->setCurrentIndex(state.bits==16);quality->setValue(state.quality);zip->setChecked(state.zip);bpc->setChecked(state.blackPoint);intent->setCurrentIndex(state.intent==0);profilesChanged();sizeModeBox->setCurrentIndex(int(state.sizeMode));widthValue->setValue(state.width);heightValue->setValue(state.height);percent->setValue(state.percent);ppi->setValue(state.ppi);cm->setChecked(state.printCm);dontEnlarge->setChecked(state.dontEnlarge);beside->setChecked(state.besideSource);subfolder->setText(state.subfolder);directory->setText(state.directory);sequence->setChecked(state.sequence);prefix->setText(state.prefix);suffix->setText(state.suffix);sequenceStart->setValue(state.start);collision->setCurrentIndex(int(state.collision));metadata->setCurrentIndex(int(state.metadata));gps->setChecked(state.gps);after->setCurrentIndex(state.after);{QSignalBlocker block(preset);preset->setCurrentIndex(0);for(auto i=presets.begin();i!=presets.end();++i)if(settingsJson(i.value())==settingsJson(state)){preset->setCurrentText(i.key());break;}}syncing=false;}
void ExportDialog::changed(){
 if(syncing||!diagnostic)return;auto next=settings();notice.clear();
 if(next.printCm!=state.printCm&&next.sizeMode==SizeMode::Print){const double factor=next.printCm?.1:10.;next.width*=factor;next.height*=factor;}
 if(next.mode!=state.mode){state.mode=next.mode;state.icc.clear();state.profileName.clear();if(next.mode==Mode::Rgb){state.profileName="sRGB";QString e;state.icc=builtinProfile("sRGB",e);}profilesChanged();next=settings();}
 if(next.mode==Mode::Cmyk&&next.format!=Format::Tiff){next.format=Format::Tiff;notice=QStringLiteral("CMYK使用TIFF，已调整文件格式。");}
 if(next.profileName=="ProPhoto RGB"&&(next.bits!=16||next.format==Format::Jpeg)){next.bits=16;if(next.format==Format::Jpeg)next.format=Format::Tiff;notice=QStringLiteral("ProPhoto RGB要求16位，已调整格式/位深。");}
 if(next.format==Format::Jpeg&&next.bits!=8){next.bits=8;notice=QStringLiteral("JPEG仅支持8位，已调整位深。");}
 state=next;sync();quality->setEnabled(state.format==Format::Jpeg);zip->setEnabled(state.format==Format::Tiff);directory->setEnabled(!state.besideSource);subfolder->setEnabled(state.besideSource);prefix->setEnabled(state.sequence);sequenceStart->setEnabled(state.sequence);const int n=int(state.sizeMode);widthValue->setEnabled(n==1||n==3||n==5);heightValue->setEnabled(n==2||n==3||n==5);percent->setEnabled(n==4);cm->setEnabled(n==5);if(previewWatcher.isRunning()&&activePreviewIndex!=representative())previewCancel.cancel();++generation;debounce.start();updateSummary();
}
QList<Asset> ExportDialog::targets() const{QList<Asset> result;for(int i=0;i<assets.size();++i)if((range->currentIndex()==0&&i==currentIndex)||(range->currentIndex()==1&&assets[i].selected)||(range->currentIndex()==2&&assets[i].kept))result.append(assets[i]);return result;}
int ExportDialog::representative() const{const auto list=targets();if(list.isEmpty())return -1;for(int i=0;i<assets.size();++i)if(assets[i].path==list.first().path)return i;return -1;}
void ExportDialog::updateSummary(){
 const auto list=targets();QString e=validate(state);if(list.isEmpty())e=QStringLiteral("此范围没有照片");QString dimensions=QStringLiteral("读取后显示尺寸");if(cachedIndex==representative()&&cachedFull.isValid()){QString err;auto sz=outputSize(cachedFull,state,err);dimensions=err.isEmpty()?QStringLiteral("%1 × %2 像素").arg(sz.width()).arg(sz.height()):err;}
 QString name;if(!list.isEmpty())name=(state.sequence?state.prefix+QString::number(state.start):QFileInfo(list.first().path).completeBaseName())+state.suffix+"."+extension(state.format);
 summary->setText(QStringLiteral("%1张照片\n%2 · %3位 · %4\n%5\n文件示例：%6\n%7").arg(list.size()).arg(extension(state.format).toUpper()).arg(state.bits).arg(state.profileName).arg(dimensions,name,state.besideSource?QStringLiteral("各原图目录 / ")+state.subfolder:state.directory));
 diagnostic->setText(e.isEmpty()?notice:e);start->setEnabled(e.isEmpty()&&!preparing&&!exportQueue->busy());previewLabel->setText(QStringLiteral("目标色彩预览 · %1%2").arg(state.profileName,(previewWatcher.isRunning()||debounce.isActive())?QStringLiteral(" · 更新中"):QString()));
}
void ExportDialog::previewNow(){
 if(previewWatcher.isRunning()){previewPending=true;return;}const int i=representative();if(i<0){canvas->setMessage(QStringLiteral("没有可预览照片"));return;}QString e=validate(state);if(!e.isEmpty()){canvas->setMessage(e);return;}
 const auto a=assets[i];const auto s=state;const auto gen=generation,sourceGen=sourceGeneration;const auto cached=cachedIndex==i?cachedLinear:QImage();const auto full=cachedFull;if(cached.isNull())canvas->setMessage(QStringLiteral("正在准备目标色彩预览…"));canvas->setToolTip(QStringLiteral("目标ICC转换预览；完整质量以导出成片为准"));
 previewCancel=CancellationSource();const auto token=previewCancel.token();activePreviewIndex=i;
 const auto reader=previewReader;
 previewWatcher.setFuture(QtConcurrent::run(&pool,[a,s,gen,sourceGen,i,cached,full,token,reader]{
  Preview r;r.index=i;r.generation=gen;r.sourceGeneration=sourceGen;r.path=a.path;r.recipe=a.recipe;r.document=a.document;r.linear=cached;r.full=full;r.hash=a.hash;
  if(isCanceled(token)){r.error=QStringLiteral("已取消");return r;}
  if(r.hash.isEmpty()&&a.document)r.hash=a.document->asset.sourceHash;
  Job binding;binding.source=QFileInfo(a.path).absoluteFilePath();binding.hash=r.hash;binding.recipe=a.recipe;binding.revision=a.revision;binding.document=a.document;
  if(!validateJobDocument(binding,r.error))return r;
  if(r.linear.isNull()){auto source=reader(a.path,a.recipe.warmth,a.recipe.tint,token,r.hash);if(!source.source){r.error=source.error;return r;}r.hash=source.source->sha256;r.full=renderedSize(source.source->originalSize,a.recipe);
   r.linear=a.document?LocalEdit::renderDocumentPreviewLinear(*source.source,*a.document,a.recipe,3840,r.error,token):renderLinear(*source.source,a.recipe,3840,r.error,{},token);}
  if(!r.linear.isNull()&&!isCanceled(token))r.image=Export::preview(r.linear,s,r.error,token.get());return r;
 }));
}
void ExportDialog::startExport(){
 if(preparing||exportQueue->busy())return;emit refreshRequested();state=settings();const auto list=targets();QString e=validate(state);if(list.isEmpty())e=QStringLiteral("没有可导出的照片");if(!e.isEmpty()){diagnostic->setText(e);return;}preparing=true;start->setEnabled(false);diagnostic->setText(QStringLiteral("冻结照片与输出设置…"));const auto s=state;
 preparationWatcher.setFuture(QtConcurrent::run(&pool,[list,s]{Preparation r;auto input=list;for(auto &a:input)if(a.hash.isEmpty()){QFile f(a.path);QCryptographicHash h(QCryptographicHash::Sha256);if(!f.open(QIODevice::ReadOnly)||!h.addData(&f)){r.error=QStringLiteral("无法读取源文件哈希");return r;}a.hash=QString::fromLatin1(h.result().toHex());}r.jobs=freeze(input,s,r.error);return r;}));
}
void ExportDialog::replaceAssets(QList<Asset> input,int index){
 const int old=representative();const auto oldAsset=old>=0?assets[old]:Asset{};
 previewCancel.cancel();++sourceGeneration;++generation;
 assets=std::move(input);currentIndex=index;const int now=representative();
 if(now<0||assets[now].path!=oldAsset.path||assets[now].recipe!=oldAsset.recipe||assets[now].hash!=oldAsset.hash||!sameDocument(assets[now].document,oldAsset.document)){cachedIndex=-1;cachedLinear={};cachedFull={};}else cachedIndex=cachedLinear.isNull()?-1:now;
 debounce.start();
 updateSummary();
}
void ExportDialog::updateResults(){
 const int selected=table->currentRow();const auto &jobs=exportQueue->jobs();table->setRowCount(jobs.size());int success=0,failed=0,cancelled=0,skipped=0;for(int i=0;i<jobs.size();++i){const auto &j=jobs[i];success+=j.status==Status::Succeeded;failed+=j.status==Status::Failed;cancelled+=j.status==Status::Cancelled;skipped+=j.status==Status::Skipped;const QStringList values{QFileInfo(j.source).fileName(),j.stage.isEmpty()?statusText(j.status):j.stage,j.error,j.path};for(int c=0;c<4;++c){auto *item=new QTableWidgetItem(values[c]);item->setToolTip(values[c]);table->setItem(i,c,item);}}
 if(selected>=0&&selected<jobs.size())table->selectRow(selected);counts->setText(QStringLiteral("成功%1 失败%2 取消%3 跳过%4").arg(success).arg(failed).arg(cancelled).arg(skipped));table->setVisible(!jobs.isEmpty());cancel->setEnabled(exportQueue->busy());retry->setEnabled(!exportQueue->busy()&&!preparing&&!jobs.isEmpty());inspect->setEnabled(success>0);folder->setEnabled(success>0);updateSummary();if(!exportQueue->error().isEmpty())diagnostic->setText(exportQueue->error());
}
void ExportDialog::inspectSelected(){const auto &jobs=exportQueue->jobs();int row=table->currentRow();if(row<0||row>=jobs.size()||jobs[row].status!=Status::Succeeded){row=-1;for(int i=jobs.size()-1;i>=0;--i)if(jobs[i].status==Status::Succeeded){row=i;break;}}if(row>=0)emit requestInspect(jobs[row].path,jobs[row].source);}
void ExportDialog::openFolder(){const auto &jobs=exportQueue->jobs();int row=table->currentRow();if(row<0||row>=jobs.size()||jobs[row].status!=Status::Succeeded){row=-1;for(int i=jobs.size()-1;i>=0;--i)if(jobs[i].status==Status::Succeeded){row=i;break;}}if(row>=0)QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(jobs[row].path).absolutePath()));}
QString ExportDialog::previewNote()const{return previewLabel->text();}
void ExportDialog::reject(){if(preparing){diagnostic->setText(QStringLiteral("正在冻结任务，请稍候；入队后可关闭窗口，后台继续导出。"));return;}QDialog::reject();}
void ExportDialog::closeEvent(QCloseEvent *event){if(preparing){diagnostic->setText(QStringLiteral("正在冻结任务，请稍候；入队后可关闭窗口。"));event->ignore();return;}QDialog::closeEvent(event);}
void ExportDialog::resizeEvent(QResizeEvent *e){QDialog::resizeEvent(e);if(left)left->setVisible(width()>=850);}
void ExportDialog::loadPresets(){
 Settings daily;QString e;daily.icc=builtinProfile("sRGB",e);presets[QStringLiteral("日常交付")]=daily;auto web=daily;web.sizeMode=SizeMode::LongEdge;web.width=3840;presets[QStringLiteral("4K分享")]=web;auto pro=daily;pro.format=Format::Tiff;pro.bits=16;pro.profileName="ProPhoto RGB";pro.icc=builtinProfile(pro.profileName,e);presets[QStringLiteral("16位精修")]=pro;
 QFile f(dataDir()+"/settings.json");if(f.open(QIODevice::ReadOnly)&&f.size()<=16*1024*1024){const auto root=QJsonDocument::fromJson(f.readAll()).object();if(root.value("version").toInt()==1){Settings saved;if(settingsFromJson(root.value("last").toObject(),saved,e))state=saved;for(const auto &v:root.value("presets").toArray()){auto o=v.toObject();const auto name=o.value("name").toString();Settings s;if(!name.isEmpty()&&!presets.contains(name)&&settingsFromJson(o.value("settings").toObject(),s,e))presets[name]=s;}}}
 preset->clear();preset->addItem(QStringLiteral("当前设置"));preset->addItems(presets.keys());
}
bool ExportDialog::persist(QString &error){QDir().mkpath(dataDir());QJsonArray array;for(auto i=presets.begin();i!=presets.end();++i)if(i.key()!=QStringLiteral("日常交付")&&i.key()!=QStringLiteral("4K分享")&&i.key()!=QStringLiteral("16位精修"))array.append(QJsonObject{{"name",i.key()},{"settings",settingsJson(i.value())}});const auto bytes=QJsonDocument(QJsonObject{{"version",1},{"last",settingsJson(state)},{"presets",array}}).toJson();QSaveFile f(dataDir()+"/settings.json");if(bytes.size()>16*1024*1024||!f.open(QIODevice::WriteOnly)||f.write(bytes)!=bytes.size()||!f.commit()){error=QStringLiteral("导出预设保存失败：")+f.errorString();return false;}return true;}
void ExportDialog::savePreset(){bool ok=false;auto name=QInputDialog::getText(this,QStringLiteral("保存导出预设"),QStringLiteral("名称"),QLineEdit::Normal,{},&ok).trimmed();if(!ok)return;if(name.isEmpty()||name==QStringLiteral("当前设置")||presets.contains(name)){diagnostic->setText(QStringLiteral("名称为空或已存在"));return;}state=settings();QString e=validate(state);if(!e.isEmpty()){diagnostic->setText(e);return;}presets[name]=state;if(!persist(e)){presets.remove(name);diagnostic->setText(e);return;}preset->addItem(name);preset->setCurrentText(name);}
void ExportDialog::renamePreset(){const auto old=preset->currentText();if(!presets.contains(old)){diagnostic->setText(QStringLiteral("请选择已保存的预设"));return;}if(old==QStringLiteral("日常交付")||old==QStringLiteral("4K分享")||old==QStringLiteral("16位精修")){diagnostic->setText(QStringLiteral("内置预设不能重命名"));return;}bool ok=false;auto name=QInputDialog::getText(this,QStringLiteral("重命名"),QStringLiteral("新名称"),QLineEdit::Normal,old,&ok).trimmed();if(!ok)return;if(name.isEmpty()||name==QStringLiteral("当前设置")||presets.contains(name)){diagnostic->setText(QStringLiteral("名称为空或已存在"));return;}auto value=presets.take(old);presets[name]=value;QString e;if(!persist(e)){presets.remove(name);presets[old]=value;diagnostic->setText(e);return;}preset->setItemText(preset->currentIndex(),name);}
void ExportDialog::deletePreset(){const auto name=preset->currentText();if(!presets.contains(name)){diagnostic->setText(QStringLiteral("请选择已保存的预设"));return;}if(name==QStringLiteral("日常交付")||name==QStringLiteral("4K分享")||name==QStringLiteral("16位精修")){diagnostic->setText(QStringLiteral("内置预设不能删除"));return;}auto previous=presets.take(name);QString e;if(!persist(e)){presets[name]=previous;diagnostic->setText(e);return;}preset->removeItem(preset->currentIndex());}
