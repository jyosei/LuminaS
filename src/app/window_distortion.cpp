#include "window.h"
#include "ui_primitives.h"
#include <QFormLayout>
#include <QHBoxLayout>
#include <QSlider>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <cmath>

// Window::distortionControl controller. This unit only wires one bounded
// coefficient to live.distortion: the radial mapping stays in
// RadialDistortion/render, commits stay with endControlEdit()/history, and no
// calibration profile, algorithm or history clone is introduced here.
namespace {
constexpr int distortionSliderScale=1000; // -400..400 maps to -0.4..0.4

// QDoubleSpinBox has no QAbstractSlider base, so the shared "any slider still
// down" predicate used by the existing editingFinished guards cannot see it;
// the editingFinished handler below goes through this explicit list instead.
bool anyDistortionSliderDown(QWidget *panel){
    for(auto *bar:panel->findChildren<QSlider *>())if(bar->isSliderDown())return true;
    return false;
}
}

void Window::setupDistortionControls(QFormLayout *form){
    if(!form)return; // setup timing guard
    // Object name and parent are set before anything can emit: the panel lookup
    // below is only valid once the parent chain reaches the adjustment panel.
    distortionControl=new QDoubleSpinBox;
    distortionControl->setObjectName(QStringLiteral("distortion"));
    auto *panel=qobject_cast<QWidget *>(form->parentWidget());
    if(panel)distortionControl->setParent(panel);
    distortionControl->setRange(-.4,.4);
    distortionControl->setDecimals(3);
    distortionControl->setSingleStep(.01);
    distortionControl->setValue(0);
    distortionControl->setKeyboardTracking(false);
    distortionControl->setAccessibleName(QStringLiteral("手动畸变校正"));
    distortionControl->setToolTip(QStringLiteral("手动调整桶形/枕形变形；透明边缘可再裁剪，尚无自动镜头标定。"));

    auto *row=new QWidget;
    auto *line=new QHBoxLayout(row);
    line->setContentsMargins(0,0,0,0);
    auto *slider=new QSlider(Qt::Horizontal);
    slider->setObjectName(QStringLiteral("slider-distortion"));
    slider->setRange(-400,400);
    slider->setValue(0);
    slider->setAccessibleName(QStringLiteral("手动畸变校正"));
    // Slider, then value, then reset: the existing panel row order.
    line->addWidget(slider,1);
    line->addWidget(distortionControl);
    auto *reset=new QPushButton;
    reset->setIcon(Ui::icon(Ui::Glyph::Reset));
    reset->setIconSize({14,14});
    reset->setFixedWidth(25);
    reset->setAccessibleName(QStringLiteral("手动畸变校正复位"));
    reset->setToolTip(QStringLiteral("复位此项校正"));
    reset->setObjectName(QStringLiteral("reset-distortion"));
    line->addWidget(reset);
    auto *label=new QLabel(QStringLiteral("畸变量"));
    label->setObjectName(QStringLiteral("label-distortion"));
    form->addRow(label,row);

    connect(slider,&QSlider::valueChanged,this,[this](int value){
        if(restoring||current<0||comparing||cropSelecting)return;
        const double amount=value/double(distortionSliderScale);
        // The slider is the only writer that would otherwise leave the visible
        // field stale: the spin handler below already mirrors back into it.
        {
            const QSignalBlocker block(distortionControl);
            distortionControl->setValue(amount);
        }
        if(live.distortion==amount)return;
        live.distortion=amount;requestEdit();
    });
    connect(slider,&QSlider::sliderReleased,this,&Window::endControlEdit);
    connect(distortionControl,&QDoubleSpinBox::valueChanged,this,[this](double value){
        if(auto *bar=editPanel?editPanel->findChild<QSlider *>(QStringLiteral("slider-distortion")):nullptr){
            const QSignalBlocker block(bar);
            bar->setValue(int(std::lround(value*distortionSliderScale)));
        }
        if(restoring||current<0||comparing||cropSelecting)return;
        if(live.distortion==value)return;
        live.distortion=value;requestEdit();
    });
    connect(distortionControl,&QDoubleSpinBox::editingFinished,this,[this]{
        if(anyDistortionSliderDown(editPanel))return;
        endControlEdit();
    });
    // Reset brackets the programmatic change with a commit on both sides so the
    // field edit is a finished gesture before and after it is zeroed.
    connect(reset,&QPushButton::clicked,this,[this]{
        endControlEdit();
        distortionControl->setValue(0);
        endControlEdit();
    });
}

void Window::syncDistortionControls(){
    if(!distortionControl)return;
    const bool wasRestoring=restoring;
    restoring=true; // user callbacks must not react to this programmatic pass
    const QSignalBlocker spinBlock(distortionControl);
    const QSignalBlocker sliderBlock(editPanel->findChild<QSlider *>(QStringLiteral("slider-distortion")));
    // Display rounding only: live.distortion keeps its exact value because no
    // control signal can travel back into the recipe during this pass.
    distortionControl->setValue(live.distortion);
    if(auto *slider=editPanel->findChild<QSlider *>(QStringLiteral("slider-distortion")))
        slider->setValue(int(std::lround(live.distortion*distortionSliderScale)));
    restoring=wasRestoring; // no requestEdit()/endControlEdit(): no gesture is emitted
}
