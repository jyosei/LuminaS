#include "jpeg_loader.h"
#include <QColorSpace>
#include <QImageReader>
#include <QtConcurrentRun>
#include <cmath>
#include "preview_size.h"
#include <QFileInfo>
#include <QElapsedTimer>

JpegResult decodeJpeg(const QString &path, int longEdge) {
    JpegResult out;
    QElapsedTimer total, stage; total.start();
    out.path = path;
    QImageReader reader(path);
    reader.setDecideFormatFromContent(true);
    reader.setAutoTransform(true);
    if (reader.format() != "jpeg" && reader.format() != "jpg") {
        out.error = QStringLiteral("不是可识别的 JPEG 文件"); return out;
    }
    const QSize size = reader.size();
    if (!size.isValid() || size.isEmpty()) {
        out.error = QStringLiteral("无法读取 JPEG 尺寸：") + reader.errorString(); return out;
    }
    const qint64 pixels = qint64(size.width()) * qint64(size.height());
    if (pixels > 45000000) {
        out.error = QStringLiteral("超过 45MP 上限（%1 × %2）").arg(size.width()).arg(size.height()); return out;
    }
    out.originalSize = size;
    const auto transform = reader.transformation();
    if (int(transform) & int(QImageIOHandler::TransformationRotate90)) out.originalSize.transpose();
    const auto target = previewSize(size,longEdge);
    if (target.isEmpty()) { out.error = QStringLiteral("无效预览尺寸"); return out; }
    reader.setScaledSize(target);
    out.timings.insert("headerMs",double(total.elapsed()));
    stage.start();
    out.image = reader.read();
    out.timings.insert("decodeAndResizeMs",double(stage.elapsed()));
    if (out.image.isNull()) { out.error = QStringLiteral("JPEG 解码失败：") + reader.errorString(); return out; }
    stage.restart();
    if (!out.image.colorSpace().isValid()) {
        out.image.setColorSpace(QColorSpace::SRgb);
        out.colorNote = QStringLiteral("无 ICC，按 sRGB 预览");
    } else {
        out.image.convertToColorSpace(QColorSpace::SRgb);
        out.colorNote = QStringLiteral("已转换为 sRGB 预览");
    }
    out.timings.insert("colorMs",double(stage.elapsed()));
    out.timings.insert("elapsedMs",double(total.elapsed()));
    return out;
}
JpegLoader::JpegLoader(QObject *parent) : QObject(parent) {
    pool.setMaxThreadCount(1);
    connect(&watcher, &QFutureWatcher<JpegResult>::finished, this, [this] {
        auto result = watcher.result();
        busy = false;
        if (!closing&&!activeInvalid) emit ready(result, active.token, active.preview);
        dispatch();
    });
}
void JpegLoader::invalidate(){pending.reset();thumbnails.clear();activeInvalid=true;}
JpegLoader::~JpegLoader() {
    closing = true;
    disconnect(&watcher, nullptr, this, nullptr);
    pending.reset(); thumbnails.clear();
    pool.waitForDone(); // JPEG read is bounded; never release buffers still in use.
}
void JpegLoader::preview(const QString &path, quint64 token) {
    pending = Request{path, token, true}; dispatch();
}
void JpegLoader::thumbnail(const QString &path) {
    thumbnails.enqueue({path, 0, false}); dispatch();
}
void JpegLoader::dispatch() {
    if (closing || busy) return;
    if (pending) { active = *pending; pending.reset(); }
    else if (!thumbnails.isEmpty()) active = thumbnails.dequeue();
    else return;
    busy = true;activeInvalid=false;
    const auto request = active;
    watcher.setFuture(QtConcurrent::run(&pool, [request] {
        if (QFileInfo(request.path).suffix().compare("cr3",Qt::CaseInsensitive)==0) {
            // The window uses EditLoader for RAW. Keep this JPEG-only API from
            // accidentally reopening an unisolated legacy RAW decoder.
            return JpegResult{request.path,QStringLiteral("RAW请通过照片编辑通道读取"),{},{},{}};
        }
        return decodeJpeg(request.path, request.preview ? 3840 : 160);
    }));
}
