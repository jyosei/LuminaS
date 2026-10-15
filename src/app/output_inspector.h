#pragma once
#include <QWidget>
#include <QString>
#include <QImage>
#include <QFutureWatcher>
#include <QThreadPool>
#include <atomic>
class Canvas;
class QLabel;
class QPushButton;

// Read-only view of a file this session actually exported. It never re-imports the
// output as a new source photo and never edits anything.
class OutputInspector:public QWidget {
    Q_OBJECT
public:
    struct ReadResult { QImage image; QString error; QString path; };
    explicit OutputInspector(QWidget *parent=nullptr);
    ~OutputInspector() override;
    // Returns false and reports why when the path cannot be inspected.
    bool inspect(const QString &path,QString &error);
    QString path() const{return currentPath;}
    Canvas *canvas() const{return view;}
signals:
    void inspected(const QString &path,bool success);
private:
    void apply(const ReadResult &result);
    Canvas *view=nullptr;
    QLabel *status=nullptr;
    QPushButton *fitButton=nullptr,*actualButton=nullptr;
    QString currentPath;
    quint64 generation=0;
    QThreadPool pool;
    QFutureWatcher<ReadResult> watcher;
    std::atomic_bool closing{false};
};
