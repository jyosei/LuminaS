#include "local_mask_options.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QSlider>
#include <QTimer>
#include <cmath>
#include <algorithm>
namespace {
double fromSlider(int tick){return std::round(std::expm1(std::log1p(16384.)*tick/1000.)*10)/10;}
int toSlider(double value){return int(std::lround(std::log1p(value)/std::log1p(16384.)*1000));}
}
LocalMaskOptions::LocalMaskOptions(QWidget *parent):QWidget(parent){
    setObjectName("local-mask-options");
    auto *form=new QFormLayout(this);form->setContentsMargins(0,0,0,0);form->setSpacing(4);
    m_spin=new QDoubleSpinBox(this);m_spin->setObjectName("local-mask-feather");m_spin->setRange(0,16384);m_spin->setDecimals(1);m_spin->setSingleStep(1);
    m_spin->setSuffix(tr(" 像素"));m_spin->setAccessibleName(tr("整体羽化，源像素"));
    m_slider=new QSlider(Qt::Horizontal,this);m_slider->setObjectName("slider-local-mask-feather");m_slider->setRange(0,1000);m_slider->setAccessibleName(tr("整体羽化"));
    auto *row=new QHBoxLayout;row->setContentsMargins(0,0,0,0);row->setSpacing(4);row->addWidget(m_slider,1);row->addWidget(m_spin);form->addRow(tr("羽化"),row);
    m_invert=new QCheckBox(tr("整体反相"),this);m_invert->setObjectName("local-mask-invert");form->addRow(QString(),m_invert);
    m_color=new QComboBox(this);m_color->setObjectName("local-mask-color");m_color->setAccessibleName(tr("蒙版覆盖色"));
    m_color->addItem(tr("紫色"),QColor(172,145,235));m_color->addItem(tr("红色"),QColor(239,90,90));m_color->addItem(tr("绿色"),QColor(75,194,128));form->addRow(tr("覆盖色"),m_color);
    m_blackWhite=new QCheckBox(tr("黑白查看蒙版"),this);m_blackWhite->setObjectName("local-mask-blackwhite");form->addRow(QString(),m_blackWhite);
    connect(m_slider,&QSlider::sliderPressed,this,[this]{++m_generation;});
    connect(m_slider,&QSlider::valueChanged,this,[this](int tick){
        if(m_syncing||!m_available||!m_hasMask)return;
        const double value=fromSlider(tick);if(m_spin->value()==value)return;
        {const QSignalBlocker blocker(m_spin);m_spin->setValue(value);}
        m_dirty=true;++m_generation;emit featherEdited(value);if(!m_slider->isSliderDown())scheduleFinish();
    });
    connect(m_slider,&QSlider::sliderReleased,this,&LocalMaskOptions::scheduleFinish);
    connect(m_spin,&QDoubleSpinBox::valueChanged,this,[this](double value){
        if(m_syncing||!m_available||!m_hasMask)return;
        {const QSignalBlocker blocker(m_slider);m_slider->setValue(toSlider(value));}
        m_dirty=true;++m_generation;emit featherEdited(value);
    });
    connect(m_spin,&QDoubleSpinBox::editingFinished,this,&LocalMaskOptions::scheduleFinish);
    connect(m_invert,&QCheckBox::toggled,this,[this](bool value){if(!m_syncing&&m_available&&m_hasMask){invalidateFinish();emit invertRequested(value);}});
    connect(m_color,&QComboBox::currentIndexChanged,this,[this](int index){if(!m_syncing&&m_available&&m_hasMask&&index>=0){const auto color=m_color->itemData(index).value<QColor>();emit colorSelected(color);}});
    connect(m_blackWhite,&QCheckBox::toggled,this,[this](bool value){if(!m_syncing&&m_available&&m_hasMask)emit blackWhiteToggled(value);});
    refreshEnabled();
}
void LocalMaskOptions::invalidateFinish(){++m_generation;m_dirty=false;}
void LocalMaskOptions::setMask(const LocalEdit::Mask *mask,const QString &owner){
    if(owner!=m_owner||!mask){invalidateFinish();m_owner=owner;}
    m_syncing=true;m_hasMask=bool(mask);
    {const QSignalBlocker spin(m_spin),slider(m_slider),inverted(m_invert);
        const double feather=mask&&std::isfinite(mask->feather)?std::clamp(mask->feather,0.,16384.):0;
        m_spin->setValue(feather);m_slider->setValue(toSlider(feather));m_invert->setChecked(mask&&mask->invert);}
    m_syncing=false;refreshEnabled();
}
void LocalMaskOptions::setAvailable(bool value){if(!value)invalidateFinish();m_available=value;refreshEnabled();}
void LocalMaskOptions::setView(QColor color,bool blackWhite){
    const QSignalBlocker colors(m_color),bw(m_blackWhite);
    for(int i=0;i<m_color->count();++i)if(m_color->itemData(i).value<QColor>()==color){m_color->setCurrentIndex(i);break;}
    m_blackWhite->setChecked(blackWhite);
}
void LocalMaskOptions::refreshEnabled(){QWidget *widgets[]{m_spin,m_slider,m_invert,m_color,m_blackWhite};for(auto *w:widgets)w->setEnabled(m_available&&m_hasMask);}
void LocalMaskOptions::scheduleFinish(){
    if(!m_dirty||!m_available||!m_hasMask||m_slider->isSliderDown())return;
    const auto generation=m_generation;
    QTimer::singleShot(0,this,[this,generation]{if(generation!=m_generation||!m_dirty||!m_available||!m_hasMask||m_slider->isSliderDown())return;m_dirty=false;emit featherFinished();});
}
