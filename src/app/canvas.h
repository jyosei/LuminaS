#pragma once
#include <QWidget>
#include <QImage>
#include <QVector>
#include <QColor>
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
    // Brush/local-mask mode. Mutually exclusive with crop, picker and hand
    // panning; the canvas routes normalised positions only and never performs
    // output-to-source mapping or brush sampling.
    void setBrushMode(bool enabled);
    // Gradient controls are view-only output UVs; invalid projections are skipped.
    // The host owns source geometry. Hit radius is fixed in widget DIP.
    void setGradientMode(bool enabled);
    void setGradientHandles(QVector<QPointF> handles);
    void setGradientGuide(QVector<QPointF> points); // NaN separates projected segments
    int pressedGradientHandle() const { return gradientHandle; }
    bool brushMode() const { return brushActive; }
    bool brushGestureActive() const { return brushGesture; }
    // Ends the current stroke. Clears internal gesture state before emitting, so
    // it is safe to call from a signal handler and idempotent when inactive.
    void finishBrushGesture(bool canceled = false);
    // View-only coverage mask (Format_Indexed8). Rejected and cleared when the
    // size does not match the canonical image; never a source and never exported.
    void setLocalMask(QImage mask);
    void setLocalMaskStyle(QColor color,bool blackWhite);
    // QA / diagnostics only: never exported and never part of a Recipe.
    void setMonitorProfile(QByteArray icc, QString note = QString());
    void useSystemMonitorProfile();
    DisplayPresenter *presenter() const { return displayPresenter; }
    QString displaySnapshot() const;
    QString displayProfileNote() const;
signals:
    void cropPicked(QRectF normalizedCrop);
    void framePainted();
    void canonicalFramePainted(qint64 canonicalKey);
    void localMaskPainted(qint64 maskKey);
    void neutralPicked(QPointF normalizedPosition);
    void brushPressed(QPointF normalizedPosition, bool temporarySubtract);
    void brushMoved(QPointF normalizedPosition);
    void brushFinished(bool canceled);
    void brushRadiusStepRequested(int direction);
    void overlayToggleRequested();
protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void showEvent(QShowEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void keyReleaseEvent(QKeyEvent *) override;
    void focusOutEvent(QFocusEvent *) override;
    bool event(QEvent *) override;
private:
    void updateCursor();
    void updateToolTip();
    void updateLocalMaskPalette();
    void imageReady(const QImage &image,const QString &note);
    // Widget point to normalised image position; no clamping, no source maths.
    QPointF normalizedFor(const QPointF &position) const;
    int gradientHandleAt(const QPointF &position) const;
    void stopHandPan(); // ends a hand/space pan without touching a brush stroke
    QImage image;            // canonical: layout, crop and picker coordinates
    QImage presented;        // actual displayed frame (device codes); may be null
    QImage localMask;        // Format_Indexed8 view-only coverage; never exported
    QVector<QRgb> localMaskColors; // palette derived from mask indices only
    QColor localMaskColor{172,145,235};
    bool localMaskBlackWhite=false;
    DisplayPresenter *displayPresenter=nullptr;
    QMetaObject::Connection screenConnection;
    QString displayNote;
    int screenSerial=0;
    QString message = QStringLiteral("导入照片开始选片\nJPEG / CR3   ·   Ctrl+O");
    QString imageContext;    // context the canonical image (and mask) belongs to
    double scale = 1;
    QPointF offset, previous, lastPointer;
    bool fitting = true, dragging = false;
    bool cropMode=false,cropDragging=false;
    bool pickerMode=false;
    bool brushActive=false;      // brush mode requested by the host
    bool gradientMode=false;
    int gradientHandle=-1;
    QVector<QPointF> gradientHandles;
    QVector<QPointF> gradientGuide;
    bool brushGesture=false;     // a stroke is currently open
    bool brushSubtract=false;    // Alt intent frozen at press time
    bool spaceHeld=false;        // temporary hand pan modifier held
    bool leftDown=false;         // left button currently pressed
    QPointF cropStart,cropEnd;
    quint64 imageSerial=0,paintedSerial=0;
    qint64 presentedCanonicalKey=0,maskSourceKey=0,paintedMaskKey=0;
    double liveAngle=0;
    double cropAspect=0;
};
