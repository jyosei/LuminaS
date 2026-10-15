#include "window.h"
#include "ui_primitives.h"
#include <QFormLayout>
#include <QHBoxLayout>
#include <QSlider>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <cmath>

// Window::chromaticRed/chromaticBlue controllers. This unit only wires the two
// bounded original-pixel corner-displacement fields to
// live.chromaticAberration: the resampling model stays with the render layer,
// commits stay with endControlEdit()/history, and no algorithm, preset or
// history clone is introduced here.
namespace {
constexpr int chromaticSliderScale=1000; // -10000..10000 maps to -10.0..10.0 px
}

// Shared row builder: the horizontal slider, the three-decimal value field and
// the per-component reset share one line, matching the existing panel row order
// (slider, value, reset) used by the perspective and lens-shading cards.
void Window::setupChromaticAberrationControls(QFormLayout *form){
    if(!form)return; // setup timing guard
    auto makeSpin=[this](QDoubleSpinBox *&spin,const QString &object){
        spin=new QDoubleSpinBox;spin->setObjectName(object);spin->setRange(-10,10);
        spin->setDecimals(3);spin->setSingleStep(.1);spin->setValue(0);
        spin->setKeyboardTracking(false);
        spin->setAccessibleName(QStringLiteral("手动横向色差"));
        return spin;
    };
    // Separate declarations: each declarator needs its own type, and blue is
    // assigned first so no spin signal can reach an uninitialized member.
    auto *blueRow=new QWidget;
    auto *blueLine=new QHBoxLayout(blueRow);
    auto *redRow=new QWidget;
    auto *redLine=new QHBoxLayout(redRow);
    blueLine->setContentsMargins(0,0,0,0);redLine->setContentsMargins(0,0,0,0);
    auto *blueSpin=makeSpin(chromaticBlue,QStringLiteral("ca-blue"));
    auto *blueSlider=new QSlider(Qt::Horizontal);
    blueSlider->setObjectName(QStringLiteral("slider-ca-blue"));
    blueSlider->setRange(-10000,10000);blueSlider->setValue(0);
    blueSlider->setAccessibleName(QStringLiteral("手动横向色差"));
    blueSlider->setToolTip(QStringLiteral("蓝/黄边：原图像素尺度的角部位移"));
    auto *redSpin=makeSpin(chromaticRed,QStringLiteral("ca-red"));
    auto *redSlider=new QSlider(Qt::Horizontal);
    redSlider->setObjectName(QStringLiteral("slider-ca-red"));
    redSlider->setRange(-10000,10000);redSlider->setValue(0);
    redSlider->setAccessibleName(QStringLiteral("手动横向色差"));
    redSlider->setToolTip(QStringLiteral("红/青边：原图像素尺度的角部位移"));
    auto add=[&](QHBoxLayout *line,QWidget *row,const QString &name,QDoubleSpinBox *spin,QSlider *slider){
        spin->setAccessibleName(QStringLiteral("手动横向色差 ")+name);
        slider->setAccessibleName(QStringLiteral("手动横向色差 ")+name);
        line->addWidget(slider,1);line->addWidget(spin);
        auto *reset=new QPushButton;reset->setIcon(Ui::icon(Ui::Glyph::Reset));reset->setIconSize({14,14});reset->setFixedWidth(25);
        reset->setAccessibleName(QStringLiteral("手动横向色差")+name+QStringLiteral("复位"));
        reset->setToolTip(QStringLiteral("复位此项位移"));
        reset->setObjectName(QStringLiteral("reset-")+spin->objectName());
        line->addWidget(reset);
        auto *label=new QLabel(name);label->setObjectName(QStringLiteral("label-")+spin->objectName());
        form->addRow(label,row);
        connect(slider,&QSlider::valueChanged,this,[this,spin](int value){
            const double amount=value/double(chromaticSliderScale);
            // The slider is the only writer that would otherwise leave the
            // visible field stale: the spin handler mirrors back already.
            {
                const QSignalBlocker block(spin);
                spin->setValue(amount);
            }
            if(restoring||current<0||comparing||cropSelecting)return;
            double &target=spin==chromaticRed?live.chromaticAberration.rx():live.chromaticAberration.ry();
            if(target==amount)return;
            target=amount;requestEdit();
        });
        connect(slider,&QSlider::sliderReleased,this,&Window::endControlEdit);
        connect(spin,&QDoubleSpinBox::valueChanged,this,[this,spin,slider](double value){
            {
                const QSignalBlocker block(slider);
                slider->setValue(int(std::lround(value*chromaticSliderScale)));
            }
            if(restoring||current<0||comparing||cropSelecting)return;
            // Own field only: the paired component and every other precision
            // field keep their exact values.
            double &target=spin==chromaticRed?live.chromaticAberration.rx():live.chromaticAberration.ry();
            if(target==value)return;
            target=value;requestEdit();
        });
        connect(spin,&QDoubleSpinBox::editingFinished,this,[this]{for(auto *bar:editPanel->findChildren<QSlider *>())if(bar->isSliderDown())return;endControlEdit();});
        // Reset commits on both sides so the zeroing is a finished gesture, and
        // it touches only this component.
        connect(reset,&QPushButton::clicked,this,[this,spin]{
            endControlEdit();
            spin->setValue(0);
            endControlEdit();
        });
    };
    add(redLine,redRow,QStringLiteral("红/青边"),redSpin,redSlider);
    add(blueLine,blueRow,QStringLiteral("蓝/黄边"),blueSpin,blueSlider);
}

void Window::syncChromaticAberrationControls(){
    if(!chromaticRed||!chromaticBlue)return;
    const bool wasRestoring=restoring;
    restoring=true; // user callbacks must not react to this programmatic pass
    const QSignalBlocker redSpinBlock(chromaticRed),blueSpinBlock(chromaticBlue);
    const QSignalBlocker redBarBlock(editPanel->findChild<QSlider *>(QStringLiteral("slider-ca-red")));
    const QSignalBlocker blueBarBlock(editPanel->findChild<QSlider *>(QStringLiteral("slider-ca-blue")));
    // Display rounding only: live.chromaticAberration keeps its exact values
    // because no control signal can travel back into the recipe here.
    chromaticRed->setValue(live.chromaticAberration.x());
    chromaticBlue->setValue(live.chromaticAberration.y());
    if(auto *slider=editPanel->findChild<QSlider *>(QStringLiteral("slider-ca-red")))
        slider->setValue(int(std::lround(live.chromaticAberration.x()*chromaticSliderScale)));
    if(auto *slider=editPanel->findChild<QSlider *>(QStringLiteral("slider-ca-blue")))
        slider->setValue(int(std::lround(live.chromaticAberration.y()*chromaticSliderScale)));
    restoring=wasRestoring; // no requestEdit()/endControlEdit(): no gesture is emitted
}
