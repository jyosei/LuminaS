#include "app/local_adjustment_panel.h"
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>

namespace {
// The nine described local parameters share one table: bounds, default and
// label live in exactly one place, so every row is built by the same code.
constexpr int kParameterMinimum=-1,kParameterMaximum=1;
constexpr double kRadiusMinimum=1,kRadiusMaximum=10000;
constexpr int kSliderSteps=100; // hundredths of the unit field
constexpr int kListMaximumHeight=170;
// Layer opacity: the slider counts tenths of a percent, the spin box percent.
constexpr int kOpacitySliderMaximum=1000;
constexpr double kOpacityPercentMaximum=100;

double clampValue(double value,double minimum,double maximum){
    return value<minimum?minimum:(value>maximum?maximum:value);
}
int stepsOf(double value){return static_cast<int>(std::lround(value*kSliderSteps));}
int unitSteps(double value){return static_cast<int>(std::lround(value*100));}
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
    m_list->setToolTip(tr("局部层列表，顶层在最上方"));
    m_list->setAccessibleName(tr("局部层列表"));
    // One row carries a stable id (UserRole), the layer name (display role) and
    // the visibility checkbox (check state). Rows are editable in place and can
    // be reordered by dragging; only local layers live in this list.
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setEditTriggers(QAbstractItemView::DoubleClicked|QAbstractItemView::EditKeyPressed);
    m_list->setDragDropMode(QAbstractItemView::InternalMove);
    m_list->setDefaultDropAction(Qt::MoveAction);
    m_list->setDragEnabled(true);
    m_list->setAcceptDrops(true);
    m_list->setDropIndicatorShown(true);
    m_list->installEventFilter(this); // never filter the inline text editor
    layout->addWidget(m_list);

    m_new=new QPushButton(tr("新建笔刷层"),this);
    m_new->setObjectName(QStringLiteral("local-new-brush"));
    m_new->setCheckable(false);
    m_new->setToolTip(tr("新建一个笔刷局部层"));
    m_new->setAccessibleName(tr("新建笔刷层"));
    // The new button and its compact layer menu share one row.
    QHBoxLayout *createRow=new QHBoxLayout;
    {
        createRow->setContentsMargins(0,0,0,0);
        createRow->setSpacing(4);
        createRow->addWidget(m_new,1);
        m_layerMenu=new QToolButton(this);
        m_layerMenu->setObjectName(QStringLiteral("local-layer-menu"));
        m_layerMenu->setText(tr("图层"));
        m_layerMenu->setPopupMode(QToolButton::InstantPopup);
        m_layerMenu->setToolButtonStyle(Qt::ToolButtonTextOnly);
        m_layerMenu->setToolTip(tr("重命名、复制、删除或调整当前局部层的顺序"));
        m_layerMenu->setAccessibleName(tr("图层菜单"));
        m_menu=new QMenu(m_layerMenu);
        m_menu->setObjectName(QStringLiteral("local-layer-menu-popup"));
        m_renameAction=m_menu->addAction(tr("重命名"));
        m_renameAction->setObjectName(QStringLiteral("local-layer-rename"));
        m_duplicateAction=m_menu->addAction(tr("复制图层"));
        m_duplicateAction->setObjectName(QStringLiteral("local-layer-duplicate"));
        m_deleteAction=m_menu->addAction(tr("删除图层"));
        m_deleteAction->setObjectName(QStringLiteral("local-layer-delete"));
        m_menu->addSeparator();
        m_upAction=m_menu->addAction(tr("上移一层"));
        m_upAction->setObjectName(QStringLiteral("local-layer-up"));
        m_downAction=m_menu->addAction(tr("下移一层"));
        m_downAction->setObjectName(QStringLiteral("local-layer-down"));
        m_layerMenu->setMenu(m_menu);
        createRow->addWidget(m_layerMenu);
        layout->addLayout(createRow);
    }

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
    tools->addWidget(m_overlay);

    m_visible=new QCheckBox(tr("显示此层"),this);
    m_visible->setObjectName(QStringLiteral("local-layer-visible"));
    m_visible->setChecked(true);
    m_visible->setToolTip(tr("显示或隐藏当前局部层"));
    m_visible->setAccessibleName(tr("显示此层"));
    createRow->insertWidget(1,m_visible);

    // The global base is a fixed reference row, never a sortable layer: it is a
    // non-interactive label outside the list.
    m_base=new QLabel(tr("全图基础（固定底层）"),this);
    m_base->setObjectName(QStringLiteral("local-layer-base"));
    m_base->setTextInteractionFlags(Qt::NoTextInteraction);
    m_base->setFocusPolicy(Qt::NoFocus);
    m_base->setEnabled(false); // the existing palette already renders muted text
    m_base->setToolTip(tr("全图基础始终位于局部层下方"));
    m_base->setAccessibleName(tr("全图基础，固定底层"));
    layout->insertWidget(1,m_base);

    // Layer strength: it belongs to the layer, so it sits with the list
    // operations and stays clearly separate from the brush strength below.
    m_opacityBox=new QWidget(this);
    QFormLayout *opacityForm=new QFormLayout(m_opacityBox);
    opacityForm->setContentsMargins(0,0,0,0);
    opacityForm->setSpacing(4);
    opacityForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    {
        QHBoxLayout *box=new QHBoxLayout;
        box->setContentsMargins(0,0,0,0);
        box->setSpacing(4);
        m_opacitySlider=new QSlider(Qt::Horizontal,m_opacityBox);
        m_opacitySlider->setObjectName(QStringLiteral("slider-local-layer-opacity"));
        m_opacitySlider->setRange(0,kOpacitySliderMaximum); // tenths of a percent
        m_opacitySlider->setValue(kOpacitySliderMaximum);
        m_opacitySlider->setToolTip(tr("当前局部层强度，单位为百分比"));
        m_opacitySlider->setAccessibleName(tr("图层强度滑块"));
        m_opacitySpin=new QDoubleSpinBox(m_opacityBox);
        m_opacitySpin->setObjectName(QStringLiteral("local-layer-opacity"));
        m_opacitySpin->setRange(0,kOpacityPercentMaximum);
        m_opacitySpin->setSingleStep(1);
        m_opacitySpin->setDecimals(1);
        m_opacitySpin->setSuffix(tr(" %"));
        m_opacitySpin->setValue(kOpacityPercentMaximum);
        m_opacitySpin->setToolTip(tr("当前局部层强度，百分之一百为原始强度"));
        m_opacitySpin->setAccessibleName(tr("图层强度"));
        box->addWidget(m_opacitySlider,1);
        box->addWidget(m_opacitySpin);
        opacityForm->addRow(tr("图层强度"),box);
    }
    layout->insertWidget(3,m_opacityBox);

    m_parameters=new QWidget(this);
    m_parameters->setObjectName(QStringLiteral("local-parameters"));
    QFormLayout *form=new QFormLayout(m_parameters);
    form->setContentsMargins(0,0,0,0);
    form->setSpacing(4);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    m_parameterForm=form;
    for(int i=0;i<FieldCount;++i){
        const FieldSpec &spec=fieldSpecs()[i];
        buildParameterRow(spec);
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
    layout->insertWidget(5,m_brushBox);

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
    connect(m_list,&QListWidget::itemChanged,this,&LocalAdjustmentPanel::handleItemChanged);
    // A drop reorders the rows first; the reported order is captured inside the
    // model change and emitted only after the drop event has fully returned.
    connect(m_list->model(),&QAbstractItemModel::rowsAboutToBeMoved,this,[this]{if(!m_restoring)m_modelMoveActive=true;});
    connect(m_list->model(),&QAbstractItemModel::rowsMoved,this,
        [this](const QModelIndex &,int,int,const QModelIndex &,int){
            if(m_restoring||m_movingRows||m_layers.size()<2){m_modelMoveActive=false;return;}
            QStringList ids;
            ids.reserve(m_layers.size());
            for(int row=0;row<m_layers.size();++row) // top to bottom
                if(QListWidgetItem *item=m_list->item(row))ids.append(item->data(Qt::UserRole).toString());
            if(ids.size()!=m_layers.size()){m_modelMoveActive=false;return;}
            m_pendingOrderIds=ids;
            m_pendingMove=true;
            QTimer::singleShot(0,this,[this](){
                m_modelMoveActive=false;
                if(!m_pendingMove)return;
                m_pendingMove=false;
                reportOrder(m_pendingOrderIds);
            });
        });
    connect(m_new,&QPushButton::clicked,this,&LocalAdjustmentPanel::handleCreate);
    connect(m_renameAction,&QAction::triggered,this,&LocalAdjustmentPanel::handleRenameAction);
    connect(m_duplicateAction,&QAction::triggered,this,&LocalAdjustmentPanel::handleDuplicateAction);
    connect(m_deleteAction,&QAction::triggered,this,&LocalAdjustmentPanel::handleDeleteAction);
    connect(m_upAction,&QAction::triggered,this,[this]{handleMoveAction(-1);});
    connect(m_downAction,&QAction::triggered,this,[this]{handleMoveAction(1);});
    // The value fields carry the live edit; the editingFinished fields carry the
    // single commit, so a finished signal is never doubled by a live drag.
    connect(m_opacitySlider,&QSlider::valueChanged,this,
        [this](int value){handleLayerOpacity(value/10.0);});
    connect(m_opacitySlider,&QSlider::sliderReleased,this,&LocalAdjustmentPanel::handleLayerOpacityFinished);
    connect(m_opacitySlider,&QSlider::actionTriggered,this,[this](int){
        if(!m_restoring&&!m_opacitySlider->isSliderDown()){
            const auto id=m_selectedId;
            QTimer::singleShot(0,this,[this,id]{if(id==m_selectedId)handleLayerOpacityFinished();});
        }
    });
    connect(m_opacitySpin,&QDoubleSpinBox::valueChanged,this,&LocalAdjustmentPanel::handleLayerOpacity);
    connect(m_opacitySpin,&QDoubleSpinBox::editingFinished,this,[this](){
        if(m_opacitySlider->isSliderDown())return;
        handleLayerOpacityFinished();
    });
    m_opacitySpin->installEventFilter(this);
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

// Nine entries in the documented visual order, keyed by their stable enum value.
const LocalAdjustmentPanel::FieldSpec *LocalAdjustmentPanel::fieldSpecs(){
    static const FieldSpec specs[]={
        {Exposure,  "exposure",  -5,5,0, QT_TRANSLATE_NOOP("LocalAdjustmentPanel","曝光"),&LocalEdit::LocalParameters::exposure},
        {Contrast,  "contrast",  kParameterMinimum,kParameterMaximum,0, QT_TRANSLATE_NOOP("LocalAdjustmentPanel","对比"),&LocalEdit::LocalParameters::contrast},
        {Highlights,"highlights",kParameterMinimum,kParameterMaximum,0, QT_TRANSLATE_NOOP("LocalAdjustmentPanel","高光"),&LocalEdit::LocalParameters::highlights},
        {Shadows,   "shadows",   kParameterMinimum,kParameterMaximum,0, QT_TRANSLATE_NOOP("LocalAdjustmentPanel","阴影"),&LocalEdit::LocalParameters::shadows},
        {Whites,    "whites",    kParameterMinimum,kParameterMaximum,0, QT_TRANSLATE_NOOP("LocalAdjustmentPanel","白色"),&LocalEdit::LocalParameters::whites},
        {Blacks,    "blacks",    kParameterMinimum,kParameterMaximum,0, QT_TRANSLATE_NOOP("LocalAdjustmentPanel","黑色"),&LocalEdit::LocalParameters::blacks},
        {Warmth,    "warmth",    kParameterMinimum,kParameterMaximum,0, QT_TRANSLATE_NOOP("LocalAdjustmentPanel","暖冷"),&LocalEdit::LocalParameters::warmth},
        {Tint,      "tint",      kParameterMinimum,kParameterMaximum,0, QT_TRANSLATE_NOOP("LocalAdjustmentPanel","色调"),&LocalEdit::LocalParameters::tint},
        {Saturation,"saturation",0,2,1, QT_TRANSLATE_NOOP("LocalAdjustmentPanel","饱和度"),&LocalEdit::LocalParameters::saturation},
    };
    static_assert(sizeof(specs)/sizeof(specs[0])==FieldCount,"one spec per field");
    return specs;
}

const LocalAdjustmentPanel::FieldSpec &LocalAdjustmentPanel::specOf(Field field){
    const FieldSpec *specs=fieldSpecs();
    for(int i=0;i<FieldCount;++i)if(specs[i].field==field)return specs[i];
    return specs[0];
}

// One shared shape for every parameter row: left label, growing slider, spin box
// and a per-field reset button. Sliders are always hundredths of the field unit,
// spin boxes always show two decimals, and no row carries its own font or color.
void LocalAdjustmentPanel::buildParameterRow(const FieldSpec &spec){
    const QString key=QString::fromLatin1(spec.key);
    const QString label=tr(spec.label);
    const QString qualified=tr("局部%1").arg(label);
    QHBoxLayout *box=new QHBoxLayout;
    box->setContentsMargins(0,0,0,0);
    box->setSpacing(4);
    QSlider *slider=new QSlider(Qt::Horizontal,m_parameters);
    slider->setObjectName(QStringLiteral("slider-local-%1").arg(key));
    slider->setRange(stepsOf(spec.minimum),stepsOf(spec.maximum));
    slider->setValue(stepsOf(spec.defaultValue));
    slider->setToolTip(tr("%1滑块").arg(qualified));
    slider->setAccessibleName(tr("%1滑块").arg(qualified));
    QDoubleSpinBox *spin=new QDoubleSpinBox(m_parameters);
    spin->setObjectName(QStringLiteral("local-%1").arg(key));
    spin->setRange(spec.minimum,spec.maximum);
    spin->setSingleStep(spec.field==Exposure?.05:.01);
    spin->setDecimals(2);
    spin->setValue(spec.defaultValue);
    spin->setToolTip(spec.field==Exposure?tr("局部曝光，单位为曝光值"):qualified);
    spin->setAccessibleName(qualified);
    QPushButton *reset=new QPushButton(Ui::icon(Ui::Glyph::Reset),QString(),m_parameters);
    reset->setIconSize({14,14});
    reset->setFixedWidth(25);
    // The historical enum values 0/1 keep local-reset-0/1 on exposure/saturation.
    reset->setObjectName(QStringLiteral("local-reset-%1").arg(static_cast<int>(spec.field)));
    reset->setToolTip(tr("将此字段恢复为默认值"));
    reset->setAccessibleName(tr("重置%1").arg(qualified));
    box->addWidget(slider,1);
    box->addWidget(spin);
    box->addWidget(reset);
    m_parameterForm->addRow(label,box);
    m_rows[spec.field]={spin,slider,reset};
}

void LocalAdjustmentPanel::setDocument(LocalEdit::DocumentSnapshot snapshot,const QString &selectedId){
    m_restoring=true; // programmatic refresh: no selection or parameter signal
    m_layers.clear();
    if(snapshot)for(const LocalEdit::AdjustmentLayer &layer:snapshot->layers){
        Layer copy;
        copy.id=layer.id;
        copy.name=layer.name;
        copy.enabled=layer.enabled;
        copy.opacity=std::isfinite(layer.opacity)?clampValue(layer.opacity,0,1):1;
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
    // A same-id render refresh must not discard an already captured user drop.
    // reportOrder checks its complete id set against the current document copy.
}

int LocalAdjustmentPanel::selectedRow() const {
    // Selection is always resolved by id inside the document order.
    for(int i=0;i<m_layers.size();++i)if(m_layers.at(i).id==m_selectedId)return i;
    return -1;
}

int LocalAdjustmentPanel::displayRowOf(const QString &id) const {
    if(id.isEmpty())return -1;
    for(int row=0;row<m_list->count();++row){ // rows are top to bottom
        const QListWidgetItem *item=m_list->item(row);
        if(item&&item->data(Qt::UserRole).toString()==id)return row;
    }
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
    const int count=m_layers.size();
    m_blockReorder=true; // one guard for the whole rebuild, not one per row
    {
        QSignalBlocker block(m_list);
        QStringList wanted,current;
        for(int i=count-1;i>=0;--i)wanted.append(m_layers[i].id);
        for(int i=0;i<m_list->count();++i)current.append(m_list->item(i)->data(Qt::UserRole).toString());
        if(current!=wanted)m_list->clear(); // never reassign an editor's row to another asset/id
        for(int documentRow=count-1;documentRow>=0;--documentRow){ // rows: top to bottom
            const Layer &layer=m_layers.at(documentRow);
            const QString text=layer.name.isEmpty()?layer.id:layer.name;
            const int row=count-1-documentRow;
            QListWidgetItem *item=m_list->item(row);
            if(!item){
                item=new QListWidgetItem(m_list);item->setData(Qt::UserRole,layer.id);
                item->setFlags(Qt::ItemIsEnabled|Qt::ItemIsSelectable|Qt::ItemIsEditable|Qt::ItemIsUserCheckable|Qt::ItemIsDragEnabled);
            }
            if(item->text()!=text)item->setText(text);
            if(item->checkState()!=(layer.enabled?Qt::Checked:Qt::Unchecked))
                item->setCheckState(layer.enabled?Qt::Checked:Qt::Unchecked);
        }
        const int row=displayRowOf(m_selectedId);
        if(row>=0)m_list->setCurrentRow(row); // no selection signal: not a user gesture
        else m_list->setCurrentRow(-1);
    }
    m_blockReorder=false;
    // Give controls the space when there are few layers; the list itself scrolls
    // once it reaches six rows instead of reserving a large empty rectangle.
    const int rowHeight=m_list->count()>0?m_list->sizeHintForRow(0):m_list->fontMetrics().height()+8;
    m_list->setFixedHeight(qMin(kListMaximumHeight,qMax(1,rowHeight)*qBound(2,m_list->count(),6)+2*m_list->frameWidth()));
}

void LocalAdjustmentPanel::refreshControls(){
    const bool selected=hasSelection();
    // Every tool and parameter control needs a prepared preview, so availability
    // gates all of them; the visibility checkbox and the nine parameter rows
    // additionally need a selected layer. Availability never mutates the stored
    // layers, and an empty new layer enables the overlay later through the host.
    const bool ready=m_available&&selected;
    // Reaching the documented layer limit disables creation instead of hiding it,
    // so the limit stays visible and explained.
    m_new->setEnabled(m_available&&m_layers.size()<LocalEdit::maximumLayers);
    m_layerMenu->setEnabled(m_available&&selected);
    m_list->setEnabled(m_available);
    m_list->setDragEnabled(m_available&&m_layers.size()>1);
    m_list->setAcceptDrops(m_available&&m_layers.size()>1);
    m_visible->setEnabled(ready);
    m_parameters->setEnabled(ready);
    m_opacityBox->setEnabled(ready);
    m_brushBox->setEnabled(ready);
    m_brushButton->setEnabled(ready);
    m_erase->setEnabled(ready);
    m_overlay->setEnabled(ready);
    for(int i=0;i<FieldCount;++i)syncRow(static_cast<Field>(i),storedField(static_cast<Field>(i)));
    m_hint->setText(!m_available?m_hintWaiting:(selected?QString():m_hintEmpty));
    m_hint->setVisible(!m_available||!selected);
    syncVisibility();
    syncLayerOpacity();
    refreshLayerActions();
}

// The layer strength is the selected layer's stored value, normalized to [0,1].
void LocalAdjustmentPanel::syncLayerOpacity(){
    const Layer *layer=selectedLayer();
    const double opacity=layer?clampValue(layer->opacity,0,1):1;
    {
        QSignalBlocker sliderBlock(m_opacitySlider);
        const int tick=static_cast<int>(std::lround(opacity*kOpacitySliderMaximum));
        if(m_opacitySlider->value()!=tick)m_opacitySlider->setValue(tick);
    }
    {
        QSignalBlocker spinBlock(m_opacitySpin);
        const double percent=std::round(opacity*kOpacityPercentMaximum*10)/10; // one decimal
        if(m_opacitySpin->value()!=percent)m_opacitySpin->setValue(percent);
    }
}

// Menu state follows the real row position, never an assumed one.
void LocalAdjustmentPanel::refreshLayerActions(){
    const int row=selectedRow();
    const bool selected=row>=0;
    const int displayRow=displayRowOf(m_selectedId);
    const bool reorder=m_available&&m_layers.size()>1;
    m_renameAction->setEnabled(m_available&&selected);
    m_duplicateAction->setEnabled(m_available&&selected&&m_layers.size()<LocalEdit::maximumLayers);
    m_deleteAction->setEnabled(m_available&&selected);
    m_upAction->setEnabled(reorder&&displayRow>0);
    m_downAction->setEnabled(reorder&&displayRow>=0&&displayRow<m_layers.size()-1);
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
    if(!layer)return specOf(field).defaultValue;
    return layer->parameters.*(specOf(field).member);
}

void LocalAdjustmentPanel::storeField(Field field,double value){
    if(m_restoring||!std::isfinite(value))return;
    Layer *layer=selectedLayer();
    if(!layer)return;
    double &target=layer->parameters.*(specOf(field).member);
    if(target==value)return;
    target=value; // exactly this field; the other eight values stay untouched
    // A direct receiver can refresh the panel and replace m_layers. Keep signal
    // arguments alive until every receiver (including observers) has returned.
    const auto id=layer->id;const auto parameters=layer->parameters;
    emit parametersEdited(id,parameters);
}

void LocalAdjustmentPanel::handleSelectionChanged(){
    if(m_restoring||m_modelMoveActive||m_movingRows)return;
    const QListWidgetItem *item=m_list->currentItem();
    const QString id=item?item->data(Qt::UserRole).toString():QString();
    if(id.isEmpty()||id==m_selectedId)return;
    m_selectedId=id;
    refreshControls();
    const auto selected=m_selectedId;emit selectedLayerChanged(selected);
}

// One item change carries three different meanings; the stable id decides which
// layer is meant and the copied name/state decides the intent.
void LocalAdjustmentPanel::handleItemChanged(QListWidgetItem *item){
    if(m_restoring||m_blockReorder||!m_available||!item)return;
    const QString id=item->data(Qt::UserRole).toString();
    if(id.isEmpty())return;
    int index=-1;
    for(int i=0;i<m_layers.size();++i)if(m_layers.at(i).id==id){index=i;break;}
    if(index<0)return; // a row without a stored layer is never edited or reported
    Layer &layer=m_layers[index];
    const bool checked=item->checkState()==Qt::Checked;
    // Both values are copied before emitting: a receiver may refresh the panel
    // and destroy this row and this Layer reference.
    const QString name=item->text();
    if((layer.name.isEmpty()?layer.id:layer.name)!=name){
        // A rejected rename is not faked here: the widget asks the host and keeps
        // showing whatever the next refresh really contains.
        emit layerRenameRequested(id,name);
        return;
    }
    if(layer.enabled!=checked){
        layer.enabled=checked;
        if(selectedRow()==index)syncVisibility();
        emit layerVisibilityChanged(id,checked);
    }
}

void LocalAdjustmentPanel::handleRenameAction(){
    if(m_restoring||!m_available||!hasSelection())return;
    QListWidgetItem *item=m_list->currentItem();
    if(!item)return;
    m_list->editItem(item); // begins the same in-place edit as a double click
}

void LocalAdjustmentPanel::handleDuplicateAction(){
    if(m_restoring||!m_available)return;
    const int row=selectedRow();
    if(row<0||m_layers.size()>=LocalEdit::maximumLayers)return;
    const auto id=m_layers.at(row).id; // the host makes the copy; no row is fabricated
    emit layerDuplicateRequested(id);
}

void LocalAdjustmentPanel::handleDeleteAction(){
    if(m_restoring||!m_available)return;
    const int row=selectedRow();
    if(row<0)return;
    const auto id=m_layers.at(row).id;
    emit layerDeleteRequested(id);
}

// The menu moves the real row first and reports the resulting order afterwards.
void LocalAdjustmentPanel::handleMoveAction(int delta){
    if(m_restoring||!m_available||m_layers.size()<2)return;
    moveSelectedLayer(delta);
}

void LocalAdjustmentPanel::moveSelectedLayer(int delta){
    const int row=selectedRow();
    if(row<0)return;
    const int displayRow=displayRowOf(m_selectedId);
    const int target=displayRow+delta; // list rows run top to bottom
    if(displayRow<0||target<0||target>=m_list->count())return;
    QStringList ids;
    for(int i=0;i<m_list->count();++i)ids.append(m_list->item(i)->data(Qt::UserRole).toString());
    ids.swapItemsAt(displayRow,target);
    reportOrder(ids);
}

// A drop is only reported when the queued id set still matches the shown layers;
// a refresh between the drop and the next event turn invalidates it.
void LocalAdjustmentPanel::reportOrder(const QStringList &ids){
    if(m_restoring||!m_available||ids.size()!=m_layers.size()||ids.size()<2)return;
    QStringList known;
    known.reserve(ids.size());
    for(const QString &id:ids){
        if(id.isEmpty()||known.contains(id))return;
        known.append(id);
    }
    if(known.size()!=m_layers.size())return; // exactly the shown layers, no more
    for(const Layer &layer:m_layers)if(!known.contains(layer.id))return;
    // Apply the reported order to the widget's own copies: the list is top to
    // bottom while m_layers stays in document order.
    QVector<Layer> reordered;
    reordered.reserve(m_layers.size());
    for(int i=ids.size()-1;i>=0;--i)
        for(const Layer &layer:m_layers)if(layer.id==ids.at(i)){reordered.append(layer);break;}
    m_movingRows=true;
    m_layers=reordered;
    refreshList();
    refreshControls();
    m_movingRows=false;
    const auto order=ids;
    emit layerOrderRequested(order);
}

// Delete only reaches the layer list when the list itself has focus: a focused
// row editor keeps its own text editing keys.
bool LocalAdjustmentPanel::handleListKeyPress(QKeyEvent *event){
    if(event->key()==Qt::Key_Delete){
        if(m_restoring||!m_available||!hasSelection())return false;
        handleDeleteAction();
        event->accept();
        return true;
    }
    if(event->key()==Qt::Key_F2){
        if(m_restoring||!m_available||!hasSelection())return false;
        handleRenameAction();
        event->accept();
        return true;
    }
    return false;
}

void LocalAdjustmentPanel::handleCreate(){
    if(m_restoring||!m_available||m_layers.size()>=LocalEdit::maximumLayers)return;
    emit createBrushLayerRequested(); // no local placeholder row is fabricated
}

// Live edit only: one normalized value per change, never a document write.
void LocalAdjustmentPanel::handleLayerOpacity(double percent){
    if(m_restoring||!m_available||!std::isfinite(percent))return;
    const double clamped=clampValue(percent,0,kOpacityPercentMaximum);
    Layer *layer=selectedLayer();
    if(!layer)return;
    const double opacity=clampValue(clamped/kOpacityPercentMaximum,0,1); // always normalized
    if(layer->opacity==opacity)return;
    layer->opacity=opacity;
    syncLayerOpacity();
    const auto id=layer->id;
    emit layerOpacityEdited(id,opacity);
}

void LocalAdjustmentPanel::handleLayerOpacityFinished(){
    if(m_restoring||!m_available||!hasSelection()||m_opacitySlider->isSliderDown())return;
    emit layerOpacityFinished();
}

void LocalAdjustmentPanel::handleVisibleToggled(bool checked){
    if(m_restoring)return;
    Layer *layer=selectedLayer();
    if(!layer||layer->enabled==checked)return;
    layer->enabled=checked;
    {const QSignalBlocker block(m_list);const int display=displayRowOf(layer->id);if(display>=0)m_list->item(display)->setCheckState(checked?Qt::Checked:Qt::Unchecked);}
    const auto id=layer->id;emit layerVisibilityChanged(id,checked);
}

void LocalAdjustmentPanel::handleSpinChanged(Field field,double value){
    if(m_restoring)return;
    storeField(field,value);
    syncRow(field,storedField(field)); // only this row follows the spin
}

void LocalAdjustmentPanel::handleSliderChanged(Field field,int value){
    if(m_restoring)return;
    // Every slider is hundredths of its field unit, so the same conversion
    // applies to all nine parameters while the spin box keeps 2 decimals.
    const double step=1.0/kSliderSteps;
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
    const auto id=m_selectedId;
    QTimer::singleShot(0,this,[this,id](){
        if(m_restoring||id!=m_selectedId)return;
        for(const auto &row:m_rows)if(row.slider->isSliderDown())return;
        emit parametersFinished();
    });
}

// A focused spin box handles arrow keys itself, so its value changes without an
// editingFinished. Only the up/down/left/right keys reach this path.
bool LocalAdjustmentPanel::eventFilter(QObject *target,QEvent *event){
    if(event->type()==QEvent::KeyPress){
        // Only a key press on the list itself reaches the layer keys. A row
        // editor is a child widget, so Backspace/Delete inside it stay there and
        // F2 starts the same in-place rename the menu does.
        if(target==m_list&&QApplication::focusWidget()==m_list&&handleListKeyPress(static_cast<QKeyEvent *>(event)))return true;
    }
    if(event->type()!=QEvent::KeyPress)return QWidget::eventFilter(target,event);
    const int key=static_cast<QKeyEvent *>(event)->key();
    if(key!=Qt::Key_Up&&key!=Qt::Key_Down&&key!=Qt::Key_Left&&key!=Qt::Key_Right)
        return QWidget::eventFilter(target,event);
    if(target==m_opacitySpin&&!m_restoring){
        const auto id=m_selectedId;
        QTimer::singleShot(0,this,[this,id]{if(id==m_selectedId)handleLayerOpacityFinished();});
    }
    for(int i=0;i<FieldCount;++i)if(target==m_rows[i].spin){
        handleKeyboardCommit(static_cast<Field>(i));
        break;
    }
    return QWidget::eventFilter(target,event);
}

void LocalAdjustmentPanel::handleReset(Field field){
    if(m_restoring||!hasSelection())return;
    storeField(field,specOf(field).defaultValue); // documented default for that field only
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
