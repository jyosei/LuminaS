#include "window.h"
#include "ui_primitives.h"
#include <QFormLayout>
#include <QHBoxLayout>
#include <QSlider>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <cmath>

// Window::defringe* controllers. This unit only wires the seven bounded
// DefringeSettings fields to live.defringe: the fringe model, the hue window
// maths and the source support stay with the render layer, commits stay with
// endControlEdit()/history, and no algorithm, preset or history clone is
// introduced here. Each control writes exactly one field of live.defringe, so a
// reset never disturbs the sibling values.
namespace {
constexpr int defringeFineScale=1000; // amounts and radius
constexpr int defringeHueScale=100;   // hue degrees and hue half-widths

// One row descriptor per control; the widget pointers themselves live on the
// Window, so two instances never alias each other.
struct DefringeRow {
    const char *object;
    const char *label;
    int scale;
    double minimum,maximum,step,initial;
    double DefringeSettings::*member;
};

// The seven rows in the shipped card order: purple, then green, then radius.
const DefringeRow defringeRows[]={
    {"defringe-purple-amount","紫边强度",defringeFineScale,0.,1.,.05,0.,&DefringeSettings::purpleAmount},
    {"defringe-purple-hue","紫色色相",defringeHueScale,0.,360.,1.,300.,&DefringeSettings::purpleHue},
    {"defringe-purple-width","紫色色相半宽",defringeHueScale,5.,90.,1.,50.,&DefringeSettings::purpleWidth},
    {"defringe-green-amount","绿边强度",defringeFineScale,0.,1.,.05,0.,&DefringeSettings::greenAmount},
    {"defringe-green-hue","绿色色相",defringeHueScale,0.,360.,1.,115.,&DefringeSettings::greenHue},
    {"defringe-green-width","绿色色相半宽",defringeHueScale,5.,90.,1.,45.,&DefringeSettings::greenWidth},
    {"defringe-radius","边缘半径",defringeFineScale,1.,20.,1.,3.,&DefringeSettings::radius},
};

QString defringeObject(const DefringeRow &row){return QString::fromLatin1(row.object);}
QString defringeSliderObject(const DefringeRow &row){return QStringLiteral("slider-")+defringeObject(row);}
}

void Window::setupDefringeControls(QFormLayout *form){
    if(!form)return; // setup timing guard
    QDoubleSpinBox **owned[]={&defringePurpleAmount,&defringePurpleHue,&defringePurpleWidth,
                              &defringeGreenAmount,&defringeGreenHue,&defringeGreenWidth,&defringeRadius};
    for(int index=0;index<7;++index){
        const DefringeRow &row=defringeRows[index];
        auto *field=new QDoubleSpinBox;
        field->setObjectName(defringeObject(row));
        field->setRange(row.minimum,row.maximum);
        field->setSingleStep(row.step);
        // Three decimals: the hue and half-width fields carry integral degrees,
        // but the shared precision keeps typed values exact and the display
        // honest, matching the existing slider cards.
        field->setDecimals(3);
        field->setValue(row.initial);
        field->setKeyboardTracking(false);
        field->setAccessibleName(QString::fromUtf8("紫绿边去色 ")+QString::fromUtf8(row.label));
        auto *lineRow=new QWidget;
        auto *line=new QHBoxLayout(lineRow);
        line->setContentsMargins(0,0,0,0);
        auto *bar=new QSlider(Qt::Horizontal);
        bar->setObjectName(defringeSliderObject(row));
        bar->setRange(int(std::lround(row.minimum*row.scale)),int(std::lround(row.maximum*row.scale)));
        bar->setValue(int(std::lround(row.initial*row.scale)));
        bar->setAccessibleName(field->accessibleName());
        line->addWidget(bar,1);
        line->addWidget(field);
        auto *resetButton=new QPushButton;
        resetButton->setIcon(Ui::icon(Ui::Glyph::Reset));
        resetButton->setIconSize({14,14});
        resetButton->setFixedWidth(25);
        resetButton->setAccessibleName(QString::fromUtf8(row.label)+QString::fromUtf8("复位"));
        resetButton->setToolTip(QString::fromUtf8("复位此项，保留其他去色参数"));
        resetButton->setObjectName(QStringLiteral("reset-")+defringeObject(row));
        line->addWidget(resetButton);
        auto *label=new QLabel(QString::fromUtf8(row.label));
        label->setObjectName(QStringLiteral("label-")+defringeObject(row));
        form->addRow(label,lineRow);
        *owned[index]=field;
        const auto member=row.member;
        const int scale=row.scale;
        const double initial=row.initial;
        // Slider first: it also mirrors the shown value, which would otherwise
        // stay stale because the spin handler below blocks its own echo.
        connect(bar,&QSlider::valueChanged,this,[this,field,member,scale](int value){
            const double amount=value/double(scale);
            {
                const QSignalBlocker block(field);
                field->setValue(amount);
            }
            if(restoring||current<0||comparing||cropSelecting)return;
            double &target=live.defringe.*member;
            if(target==amount)return;
            target=amount;requestEdit();
        });
        connect(bar,&QSlider::sliderReleased,this,&Window::endControlEdit);
        connect(field,&QDoubleSpinBox::valueChanged,this,[this,field,bar,member,scale](double value){
            {
                const QSignalBlocker block(bar);
                bar->setValue(int(std::lround(value*scale)));
            }
            if(restoring||current<0||comparing||cropSelecting)return;
            // Own field only: the other six parameters keep their exact values.
            double &target=live.defringe.*member;
            if(target==value)return;
            target=value;requestEdit();
        });
        connect(field,&QDoubleSpinBox::editingFinished,this,[this]{for(auto *other:editPanel->findChildren<QSlider *>())if(other->isSliderDown())return;endControlEdit();});
        // Reset brackets the programmatic change with a commit on both sides so
        // the restore is a finished gesture, and it writes only this default.
        connect(resetButton,&QPushButton::clicked,this,[this,field,initial]{
            endControlEdit();
            field->setValue(initial);
            endControlEdit();
        });
    }
}

void Window::syncDefringeControls(){
    if(!defringePurpleAmount)return;
    const bool wasRestoring=restoring;
    restoring=true; // user callbacks must not react to this programmatic pass
    QDoubleSpinBox *owned[]={defringePurpleAmount,defringePurpleHue,defringePurpleWidth,
                               defringeGreenAmount,defringeGreenHue,defringeGreenWidth,defringeRadius};
    for(int index=0;index<7;++index){
        const DefringeRow &row=defringeRows[index];
        QDoubleSpinBox *field=owned[index];
        if(!field)continue;
        auto *bar=editPanel->findChild<QSlider *>(defringeSliderObject(row));
        // Display rounding only: live.defringe keeps its exact recipe values
        // because every control signal is blocked during this pass.
        const QSignalBlocker fieldBlock(field);
        const QSignalBlocker barBlock(bar);
        field->setValue(live.defringe.*row.member);
        if(bar)bar->setValue(int(std::lround((live.defringe.*row.member)*row.scale)));
    }
    restoring=wasRestoring; // no requestEdit()/endControlEdit(): no gesture is emitted
}
