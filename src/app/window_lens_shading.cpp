#include "window.h"
#include "ui_primitives.h"
#include <QFormLayout>
#include <QHBoxLayout>
#include <QSlider>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <cmath>

// Window::lensShadingControl controller. This unit only wires one bounded EV
// field to live.lensShading: the radial model stays in LensShading/render,
// commits stay with endControlEdit()/history, and nothing here is cloned.
namespace {
constexpr int lensSliderScale=1000; // -2000..2000 maps to -2.0..2.0 EV

// QDoubleSpinBox has no QAbstractSlider base, so the "any slider still down"
// predicate used by the existing editingFinished guards cannot see it; the
// editingFinished handler below goes through this explicit list instead.
bool anyShadingSliderDown(QWidget *panel){
    for(auto *bar:panel->findChildren<QSlider *>())if(bar->isSliderDown())return true;
    return false;
}
}

void Window::setupLensShadingControls(QFormLayout *form){
    if(!form)return; // setup timing guard
    // Object name and parent are set before anything can emit: the panel lookup
    // below is only valid once the parent chain reaches the adjustment panel.
    lensShadingControl=new QDoubleSpinBox;
    lensShadingControl->setObjectName(QStringLiteral("lens-shading"));
    auto *panel=qobject_cast<QWidget *>(form->parentWidget());
    if(panel)lensShadingControl->setParent(panel);
    lensShadingControl->setRange(-2,2);
    lensShadingControl->setDecimals(3);
    lensShadingControl->setSingleStep(.05);
    lensShadingControl->setValue(0);
    lensShadingControl->setKeyboardTracking(false);
    lensShadingControl->setAccessibleName(QStringLiteral("镜头暗角校正"));
    lensShadingControl->setToolTip(QStringLiteral("按平方径向模型校正源平面亮度；中心为 0，角部为设定 EV"));

    auto *row=new QWidget;
    auto *line=new QHBoxLayout(row);
    line->setContentsMargins(0,0,0,0);
    auto *slider=new QSlider(Qt::Horizontal);
    slider->setObjectName(QStringLiteral("slider-lens-shading"));
    slider->setRange(-2000,2000);
    slider->setValue(0);
    slider->setAccessibleName(QStringLiteral("镜头暗角校正"));
    // Slider, then value, then reset: the existing panel row order.
    line->addWidget(slider,1);
    line->addWidget(lensShadingControl);
    auto *reset=new QPushButton;
    reset->setIcon(Ui::icon(Ui::Glyph::Reset));
    reset->setIconSize({14,14});
    reset->setFixedWidth(25);
    reset->setAccessibleName(QStringLiteral("镜头暗角校正复位"));
    reset->setToolTip(QStringLiteral("复位此项校正"));
    reset->setObjectName(QStringLiteral("reset-lens-shading"));
    line->addWidget(reset);
    auto *label=new QLabel(QStringLiteral("暗角校正 EV"));
    label->setObjectName(QStringLiteral("label-lens-shading"));
    form->addRow(label,row);

    connect(slider,&QSlider::valueChanged,this,[this](int value){
        if(restoring||current<0||comparing||cropSelecting)return;
        const double amount=value/double(lensSliderScale);
        // The slider is the only writer that would otherwise leave the visible
        // field stale: the spin handler below already mirrors back into it.
        {
            const QSignalBlocker block(lensShadingControl);
            lensShadingControl->setValue(amount);
        }
        if(live.lensShading==amount)return;
        live.lensShading=amount;requestEdit();
    });
    connect(slider,&QSlider::sliderReleased,this,&Window::endControlEdit);
    connect(lensShadingControl,&QDoubleSpinBox::valueChanged,this,[this](double value){
        if(auto *bar=editPanel?editPanel->findChild<QSlider *>(QStringLiteral("slider-lens-shading")):nullptr){
            const QSignalBlocker block(bar);
            bar->setValue(int(std::lround(value*lensSliderScale)));
        }
        if(restoring||current<0||comparing||cropSelecting)return;
        if(live.lensShading==value)return;
        live.lensShading=value;requestEdit();
    });
    connect(lensShadingControl,&QDoubleSpinBox::editingFinished,this,[this]{
        if(anyShadingSliderDown(editPanel))return;
        endControlEdit();
    });
    // Reset brackets the programmatic change with a commit on both sides so the
    // field edit is a finished gesture before and after it is zeroed.
    connect(reset,&QPushButton::clicked,this,[this]{
        endControlEdit();
        lensShadingControl->setValue(0);
        endControlEdit();
    });
}

void Window::syncLensShadingControls(){
    if(!lensShadingControl)return;
    const bool wasRestoring=restoring;
    restoring=true; // user callbacks must not react to this programmatic pass
    const QSignalBlocker spinBlock(lensShadingControl);
    const QSignalBlocker sliderBlock(editPanel->findChild<QSlider *>(QStringLiteral("slider-lens-shading")));
    // Display rounding only: live.lensShading keeps its exact value because no
    // control signal can travel back into the recipe during this pass.
    lensShadingControl->setValue(live.lensShading);
    if(auto *slider=editPanel->findChild<QSlider *>(QStringLiteral("slider-lens-shading")))
        slider->setValue(int(std::lround(live.lensShading*lensSliderScale)));
    restoring=wasRestoring; // no requestEdit()/endControlEdit(): no gesture is emitted
}
