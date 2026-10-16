#pragma once
#include "core/photo_document.h"
#include <QWidget>
#include <QColor>
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QSlider;

// Display/intent only. Mask values are copied; color/diagnostics stay transient.
class LocalMaskOptions:public QWidget {
    Q_OBJECT
public:
    explicit LocalMaskOptions(QWidget *parent=nullptr);
    void setMask(const LocalEdit::Mask *mask,const QString &owner={});
    void setAvailable(bool available);
    void setView(QColor color,bool blackWhite);
signals:
    void featherEdited(double sourcePixels);
    void featherFinished();
    void invertRequested(bool inverted);
    void colorSelected(QColor color);
    void blackWhiteToggled(bool blackWhite);
private:
    void refreshEnabled();
    void scheduleFinish();
    void invalidateFinish();
    QDoubleSpinBox *m_spin=nullptr;
    QSlider *m_slider=nullptr;
    QCheckBox *m_invert=nullptr,*m_blackWhite=nullptr;
    QComboBox *m_color=nullptr;
    bool m_available=false,m_hasMask=false,m_syncing=false,m_dirty=false;
    quint64 m_generation=0;
    QString m_owner;
};
