#include "local_gradient_controls.h"
#include <QComboBox>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

LocalGradientControls::LocalGradientControls(QWidget *parent):QWidget(parent){
    setObjectName("local-gradient-controls");
    auto *layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);layout->setSpacing(4);
    auto *tools=new QHBoxLayout;tools->setSpacing(4);
    m_linear=new QPushButton(tr("线性"),this);m_linear->setObjectName("local-gradient-linear");
    m_radial=new QPushButton(tr("径向"),this);m_radial->setObjectName("local-gradient-radial");
    for(auto *button:{m_linear,m_radial}){button->setCheckable(true);tools->addWidget(button);}
    tools->addStretch();layout->addLayout(tools);
    auto *components=new QHBoxLayout;components->setSpacing(4);
    m_components=new QComboBox(this);m_components->setObjectName("local-mask-components");
    m_components->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_components->setMinimumContentsLength(10);
    m_components->setAccessibleName(tr("蒙版组成项"));
    m_remove=new QPushButton(tr("移除"),this);m_remove->setObjectName("local-mask-remove");
    components->addWidget(m_components,1);components->addWidget(m_remove);layout->addLayout(components);
    connect(m_linear,&QPushButton::clicked,this,[this]{requestTool(m_kind==1?0:1);});
    connect(m_radial,&QPushButton::clicked,this,[this]{requestTool(m_kind==2?0:2);});
    connect(m_components,&QComboBox::currentIndexChanged,this,[this]{
        refreshControls();const QString id=m_components->currentData().toString();
        if(m_available&&m_hasLayer)emit componentSelected(id);
    });
    connect(m_remove,&QPushButton::clicked,this,[this]{
        const QString id=m_components->currentData().toString();
        if(m_available&&m_hasLayer&&!id.isEmpty())emit componentRemovalRequested(id);
    });
    setDocument({},{});
}
void LocalGradientControls::setDocument(LocalEdit::DocumentSnapshot snapshot,const QString &layerId,const QString &componentId){
    const QSignalBlocker block(m_components);
    m_hasLayer=false;m_components->clear();m_components->addItem(tr("选择蒙版组成项"),QString());
    if(snapshot)for(const auto &layer:snapshot->layers){
        if(layer.id!=layerId)continue;
        m_hasLayer=true;int number=0;
        for(const auto &component:layer.mask.components){
            const auto type=std::holds_alternative<LocalEdit::BrushGeometry>(component.geometry)?tr("画笔"):
                std::holds_alternative<LocalEdit::LinearGeometry>(component.geometry)?tr("线性"):tr("径向");
            const auto operation=component.operation==LocalEdit::MaskOperation::Subtract?tr("减去"):tr("添加");
            m_components->addItem(tr("%1 · %2 %3").arg(++number).arg(type,operation),component.id);
        }
        break;
    }
    const int index=m_components->findData(componentId);
    m_components->setCurrentIndex(index<0?0:index);refreshControls();
}
void LocalGradientControls::setAvailable(bool available){m_available=available;refreshControls();}
void LocalGradientControls::setActiveKind(int kind){
    m_kind=kind==1||kind==2?kind:0;
    const QSignalBlocker linear(m_linear),radial(m_radial);
    m_linear->setChecked(m_kind==1);m_radial->setChecked(m_kind==2);
}
void LocalGradientControls::requestTool(int kind){
    if(!m_available||!m_hasLayer){setActiveKind(m_kind);return;}
    setActiveKind(kind);emit toolRequested(kind);
}
void LocalGradientControls::refreshControls(){
    const bool usable=m_available&&m_hasLayer;
    m_linear->setEnabled(usable);m_radial->setEnabled(usable);m_components->setEnabled(usable);
    m_remove->setEnabled(usable&&!m_components->currentData().toString().isEmpty());
}
