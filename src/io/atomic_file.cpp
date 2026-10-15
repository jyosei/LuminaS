#include "io/atomic_file.h"
#include <QElapsedTimer>
#include <QFileDevice>
#include <QIODevice>
#include <QSaveFile>

namespace Persistence {
namespace {
QString stageError(const char *stage,QFileDevice::FileError code,const QString &detail,const QString &path){
    return QStringLiteral("原子写入失败：阶段=")+QString::fromUtf8(stage)
        +QStringLiteral("；错误枚举=")+QString::number(int(code))
        +QStringLiteral("；错误详情=")+detail
        +QStringLiteral("；文件=")+path;
}
}

bool writeAtomicFile(const QString &path,const AtomicBody &body,QString &error,
    CancellationToken cancel,AtomicWriteStats *stats){
    constexpr int maxAttempts=8;
    constexpr qint64 budgetMs=20;
    error.clear();
    QElapsedTimer timer;
    timer.start();
    int attempts=0;
    auto finish=[&](bool ok){
        if(stats){stats->attempts=attempts;stats->elapsedMs=timer.elapsed();}
        return ok;
    };
    if(!body){error=QStringLiteral("原子写入失败：缺少写出回调");return finish(false);}
    for(int attempt=0;attempt<maxAttempts;++attempt){
        if(isCanceled(cancel)){
            error=stageError("取消检查",QFileDevice::NoError,QStringLiteral("写入前已请求取消"),path);
            return finish(false);
        }
        if(attempt>0&&timer.elapsed()>=budgetMs)return finish(false);
        ++attempts;
        // A fresh QSaveFile per round, so a retry works on a new temporary file.
        QSaveFile file(path);
        file.setDirectWriteFallback(false);
        if(!file.open(QIODevice::WriteOnly)){
            error=stageError("打开",file.error(),file.errorString(),path);
            return finish(false);
        }
        QString bodyError;
        if(!body(file,bodyError)){
            file.cancelWriting();
            error=bodyError.isEmpty()?stageError("写出",file.error(),file.errorString(),path):bodyError;
            return finish(false);
        }
        if(isCanceled(cancel)){
            file.cancelWriting();
            error=stageError("取消检查",file.error(),QStringLiteral("写出后已请求取消"),path);
            return finish(false);
        }
        if(file.commit()){error.clear();return finish(true);}
        error=stageError("提交原子替换",file.error(),file.errorString(),path);
#if defined(Q_OS_WIN)
        // Only a Windows rename collision is retryable, and only while the budget allows
        // starting one more round. The budget never bounds how long an OS I/O call runs.
        if(file.error()==QFileDevice::RenameError&&attempt+1<maxAttempts&&timer.elapsed()<budgetMs)continue;
#endif
        return finish(false);
    }
    error=stageError("提交原子替换",QFileDevice::RenameError,QStringLiteral("重试次数已用尽"),path);
    return finish(false);
}

bool writeAtomicBytes(const QString &path,QByteArrayView bytes,QString &error,
    CancellationToken cancel,AtomicWriteStats *stats){
    constexpr qint64 chunkSize=1024*1024;
    return writeAtomicFile(path,[bytes,cancel,path](QIODevice &device,QString &bodyError){
        // Offset restarts at 0 every round: the frozen bytes are replayed from the beginning.
        qint64 offset=0;
        const qint64 total=bytes.size();
        while(offset<total){
            if(isCanceled(cancel)){
                bodyError=stageError("写出",QFileDevice::WriteError,QStringLiteral("写出期间已请求取消"),path);
                return false;
            }
            const qint64 want=qMin(chunkSize,total-offset);
            const qint64 written=device.write(bytes.data()+offset,want);
            if(written<=0){
                bodyError=stageError("写出",QFileDevice::WriteError,
                    QStringLiteral("写出返回")+QString::number(written),path);
                return false;
            }
            offset+=written;
        }
        return true;
    },error,cancel,stats);
}
}
