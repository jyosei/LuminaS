#pragma once
#include <QWidget>
#include <QImage>
class DisplayPresenter;
class Canvas : public QWidget {
    Q_OBJECT
public:
    explicit Canvas(QWidget *parent = nullptr);
    // Canonical frame: layout, fit/zoom, crop, picker and cursor keep using it.
    // The presented frame is requested from the display presenter instead.
    void setImage(QImage image, QString context = QString());
    void setMessage(QString message);
    void fit();
    void actualPixels(); // one image pixel per physical display pixel
    double zoom() const { return scale; }
    void setCropMode(bool enabled);
    void setPickerMode(bool enabled);
    void setLiveAngle(double angle);
    void setCropAspect(double ratio){cropAspect=ratio;}
    // QA / diagnostics only: never exported and never part of a Recipe.
    void setMonitorProfile(QByteArray icc, QString note = QString());
    void useSystemMonitorProfile();
    DisplayPresenter *presenter() const { return displayPresenter; }
    QString displaySnapshot() const;
    QString displayProfileNote() const;
signals:
    void cropPicked(QRectF normalizedCrop);
    void framePainted();
    void neutralPicked(QPointF normalizedPosition);
protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void showEvent(QShowEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
private:
    void updateCursor();
    void updateToolTip();
    void imageReady(const QImage &image,const QString &note);
    QImage image;            // canonical: layout, crop and picker coordinates
    QImage presented;        // actual displayed frame (device codes); may be null
    DisplayPresenter *displayPresenter=nullptr;
    QMetaObject::Connection screenConnection;
    QString displayNote;
    int screenSerial=0;
    QString message = QStringLiteral("导入照片开始选片\nJPEG / CR3   ·   Ctrl+O");
    double scale = 1;
    QPointF offset, previous;
    bool fitting = true, dragging = false;
    bool cropMode=false,cropDragging=false;
    bool pickerMode=false;
    QPointF cropStart,cropEnd;
    quint64 imageSerial=0,paintedSerial=0;
    double liveAngle=0;
    double cropAspect=0;
};
