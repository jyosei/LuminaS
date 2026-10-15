#include "output_inspector.h"
#include "canvas.h"
#include "export/export_engine.h"
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtConcurrentRun>

OutputInspector::OutputInspector(QWidget *parent):QWidget(parent){
    setObjectName(QStringLiteral("output-inspector"));
    pool.setMaxThreadCount(1);
    auto *layout=new QVBoxLayout(this);
    layout->setContentsMargins(0,0,0,0);
    layout->setSpacing(4);
    auto *bar=new QWidget;
    auto *line=new QHBoxLayout(bar);
    line->setContentsMargins(0,0,0,0);
    line->setSpacing(4);
    status=new QLabel(QStringLiteral("尚未检查输出文件"));
    status->setObjectName(QStringLiteral("output-inspector-status"));
    status->setProperty("role","muted");
    status->setWordWrap(true);
    line->addWidget(status,1);
    fitButton=new QPushButton(QStringLiteral("适合窗口"));
    fitButton->setObjectName(QStringLiteral("output-inspector-fit"));
    actualButton=new QPushButton(QStringLiteral("100%"));
    actualButton->setObjectName(QStringLiteral("output-inspector-actual"));
    actualButton->setEnabled(false);fitButton->setEnabled(false);
    actualButton->setToolTip(QStringLiteral("一个图像像素对应一个物理显示像素"));
    line->addWidget(fitButton);
    line->addWidget(actualButton);
    layout->addWidget(bar);
    view=new Canvas;
    view->setObjectName(QStringLiteral("output-inspector-canvas"));
    view->setMinimumSize(240,160);
    layout->addWidget(view,1);
    connect(fitButton,&QPushButton::clicked,this,[this]{view->fit();});
    connect(actualButton,&QPushButton::clicked,this,[this]{
        view->actualPixels();
        status->setText(QStringLiteral("%1 · 100% 实际像素").arg(QFileInfo(currentPath).fileName()));
    });
    connect(&watcher,&QFutureWatcher<ReadResult>::finished,this,[this]{
        if(closing.load())return;
        apply(watcher.result());
    });
}

OutputInspector::~OutputInspector(){
    closing.store(true);
    disconnect(&watcher,nullptr,this,nullptr);
    pool.waitForDone();
}

bool OutputInspector::inspect(const QString &path,QString &error){
    error.clear();
    const auto fail=[&](QString message){error=message;status->setText(message);view->setMessage(message);actualButton->setEnabled(false);fitButton->setEnabled(false);return false;};
    if(path.trimmed().isEmpty())return fail(QStringLiteral("没有可检查的输出文件"));
    const QFileInfo info(path);
    if(!info.exists()||!info.isFile())return fail(QStringLiteral("输出文件不存在：%1").arg(info.fileName()));
    const auto canonical=info.canonicalFilePath();
    currentPath=QDir::cleanPath(canonical.isEmpty()?info.absoluteFilePath():canonical);
    actualButton->setEnabled(false);fitButton->setEnabled(false);
    ++generation;
    view->setMessage(QStringLiteral("正在读取输出文件…"));
    status->setText(QStringLiteral("正在读取 %1").arg(info.fileName()));
    const QString target=currentPath;
    watcher.setFuture(QtConcurrent::run(&pool,[target]{
        ReadResult result;
        result.path=target;
        result.image=Export::readOutput(target,result.error);
        return result;
    }));
    return true;
}

void OutputInspector::apply(const ReadResult &result){
    if(result.path!=currentPath)return; // a newer inspection already superseded this one
    if(result.image.isNull()){
        view->setMessage(QStringLiteral("无法显示该输出文件"));
        status->setText(QStringLiteral("读取失败：%1").arg(result.error.isEmpty()?QFileInfo(result.path).fileName():result.error));
        emit inspected(result.path,false);
        return;
    }
    view->setImage(result.image,QStringLiteral("output|%1").arg(result.path));
    actualButton->setEnabled(true);fitButton->setEnabled(true);
    view->fit();
    status->setText(QStringLiteral("%1 · %2×%3 · 显示的是输出文件本身，不会重新导入为照片")
        .arg(QFileInfo(result.path).fileName()).arg(result.image.width()).arg(result.image.height()));
    emit inspected(result.path,true);
}
