#pragma once
#include <QFutureWatcher>
#include <QImage>
#include <QObject>
#include <QQueue>
#include <QThreadPool>
#include <optional>
#include <QJsonObject>

struct JpegResult {
    QString path, error, colorNote;
    QSize originalSize;
    QImage image;
    QJsonObject timings;
};
JpegResult decodeJpeg(const QString &path, int longEdge = 3840);

// One decode in flight, one replaceable preview request, small thumbnail results.
class JpegLoader : public QObject {
    Q_OBJECT
public:
    explicit JpegLoader(QObject *parent = nullptr);
    ~JpegLoader() override;
    void preview(const QString &path, quint64 token);
    void thumbnail(const QString &path);
    void invalidate();
signals:
    void ready(const JpegResult &result, quint64 token, bool preview);
private:
    struct Request { QString path; quint64 token; bool preview; };
    void dispatch();
    QThreadPool pool;
    QFutureWatcher<JpegResult> watcher;
    std::optional<Request> pending;
    QQueue<Request> thumbnails;
    Request active;
    bool busy = false, closing = false, activeInvalid=false;
};
Q_DECLARE_METATYPE(JpegResult)
