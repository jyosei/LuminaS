#include "window.h"
#include "ui_primitives.h"
#include <QFormLayout>
#include <QHBoxLayout>
#include <QSlider>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <cmath>

// Window::perspectiveHorizontal/Vertical controller. This unit only wires two
// bounded tilt controls to the live recipe: geometry stays with the render
// layer, commits stay with endControlEdit()/history, and no algorithm, preset
// or history clone is introduced here.

void Window::setupPerspectiveControls(QFormLayout *form){
    if(!form)return; // setup timing guard
    // Separate declarations: each declarator needs its own type, and vertical
    // is assigned first so no spin signal can reach an uninitialized member.
    auto *horizontalRow=new QWidget;
    auto *verticalRow=new QWidget;
    auto *horizontalLine=new QHBoxLayout(horizontalRow);
    auto *verticalLine=new QHBoxLayout(verticalRow);
    horizontalLine->setContentsMargins(0,0,0,0);verticalLine->setContentsMargins(0,0,0,0);
    auto makeSpin=[this](QDoubleSpinBox *&spin,const QString &object){
        spin=new QDoubleSpinBox;spin->setObjectName(object);spin->setRange(-.3,.3);spin->setDecimals(3);spin->setSingleStep(.01);spin->setValue(0);spin->setKeyboardTracking(false);return spin;
    };
    auto *verticalSpin=makeSpin(perspectiveVertical,QStringLiteral("perspective-vertical"));
    auto *verticalSlider=new QSlider(Qt::Horizontal);verticalSlider->setObjectName("slider-perspective-vertical");verticalSlider->setRange(-300,300);verticalSlider->setValue(0);verticalSlider->setAccessibleName(QStringLiteral("垂直透视"));
    auto *horizontalSpin=makeSpin(perspectiveHorizontal,QStringLiteral("perspective-horizontal"));
    auto *horizontalSlider=new QSlider(Qt::Horizontal);horizontalSlider->setObjectName("slider-perspective-horizontal");horizontalSlider->setRange(-300,300);horizontalSlider->setValue(0);horizontalSlider->setAccessibleName(QStringLiteral("水平透视"));
    auto add=[&](QHBoxLayout *targetLine,QWidget *row,const QString &name,QDoubleSpinBox *spin,QSlider *slider){
        targetLine->addWidget(slider,1);targetLine->addWidget(spin);
        auto *reset=new QPushButton;reset->setIcon(Ui::icon(Ui::Glyph::Reset));reset->setIconSize({14,14});reset->setFixedWidth(25);
        reset->setAccessibleName(name+QStringLiteral("透视复位"));reset->setToolTip(QStringLiteral("复位此项透视"));reset->setObjectName("reset-"+spin->objectName());targetLine->addWidget(reset);
        connect(reset,&QPushButton::clicked,this,[this,spin]{endControlEdit();spin->setValue(0);endControlEdit();});
        connect(slider,&QSlider::valueChanged,this,[spin](int value){spin->setValue(value/1000.);});
        connect(slider,&QSlider::sliderReleased,this,&Window::endControlEdit);
        connect(spin,&QDoubleSpinBox::valueChanged,this,[this,spin,slider](double value){
            {const QSignalBlocker block(slider);slider->setValue(int(std::lround(value*1000.)));}
            // Own control only: editControls() must never be given a pointer to
            // either precision twin, so both doubles keep their exact values.
            if(restoring||current<0||comparing||cropSelecting)return;
            double &target=spin==perspectiveHorizontal?this->live.perspective.rx():this->live.perspective.ry();
            if(target==value)return;
            target=value;requestEdit();
        });
        connect(spin,&QDoubleSpinBox::editingFinished,this,[this]{for(auto *bar:editPanel->findChildren<QSlider *>())if(bar->isSliderDown())return;endControlEdit();});
        auto *label=new QLabel(name);label->setObjectName("label-"+spin->objectName());
        // Slider first, then value, then reset: the existing panel row order.
        form->addRow(label,row);
    };
    add(horizontalLine,horizontalRow,QStringLiteral("水平透视"),horizontalSpin,horizontalSlider);
    add(verticalLine,verticalRow,QStringLiteral("垂直透视"),verticalSpin,verticalSlider);
}

void Window::syncPerspectiveControls(){
    if(!perspectiveHorizontal||!perspectiveVertical)return;
    const bool wasRestoring=restoring;
    restoring=true; // user callbacks must not react to this programmatic pass
    const QSignalBlocker horizontalBlock(perspectiveHorizontal),verticalBlock(perspectiveVertical);
    const QSignalBlocker horizontalBarBlock(editPanel->findChild<QSlider *>("slider-perspective-horizontal"));
    const QSignalBlocker verticalBarBlock(editPanel->findChild<QSlider *>("slider-perspective-vertical"));
    // Display rounding only: live.perspective keeps its exact value because no
    // control signal can travel back into the recipe during this pass.
    perspectiveHorizontal->setValue(live.perspective.x());perspectiveVertical->setValue(live.perspective.y());
    if(auto *slider=editPanel->findChild<QSlider *>("slider-perspective-horizontal"))slider->setValue(int(std::lround(live.perspective.x()*1000.)));
    if(auto *slider=editPanel->findChild<QSlider *>("slider-perspective-vertical"))slider->setValue(int(std::lround(live.perspective.y()*1000.)));
    restoring=wasRestoring; // no requestEdit()/endControlEdit(): no gesture is emitted
}
