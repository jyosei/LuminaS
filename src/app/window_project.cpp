#include "window.h"
#include "project_persistence.h"
#include <QAction>
#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QMenu>
#include <QMessageBox>
#include <QStatusBar>
#include <QTimer>
#include <QUuid>
#include <QtConcurrentRun>
#include <limits>

namespace {
QString newId(){return QUuid::createUuid().toString(QUuid::WithoutBraces);}
bool sameContent(const LocalEdit::RecoveryState &a,const LocalEdit::RecoveryState &b){
    if(a.order!=b.order||a.pending!=b.pending||a.activePath!=b.activePath||a.project->photos.size()!=b.project->photos.size())return false;
    for(qsizetype i=0;i<a.project->photos.size();++i){
        const auto &x=a.project->photos[i];const auto &y=b.project->photos[i];
        // EditHistory publishes immutable snapshots; no million-point traversal
        // or JSON work is needed for unchanged documents on the GUI thread.
        if(x.mark!=y.mark||x.document!=y.document)return false;
    }
    return true;
}
}
void Window::setupProjectPersistence(QMenu *menu,const QString &root){
    projectRoot=root;projectId=newId();projectName=QStringLiteral("未命名项目");
    menu->addSeparator();
    auto action=[&](const QString &name,const QString &id,const QString &key,auto callback){
        auto *a=menu->addAction(name);a->setObjectName(id);if(!key.isEmpty())a->setShortcut(QKeySequence(key));addAction(a);connect(a,&QAction::triggered,this,callback);return a;
    };
    action(QStringLiteral("打开项目…"),"project-open","Ctrl+Shift+O",[this]{chooseProjectOpen();});
    action(QStringLiteral("保存项目…"),"project-save","Ctrl+S",[this]{chooseProjectSave();});
    action(QStringLiteral("项目另存为…"),"project-save-as","Ctrl+Shift+S",[this]{chooseProjectSave(true);});
    action(QStringLiteral("恢复会话…"),"project-recover",{},[this]{chooseProjectOpen(true);});
    auto *relinkAction=action(QStringLiteral("恢复当前照片链接…"),"project-relink",{},[this]{chooseSourceRelink();});relinkAction->setEnabled(false);
    action(QStringLiteral("重试恢复写入"),"project-recovery-retry",{},[this]{if(projectPersistence){syncProjectSnapshot();projectPersistence->retryRecovery();}});
    if(root.isEmpty()){refreshProjectStatus();return;}
    projectStatus->setText(QStringLiteral("准备恢复存储…"));
    auto *setup=new QFutureWatcher<QString>(this);
    connect(setup,&QFutureWatcher<QString>::finished,this,[this,setup]{
        const auto error=setup->result();setup->deleteLater();
        if(projectClosing)return;
        if(!error.isEmpty()){projectStatus->setText(QStringLiteral("恢复存储不可用"));projectStatus->setToolTip(error);return;}
        projectPersistence=new ProjectPersistence(this);
        connect(projectPersistence,&ProjectPersistence::changed,this,&Window::refreshProjectStatus);
        connect(projectPersistence,&ProjectPersistence::completed,this,[this](const PersistenceResult &result){
            if(projectClosing)return;
            const bool load=result.operation==PersistenceOperation::ProjectLoad||result.operation==PersistenceOperation::RecoveryLoad||result.operation==PersistenceOperation::SourceRelink;
            if(load){
                projectLoading=false;setEnabled(true);
                if(result.error.isEmpty()&&result.state)applyProjectLoad(result);
                else {
                    QStringList paths;for(const auto &photo:photos)if(!sourceBlocks.contains(photo.path))paths<<photo.path;
                    editor->prefetch(paths);requestEdit();
                }
            }
            if(!result.error.isEmpty()){
                closeAfterSave=false;statusBar()->showMessage(result.error,12000);
            }else if(result.operation==PersistenceOperation::ProjectSave){
                statusBar()->showMessage(QStringLiteral("已保存项目：")+QFileInfo(result.path).fileName(),6000);
                if(closeAfterSave){closeAfterSave=false;if(!projectPersistence->dirty())close();}
            }
            refreshProjectStatus();
        });
        projectPoll=new QTimer(this);projectPoll->setInterval(200);
        connect(projectPoll,&QTimer::timeout,this,&Window::syncProjectSnapshot);
        syncProjectSnapshot();projectPoll->start();
    });
    // Only the owned recovery directory is prepared; no history/TEMP scanning.
    setup->setFuture(QtConcurrent::run([root]{
        if(!QDir::isAbsolutePath(root)||QFileInfo(root).isSymLink()||!QDir().mkpath(root))return QStringLiteral("无法创建恢复目录：")+root;
        return QString{};
    }));
}
void Window::syncProjectSnapshot(){
    if(!projectPersistence||projectLoading||projectApplying||projectClosing)return;
    LocalEdit::ProjectState project;project.id=projectId;project.name=projectName;project.revision=projectRevision;
    LocalEdit::RecoveryState state;
    if(auto previous=projectPersistence->state()){state.explicitPath=previous->explicitPath;state.explicitHash=previous->explicitHash;}
    for(const auto &photo:photos){
        state.order<<photo.path;
        const auto document=history.snapshot(photo.path);
        if(document&&!document->asset.sourceHash.isEmpty()&&!document->asset.normalizedSize.isEmpty()){
            project.photos.append({document,LocalEdit::PhotoMark(int(photo.pick))});
            if(current>=0&&photos[current].path==photo.path)project.activeAssetId=document->asset.id;
        }else state.pending.append({photo.path,importErrors.value(photo.path),LocalEdit::PhotoMark(int(photo.pick))});
    }
    if(current>=0)state.activePath=photos[current].path;
    state.project=std::make_shared<const LocalEdit::ProjectState>(std::move(project));
    if(projectObserved&&sameContent(state,*projectObserved))return;
    QString error;bool ok=false;
    if(!projectPersistence->state()){
        ok=projectPersistence->reset(std::make_shared<const LocalEdit::RecoveryState>(state),QDir(projectRoot).filePath(newId()+".luminas-recovery.json"),photos.isEmpty(),error);
    }else {
        if(projectRevision==std::numeric_limits<quint64>::max()){statusBar()->showMessage(QStringLiteral("项目版本已达上限，请另建项目"),10000);return;}
        auto next=*state.project;next.revision=projectRevision+1;state.project=std::make_shared<const LocalEdit::ProjectState>(std::move(next));
        ok=projectPersistence->update(std::make_shared<const LocalEdit::RecoveryState>(state),error);
    }
    if(ok){projectRevision=state.project->revision;projectObserved=std::make_shared<const LocalEdit::RecoveryState>(std::move(state));}
    else statusBar()->showMessage(error,10000);
}
QString Window::projectRecoveryPath() const{return projectPersistence?projectPersistence->recoveryPath():QString{};}
bool Window::saveProjectFile(const QString &path,QString &error){
    if(!projectPersistence){error=QStringLiteral("项目存储尚未就绪");return false;}
    if(projectLoading){error=QStringLiteral("正在打开项目");return false;}
    endControlEdit();finishLocalEdits();syncProjectSnapshot();
    return projectPersistence->save(path,error);
}
bool Window::openProjectFile(const QString &path,bool recovery,QString &error){
    if(!projectPersistence){error=QStringLiteral("项目存储尚未就绪");return false;}
    if(projectLoading){error=QStringLiteral("正在打开项目");return false;}
    endControlEdit();finishLocalEdits();syncProjectSnapshot();
    projectLoading=true;
    if(!projectPersistence->open(path,recovery,error)){projectLoading=false;return false;}
    editor->resetImports();loader->invalidate();setEnabled(false);return true;
}
bool Window::relinkCurrentSource(const QString &newPath,QString &error){
    if(!projectPersistence||projectLoading||current<0){error=QStringLiteral("项目存储未就绪或没有当前照片");return false;}
    endControlEdit();finishLocalEdits();syncProjectSnapshot();
    const auto document=history.snapshot(photos[current].path);
    if(!document||document->asset.sourceHash.isEmpty()){error=QStringLiteral("当前照片尚无完整源身份，不能恢复链接");return false;}
    projectLoading=true;
    if(!projectPersistence->relink(document->asset.id,newPath,error)){projectLoading=false;return false;}
    editor->resetImports();loader->invalidate();setEnabled(false);return true;
}
void Window::chooseSourceRelink(){
    if(current<0)return;
    const auto path=QFileDialog::getOpenFileName(this,QStringLiteral("选择与记录指纹一致的原图"),QFileInfo(photos[current].path).absolutePath(),QStringLiteral("照片 (*.jpg *.jpeg *.cr3 *.JPG *.JPEG *.CR3)"));
    if(path.isEmpty())return;
    QString error;if(!relinkCurrentSource(path,error))QMessageBox::warning(this,QStringLiteral("无法恢复链接"),error);
}
void Window::applyProjectLoad(const PersistenceResult &result){
    const auto state=result.state;if(!state||!state->project)return;
    if(!result.loadedHistory){statusBar()->showMessage(QStringLiteral("项目历史尚未验证，未替换当前状态"),12000);return;}
    EditHistory replacement=*result.loadedHistory;QString error;
    const auto nextJournal=QDir(projectRoot).filePath(newId()+".luminas-recovery.json");
    const bool clean=result.operation==PersistenceOperation::ProjectLoad||(result.operation==PersistenceOperation::SourceRelink&&state->project->revision==projectRevision&&!projectPersistence->dirty());
    if(!projectPersistence->reset(state,nextJournal,clean,error)){statusBar()->showMessage(error,12000);return;}
    projectApplying=true;editor->resetImports();cancelLocalEdits();invalidatePresetStrength();
    history=std::move(replacement);current=-1;displayed.clear();known.clear();photos.clear();
    sourceBlocks.clear();importErrors.clear();
    {const QSignalBlocker block(strip);strip->clear();}
    {const QSignalBlocker block(filters);filters->setCurrentIndex(0);}
    QHash<QString,LocalEdit::ProjectPhoto> indexed;for(const auto &photo:state->project->photos)indexed.insert(photo.document->asset.sourcePath,photo);
    for(const auto &source:result.sources)if(source.state!=LocalEdit::SourceState::Available)
        sourceBlocks[source.path]=QStringLiteral("源照片缺失、不可读或内容已变化\n编辑数据已保留；请使用与记录指纹完全一致的原图恢复链接。\n")+source.error;
    QHash<QString,LocalEdit::PendingImport> pending;for(const auto &item:state->pending){pending.insert(item.path,item);importErrors[item.path]=item.error;}
    int active=-1;QStringList prefetch;
    {const QSignalBlocker block(strip);
        for(const auto &path:state->order){
            auto *item=new QListWidgetItem(strip);item->setData(Qt::UserRole,photos.size());item->setToolTip(path);
            Photo photo;photo.path=path;photo.item=item;
            if(indexed.contains(path)){const auto &stored=indexed[path];photo.pick=Pick(int(stored.mark));photo.sha256=stored.document->asset.sourceHash;}
            else photo.pick=Pick(int(pending.value(path).mark));
            if(sourceBlocks.contains(path)){photo.cacheState=2;item->setToolTip(path+"\n"+sourceBlocks[path]);}
            else {prefetch<<path;if(!importErrors.value(path).isEmpty())photo.cacheState=2;}
            if(path==state->activePath)active=int(photos.size());
            photos.append(std::move(photo));known.insert(path);
        }
    }
    projectId=state->project->id;projectName=state->project->name;projectRevision=state->project->revision;
    auto observed=*state;auto observedProject=*state->project;
    for(auto &photo:observedProject.photos)photo.document=history.snapshot(photo.document->asset.sourcePath);
    observed.project=std::make_shared<const LocalEdit::ProjectState>(std::move(observedProject));
    projectObserved=std::make_shared<const LocalEdit::RecoveryState>(std::move(observed));
    localSourceReady=false;selectedLocalLayer.clear();selectedLocalComponent.clear();
    ++revision;++geometry;markViewEpochChange();requestedView={};requestedViewRole=-1;
    projectApplying=false;
    if(active>=0)activate(active);else {live={};restoreControls();canvas->setMessage(QStringLiteral("选择照片查看"));histogram->setImage({});editPanel->setEnabled(false);}
    refreshLabels();refreshProjectStatus();editor->prefetch(prefetch);
    statusBar()->showMessage(sourceBlocks.isEmpty()?QStringLiteral("项目已载入"):QStringLiteral("项目已载入，%1 张源照片需要恢复链接").arg(sourceBlocks.size()),10000);
}
void Window::refreshProjectStatus(){
    if(!projectStatus)return;
    if(auto *action=findChild<QAction *>("project-relink"))action->setEnabled(projectPersistence&&!projectLoading&&current>=0&&sourceBlocks.contains(photos[current].path));
    if(!projectPersistence){projectStatus->setText(QStringLiteral("本次会话"));projectStatus->setToolTip(QStringLiteral("当前窗口未启用项目存储"));return;}
    const auto state=projectPersistence->state();
    const auto title=state&&!state->explicitPath.isEmpty()?QFileInfo(state->explicitPath).fileName():QStringLiteral("未命名项目");
    setWindowTitle(title+(projectPersistence->dirty()?QStringLiteral(" *"):QString{})+QStringLiteral(" · LuminaS"));
    QString text;
    if(projectLoading)text=QStringLiteral("正在打开项目…");
    else if(!projectPersistence->lastError().isEmpty())text=QStringLiteral("存储失败 · 可重试");
    else if(projectPersistence->busy())text=QStringLiteral("后台存储中…");
    else if(!projectPersistence->dirty())text=state&&!state->explicitPath.isEmpty()?QStringLiteral("项目已保存"):QStringLiteral("空项目");
    else if(projectPersistence->recoveredRevision()==projectRevision)text=QStringLiteral("恢复副本已更新 · 项目未保存");
    else text=QStringLiteral("项目未保存 · 等待恢复写入");
    projectStatus->setText(text);
    projectStatus->setToolTip(text+"\n"+projectPersistence->lastError()+QStringLiteral("\n自动恢复副本：")+projectPersistence->recoveryPath()+QStringLiteral("\n显式项目：")+(state?state->explicitPath:QString{}));
}
void Window::chooseProjectSave(bool saveAs){
    QString path;if(projectPersistence&&projectPersistence->state()&&!saveAs)path=projectPersistence->state()->explicitPath;
    if(path.isEmpty())path=QFileDialog::getSaveFileName(this,QStringLiteral("保存 LuminaS 项目"),QStringLiteral("Untitled.luminas"),QStringLiteral("LuminaS 项目 (*.luminas)"),nullptr,QFileDialog::DontConfirmOverwrite);
    if(path.isEmpty()){closeAfterSave=false;return;}
    if(!path.endsWith(".luminas",Qt::CaseInsensitive))path+=".luminas";
    QString error;if(!saveProjectFile(path,error)){closeAfterSave=false;QMessageBox::warning(this,QStringLiteral("无法保存项目"),error);}
}
bool Window::confirmProjectReplacement(){
    endControlEdit();finishLocalEdits();syncProjectSnapshot();
    if(!projectPersistence||!projectPersistence->dirty())return true;
    const auto reply=QMessageBox::warning(this,QStringLiteral("尚未保存项目"),QStringLiteral("当前项目有未保存更改。放弃后打开其他项目？\n自动恢复副本与显式项目文件分别保存。"),QMessageBox::Discard|QMessageBox::Cancel,QMessageBox::Cancel);
    return reply==QMessageBox::Discard;
}
void Window::chooseProjectOpen(bool recovery){
    const auto path=QFileDialog::getOpenFileName(this,recovery?QStringLiteral("选择恢复副本"):QStringLiteral("打开 LuminaS 项目"),recovery?projectRoot:QString{},recovery?QStringLiteral("LuminaS 恢复副本 (*.luminas-recovery.json)"):QStringLiteral("LuminaS 项目 (*.luminas)"));
    if(path.isEmpty()||!confirmProjectReplacement())return;
    QString error;if(!openProjectFile(path,recovery,error))QMessageBox::warning(this,QStringLiteral("无法打开项目"),error);
}
void Window::closeEvent(QCloseEvent *event){
    endControlEdit();finishLocalEdits();syncProjectSnapshot();
    if((projectPersistence&&projectPersistence->dirty())||(!projectRoot.isEmpty()&&!projectPersistence&&!photos.isEmpty())){
        const auto reply=QMessageBox::warning(this,QStringLiteral("保存项目后关闭？"),QStringLiteral("当前项目有未保存更改。自动恢复副本不等同于显式保存。"),QMessageBox::Save|QMessageBox::Discard|QMessageBox::Cancel,QMessageBox::Save);
        if(reply==QMessageBox::Cancel){event->ignore();return;}
        if(reply==QMessageBox::Save){closeAfterSave=true;event->ignore();chooseProjectSave();return;}
    }
    projectClosing=true;if(projectPoll)projectPoll->stop();if(projectPersistence)projectPersistence->cancel();
    QMainWindow::closeEvent(event);
}
