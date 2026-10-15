#pragma once
#include "canvas.h"
#include "io/jpeg_loader.h"
#include <QMainWindow>
#include <QListWidget>
#include <QComboBox>
#include <QLabel>
#include <QSet>
#include "edit_loader.h"
#include "core/commands.h"
#include <QDoubleSpinBox>
#include <QPushButton>
#include "preview_histogram.h"
#include "curve_editor.h"
#include <QCheckBox>
#include <QSlider>
#include "core/presets.h"
#include "point_color_panel.h"
#include "export/export_types.h"
#include <QPointer>
namespace Export {class Queue;}
class ExportDialog;
class QFormLayout;

enum class Pick { Unmarked, Keep, Reject };
struct Photo { QString path; Pick pick = Pick::Unmarked; QListWidgetItem *item = nullptr; QString sha256; int cacheState=0;};
class Window : public QMainWindow {
    Q_OBJECT
public:
    explicit Window(QWidget *parent = nullptr,const QString &libraryDirectory = QString());
    ~Window() override;
    void addFiles(const QStringList &paths);
    void activate(int index);
    void mark(Pick pick);
    void filter(int value);
    int currentIndex() const { return current; }
    Pick pickAt(int index) const { return photos.at(index).pick; }
    QString displayedPath() const { return displayed; }
    QListWidget *filmstrip() const { return strip; }
    Recipe currentRecipe() const;
    void applyRecipe(const Recipe &recipe,const QString &name=QStringLiteral("调整"));
    void undoEdit(); void redoEdit();
    void compareOriginal(bool enabled);
    bool saveAdjustmentPreset(const QString &path,QString &error);
    bool loadAdjustmentPreset(const QString &path,QString &error);
    bool renameSelectedPreset(const QString &name,QString &error);
    bool removeSelectedPreset(QString &error);
    bool pickNeutralBalance(QPointF uv,QString &error);
    bool neutralPickerAvailable() const;
    bool restoreOriginalWhiteBalance();
    bool autoWhiteBalance(QString &error);
    bool pickPointColor(QPointF uv,QString &error);
    void showExportDialog();
    QList<Export::Asset> exportAssets() const;
signals:
    void previewChanged(const QString &path, bool success);
protected:
    void resizeEvent(QResizeEvent *event) override;
private:
    Export::Queue *exportQueue=nullptr;
    QPointer<ExportDialog> exportDialog;
    void inspectExport(const QString &path,const QString &source);
    void setupPointColorPanel(QFormLayout *form);
    void setupPerspectiveControls(QFormLayout *form);
    void setupDistortionControls(QFormLayout *form);
    void syncDistortionControls();
    QDoubleSpinBox *distortionControl=nullptr;
    void setupChromaticAberrationControls(QFormLayout *form);
    void syncChromaticAberrationControls();
    QDoubleSpinBox *chromaticRed=nullptr,*chromaticBlue=nullptr;
    void setupDefringeControls(QFormLayout *form);
    void syncDefringeControls();
    QDoubleSpinBox *defringePurpleAmount=nullptr,*defringePurpleHue=nullptr,*defringePurpleWidth=nullptr;
    QDoubleSpinBox *defringeGreenAmount=nullptr,*defringeGreenHue=nullptr,*defringeGreenWidth=nullptr;
    QDoubleSpinBox *defringeRadius=nullptr;
    void setupLensShadingControls(QFormLayout *form);
    void syncLensShadingControls();
    QDoubleSpinBox *lensShadingControl=nullptr;
    void syncPerspectiveControls();
    QDoubleSpinBox *perspectiveHorizontal=nullptr,*perspectiveVertical=nullptr;
    void updatePointPickerState(bool available);
    void clearPointColorModes();
    PointColorPanel *pointColorPanel=nullptr;
    bool pointPickerActive=false,pointRangeActive=false;
    void refreshLabels();
    void updateResponsiveLayout();
    bool storeAdjustmentPreset(const AdjustmentPreset &preset,QString &error);
    void registerAdjustmentPreset(const AdjustmentPreset &preset){
        for(int i=presetChoiceCount;i<presets.size();++i)if(presets[i].name==preset.name){presets[i]=preset;return;}
        presets.append(preset);++customPresetCount;
    }
    void refreshPresetChoice(int selected,const QString &hint=QString());
    void updatePresetManagement();
    bool beginPresetStrength(QString &error);
    void updatePresetStrength(int percentage);
    void finishPresetStrength();
    void refreshPresetStrengthState();
    void invalidatePresetStrength();
    void syncControlsFromLive();
    void presentResult(const EditResult &result,bool immediate=false);
    void step(int delta);
    void requestEdit();void restoreControls();void editControls(QDoubleSpinBox *changed);void endControlEdit();
    QList<Photo> photos;
    QSet<QString> known;
    int current = -1;
    quint64 token = 0;
    QString displayed;
    Canvas *canvas;
    QListWidget *strip=nullptr;
    QComboBox *filters;
    QLabel *details;
    JpegLoader *loader;
    EditLoader *editor=nullptr;
    EditHistory history;
    EditTag expected;
    quint64 session=0,revision=1,draft=1,geometry=1;
    bool restoring=false,comparing=false,cropSelecting=false;
    QDoubleSpinBox *exposure,*contrast,*saturation,*warmth,*tint,*angle;
    QDoubleSpinBox *highlights,*shadows,*whites,*blacks,*vibrance;
    QDoubleSpinBox *texture,*clarity,*dehaze;
    QDoubleSpinBox *sharpenAmount,*sharpenRadius,*sharpenThreshold,*sharpenMask;
    QPushButton *sharpenMaskPreview=nullptr;bool sharpenMaskActive=false;
    QDoubleSpinBox *noiseLuminance,*noiseChroma;
    QDoubleSpinBox *vignetteAmount,*vignetteMidpoint,*vignetteFeather;
    QDoubleSpinBox *grainAmount,*grainSize,*grainRoughness;
    QComboBox *primaryChannel;
    QDoubleSpinBox *primaryHue,*primarySaturation;
    QLabel *warmthLabel,*tintLabel;
    QWidget *editPanel;
    PreviewHistogram *histogram=nullptr;
    CurveEditor *curveEditor;
    CurveEditor *parametricEditor;
    std::array<QDoubleSpinBox *,4> parametricRegions;
    std::array<QDoubleSpinBox *,3> parametricSplits;
    QComboBox *curveChannel,*hslBand;
    QDoubleSpinBox *hslHue,*hslSaturation,*hslLuminance;
    QComboBox *gradeRange,*monochromeBand;
    QDoubleSpinBox *gradeHue,*gradeSaturation,*gradeLuminance,*gradeBlend,*gradeBalance,*monochromeMix;
    QCheckBox *monochromeEnabled;QDoubleSpinBox *monochromeStrength;
    QPushButton *flipHorizontal,*flipVertical;
    QComboBox *cropAspect;
    QComboBox *presetChoice;
    QSlider *presetStrengthSlider=nullptr;
    QLabel *presetStrengthHint=nullptr;
    bool presetStrengthActive=false,presetStrengthDirty=false;
    Recipe presetStrengthBase,presetStrengthLast;
    AdjustmentPreset presetStrengthSnapshot;
    QString presetStrengthPath;
    QPushButton *presetManage=nullptr;
    QLabel *presetLibraryHint=nullptr;
    QVector<AdjustmentPreset> presets;
    QString presetLibrary; // null QString: default AppLocalDataLocation/presets; non-null empty QString disables persistence
    int presetChoiceCount=0,customPresetCount=0;
    QSize currentOriginalSize;
    void updateCropAspect();
    void updatePickerState();
    void beginCropSelection();
    void showAdjustmentSection(const QString &title);
    void syncCanvasTools();
    QLabel *pickerHint;
    EditTag displayedTag;
    QString previewError;
    QPushButton *compareButton;
    QAction *undoAction,*redoAction;
    Recipe live;
    quint64 presentedJob=0;
    double displayedAngle=0;
    int cacheCompleted=0,cacheTotal=0,cacheFailures=0;
};
