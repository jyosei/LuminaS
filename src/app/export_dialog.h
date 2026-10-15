#pragma once
#include "export/export_queue.h"
#include <QDialog>
#include <QFutureWatcher>
#include <QThreadPool>
#include <QMap>
#include <QTimer>
class Canvas;class QLabel;class QComboBox;class QSpinBox;class QDoubleSpinBox;class QCheckBox;class QLineEdit;class QPushButton;class QTableWidget;class QFormLayout;class QVBoxLayout;
class ExportDialog:public QDialog {
 Q_OBJECT
public:
 ExportDialog(QList<Export::Asset> assets,int currentIndex,Export::Queue *queue,QWidget *parent=nullptr);
 ~ExportDialog() override;
 Export::Settings settings() const;void setSettings(const Export::Settings &settings);
 void startExport();QString previewNote() const;
 void replaceAssets(QList<Export::Asset> input,int index);
signals:
 void refreshRequested();
 void requestEdit(QString source);void requestInspect(QString output,QString source);
protected:
 void resizeEvent(QResizeEvent *event) override;
 void closeEvent(QCloseEvent *event) override;
 void reject() override;
private:
 struct Preview {QImage linear,image;QSize full;QString hash,error;int index=-1;quint64 generation=0;};
 struct Preparation {QList<Export::Job> jobs;QString error;};
 void changed();void sync();void profilesChanged();void updateSummary();void updateResults();void previewNow();void inspectSelected();void openFolder();
 void savePreset();void renamePreset();void deletePreset();void loadPresets();bool persist(QString &error);QList<Export::Asset> targets() const;int representative() const;QString dataDir() const;
 Export::Queue *exportQueue;QList<Export::Asset> assets;int currentIndex;Export::Settings state;QList<Export::Profile> profiles;QMap<QString,Export::Settings> presets;
 bool syncing=false,preparing=false,previewPending=false,ownBatch=false;quint64 generation=0;int cachedIndex=-1;QImage cachedLinear;QSize cachedFull;QString notice;
 QThreadPool pool;QFutureWatcher<Preview> previewWatcher;QFutureWatcher<Preparation> preparationWatcher;QTimer debounce;
 QWidget *left=nullptr;Canvas *canvas=nullptr;QLabel *summary=nullptr,*diagnostic=nullptr,*previewLabel=nullptr,*counts=nullptr;QTableWidget *table=nullptr;
 QComboBox *range,*format,*mode,*bits,*profile,*sizeModeBox,*intent,*metadata,*collision,*after,*preset;
 QSpinBox *quality,*sequenceStart;QDoubleSpinBox *widthValue,*heightValue,*percent,*ppi;
 QCheckBox *zip,*bpc,*dontEnlarge,*cm,*beside,*sequence,*gps;
 QLineEdit *directory,*subfolder,*prefix,*suffix;
 QPushButton *start,*cancel,*retry,*inspect,*folder;
};
