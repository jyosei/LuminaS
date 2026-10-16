#pragma once
#include "core/photo_document.h"
#include <QWidget>
class QComboBox;
class QPushButton;

// View and intent only. The host owns geometry, selection and document edits.
class LocalGradientControls : public QWidget {
    Q_OBJECT
public:
    explicit LocalGradientControls(QWidget *parent=nullptr);
    void setDocument(LocalEdit::DocumentSnapshot snapshot,const QString &layerId,
                     const QString &componentId={});
    void setAvailable(bool available);
    void setActiveKind(int kind); // 0 none/brush, 1 linear, 2 radial
signals:
    void toolRequested(int kind);
    void componentSelected(QString componentId);
    void componentRemovalRequested(QString componentId);
private:
    void refreshControls();
    void requestTool(int kind);
    bool m_available=false,m_hasLayer=false;
    int m_kind=0;
    QPushButton *m_linear=nullptr,*m_radial=nullptr,*m_remove=nullptr;
    QComboBox *m_components=nullptr;
};
