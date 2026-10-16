#include "app/local_adjustment_panel.h"
#include <QCheckBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFont>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>

namespace {
// The compact block shows only exposure and saturation. Contrast and the five
// remaining tonal/color fields travel verbatim inside each emitted layer copy.
constexpr double kExposureMinimum=-5,kExposureMaximum=5;
constexpr double kSaturationMinimum=0,kSaturationMaximum=2;
constexpr double kRadiusMinimum=1,kRadiusMaximum=10000;
constexpr int kSliderSteps=100; // hundredths of the unit field
constexpr int kListMaximumHeight=170;

double clampValue(double value,double minimum,double maximum){
    return value<minimum?minimum:(value>maximum?maximum:value);
}
int stepsOf(double value){return static_cast<int>(std::lround(value*kSliderSteps));}
int unitSteps(double value){return static_cast<int>(std::lround(value*100));}

// Optional read-only reset affordance for the two compact fields.
QPushButton *resetButton(QWidget *parent,int field,const QString &label){
    QPushButton *button=new QPushButton(Ui::icon(Ui::Glyph::Reset),QString(),parent);
    button->setIconSize({14,14});
    button->setFixedWidth(25);
    button->setObjectName(QStringLiteral("local-reset-%1").arg(field));
    button->setToolTip(QCoreApplication::translate("LocalAdjustmentPanel","将此字段恢复为默认值"));
    button->setAccessibleName(QCoreApplication::translate("LocalAdjustmentPanel","重置%1").arg(label));
    return button;
}
}

LocalAdjustmentPanel::LocalAdjustmentPanel(QWidget *parent):QWidget(parent){
    buildUi();
    refreshList();
    refreshControls();
    syncBrushControls();
}

void LocalAdjustmentPanel::buildUi(){
    // Compact vertical inspector block: no cards, no local colors or fonts.
    QVBoxLayout *layout=new QVBoxLayout(this);
    layout->setContentsMargins(8,8,8,8);
    layout->setSpacing(8);

    m_list=new QListWidget(this);
    m_list->setObjectName(QStringLiteral("local-layer-list"));
    m_list->setMinimumWidth(0);
    m_list->setMaximumHeight(kListMaximumHeight);
    m_list->setToolTip(tr("局部层列表"));
    m_list->setAccessibleName(tr("局部层列表"));
    layout->addWidget(m_list);

    m_new=new QPushButton(tr("新建笔刷层"),this);
    m_new->setObjectName(QStringLiteral("local-new-brush"));
    m_new->setCheckable(false);
    m_new->setToolTip(tr("新建一个笔刷局部层"));
    m_new->setAccessibleName(tr("新建笔刷层"));
    layout->addWidget(m_new);

    QHBoxLayout *tools=new QHBoxLayout;
    tools->setContentsMargins(0,0,0,0);
    tools->setSpacing(8);
    // Two independent toggles: erase only flips the subtract flag of the same brush.
    m_brushButton=new QPushButton(tr("笔刷"),this);
    m_brushButton->setObjectName(QStringLiteral("local-brush-tool"));
    m_brushButton->setCheckable(true);
    m_brushButton->setToolTip(tr("在照片上涂抹以绘制蒙版"));
    m_brushButton->setAccessibleName(tr("笔刷"));
    m_erase=new QPushButton(tr("擦除"),this);
    m_erase->setObjectName(QStringLiteral("local-erase"));
    m_erase->setCheckable(true);
    m_erase->setToolTip(tr("涂抹以擦除蒙版"));
    m_erase->setAccessibleName(tr("擦除"));
    tools->addWidget(m_brushButton);
    tools->addWidget(m_erase);
    tools->addStretch(1);
    layout->addLayout(tools);

    m_overlay=new QCheckBox(tr("显示蒙版"),this);
    m_overlay->setObjectName(QStringLiteral("local-overlay"));
    m_overlay->setChecked(false);
    m_overlay->setToolTip(tr("在照片上叠加显示蒙版范围"));
    m_overlay->setAccessibleName(tr("显示蒙版"));
    layout->addWidget(m_overlay);

    m_visible=new QCheckBox(tr("显示此层"),this);
    m_visible->setObjectName(QStringLiteral("local-layer-visible"));
    m_visible->setChecked(true);
    m_visible->setToolTip(tr("显示或隐藏当前局部层"));
    m_visible->setAccessibleName(tr("显示此层"));
    layout->addWidget(m_visible);

    m_parameters=new QWidget(this);
    QFormLayout *form=new QFormLayout(m_parameters);
    form->setContentsMargins(0,0,0,0);
    form->setSpacing(4);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    {
        QHBoxLayout *box=new QHBoxLayout;
        box->setContentsMargins(0,0,0,0);
        box->setSpacing(4);
        QDoubleSpinBox *spin=new QDoubleSpinBox(m_parameters);
        spin->setObjectName(QStringLiteral("local-exposure"));
        spin->setRange(kExposureMinimum,kExposureMaximum);
        spin->setSingleStep(.05);
        spin->setDecimals(2);
        spin->setValue(0);
        spin->setToolTip(tr("局部曝光，单位为曝光值"));
        spin->setAccessibleName(tr("局部曝光"));
        QSlider *slider=new QSlider(Qt::Horizontal,m_parameters);
        slider->setObjectName(QStringLiteral("slider-local-exposure"));
        slider->setRange(stepsOf(kExposureMinimum),stepsOf(kExposureMaximum));
        slider->setValue(0);
        slider->setToolTip(tr("局部曝光滑块"));
        slider->setAccessibleName(tr("局部曝光滑块"));
        QPushButton *reset=resetButton(m_parameters,Exposure,tr("局部曝光"));
        box->addWidget(slider,1);
        box->addWidget(spin);
        box->addWidget(reset);
        form->addRow(tr("曝光"),box);
        m_rows[Exposure]={spin,slider,reset};
    }
    {
        QHBoxLayout *box=new QHBoxLayout;
        box->setContentsMargins(0,0,0,0);
        box->setSpacing(4);
        QDoubleSpinBox *spin=new QDoubleSpinBox(m_parameters);
        spin->setObjectName(QStringLiteral("local-saturation"));
        spin->setRange(kSaturationMinimum,kSaturationMaximum);
        spin->setSingleStep(.01);
        spin->setDecimals(2);
        spin->setValue(1);
        spin->setToolTip(tr("局部饱和度"));
        spin->setAccessibleName(tr("局部饱和度"));
        QSlider *slider=new QSlider(Qt::Horizontal,m_parameters);
        slider->setObjectName(QStringLiteral("slider-local-saturation"));
        slider->setRange(stepsOf(kSaturationMinimum),stepsOf(kSaturationMaximum));
        slider->setValue(kSliderSteps);
        slider->setToolTip(tr("局部饱和度滑块"));
        slider->setAccessibleName(tr("局部饱和度滑块"));
        QPushButton *reset=resetButton(m_parameters,Saturation,tr("局部饱和度"));
        box->addWidget(slider,1);
        box->addWidget(spin);
        box->addWidget(reset);
        form->addRow(tr("饱和度"),box);
        m_rows[Saturation]={spin,slider,reset};
    }
    layout->addWidget(m_parameters);

    m_brushBox=new QWidget(this);
    QFormLayout *brushForm=new QFormLayout(m_brushBox);
    brushForm->setContentsMargins(0,0,0,0);
    brushForm->setSpacing(4);
    brushForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    {
        QHBoxLayout *box=new QHBoxLayout;
        box->setContentsMargins(0,0,0,0);
        box->setSpacing(4);
        QDoubleSpinBox *spin=new QDoubleSpinBox(m_brushBox);
        spin->setObjectName(QStringLiteral("local-radius"));
        spin->setRange(kRadiusMinimum,kRadiusMaximum);
        spin->setSingleStep(1);
        spin->setDecimals(0);
        spin->setSuffix(tr(" 像素"));
        spin->setValue(m_brush.radius);
        spin->setToolTip(tr("笔刷大小，单位为像素"));
        spin->setAccessibleName(tr("笔刷大小"));
        QSlider *slider=new QSlider(Qt::Horizontal,m_brushBox);
        slider->setObjectName(QStringLiteral("slider-local-radius"));
        slider->setRange(static_cast<int>(kRadiusMinimum),static_cast<int>(kRadiusMaximum));
        slider->setValue(static_cast<int>(m_brush.radius));
        slider->setToolTip(tr("笔刷大小滑块"));
        slider->setAccessibleName(tr("笔刷大小滑块"));
        box->addWidget(slider,1);
        box->addWidget(spin);
        brushForm->addRow(tr("大小"),box);
        m_radiusSpin=spin;
        m_radiusSlider=slider;
    }
    {
        QHBoxLayout *box=new QHBoxLayout;
        box->setContentsMargins(0,0,0,0);
        box->setSpacing(4);
        QDoubleSpinBox *spin=new QDoubleSpinBox(m_brushBox);
        spin->setObjectName(QStringLiteral("local-hardness"));
        spin->setRange(0,1);
        spin->setSingleStep(.01);
        spin->setDecimals(2);
        spin->setValue(m_brush.hardness);
        spin->setToolTip(tr("笔刷硬度"));
        spin->setAccessibleName(tr("笔刷硬度"));
        QSlider *slider=new QSlider(Qt::Horizontal,m_brushBox);
        slider->setObjectName(QStringLiteral("slider-local-hardness"));
        slider->setRange(0,100);
        slider->setValue(unitSteps(m_brush.hardness));
        slider->setToolTip(tr("笔刷硬度滑块"));
        slider->setAccessibleName(tr("笔刷硬度滑块"));
        box->addWidget(slider,1);
        box->addWidget(spin);
        brushForm->addRow(tr("硬度"),box);
        m_hardnessSpin=spin;
        m_hardnessSlider=slider;
    }
    {
        QHBoxLayout *box=new QHBoxLayout;
        box->setContentsMargins(0,0,0,0);
        box->setSpacing(4);
        QDoubleSpinBox *spin=new QDoubleSpinBox(m_brushBox);
        spin->setObjectName(QStringLiteral("local-strength"));
        spin->setRange(0,1);
        spin->setSingleStep(.01);
        spin->setDecimals(2);
        spin->setValue(m_brush.strength);
        spin->setToolTip(tr("笔刷强度"));
        spin->setAccessibleName(tr("笔刷强度"));
        QSlider *slider=new QSlider(Qt::Horizontal,m_brushBox);
        slider->setObjectName(QStringLiteral("slider-local-strength"));
        slider->setRange(0,100);
        slider->setValue(unitSteps(m_brush.strength));
        slider->setToolTip(tr("笔刷强度滑块"));
        slider->setAccessibleName(tr("笔刷强度滑块"));
        box->addWidget(slider,1);
        box->addWidget(spin);
        brushForm->addRow(tr("强度"),box);
        m_strengthSpin=spin;
        m_strengthSlider=slider;
    }
    layout->addWidget(m_brushBox);

    m_hint=new QLabel(this);
    m_hint->setObjectName(QStringLiteral("local-hint"));
    m_hint->setWordWrap(true);
    layout->addWidget(m_hint);

    QLabel *footer=new QLabel(tr("Alt 临时擦除 · Space 平移 · [ ] 大小 · O 覆盖"),this);
    footer->setObjectName(QStringLiteral("local-shortcuts"));
    footer->setWordWrap(true);
    footer->setEnabled(false); // the existing palette already renders muted text
    footer->setToolTip(tr("键盘快捷键"));
    footer->setAccessibleName(tr("快捷键"));
    layout->addWidget(footer);

    m_hintEmpty=tr("新建局部层后涂抹；全图基础固定在底部");
    m_hintWaiting=tr("等待照片预览完成");

    connect(m_list,&QListWidget::currentRowChanged,this,&LocalAdjustmentPanel::handleSelectionChanged);
    connect(m_new,&QPushButton::clicked,this,&LocalAdjustmentPanel::handleCreate);
    connect(m_brushButton,&QPushButton::toggled,this,&LocalAdjustmentPanel::handleBrushToggled);
    connect(m_erase,&QPushButton::toggled,this,&LocalAdjustmentPanel::handleEraseToggled);
    connect(m_overlay,&QCheckBox::toggled,this,&LocalAdjustmentPanel::handleOverlayToggled);
    connect(m_visible,&QCheckBox::toggled,this,&LocalAdjustmentPanel::handleVisibleToggled);
    for(int i=0;i<FieldCount;++i){
        Row &r=m_rows[i];
        const Field field=static_cast<Field>(i);
        // Lambdas capture only 'this' plus the value field; the widgets live
        // exactly as long as the panel does.
        connect(r.spin,&QDoubleSpinBox::valueChanged,this,[this,field](double v){handleSpinChanged(field,v);});
        connect(r.slider,&QSlider::valueChanged,this,[this,field](int v){handleSliderChanged(field,v);});
        connect(r.slider,&QSlider::sliderReleased,this,[this,field](){handleSliderReleased(field);});
        connect(r.slider,&QSlider::actionTriggered,this,[this,field](int){
            if(!m_restoring&&!m_rows[field].slider->isSliderDown())handleKeyboardCommit(field);
        });
        connect(r.spin,&QDoubleSpinBox::editingFinished,this,[this,field](){handleSpinFinished(field);});
        connect(r.reset,&QPushButton::clicked,this,[this,field](){handleReset(field);});
    }
    // Arrow-key nudges inside a spin box change the value without an
    // editingFinished; those commits are reported once the key is processed.
    for(int i=0;i<FieldCount;++i)m_rows[i].spin->installEventFilter(this);
    connect(m_radiusSpin,&QDoubleSpinBox::valueChanged,this,&LocalAdjustmentPanel::handleRadiusChanged);
    connect(m_radiusSlider,&QSlider::valueChanged,this,[this](int v){handleRadiusChanged(static_cast<double>(v));});
    connect(m_hardnessSpin,&QDoubleSpinBox::valueChanged,this,&LocalAdjustmentPanel::handleHardnessChanged);
    connect(m_hardnessSlider,&QSlider::valueChanged,this,[this](int v){handleHardnessChanged(v/100.0);});
    connect(m_strengthSpin,&QDoubleSpinBox::valueChanged,this,&LocalAdjustmentPanel::handleStrengthChanged);
    connect(m_strengthSlider,&QSlider::valueChanged,this,[this](int v){handleStrengthChanged(v/100.0);});
}

void LocalAdjustmentPanel::setDocument(LocalEdit::DocumentSnapshot snapshot,const QString &selectedId){
    m_restoring=true; // programmatic refresh: no selection or parameter signal
    m_layers.clear();
    if(snapshot)for(const LocalEdit::AdjustmentLayer &layer:snapshot->layers){
        Layer copy;
        copy.id=layer.id;
        copy.name=layer.name;
        copy.enabled=layer.enabled;
        copy.parameters=layer.parameters; // all field values are kept verbatim
        m_layers.append(copy);
    }
    const QString requested=selectedId.isEmpty()?m_selectedId:selectedId;
    m_selectedId=QString();
    for(const Layer &layer:m_layers)if(layer.id==requested){m_selectedId=layer.id;break;}
    if(m_selectedId.isEmpty()&&!m_layers.isEmpty())m_selectedId=m_layers.first().id;
    refreshList();
    refreshControls();
    syncBrushControls();
    m_restoring=false;
}

int LocalAdjustmentPanel::selectedRow() const {
    for(int i=0;i<m_layers.size();++i)if(m_layers.at(i).id==m_selectedId)return i;
    return -1;
}

LocalAdjustmentPanel::Layer *LocalAdjustmentPanel::selectedLayer(){
    const int row=selectedRow();
    return row<0?nullptr:&m_layers[row];
}

const LocalAdjustmentPanel::Layer *LocalAdjustmentPanel::selectedLayer() const {
    const int row=selectedRow();
    return row<0?nullptr:&m_layers.at(row);
}

bool LocalAdjustmentPanel::hasSelection() const { return selectedRow()>=0; }

void LocalAdjustmentPanel::refreshList(){
    QSignalBlocker block(m_list);
    m_list->clear();
    for(const Layer &layer:m_layers){
        QListWidgetItem *item=new QListWidgetItem(layer.name.isEmpty()?layer.id:layer.name,m_list);
        item->setData(Qt::UserRole,layer.id); // rows always carry the stable layer id
    }
    const int row=selectedRow();
    if(row>=0)m_list->setCurrentRow(row); // no selection signal: not a user gesture
    else m_list->setCurrentRow(-1);
}

void LocalAdjustmentPanel::refreshControls(){
    const bool selected=hasSelection();
    // Every tool and parameter control needs a prepared preview, so availability
    // gates all of them; the visibility checkbox and the compact parameters
    // additionally need a selected layer. Availability never mutates the stored
    // layers, and an empty new layer enables the overlay later through the host.
    const bool ready=m_available&&selected;
    m_new->setEnabled(m_available);
    m_visible->setEnabled(ready);
    m_parameters->setEnabled(ready);
    m_brushBox->setEnabled(ready);
    m_brushButton->setEnabled(ready);
    m_erase->setEnabled(ready);
    m_overlay->setEnabled(ready);
    for(int i=0;i<FieldCount;++i)syncRow(static_cast<Field>(i),storedField(static_cast<Field>(i)));
    m_hint->setText(!m_available?m_hintWaiting:(selected?QString():m_hintEmpty));
    m_hint->setVisible(!m_available||!selected);
    syncVisibility();
}

void LocalAdjustmentPanel::syncVisibility(){
    const Layer *layer=selectedLayer();
    QSignalBlocker block(m_visible);
    m_visible->setChecked(layer!=nullptr&&layer->enabled);
}

void LocalAdjustmentPanel::syncRow(Field field,double value){
    Row &r=m_rows[field];
    QSignalBlocker spinBlock(r.spin);
    QSignalBlocker sliderBlock(r.slider);
    if(r.spin->value()!=value)r.spin->setValue(value);
    const int tick=stepsOf(value);
    if(r.slider->value()!=tick)r.slider->setValue(tick);
}

void LocalAdjustmentPanel::syncBrushControls(){
    {
        QSignalBlocker spinBlock(m_radiusSpin);
        QSignalBlocker sliderBlock(m_radiusSlider);
        if(m_radiusSpin->value()!=m_brush.radius)m_radiusSpin->setValue(m_brush.radius);
        const int tick=static_cast<int>(std::lround(m_brush.radius));
        if(m_radiusSlider->value()!=tick)m_radiusSlider->setValue(tick);
    }
    {
        QSignalBlocker spinBlock(m_hardnessSpin);
        QSignalBlocker sliderBlock(m_hardnessSlider);
        if(m_hardnessSpin->value()!=m_brush.hardness)m_hardnessSpin->setValue(m_brush.hardness);
        const int tick=unitSteps(m_brush.hardness);
        if(m_hardnessSlider->value()!=tick)m_hardnessSlider->setValue(tick);
    }
    {
        QSignalBlocker spinBlock(m_strengthSpin);
        QSignalBlocker sliderBlock(m_strengthSlider);
        if(m_strengthSpin->value()!=m_brush.strength)m_strengthSpin->setValue(m_brush.strength);
        const int tick=unitSteps(m_brush.strength);
        if(m_strengthSlider->value()!=tick)m_strengthSlider->setValue(tick);
    }
    QSignalBlocker brushBlock(m_brushButton);
    QSignalBlocker eraseBlock(m_erase);
    m_brushButton->setChecked(m_brushActive);
    m_erase->setChecked(m_brush.subtract); // one brush with a subtract flag
}

void LocalAdjustmentPanel::setAvailable(bool available){
    if(m_available==available)return;
    m_available=available;
    refreshControls();
}

void LocalAdjustmentPanel::setBrushActive(bool active){
    if(m_brushActive==active)return;
    m_brushActive=active;
    QSignalBlocker block(m_brushButton);
    m_brushButton->setChecked(active);
    if(!active){ // the erase flag belongs to an active brush only
        QSignalBlocker eraseBlock(m_erase);
        m_erase->setChecked(false);
        m_brush.subtract=false;
    }
}

bool LocalAdjustmentPanel::brushActive() const { return m_brushActive; }

QString LocalAdjustmentPanel::selectedLayerId() const { return m_selectedId; }

LocalEdit::BrushSettings LocalAdjustmentPanel::brushSettings() const { return m_brush; }

void LocalAdjustmentPanel::setBrushRadius(double radius){
    if(!std::isfinite(radius))return;
    const double clamped=clampValue(radius,kRadiusMinimum,kRadiusMaximum);
    if(m_brush.radius==clamped)return;
    m_brush.radius=clamped;
    syncBrushControls(); // signal-blocked: a host refresh is not a user gesture
}

bool LocalAdjustmentPanel::overlayVisible() const { return m_overlay->isChecked(); }

void LocalAdjustmentPanel::setOverlayVisible(bool visible){
    if(m_overlay->isChecked()==visible)return;
    QSignalBlocker block(m_overlay);
    m_overlay->setChecked(visible);
}

double LocalAdjustmentPanel::storedField(Field field) const {
    const Layer *layer=selectedLayer();
    if(!layer)return field==Saturation?1:0;
    return field==Exposure?layer->parameters.exposure:layer->parameters.saturation;
}

void LocalAdjustmentPanel::storeField(Field field,double value){
    if(m_restoring||!std::isfinite(value))return;
    Layer *layer=selectedLayer();
    if(!layer)return;
    double *target=field==Exposure?&layer->parameters.exposure:&layer->parameters.saturation;
    if(*target==value)return;
    *target=value; // exactly this field; the other seven values stay untouched
    // A direct receiver can refresh the panel and replace m_layers. Keep signal
    // arguments alive until every receiver (including observers) has returned.
    const auto id=layer->id;const auto parameters=layer->parameters;
    emit parametersEdited(id,parameters);
}

void LocalAdjustmentPanel::handleSelectionChanged(){
    if(m_restoring)return;
    const QListWidgetItem *item=m_list->currentItem();
    const QString id=item?item->data(Qt::UserRole).toString():QString();
    if(id.isEmpty()||id==m_selectedId)return;
    m_selectedId=id;
    refreshControls();
    const auto selected=m_selectedId;emit selectedLayerChanged(selected);
}

void LocalAdjustmentPanel::handleCreate(){
    if(m_restoring||!m_available)return;
    emit createBrushLayerRequested(); // no local placeholder row is fabricated
}

void LocalAdjustmentPanel::handleVisibleToggled(bool checked){
    if(m_restoring)return;
    Layer *layer=selectedLayer();
    if(!layer||layer->enabled==checked)return;
    layer->enabled=checked;
    const auto id=layer->id;emit layerVisibilityChanged(id,checked);
}

void LocalAdjustmentPanel::handleSpinChanged(Field field,double value){
    if(m_restoring)return;
    storeField(field,value);
    syncRow(field,storedField(field)); // only this row follows the spin
}

void LocalAdjustmentPanel::handleSliderChanged(Field field,int value){
    if(m_restoring)return;
    const double step=1.0/kSliderSteps; // slider units are hundredths of the field unit
    storeField(field,value*step);
    syncRow(field,storedField(field));
}

void LocalAdjustmentPanel::handleSliderReleased(Field){
    if(m_restoring)return;
    emit parametersFinished();
}

void LocalAdjustmentPanel::handleSpinFinished(Field field){
    if(m_restoring)return;
    if(m_rows[field].slider->isSliderDown())return;
    emit parametersFinished();
}

// Keyboard nudges commit through the same finished path; the queued call keeps
// it from firing while the key is still being processed.
void LocalAdjustmentPanel::handleKeyboardCommit(Field){
    if(m_restoring)return;
    QTimer::singleShot(0,this,[this](){
        if(m_restoring)return;
        emit parametersFinished();
    });
}

// A focused spin box handles arrow keys itself, so its value changes without an
// editingFinished. Only the up/down/left/right keys reach this path.
bool LocalAdjustmentPanel::eventFilter(QObject *target,QEvent *event){
    if(event->type()!=QEvent::KeyPress)return QWidget::eventFilter(target,event);
    const int key=static_cast<QKeyEvent *>(event)->key();
    if(key!=Qt::Key_Up&&key!=Qt::Key_Down&&key!=Qt::Key_Left&&key!=Qt::Key_Right)
        return QWidget::eventFilter(target,event);
    for(int i=0;i<FieldCount;++i)if(target==m_rows[i].spin){
        handleKeyboardCommit(static_cast<Field>(i));
        break;
    }
    return QWidget::eventFilter(target,event);
}

void LocalAdjustmentPanel::handleReset(Field field){
    if(m_restoring||!hasSelection())return;
    storeField(field,field==Saturation?1:0); // documented default for that field only
    syncRow(field,storedField(field));
    emit parametersFinished();
}

void LocalAdjustmentPanel::handleBrushToggled(bool checked){
    if(m_restoring)return;
    if(m_brushActive==checked)return;
    m_brushActive=checked;
    if(!checked){
        QSignalBlocker eraseBlock(m_erase);
        m_erase->setChecked(false);
        m_brush.subtract=false;
    }
    emit brushToolToggled(checked);
}

void LocalAdjustmentPanel::handleEraseToggled(bool checked){
    if(m_restoring)return;
    if(checked&&!m_brushActive){
        setBrushActive(true);
        emit brushToolToggled(true);
    }
    if(m_brush.subtract==checked)return;
    m_brush.subtract=checked; // the same tool with its subtract flag, not a new tool
    emit brushSettingsChanged(m_brush);
}

void LocalAdjustmentPanel::handleOverlayToggled(bool checked){
    if(m_restoring)return;
    emit overlayToggled(checked);
}

void LocalAdjustmentPanel::handleRadiusChanged(double radius){
    if(m_restoring||!std::isfinite(radius))return;
    const double clamped=clampValue(radius,kRadiusMinimum,kRadiusMaximum);
    const bool changed=m_brush.radius!=clamped;
    {
        QSignalBlocker block(m_radiusSlider);
        if(m_radiusSlider->value()!=static_cast<int>(std::lround(clamped)))
            m_radiusSlider->setValue(static_cast<int>(std::lround(clamped)));
    }
    if(!changed)return;
    m_brush.radius=clamped;
    syncBrushControls();
    emit brushSettingsChanged(m_brush);
}

void LocalAdjustmentPanel::handleHardnessChanged(double value){
    if(m_restoring||!std::isfinite(value))return;
    const double clamped=clampValue(value,0,1);
    const bool changed=m_brush.hardness!=clamped;
    {
        QSignalBlocker block(m_hardnessSlider);
        const int tick=unitSteps(clamped);
        if(m_hardnessSlider->value()!=tick)m_hardnessSlider->setValue(tick);
    }
    if(!changed)return;
    m_brush.hardness=clamped;
    syncBrushControls();
    emit brushSettingsChanged(m_brush);
}

void LocalAdjustmentPanel::handleStrengthChanged(double value){
    if(m_restoring||!std::isfinite(value))return;
    const double clamped=clampValue(value,0,1);
    const bool changed=m_brush.strength!=clamped;
    {
        QSignalBlocker block(m_strengthSlider);
        const int tick=unitSteps(clamped);
        if(m_strengthSlider->value()!=tick)m_strengthSlider->setValue(tick);
    }
    if(!changed)return;
    m_brush.strength=clamped;
    syncBrushControls();
    emit brushSettingsChanged(m_brush);
}
