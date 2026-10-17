#include "window.h"
#include "export_dialog.h"
#include "output_inspector.h"
#include "export/export_queue.h"
#include <QDialog>
#include <QVBoxLayout>
#include <QPushButton>
#include <QStandardPaths>
#include <QFileInfo>
#include <QScreen>
#include <QAction>

QList<Export::Asset> Window::exportAssets() const{
    QList<Export::Asset> result;
    for(int i=0;i<photos.size();++i){
        const auto &p=photos[i];Export::Asset asset{p.path,p.sha256,i==current?live:history.value(p.path),revision,p.item&&p.item->isSelected(),p.pick==Pick::Keep};
        const auto document=i==current?currentPhotoDocument():history.snapshot(p.path);
        if(document&&!document->layers.isEmpty()){
            asset.document=std::make_shared<const LocalEdit::PhotoDocument>(*document);
            asset.recipe=document->global;asset.revision=document->revision;
            if(asset.hash.isEmpty())asset.hash=document->asset.sourceHash;
        }
        result.append(std::move(asset));
    }
    return result;
}
void Window::showExportDialog(){
    endControlEdit();
    if(exportDialog){exportDialog->replaceAssets(exportAssets(),current);exportDialog->show();exportDialog->raise();exportDialog->activateWindow();return;}
    QString loadError;
    if(!exportQueue){const auto root=qEnvironmentVariable("LUMINA_EXPORT_DATA_DIR",QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/export");exportQueue=new Export::Queue(root+"/queue.json",this);exportQueue->load(loadError);}
    auto *dialog=new ExportDialog(exportAssets(),current,exportQueue,this);exportDialog=dialog;
    connect(dialog,&ExportDialog::refreshRequested,this,[this,dialog]{endControlEdit();dialog->replaceAssets(exportAssets(),current);});
    connect(dialog,&ExportDialog::requestInspect,this,&Window::inspectExport);
    connect(this,&Window::previewChanged,dialog,[this,dialog](const QString &,bool success){if(success)dialog->replaceAssets(exportAssets(),current);});
    if(!loadError.isEmpty())dialog->setWindowTitle(QStringLiteral("导出照片 · 队列记录未恢复：")+loadError);
    dialog->show();
}
void Window::inspectExport(const QString &path,const QString &source){
    auto *dialog=new QDialog(this);dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setObjectName("export-output-dialog");dialog->setWindowTitle(QStringLiteral("检查成片 · ")+QFileInfo(path).fileName());const auto available=screen()?screen()->availableGeometry().size():QSize(1120,800);dialog->resize(qMax(620,qMin(1050,available.width()-40)),qMax(400,qMin(740,available.height()-60)));
    auto *layout=new QVBoxLayout(dialog);auto *view=new OutputInspector;layout->addWidget(view,1);
    auto *back=new QPushButton(QStringLiteral("返回对应原图编辑"));back->setObjectName("export-return-edit");layout->addWidget(back);
    connect(back,&QPushButton::clicked,this,[this,dialog,source]{for(int i=0;i<photos.size();++i)if(photos[i].path==source){activate(i);break;}if(auto *edit=findChild<QAction *>("workspace-edit"))edit->trigger();if(exportDialog)exportDialog->hide();dialog->close();raise();activateWindow();});
    dialog->show();QString error;if(!view->inspect(path,error))view->setToolTip(error);
}
