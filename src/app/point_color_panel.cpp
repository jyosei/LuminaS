#include "app/point_color_panel.h"
#include "core/point_color_json.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QGuiApplication>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>
#include <cmath>

namespace {
const double kShiftInitial[3]={0,0,0};
const double kRangeInitial[4]={30,.25,.25,.5};
// Compact read-only reset affordance: icon plus a short word, no local colors or fonts.
QPushButton *resetButton(QWidget *parent,int field,const QString &label){
    QPushButton *button=new QPushButton(Ui::icon(Ui::Glyph::Reset),QString(),parent);
    button->setIconSize({14,14});button->setFixedWidth(25);
    button->setObjectName(QStringLiteral("point-color-reset-%1").arg(field));
    button->setToolTip(QCoreApplication::translate("PointColorPanel","将此字段恢复为默认值"));
    button->setAccessibleName(QCoreApplication::translate("PointColorPanel","重置%1").arg(label));
    return button;
}
double clampUnit(double v){return v<0?0:(v>1?1:v);}
}

PointColorPanel::PointColorPanel(QWidget *parent):QWidget(parent){
    buildUi();
    refreshControls();
    refreshList();
    refreshReference();
}

void PointColorPanel::buildUi(){
    QVBoxLayout *layout=new QVBoxLayout(this);
    layout->setContentsMargins(0,0,0,0);
    layout->setSpacing(8);

    QHBoxLayout *row=new QHBoxLayout;
    row->setContentsMargins(0,0,0,0);
    row->setSpacing(8);
    m_picker=new QPushButton(Ui::icon(Ui::Glyph::Balance),tr("取色"),this);
    m_picker->setObjectName(QStringLiteral("point-color-picker"));
    m_picker->setCheckable(true);
    m_picker->setToolTip(tr("在照片上点击以添加一个选色点"));
    m_picker->setAccessibleName(tr("取色"));
    m_delete=new QPushButton(tr("删除"),this);
    m_delete->setObjectName(QStringLiteral("point-color-delete"));
    m_delete->setCheckable(false);
    m_delete->setToolTip(tr("删除当前选色点"));
    m_delete->setAccessibleName(tr("删除选色点"));
    row->addWidget(m_picker);
    row->addWidget(m_delete);
    row->addStretch(1);
    layout->addLayout(row);

    m_samples=new QComboBox(this);
    m_samples->setObjectName(QStringLiteral("point-color-samples"));
    m_samples->setToolTip(tr("选择要调整的选色点"));
    m_samples->setAccessibleName(tr("选色点列表"));
    layout->addWidget(m_samples);

    m_reference=new QLabel(this);
    m_reference->setObjectName(QStringLiteral("point-color-reference"));
    m_reference->setToolTip(tr("当前选色点的参考色相与归一化明度"));
    m_reference->setAccessibleName(tr("当前参考色"));
    m_reference->setWordWrap(true);
    layout->addWidget(m_reference);

    m_hint=new QLabel(tr("点击取色后，在照片上选择要调整的颜色。"),this);
    m_hint->setObjectName(QStringLiteral("point-color-hint"));
    m_hint->setWordWrap(true);
    layout->addWidget(m_hint);

    m_rangePreview=new QCheckBox(tr("显示选色范围"),this);
    m_rangePreview->setObjectName(QStringLiteral("point-color-range-preview"));
    m_rangePreview->setToolTip(tr("显示当前选色点影响的范围"));
    m_rangePreview->setAccessibleName(tr("显示选色范围"));
    layout->addWidget(m_rangePreview);

    m_controls=new QWidget(this);
    QFormLayout *form=new QFormLayout(m_controls);
    form->setContentsMargins(0,0,0,0);
    form->setSpacing(4);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    // Hue shift: shift fields take [-1,1] step .05 before the widths and feather.
    {
        QHBoxLayout *box=new QHBoxLayout;
        box->setContentsMargins(0,0,0,0);
        box->setSpacing(4);
        QDoubleSpinBox *spin=new QDoubleSpinBox(m_controls);
        spin->setObjectName(QStringLiteral("point-color-hue-shift"));
        spin->setRange(-1,1);
        spin->setSingleStep(.05);
        spin->setDecimals(2);
        spin->setValue(0);
        spin->setToolTip(tr("色相偏移"));
        spin->setAccessibleName(tr("色相偏移"));
        QSlider *slider=new QSlider(Qt::Horizontal,m_controls);
        slider->setObjectName(QStringLiteral("slider-point-color-hue-shift"));
        slider->setRange(-100,100); // hundredths of the field range
        slider->setValue(0);
        slider->setToolTip(tr("色相偏移滑块"));
        slider->setAccessibleName(tr("色相偏移滑块"));
        QPushButton *reset=resetButton(m_controls,HueShift,tr("色相偏移"));
        box->addWidget(slider,1);
        box->addWidget(spin);
        box->addWidget(reset);
        form->addRow(tr("色相偏移"),box);
        m_rows[HueShift]={spin,slider,reset,kShiftInitial[0]};
    }
    {
        QHBoxLayout *box=new QHBoxLayout;
        box->setContentsMargins(0,0,0,0);
        box->setSpacing(4);
        QDoubleSpinBox *spin=new QDoubleSpinBox(m_controls);
        spin->setObjectName(QStringLiteral("point-color-saturation-shift"));
        spin->setRange(-1,1);
        spin->setSingleStep(.05);
        spin->setDecimals(2);
        spin->setValue(0);
        spin->setToolTip(tr("饱和度偏移"));
        spin->setAccessibleName(tr("饱和度偏移"));
        QSlider *slider=new QSlider(Qt::Horizontal,m_controls);
        slider->setObjectName(QStringLiteral("slider-point-color-saturation-shift"));
        slider->setRange(-100,100);
        slider->setValue(0);
        slider->setToolTip(tr("饱和度偏移滑块"));
        slider->setAccessibleName(tr("饱和度偏移滑块"));
        QPushButton *reset=resetButton(m_controls,SaturationShift,tr("饱和度偏移"));
        box->addWidget(slider,1);
        box->addWidget(spin);
        box->addWidget(reset);
        form->addRow(tr("饱和度偏移"),box);
        m_rows[SaturationShift]={spin,slider,reset,kShiftInitial[1]};
    }
    {
        QHBoxLayout *box=new QHBoxLayout;
        box->setContentsMargins(0,0,0,0);
        box->setSpacing(4);
        QDoubleSpinBox *spin=new QDoubleSpinBox(m_controls);
        spin->setObjectName(QStringLiteral("point-color-luminance-shift"));
        spin->setRange(-1,1);
        spin->setSingleStep(.05);
        spin->setDecimals(2);
        spin->setValue(0);
        spin->setToolTip(tr("明度偏移"));
        spin->setAccessibleName(tr("明度偏移"));
        QSlider *slider=new QSlider(Qt::Horizontal,m_controls);
        slider->setObjectName(QStringLiteral("slider-point-color-luminance-shift"));
        slider->setRange(-100,100);
        slider->setValue(0);
        slider->setToolTip(tr("明度偏移滑块"));
        slider->setAccessibleName(tr("明度偏移滑块"));
        QPushButton *reset=resetButton(m_controls,LuminanceShift,tr("明度偏移"));
        box->addWidget(slider,1);
        box->addWidget(spin);
        box->addWidget(reset);
        form->addRow(tr("明度偏移"),box);
        m_rows[LuminanceShift]={spin,slider,reset,kShiftInitial[2]};
    }
    // Hue width keeps a degree spin, hence 100 stepped slider units.
    {
        QHBoxLayout *box=new QHBoxLayout;
        box->setContentsMargins(0,0,0,0);
        box->setSpacing(4);
        QDoubleSpinBox *spin=new QDoubleSpinBox(m_controls);
        spin->setObjectName(QStringLiteral("point-color-hue-width"));
        spin->setRange(1,180);
        spin->setSingleStep(1);
        spin->setDecimals(1);
        spin->setValue(30);
        spin->setToolTip(tr("色相宽度（度）"));
        spin->setAccessibleName(tr("色相宽度"));
        QSlider *slider=new QSlider(Qt::Horizontal,m_controls);
        slider->setObjectName(QStringLiteral("slider-point-color-hue-width"));
        slider->setRange(100,18000);
        slider->setValue(3000);
        slider->setToolTip(tr("色相宽度滑块"));
        slider->setAccessibleName(tr("色相宽度滑块"));
        QPushButton *reset=resetButton(m_controls,HueWidth,tr("色相宽度"));
        box->addWidget(slider,1);
        box->addWidget(spin);
        box->addWidget(reset);
        form->addRow(tr("色相宽度"),box);
        m_rows[HueWidth]={spin,slider,reset,kRangeInitial[0]};
    }
    {
        QHBoxLayout *box=new QHBoxLayout;
        box->setContentsMargins(0,0,0,0);
        box->setSpacing(4);
        QDoubleSpinBox *spin=new QDoubleSpinBox(m_controls);
        spin->setObjectName(QStringLiteral("point-color-saturation-width"));
        spin->setRange(.01,1);
        spin->setSingleStep(.01);
        spin->setDecimals(2);
        spin->setValue(.25);
        spin->setToolTip(tr("饱和度宽度"));
        spin->setAccessibleName(tr("饱和度宽度"));
        QSlider *slider=new QSlider(Qt::Horizontal,m_controls);
        slider->setObjectName(QStringLiteral("slider-point-color-saturation-width"));
        slider->setRange(1,100);
        slider->setValue(25);
        slider->setToolTip(tr("饱和度宽度滑块"));
        slider->setAccessibleName(tr("饱和度宽度滑块"));
        QPushButton *reset=resetButton(m_controls,SaturationWidth,tr("饱和度宽度"));
        box->addWidget(slider,1);
        box->addWidget(spin);
        box->addWidget(reset);
        form->addRow(tr("饱和度宽度"),box);
        m_rows[SaturationWidth]={spin,slider,reset,kRangeInitial[1]};
    }
    {
        QHBoxLayout *box=new QHBoxLayout;
        box->setContentsMargins(0,0,0,0);
        box->setSpacing(4);
        QDoubleSpinBox *spin=new QDoubleSpinBox(m_controls);
        spin->setObjectName(QStringLiteral("point-color-luminance-width"));
        spin->setRange(.01,1);
        spin->setSingleStep(.01);
        spin->setDecimals(2);
        spin->setValue(.25);
        spin->setToolTip(tr("明度宽度"));
        spin->setAccessibleName(tr("明度宽度"));
        QSlider *slider=new QSlider(Qt::Horizontal,m_controls);
        slider->setObjectName(QStringLiteral("slider-point-color-luminance-width"));
        slider->setRange(1,100);
        slider->setValue(25);
        slider->setToolTip(tr("明度宽度滑块"));
        slider->setAccessibleName(tr("明度宽度滑块"));
        QPushButton *reset=resetButton(m_controls,LuminanceWidth,tr("明度宽度"));
        box->addWidget(slider,1);
        box->addWidget(spin);
        box->addWidget(reset);
        form->addRow(tr("明度宽度"),box);
        m_rows[LuminanceWidth]={spin,slider,reset,kRangeInitial[2]};
    }
    {
        QHBoxLayout *box=new QHBoxLayout;
        box->setContentsMargins(0,0,0,0);
        box->setSpacing(4);
        QDoubleSpinBox *spin=new QDoubleSpinBox(m_controls);
        spin->setObjectName(QStringLiteral("point-color-feather"));
        spin->setRange(.01,1);
        spin->setSingleStep(.01);
        spin->setDecimals(2);
        spin->setValue(.5);
        spin->setToolTip(tr("羽化"));
        spin->setAccessibleName(tr("羽化"));
        QSlider *slider=new QSlider(Qt::Horizontal,m_controls);
        slider->setObjectName(QStringLiteral("slider-point-color-feather"));
        slider->setRange(1,100);
        slider->setValue(50);
        slider->setToolTip(tr("羽化滑块"));
        slider->setAccessibleName(tr("羽化滑块"));
        QPushButton *reset=resetButton(m_controls,Feather,tr("羽化"));
        box->addWidget(slider,1);
        box->addWidget(spin);
        box->addWidget(reset);
        form->addRow(tr("羽化"),box);
        m_rows[Feather]={spin,slider,reset,kRangeInitial[3]};
    }
    layout->addWidget(m_controls);

    connect(m_picker,&QPushButton::toggled,this,&PointColorPanel::handlePickToggled);
    connect(m_delete,&QPushButton::clicked,this,&PointColorPanel::handleDelete);
    connect(m_samples,&QComboBox::currentIndexChanged,this,&PointColorPanel::handleSampleChanged);
    connect(m_rangePreview,&QCheckBox::toggled,this,&PointColorPanel::handleRangeToggled);
    for(int i=0;i<FieldCount;++i){
        Row &r=m_rows[i];
        const Field field=static_cast<Field>(i);
        // Lambdas capture only 'this' plus the value field, so the connections
        // stay valid while the widgets live exactly as long as the panel does.
        connect(r.spin,&QDoubleSpinBox::valueChanged,this,[this,field](double v){handleSpinChanged(field,v);});
        connect(r.slider,&QSlider::valueChanged,this,[this,field](int v){handleSliderChanged(field,v);});
        connect(r.slider,&QSlider::sliderReleased,this,[this,field](){handleSliderReleased(field);});
        connect(r.spin,&QDoubleSpinBox::editingFinished,this,[this,field](){handleSpinFinished(field);});
        connect(r.reset,&QPushButton::clicked,this,[this,field](){handleReset(field);});
    }
}

int PointColorPanel::selectedIndex() const { return m_selected; }

int PointColorPanel::resolvedSelection(int requested) const {
    const int count=m_points.size();
    if(count==0)return -1;
    if(requested<0)return 0;
    if(requested>=count)return count-1;
    return requested;
}

bool PointColorPanel::acceptPoints(const QVector<PointColorAdjustment> &points){
    if(!validPointColors(points))return false;
    m_points=points; // raw widget-owned copy; nothing here fabricates or samples points
    return true;
}

void PointColorPanel::setPoints(const QVector<PointColorAdjustment> &points,int selected){
    if(!validPointColors(points))return; // reject outright, keep the current list
    m_restoring=true;
    acceptPoints(points);
    m_selected=resolvedSelection(selected>=0?selected:m_selected);
    if(m_points.isEmpty())m_rangePreview->setChecked(false);
    refreshControls();
    refreshList();
    refreshReference();
    m_restoring=false; // no pointsEdited/selectedChanged/rangePreviewChanged leaves this function
}

double PointColorPanel::storedField(Field field) const {
    if(m_selected<0||m_selected>=m_points.size())return 0;
    const PointColorAdjustment &p=m_points.at(m_selected);
    switch(field){
    case HueShift:return p.hueShift;
    case SaturationShift:return p.saturationShift;
    case LuminanceShift:return p.luminanceShift;
    case HueWidth:return p.hueWidth;
    case SaturationWidth:return p.saturationWidth;
    case LuminanceWidth:return p.luminanceWidth;
    case Feather:return p.feather;
    default:break;
    }
    return 0;
}

void PointColorPanel::setStoredField(Field field,double value){
    if(m_selected<0||m_selected>=m_points.size())return;
    if(!std::isfinite(value))return;
    PointColorAdjustment &p=m_points[m_selected];
    double *target=nullptr;
    switch(field){
    case HueShift:target=&p.hueShift;break;
    case SaturationShift:target=&p.saturationShift;break;
    case LuminanceShift:target=&p.luminanceShift;break;
    case HueWidth:target=&p.hueWidth;break;
    case SaturationWidth:target=&p.saturationWidth;break;
    case LuminanceWidth:target=&p.luminanceWidth;break;
    case Feather:target=&p.feather;break;
    default:return;
    }
    if(*target==value)return;
    *target=value; // exactly the edited field; every other double stays untouched
    emit pointsEdited(m_points);
}

void PointColorPanel::syncRow(Field field,double value){
    Row &r=m_rows[field];
    QSignalBlocker spinBlock(r.spin);
    QSignalBlocker sliderBlock(r.slider);
    if(r.spin->value()!=value)r.spin->setValue(value);
    const int steps = 100; // hundredths of the field unit for every row
    const double scaled=value*steps;
    const int tick=static_cast<int>(std::lround(scaled));
    if(r.slider->value()!=tick)r.slider->setValue(tick);
}

void PointColorPanel::refreshControls(){
    const bool empty=m_selected<0;
    m_delete->setEnabled(!empty);
    m_controls->setEnabled(!empty); // range checkbox and resets share the same gate
    m_samples->setEnabled(!empty);
    m_rangePreview->setEnabled(!empty);
    for(int i=0;i<FieldCount;++i){
        if(!empty)syncRow(static_cast<Field>(i),storedField(static_cast<Field>(i)));
    }
    m_hint->setVisible(empty);
    m_samples->setVisible(!empty);
    m_reference->setVisible(!empty);
    m_controls->setVisible(!empty);
    // The picker stays available whenever the host allows it; live rendering
    // must not knock adjustment controls out from under the user.
    m_picker->setEnabled(m_pickerAvailable&&m_points.size()<maximumPointColors);
}

void PointColorPanel::refreshList(){
    QSignalBlocker block(m_samples);
    m_samples->clear();
    for(int i=0;i<m_points.size();++i)m_samples->addItem(sampleLabel(i));
    if(!m_points.isEmpty())m_samples->setCurrentIndex(m_selected); // no editFinished: not a user gesture
}

void PointColorPanel::refreshReference(){
    if(m_selected<0){
        m_reference->clear();
        return;
    }
    const PointColorAdjustment &p=m_points.at(m_selected);
    m_reference->setText(tr("参考色相 %1° · 明度 %2 · 饱和度 %3")
        .arg(QString::number(p.hue,'f',1))
        .arg(QString::number(clampUnit(p.luminance),'f',2))
        .arg(QString::number(clampUnit(p.saturation),'f',2)));
}

QString PointColorPanel::sampleLabel(int index) const {
    const PointColorAdjustment &p=m_points.at(index);
    return tr("选色点 %1 · H %2° S %3 L %4")
        .arg(index+1)
        .arg(QString::number(p.hue,'f',1))
        .arg(QString::number(clampUnit(p.saturation),'f',2))
        .arg(QString::number(clampUnit(p.luminance),'f',2));
}

void PointColorPanel::handleSpinChanged(Field field,double value){
    if(m_restoring)return;
    setStoredField(field,value);
    if(m_selected>=0)syncRow(field,storedField(field)); // only this row follows the spin
}

void PointColorPanel::handleSliderChanged(Field field,int value){
    if(m_restoring)return;
    const double step=.01; // slider units are hundredths of the field unit
    setStoredField(field,value*step);
    if(m_selected>=0)syncRow(field,storedField(field));
}

void PointColorPanel::handleSliderReleased(Field){
    if(m_restoring)return;
    emit editFinished();
}

void PointColorPanel::handleSpinFinished(Field){
    if(m_restoring)return;
    for(const auto &row:m_rows)if(row.slider->isSliderDown())return;
    emit editFinished();
}

void PointColorPanel::handleReset(Field field){
    if(m_restoring||m_selected<0)return;
    // The row initial is the documented default for that single field.
    setStoredField(field,m_rows[field].initial); // commits finished; every other field is preserved
    syncRow(field,storedField(field));
    emit editFinished();
}

void PointColorPanel::handleDelete(){
    if(m_restoring||m_selected<0||m_selected>=m_points.size())return;
    emit editFinished(); // host closes the old active gesture before the list shifts
    finishRemoval(m_selected);
}

// The combo reports its change after QComboBox::setCurrentIndex has already
// moved the current index; the model update therefore happens inside a signal
// blocker so only one selectionChanged is emitted, after the old gesture ends.
void PointColorPanel::finishRemoval(int removedIndex){
    m_points.remove(removedIndex); // this is the widget's own raw copy
    m_selected=resolvedSelection(removedIndex);
    refreshControls();
    refreshList();
    refreshReference();
    emit pointsEdited(m_points);
    if(m_rangePreview->isChecked()){
        m_rangePreview->setChecked(false); // emits rangePreviewChanged(false, selected)
    }
    if(m_selected>=0)emit selectedChanged(m_selected);
    emit editFinished();
}

void PointColorPanel::handleSampleChanged(int index){
    if(m_restoring)return;
    if(index<0){ // the host removed the selected point and resyncs through setPoints
        return;
    }
    if(index>=m_points.size()||index==m_selected)return;
    emit editFinished(); // the previous point's active gesture ends before the switch
    m_selected=index;
    refreshControls();
    refreshReference();
    emit selectedChanged(m_selected);
    if(m_rangePreview->isChecked())emit rangePreviewChanged(true,m_selected);
}

void PointColorPanel::handleRangeToggled(bool checked){
    if(m_restoring)return;
    emit rangePreviewChanged(checked,m_selected);
}

void PointColorPanel::handlePickToggled(bool checked){
    if(m_restoring)return;
    emit pickRequested(checked);
}

void PointColorPanel::setPickerAvailable(bool available){
    if(m_pickerAvailable==available)return;
    m_pickerAvailable=available;
    m_picker->setEnabled(available&&m_points.size()<maximumPointColors);
    if(!available&&m_picker->isChecked()){
        QSignalBlocker block(m_picker);
        m_picker->setChecked(false); // cancelled without a pickRequested round trip
    }
}

void PointColorPanel::setPickActive(bool active){
    if(m_picker->isChecked()==active)return;
    QSignalBlocker block(m_picker);
    m_picker->setChecked(active);
}

void PointColorPanel::setRangeActive(bool active){
    if(m_rangePreview->isChecked()==active)return;
    QSignalBlocker block(m_rangePreview);
    m_rangePreview->setChecked(active);
}


