#include "app/local_adjustment_panel.h"
#include <QtTest>
#include <cmath>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSignalSpy>
#include <QSlider>

using namespace LocalEdit;

namespace {
QString layerName(int index){return QStringLiteral("局部层 %1").arg(index+1);}

// One layer with every parameter field deliberately non-default, so a copy that
// loses a hidden field is detectable.
AdjustmentLayer layer(const QString &id,const QString &name,bool enabled=true,
                      double exposure=0.0,double saturation=1.0){
    AdjustmentLayer value;
    value.id=id;
    value.name=name;
    value.enabled=enabled;
    value.parameters.exposure=exposure;
    value.parameters.contrast=.25;
    value.parameters.highlights=-.4;
    value.parameters.shadows=.35;
    value.parameters.whites=-.15;
    value.parameters.blacks=.45;
    value.parameters.warmth=.3;
    value.parameters.tint=-.2;
    value.parameters.saturation=saturation;
    return value;
}

DocumentSnapshot snapshotOf(const QVector<AdjustmentLayer> &layers){
    auto document=std::make_shared<PhotoDocument>();
    document->layers=layers;
    return document;
}

// The nine local parameters in the panel's documented visual order. The index is
// reset-button numbers remain enum order, independently of this visual order.
const QStringList &fieldKeys(){
    static const QStringList keys{QStringLiteral("exposure"),QStringLiteral("contrast"),
        QStringLiteral("highlights"),QStringLiteral("shadows"),QStringLiteral("whites"),
        QStringLiteral("blacks"),QStringLiteral("warmth"),QStringLiteral("tint"),
        QStringLiteral("saturation")};
    return keys;
}
int indexOfField(const QString &key){
    if(key==QStringLiteral("exposure"))return 0;
    if(key==QStringLiteral("saturation"))return 1;
    return fieldKeys().indexOf(key)+1;
}

double defaultValueOf(const QString &key){return key==QStringLiteral("saturation")?1.0:0.0;}

// Reads one named member of a parameter value without an object-name lookup, so
// a test failure names the field that actually drifted.
double valueOf(const LocalParameters &parameters,const QString &key){
    if(key==QStringLiteral("exposure"))return parameters.exposure;
    if(key==QStringLiteral("contrast"))return parameters.contrast;
    if(key==QStringLiteral("highlights"))return parameters.highlights;
    if(key==QStringLiteral("shadows"))return parameters.shadows;
    if(key==QStringLiteral("whites"))return parameters.whites;
    if(key==QStringLiteral("blacks"))return parameters.blacks;
    if(key==QStringLiteral("warmth"))return parameters.warmth;
    if(key==QStringLiteral("tint"))return parameters.tint;
    return parameters.saturation;
}
}

class LocalAdjustmentPanelTests : public QObject {
    Q_OBJECT
private slots:
    void parameterRowsFollowPhotographicOrder(){
        LocalAdjustmentPanel panel;
        auto *spin=panel.findChild<QDoubleSpinBox *>("local-exposure");QVERIFY(spin);
        auto *form=qobject_cast<QFormLayout *>(spin->parentWidget()->layout());QVERIFY(form);
        const QStringList labels{QStringLiteral("曝光"),QStringLiteral("对比"),QStringLiteral("高光"),QStringLiteral("阴影"),
            QStringLiteral("白色"),QStringLiteral("黑色"),QStringLiteral("暖冷"),QStringLiteral("色调"),QStringLiteral("饱和度")};
        QCOMPARE(form->rowCount(),labels.size());
        for(int i=0;i<labels.size();++i){
            auto *label=qobject_cast<QLabel *>(form->itemAt(i,QFormLayout::LabelRole)->widget());QVERIFY(label);QCOMPARE(label->text(),labels[i]);
        }
    }
    void init(){
        qRegisterMetaType<LocalParameters>("LocalEdit::LocalParameters");
        qRegisterMetaType<BrushSettings>("LocalEdit::BrushSettings");
    }

    void populatedTwoLayersAndSelection();
    void refreshEmitsNoSignalsAndPreservesParameters();
    void userParameterChangeReportsLayerAndValues();
    void availabilityGatesControlsAndShowsWaitingHint();
    void brushSettingsAndClampedRadius();
    void visibilityToggleReportsStableId();
    void parameterFieldsDefaultRangeAndIsolation_data();
    void parameterFieldsDefaultRangeAndIsolation();
    void parameterResetPerField_data();
    void parameterResetPerField();
    void brushSlidersSynchronizeAndEraseActivatesBrush(){
        LocalAdjustmentPanel panel;panel.setAvailable(true);
        panel.setDocument(snapshotOf({layer("sync","同步")}));
        auto *radius=panel.findChild<QSlider *>("slider-local-radius");
        auto *hardness=panel.findChild<QSlider *>("slider-local-hardness");
        auto *strength=panel.findChild<QSlider *>("slider-local-strength");
        QVERIFY(radius&&hardness&&strength);
        radius->setValue(280);hardness->setValue(35);strength->setValue(70);
        QCOMPARE(panel.findChild<QDoubleSpinBox *>("local-radius")->value(),280.);
        QCOMPARE(panel.findChild<QDoubleSpinBox *>("local-hardness")->value(),.35);
        QCOMPARE(panel.findChild<QDoubleSpinBox *>("local-strength")->value(),.7);
        QCOMPARE(panel.brushSettings().radius,280.);
        QSignalSpy tool(&panel,&LocalAdjustmentPanel::brushToolToggled);
        panel.findChild<QPushButton *>("local-erase")->click();
        QVERIFY(panel.brushActive());QVERIFY(panel.brushSettings().subtract);QCOMPARE(tool.size(),1);
        panel.setAvailable(false);
        QVERIFY(!radius->isEnabled());QVERIFY(!hardness->isEnabled());QVERIFY(!strength->isEnabled());
        QVERIFY(!panel.findChild<QLabel *>("local-hint")->isHidden());
    }
    void keyboardSliderFinishesOneEdit(){
        LocalAdjustmentPanel panel;panel.setAvailable(true);
        panel.setDocument(snapshotOf({layer("keyboard","键盘")}));
        auto *slider=panel.findChild<QSlider *>("slider-local-exposure");QVERIFY(slider);
        QSignalSpy edited(&panel,&LocalAdjustmentPanel::parametersEdited);
        QSignalSpy finished(&panel,&LocalAdjustmentPanel::parametersFinished);
        QTest::keyClick(slider,Qt::Key_Right);
        QTRY_COMPARE(finished.size(),1);QCOMPARE(edited.size(),1);
        QCOMPARE(edited.first()[1].value<LocalParameters>().exposure,.01);
    }
    void directHostRefreshKeepsSignalArgumentsAlive(){
        LocalAdjustmentPanel panel;panel.setAvailable(true);
        const auto first=snapshotOf({layer("original","原层")});
        const auto second=snapshotOf({layer("replacement","新层")});panel.setDocument(first);
        connect(&panel,&LocalAdjustmentPanel::parametersEdited,&panel,[&]{panel.setDocument(second);});
        QSignalSpy edited(&panel,&LocalAdjustmentPanel::parametersEdited);
        panel.findChild<QDoubleSpinBox *>("local-exposure")->setValue(.5);
        QCOMPARE(edited.size(),1);QCOMPARE(edited.first()[0].toString(),QString("original"));
        QCOMPARE(edited.first()[1].value<LocalParameters>().exposure,.5);
        QCOMPARE(panel.selectedLayerId(),QString("replacement"));
    }
};

void LocalAdjustmentPanelTests::populatedTwoLayersAndSelection(){
    LocalAdjustmentPanel panel;
    panel.setAvailable(true);
    panel.setDocument(snapshotOf({layer(QStringLiteral("a"),layerName(0)),
                                  layer(QStringLiteral("b"),layerName(1))}));
    QListWidget *list=panel.findChild<QListWidget *>(QStringLiteral("local-layer-list"));
    QVERIFY(list!=nullptr);
    QCOMPARE(list->count(),2);
    QCOMPARE(list->item(0)->text(),layerName(0));
    QCOMPARE(list->item(1)->text(),layerName(1));
    QCOMPARE(list->item(0)->data(Qt::UserRole).toString(),QStringLiteral("a"));
    QCOMPARE(list->item(1)->data(Qt::UserRole).toString(),QStringLiteral("b"));
    // A populated refresh shows the first layer without reporting a selection.
    QCOMPARE(panel.selectedLayerId(),QStringLiteral("a"));
    QCOMPARE(list->currentRow(),0);

    QSignalSpy changed(&panel,&LocalAdjustmentPanel::selectedLayerChanged);
    list->setCurrentRow(1);
    QCOMPARE(changed.count(),1);
    QCOMPARE(changed.first().at(0).toString(),QStringLiteral("b"));
    QCOMPARE(panel.selectedLayerId(),QStringLiteral("b"));
}

void LocalAdjustmentPanelTests::refreshEmitsNoSignalsAndPreservesParameters(){
    LocalAdjustmentPanel panel;
    panel.setAvailable(true);
    panel.setDocument(snapshotOf({layer(QStringLiteral("a"),layerName(0),true,.5,.8),
                                  layer(QStringLiteral("b"),layerName(1),false)}));
    panel.setDocument(snapshotOf({layer(QStringLiteral("a"),layerName(0),true,.5,.8),
                                  layer(QStringLiteral("b"),layerName(1),false)}),
                      QStringLiteral("b"));
    QCOMPARE(panel.selectedLayerId(),QStringLiteral("b"));

    QSignalSpy selected(&panel,&LocalAdjustmentPanel::selectedLayerChanged);
    QSignalSpy edited(&panel,&LocalAdjustmentPanel::parametersEdited);
    QSignalSpy finished(&panel,&LocalAdjustmentPanel::parametersFinished);
    QSignalSpy visibility(&panel,&LocalAdjustmentPanel::layerVisibilityChanged);
    // A hidden field changed on the documented layer while the panel refreshes.
    panel.setDocument(snapshotOf({layer(QStringLiteral("a"),layerName(0),true,.5,.8),
                                  layer(QStringLiteral("b"),layerName(1),true,-1.25,1.5)}),
                      QStringLiteral("b"));
    QCOMPARE(selected.count(),0);
    QCOMPARE(edited.count(),0);
    QCOMPARE(finished.count(),0);
    QCOMPARE(visibility.count(),0);
    QCOMPARE(panel.selectedLayerId(),QStringLiteral("b"));

    // The refresh re-reads the visible fields of the still-valid selection.
    QDoubleSpinBox *exposure=panel.findChild<QDoubleSpinBox *>(QStringLiteral("local-exposure"));
    QDoubleSpinBox *saturation=panel.findChild<QDoubleSpinBox *>(QStringLiteral("local-saturation"));
    QVERIFY(exposure!=nullptr);
    QVERIFY(saturation!=nullptr);
    QCOMPARE(exposure->value(),-1.25);
    QCOMPARE(saturation->value(),1.5);

    // A user edit then carries every hidden field verbatim.
    QSignalSpy userEdit(&panel,&LocalAdjustmentPanel::parametersEdited);
    exposure->setValue(-2.0);
    QCOMPARE(userEdit.count(),1);
    QCOMPARE(userEdit.first().at(0).toString(),QStringLiteral("b"));
    const LocalParameters emitted=userEdit.first().at(1).value<LocalParameters>();
    QCOMPARE(emitted.exposure,-2.0);
    QCOMPARE(emitted.contrast,.25);
    QCOMPARE(emitted.highlights,-.4);
    QCOMPARE(emitted.shadows,.35);
    QCOMPARE(emitted.whites,-.15);
    QCOMPARE(emitted.blacks,.45);
    QCOMPARE(emitted.warmth,.3);
    QCOMPARE(emitted.tint,-.2);
    QCOMPARE(emitted.saturation,1.5);
}

void LocalAdjustmentPanelTests::userParameterChangeReportsLayerAndValues(){
    LocalAdjustmentPanel panel;
    panel.setAvailable(true);
    panel.setDocument(snapshotOf({layer(QStringLiteral("a"),layerName(0)),layer(QStringLiteral("b"),layerName(1))}),
                      QStringLiteral("b"));
    QSignalSpy edited(&panel,&LocalAdjustmentPanel::parametersEdited);
    QSignalSpy finished(&panel,&LocalAdjustmentPanel::parametersFinished);
    QDoubleSpinBox *saturation=panel.findChild<QDoubleSpinBox *>(QStringLiteral("local-saturation"));
    QSlider *exposureSlider=panel.findChild<QSlider *>(QStringLiteral("slider-local-exposure"));
    QSlider *saturationSlider=panel.findChild<QSlider *>(QStringLiteral("slider-local-saturation"));
    QVERIFY(saturation!=nullptr);
    QVERIFY(exposureSlider!=nullptr);
    QVERIFY(saturationSlider!=nullptr);

    exposureSlider->setSliderDown(true);
    exposureSlider->setValue(-250); // slider units are hundredths of an EV
    QCOMPARE(edited.count(),1);
    QCOMPARE(edited.first().at(0).toString(),QStringLiteral("b"));
    QCOMPARE(edited.first().at(1).value<LocalParameters>().exposure,-2.5);
    QCOMPARE(finished.count(),0); // a live drag does not finish the edit
    QDoubleSpinBox *exposure=panel.findChild<QDoubleSpinBox *>(QStringLiteral("local-exposure"));
    QCOMPARE(exposure->value(),-2.5);
    exposureSlider->setSliderDown(false);
    QCOMPARE(finished.count(),1);

    saturation->setValue(1.75);
    QCOMPARE(edited.count(),2);
    QCOMPARE(edited.last().at(1).value<LocalParameters>().saturation,1.75);
    QCOMPARE(edited.last().at(1).value<LocalParameters>().exposure,-2.5);
    QCOMPARE(saturationSlider->value(),175);
}

void LocalAdjustmentPanelTests::availabilityGatesControlsAndShowsWaitingHint(){
    LocalAdjustmentPanel panel;
    QPushButton *create=panel.findChild<QPushButton *>(QStringLiteral("local-new-brush"));
    QPushButton *brush=panel.findChild<QPushButton *>(QStringLiteral("local-brush-tool"));
    QPushButton *erase=panel.findChild<QPushButton *>(QStringLiteral("local-erase"));
    QCheckBox *overlay=panel.findChild<QCheckBox *>(QStringLiteral("local-overlay"));
    QCheckBox *visible=panel.findChild<QCheckBox *>(QStringLiteral("local-layer-visible"));
    QLabel *hint=panel.findChild<QLabel *>(QStringLiteral("local-hint"));
    QDoubleSpinBox *exposure=panel.findChild<QDoubleSpinBox *>(QStringLiteral("local-exposure"));
    QListWidget *list=panel.findChild<QListWidget *>(QStringLiteral("local-layer-list"));
    QVERIFY(create!=nullptr);
    QVERIFY(brush!=nullptr);
    QVERIFY(erase!=nullptr);
    QVERIFY(overlay!=nullptr);
    QVERIFY(visible!=nullptr);
    QVERIFY(hint!=nullptr);
    QVERIFY(exposure!=nullptr);
    QVERIFY(list!=nullptr);

    // No preview yet: only a clear waiting hint, nothing enabled.
    QVERIFY(!create->isEnabled());
    QVERIFY(!brush->isEnabled());
    QVERIFY(!erase->isEnabled());
    QVERIFY(!overlay->isEnabled());
    QVERIFY(!visible->isEnabled());
    QVERIFY(!exposure->isEnabled());
    QCOMPARE(hint->text(),QStringLiteral("等待照片预览完成"));

    // The waiting hint is the availability hint, not the empty-document hint.
    panel.setDocument(snapshotOf({layer(QStringLiteral("a"),layerName(0))}));
    QVERIFY(!create->isEnabled());
    QCOMPARE(hint->text(),QStringLiteral("等待照片预览完成"));

    panel.setAvailable(true);
    QVERIFY(create->isEnabled());
    QVERIFY(brush->isEnabled());
    QVERIFY(erase->isEnabled());
    QVERIFY(overlay->isEnabled());
    QVERIFY(visible->isEnabled());
    QVERIFY(exposure->isEnabled());
    QVERIFY(hint->isHidden()); // a selected layer hides the hint

    // Availability never shows its own hint while a layer is selectable.
    QCOMPARE(panel.selectedLayerId(),QStringLiteral("a"));

    // Without any layer the empty hint appears and only the create button stays usable.
    panel.setDocument(DocumentSnapshot());
    QVERIFY(create->isEnabled());
    QVERIFY(!brush->isEnabled());
    QVERIFY(!erase->isEnabled());
    QVERIFY(!overlay->isEnabled());
    QVERIFY(!visible->isEnabled());
    QVERIFY(!exposure->isEnabled());
    QVERIFY(!hint->isHidden()); // the empty-document hint is shown
    QCOMPARE(hint->text(),QStringLiteral("新建局部层后涂抹；全图基础固定在底部"));

    // Availability false disables every tool and parameter control again.
    panel.setAvailable(false);
    QVERIFY(!create->isEnabled());
    QVERIFY(!brush->isEnabled());
    QCOMPARE(hint->text(),QStringLiteral("等待照片预览完成"));

    QSignalSpy created(&panel,&LocalAdjustmentPanel::createBrushLayerRequested);
    panel.setAvailable(true);
    panel.setDocument(snapshotOf({layer(QStringLiteral("a"),layerName(0))}));
    QTest::mouseClick(create,Qt::LeftButton);
    QCOMPARE(created.count(),1);
}

void LocalAdjustmentPanelTests::brushSettingsAndClampedRadius(){
    LocalAdjustmentPanel panel;
    panel.setAvailable(true);
    panel.setDocument(snapshotOf({layer(QStringLiteral("a"),layerName(0))}));
    QPushButton *brush=panel.findChild<QPushButton *>(QStringLiteral("local-brush-tool"));
    QPushButton *erase=panel.findChild<QPushButton *>(QStringLiteral("local-erase"));
    QDoubleSpinBox *radius=panel.findChild<QDoubleSpinBox *>(QStringLiteral("local-radius"));
    QSlider *radiusSlider=panel.findChild<QSlider *>(QStringLiteral("slider-local-radius"));
    QVERIFY(brush!=nullptr);
    QVERIFY(erase!=nullptr);
    QVERIFY(radius!=nullptr);
    QVERIFY(radiusSlider!=nullptr);

    // Documented defaults.
    const BrushSettings defaults=panel.brushSettings();
    QCOMPARE(defaults.radius,100.0);
    QCOMPARE(defaults.hardness,.6);
    QCOMPARE(defaults.strength,1.0);
    QVERIFY(!defaults.subtract);
    QVERIFY(!panel.brushActive());

    QSignalSpy toggled(&panel,&LocalAdjustmentPanel::brushToolToggled);
    QSignalSpy settings(&panel,&LocalAdjustmentPanel::brushSettingsChanged);
    // A programmatic activation is reported through the getter only.
    panel.setBrushActive(true);
    QVERIFY(panel.brushActive());
    QVERIFY(brush->isChecked());
    QCOMPARE(toggled.count(),0);

    // Clicking the tool reports the user gesture exactly once.
    brush->setChecked(false);
    QCOMPARE(toggled.count(),1);
    QCOMPARE(toggled.first().at(0).toBool(),false);
    QVERIFY(!panel.brushActive());
    brush->setChecked(true);
    QCOMPARE(toggled.count(),2);
    QCOMPARE(toggled.last().at(0).toBool(),true);

    // Erase is the same brush with its subtract flag, not a third tool.
    erase->setChecked(true);
    QCOMPARE(settings.count(),1);
    QVERIFY(settings.first().at(0).value<BrushSettings>().subtract);
    QVERIFY(panel.brushSettings().subtract);
    QVERIFY(panel.brushActive());
    erase->setChecked(false);
    QCOMPARE(settings.count(),2);
    QVERIFY(!panel.brushSettings().subtract);

    // The radius setter clamps into [1,10000] and never emits a gesture.
    QSignalSpy radiusSignals(&panel,&LocalAdjustmentPanel::brushSettingsChanged);
    panel.setBrushRadius(0.25);
    QCOMPARE(panel.brushSettings().radius,1.0);
    QCOMPARE(radius->value(),1.0);
    QCOMPARE(radiusSlider->value(),1);
    panel.setBrushRadius(50000.0);
    QCOMPARE(panel.brushSettings().radius,10000.0);
    QCOMPARE(radius->value(),10000.0);
    QCOMPARE(radiusSlider->value(),10000);
    panel.setBrushRadius(240.0);
    QCOMPARE(panel.brushSettings().radius,240.0);
    QCOMPARE(radiusSignals.count(),0);

    // A user radius edit does report the settings.
    radius->setValue(320.0);
    QCOMPARE(radiusSignals.count(),1);
    QCOMPARE(radiusSignals.last().at(0).value<BrushSettings>().radius,320.0);
    QCOMPARE(radiusSlider->value(),320);

    // Deactivating the brush clears the subtract flag without a tool signal.
    erase->setChecked(true);
    const int before=toggled.count();
    panel.setBrushActive(false);
    QVERIFY(!panel.brushActive());
    QVERIFY(!panel.brushSettings().subtract);
    QVERIFY(!brush->isChecked());
    QVERIFY(!erase->isChecked());
    QCOMPARE(toggled.count(),before);
}

void LocalAdjustmentPanelTests::visibilityToggleReportsStableId(){
    LocalAdjustmentPanel panel;
    panel.setAvailable(true);
    panel.setDocument(snapshotOf({layer(QStringLiteral("a"),layerName(0),true),
                                  layer(QStringLiteral("b"),layerName(1),false)}),
                      QStringLiteral("b"));
    QCheckBox *visible=panel.findChild<QCheckBox *>(QStringLiteral("local-layer-visible"));
    QVERIFY(visible!=nullptr);
    QCOMPARE(panel.selectedLayerId(),QStringLiteral("b"));
    QVERIFY(!visible->isChecked()); // the selected layer starts hidden

    QSignalSpy changed(&panel,&LocalAdjustmentPanel::layerVisibilityChanged);
    visible->setChecked(true);
    QCOMPARE(changed.count(),1);
    QCOMPARE(changed.first().at(0).toString(),QStringLiteral("b"));
    QCOMPARE(changed.first().at(1).toBool(),true);

    // A host refresh of the same state stays silent and keeps the selection.
    panel.setDocument(snapshotOf({layer(QStringLiteral("a"),layerName(0),true),
                                  layer(QStringLiteral("b"),layerName(1),true)}),
                      QStringLiteral("b"));
    QCOMPARE(changed.count(),1);
    QVERIFY(visible->isChecked());
    QCOMPARE(panel.selectedLayerId(),QStringLiteral("b"));

    // The reported id follows the selection, not the row position.
    QListWidget *list=panel.findChild<QListWidget *>(QStringLiteral("local-layer-list"));
    QVERIFY(list!=nullptr);
    list->setCurrentRow(0);
    QCOMPARE(panel.selectedLayerId(),QStringLiteral("a"));
    visible->setChecked(false);
    QCOMPARE(changed.count(),2);
    QCOMPARE(changed.last().at(0).toString(),QStringLiteral("a"));
    QCOMPARE(changed.last().at(1).toBool(),false);
}

// The visual order of the nine compact parameter rows; each entry names the
// object-name key, the widget label, the documented default and the bounds.
struct FieldCase { const char *key; double defaultValue,minimum,maximum; };

static const FieldCase kFields[]={
    {"exposure",0,-5,5},{"contrast",0,-1,1},{"highlights",0,-1,1},{"shadows",0,-1,1},
    {"whites",0,-1,1},{"blacks",0,-1,1},{"warmth",0,-1,1},{"tint",0,-1,1},{"saturation",1,0,2},
};

// Every field is checked through its own widgets: the row exists, the documented
// default and range are wired to both the spin box and the hundred-step slider,
// the labels are present, and a slider drag changes exactly one field.
void LocalAdjustmentPanelTests::parameterFieldsDefaultRangeAndIsolation_data(){
    QTest::addColumn<QString>("key");
    QTest::addColumn<double>("defaultValue");
    QTest::addColumn<double>("minimum");
    QTest::addColumn<double>("maximum");
    for(const FieldCase &field:kFields)
        QTest::newRow(field.key)<<QString::fromLatin1(field.key)<<field.defaultValue
                                <<field.minimum<<field.maximum;
}

void LocalAdjustmentPanelTests::parameterFieldsDefaultRangeAndIsolation(){
    QFETCH(QString,key);
    QFETCH(double,defaultValue);
    QFETCH(double,minimum);
    QFETCH(double,maximum);

    LocalAdjustmentPanel panel;panel.setAvailable(true);
    AdjustmentLayer neutral;neutral.id="a";neutral.name=layerName(0);
    panel.setDocument(snapshotOf({neutral}));
    QDoubleSpinBox *spin=panel.findChild<QDoubleSpinBox *>("local-"+key);
    QSlider *slider=panel.findChild<QSlider *>("slider-local-"+key);
    QPushButton *reset=panel.findChild<QPushButton *>("local-reset-"+QString::number(indexOfField(key)));
    QVERIFY2(spin,"missing spin box");
    QVERIFY2(slider,"missing slider");
    QVERIFY2(reset,"missing reset button");

    // Documented default, range and shared presentation rules.
    QCOMPARE(spin->value(),defaultValue);
    QCOMPARE(spin->minimum(),minimum);
    QCOMPARE(spin->maximum(),maximum);
    QCOMPARE(spin->decimals(),2);
    QCOMPARE(slider->minimum(),static_cast<int>(std::lround(minimum*100)));
    QCOMPARE(slider->maximum(),static_cast<int>(std::lround(maximum*100)));
    QCOMPARE(slider->value(),static_cast<int>(std::lround(defaultValue*100)));
    QVERIFY(!spin->toolTip().isEmpty());
    QVERIFY(!spin->accessibleName().isEmpty());
    QVERIFY(!slider->toolTip().isEmpty());
    QVERIFY(!slider->accessibleName().isEmpty());
    QVERIFY(!reset->toolTip().isEmpty());
    QVERIFY(!reset->accessibleName().isEmpty());

    // Moving this slider reports this field once and leaves the other eight at the
    // values the layer carries (here every one of them is at its default).
    const double target=defaultValue==0?.5:1.5;
    if(target!=defaultValue){
        QSignalSpy edited(&panel,&LocalAdjustmentPanel::parametersEdited);
        QSignalSpy finished(&panel,&LocalAdjustmentPanel::parametersFinished);
        slider->setSliderDown(true);
        slider->setValue(static_cast<int>(std::lround(target*100)));
        QCOMPARE(edited.count(),1);
        const LocalParameters changed=edited.first().at(1).value<LocalParameters>();
        for(const FieldCase &other:kFields){
            const double expected=QString::fromLatin1(other.key)==key?target:other.defaultValue;
            QCOMPARE(valueOf(changed,other.key),expected);
        }
        QCOMPARE(spin->value(),target);
        slider->setSliderDown(false);
        QCOMPARE(finished.count(),1);
    }
}

// Clicking one row's reset restores that field's default only; every other field
// keeps the value the layer already carried.
void LocalAdjustmentPanelTests::parameterResetPerField_data(){
    QTest::addColumn<QString>("key");
    QTest::addColumn<double>("defaultValue");
    for(const FieldCase &field:kFields)
        QTest::newRow(field.key)<<QString::fromLatin1(field.key)<<field.defaultValue;
}

void LocalAdjustmentPanelTests::parameterResetPerField(){
    QFETCH(QString,key);
    QFETCH(double,defaultValue);

    LocalAdjustmentPanel panel;panel.setAvailable(true);
    // The layer is pushed away from every default through the public signals, so
    // a reset that wiped its neighbours would be visible.
    panel.setDocument(snapshotOf({layer(QStringLiteral("a"),layerName(0))}));
    for(const QString &other:fieldKeys()){
        QDoubleSpinBox *box=panel.findChild<QDoubleSpinBox *>("local-"+other);
        QVERIFY(box);
        box->setValue(defaultValueOf(other)==0?0.75:1.75);
    }
    QDoubleSpinBox *spin=panel.findChild<QDoubleSpinBox *>("local-"+key);
    QVERIFY(spin);
    const double moved=spin->value();
    QCOMPARE(moved,defaultValue==0?0.75:1.75);

    QSignalSpy edited(&panel,&LocalAdjustmentPanel::parametersEdited);
    QSignalSpy finished(&panel,&LocalAdjustmentPanel::parametersFinished);
    panel.findChild<QPushButton *>("local-reset-"+QString::number(indexOfField(key)))->click();
    QCOMPARE(edited.count(),1);
    QCOMPARE(finished.count(),1);
    const LocalParameters reset=edited.first().at(1).value<LocalParameters>();
    QCOMPARE(valueOf(reset,key),defaultValue);
    QCOMPARE(spin->value(),defaultValue);
    for(const FieldCase &other:kFields){
        if(QString::fromLatin1(other.key)==key)continue;
        QCOMPARE(valueOf(reset,other.key),other.defaultValue==0?0.75:1.75);
    }
}

QTEST_MAIN(LocalAdjustmentPanelTests)
#include "local_adjustment_panel_tests.moc"
