#pragma once
#include "export/export_queue.h"
#include <QDialog>
#include <QFutureWatcher>
#include <QThreadPool>
#include <QMap>
#include <QTimer>
#include "core/cancellation.h"
#include <functional>
class Canvas;class QLabel;class QComboBox;class QSpinBox;class QDoubleSpinBox;class QCheckBox;class QLineEdit;class QPushButton;class QTableWidget;class QFormLayout;class QVBoxLayout;
class ExportDialog:public QDialog {
 Q_OBJECT
public:
 using PreviewReader=std::function<FrameResult(const QString &,double,double,const CancellationToken &,const QString &)>;
 ExportDialog(QList<Export::Asset> assets,int currentIndex,Export::Queue *queue,QWidget *parent=nullptr);
 // Test seam; reader runs in the pool and must not access GUI objects.
 ExportDialog(QList<Export::Asset> assets,int currentIndex,Export::Queue *queue,QWidget *parent,PreviewReader reader);
 ~ExportDialog() override;
 Export::Settings settings() const;void setSettings(const Export::Settings &settings);
 void startExport();QString previewNote() const;
 void replaceAssets(QList<Export::Asset> input,int index);
signals:
 // GUI-thread provenance/QA: emitted only after admitting a source cache for
 // the current asset generation, never merely after a worker finishes.
 void previewSourceCached(QString path,QString sourceHash,quint64 sourceGeneration);
 void refreshRequested();
 void requestEdit(QString source);void requestInspect(QString output,QString source);
protected:
 void resizeEvent(QResizeEvent *event) override;
 void closeEvent(QCloseEvent *event) override;
 void reject() override;
private:
 struct Preview {QImage linear,image;QSize full;QString path,hash,error;Recipe recipe;int index=-1;quint64 generation=0,sourceGeneration=0;};
 struct Preparation {QList<Export::Job> jobs;QString error;};
 void changed();void sync();void profilesChanged();void updateSummary();void updateResults();void previewNow();void inspectSelected();void openFolder();
 void savePreset();void renamePreset();void deletePreset();void loadPresets();bool persist(QString &error);QList<Export::Asset> targets() const;int representative() const;QString dataDir() const;
 Export::Queue *exportQueue;QList<Export::Asset> assets;int currentIndex;Export::Settings state;QList<Export::Profile> profiles;QMap<QString,Export::Settings> presets;
 bool syncing=false,preparing=false,previewPending=false,ownBatch=false;quint64 generation=0;int cachedIndex=-1;QImage cachedLinear;QSize cachedFull;QString notice;
 quint64 sourceGeneration=1;int activePreviewIndex=-1;CancellationSource previewCancel;
 PreviewReader previewReader;
 QThreadPool pool;QFutureWatcher<Preview> previewWatcher;QFutureWatcher<Preparation> preparationWatcher;QTimer debounce;
 QWidget *left=nullptr;Canvas *canvas=nullptr;QLabel *summary=nullptr,*diagnostic=nullptr,*previewLabel=nullptr,*counts=nullptr;QTableWidget *table=nullptr;
 QComboBox *range,*format,*mode,*bits,*profile,*sizeModeBox,*intent,*metadata,*collision,*after,*preset;
 QSpinBox *quality,*sequenceStart;QDoubleSpinBox *widthValue,*heightValue,*percent,*ppi;
 QCheckBox *zip,*bpc,*dontEnlarge,*cm,*beside,*sequence,*gps;
 QLineEdit *directory,*subfolder,*prefix,*suffix;
 QPushButton *start,*cancel,*retry,*inspect,*folder;
};
