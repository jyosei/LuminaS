#pragma once
// PointColorPanel: the point-color widget only. It stores and edits
// PointColorAdjustment fields verbatim, never samples pixels, renders previews
// or keeps its own histories; the host owns the document and listens to the
// pointsEdited/editFinished/pickRequested/selectedChanged/rangePreviewChanged
// signals, while references and picker state arrive through the setters.
#include "core/document.h"
#include "app/ui_primitives.h"
#include <QCoreApplication>
#include <QVector>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QPushButton;
class QSlider;

class PointColorPanel : public QWidget {
    Q_OBJECT
public:
    explicit PointColorPanel(QWidget *parent=nullptr);

    void setPoints(const QVector<PointColorAdjustment> &points,int selected=-1);
    int selectedIndex() const;
    void setPickerAvailable(bool available);
    void setPickActive(bool active);
    void setRangeActive(bool active);

signals:
    void pointsEdited(const QVector<PointColorAdjustment> &points);
    void editFinished();
    void pickRequested(bool checked);
    void selectedChanged(int index);
    void rangePreviewChanged(bool checked,int index);

private:
    enum Field { HueShift,SaturationShift,LuminanceShift,HueWidth,SaturationWidth,LuminanceWidth,Feather,FieldCount };
    struct Row {
        QDoubleSpinBox *spin=nullptr;
        QSlider *slider=nullptr;
        QPushButton *reset=nullptr;
        double initial=0;
    };

    void buildUi();
    void setStoredField(Field field,double value); // writes data[selected] and re-emits pointsEdited only
    double storedField(Field field) const;
    void syncRow(Field field,double value);
    void refreshControls();       // enables/disables from emptiness and re-reads every row
    void refreshList();           // rebuilds the sample combo from the stored points
    void refreshReference();
    bool acceptPoints(const QVector<PointColorAdjustment> &points); // validates before mutating stored state
    int resolvedSelection(int requested) const;
    void handleSpinChanged(Field field,double value);
    void handleSliderChanged(Field field,int value);
    void handleSliderReleased(Field field);
    void handleSpinFinished(Field field);
    void handleReset(Field field);
    void handleDelete();
    void finishRemoval(int removedIndex);
    void handleSampleChanged(int index);
    void handleRangeToggled(bool checked);
    void handlePickToggled(bool checked);
    QString sampleLabel(int index) const;
    QString referenceText() const;

    QVector<PointColorAdjustment> m_points; // raw widget-owned copy, authoritative for this widget
    int m_selected=-1;
    bool m_restoring=false;
    bool m_pickerAvailable=false;
    QPushButton *m_picker=nullptr;
    QPushButton *m_delete=nullptr;
    QComboBox *m_samples=nullptr;
    QLabel *m_reference=nullptr;
    QLabel *m_hint=nullptr;
    QCheckBox *m_rangePreview=nullptr;
    QWidget *m_controls=nullptr;
    Row m_rows[FieldCount];
};
