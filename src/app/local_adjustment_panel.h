#pragma once
// LocalAdjustmentPanel: the local-adjustment widget only. It shows the layer
// list and the compact local parameters of the selected layer and reports user
// intent through signals. It never edits the photo document, never keeps
// document history, never touches files and never renders or samples pixels;
// the host owns the document, applies every edit and drives refreshes.
#include "core/photo_document.h"
#include "app/ui_primitives.h"
#include <QMetaType>
#include <QString>
#include <QVector>
#include <QWidget>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSlider;

namespace LocalEdit {
// Widget-owned brush settings: a plain description of the requested brush, not
// a mask component and not part of the document. The host converts them.
struct BrushSettings {
    double radius=100,hardness=.6,strength=1;
    bool subtract=false;
    bool operator==(const BrushSettings &) const = default;
};
}
Q_DECLARE_METATYPE(LocalEdit::BrushSettings)
// The parameters signal carries the whole struct by value, so the metatype is
// declared here for queued delivery and for QSignalSpy; core stays untouched.
Q_DECLARE_METATYPE(LocalEdit::LocalParameters)

class LocalAdjustmentPanel : public QWidget {
    Q_OBJECT
public:
    explicit LocalAdjustmentPanel(QWidget *parent=nullptr);

    // Replaces the shown layers. The selection id is preserved when it still
    // exists, otherwise the first layer is shown. No signal is emitted while
    // refreshing: a fallback is never reported as a user selection.
    void setDocument(LocalEdit::DocumentSnapshot snapshot,const QString &selectedId={});
    // Gates every control that needs a prepared preview. It never mutates the
    // document and never clears a stored selection.
    void setAvailable(bool available);
    void setBrushActive(bool active);
    bool brushActive() const;
    QString selectedLayerId() const;
    LocalEdit::BrushSettings brushSettings() const;
    // Clamped to the documented brush radius range.
    void setBrushRadius(double radius);
    bool overlayVisible() const;
    void setOverlayVisible(bool visible);

signals:
    void createBrushLayerRequested();
    void selectedLayerChanged(const QString &layerId);
    void layerVisibilityChanged(const QString &layerId,bool visible);
    void parametersEdited(const QString &layerId,LocalEdit::LocalParameters parameters);
    void parametersFinished();
    void brushSettingsChanged(LocalEdit::BrushSettings settings);
    void brushToolToggled(bool active);
    void overlayToggled(bool visible);

private:
    enum Field { Exposure,Saturation,FieldCount };
    struct Row {
        QDoubleSpinBox *spin=nullptr;
        QSlider *slider=nullptr;
        QPushButton *reset=nullptr;
    };
    struct Layer {
        QString id,name;
        bool enabled=true;
        LocalEdit::LocalParameters parameters;
    };

    void buildUi();
    void refreshList();       // rebuilds rows from the stored layers without emitting
    void refreshControls();   // enables/disables and re-reads the selected layer
    void syncVisibility();    // the selected-layer checkbox only
    void syncRow(Field field,double value);
    void syncBrushControls();
    int selectedRow() const;
    Layer *selectedLayer();
    const Layer *selectedLayer() const;
    void storeField(Field field,double value); // emits parametersEdited with every field
    double storedField(Field field) const;
    void handleSelectionChanged();
    void handleCreate();
    void handleVisibleToggled(bool checked);
    void handleSpinChanged(Field field,double value);
    void handleSliderChanged(Field field,int value);
    void handleSliderReleased(Field field);
    void handleSpinFinished(Field field);
    void handleKeyboardCommit(Field field);
    bool eventFilter(QObject *target,QEvent *event) override;
    void handleReset(Field field);
    void handleBrushToggled(bool checked);
    void handleEraseToggled(bool checked);
    void handleOverlayToggled(bool checked);
    void handleRadiusChanged(double radius);
    void handleHardnessChanged(double value);
    void handleStrengthChanged(double value);
    bool hasSelection() const;

    QVector<Layer> m_layers; // raw widget-owned copy, authoritative for this widget
    QString m_selectedId;
    bool m_available=false;
    bool m_restoring=false; // programmatic refresh: suppresses every emitted signal
    bool m_brushActive=false;
    LocalEdit::BrushSettings m_brush;

    QListWidget *m_list=nullptr;
    QPushButton *m_new=nullptr;
    QPushButton *m_brushButton=nullptr;
    QPushButton *m_erase=nullptr;
    QCheckBox *m_overlay=nullptr;
    QCheckBox *m_visible=nullptr;
    QLabel *m_hint=nullptr;
    QWidget *m_parameters=nullptr;
    QWidget *m_brushBox=nullptr;
    QDoubleSpinBox *m_radiusSpin=nullptr;
    QSlider *m_radiusSlider=nullptr;
    QDoubleSpinBox *m_hardnessSpin=nullptr;
    QSlider *m_hardnessSlider=nullptr;
    QDoubleSpinBox *m_strengthSpin=nullptr;
    QSlider *m_strengthSlider=nullptr;
    QString m_hintEmpty;   // shown when the document has no local layer
    QString m_hintWaiting; // shown when no prepared preview exists yet
    Row m_rows[FieldCount];
};