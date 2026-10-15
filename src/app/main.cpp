#include "window.h"
#include <QApplication>
#include <QImageReader>
#include <QTimer>
#include <QDebug>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
int main(int argc, char **argv) {
    QElapsedTimer elapsed;elapsed.start();
    if(qEnvironmentVariableIsEmpty("OMP_NUM_THREADS"))qputenv("OMP_NUM_THREADS","4");
    if(qEnvironmentVariableIsEmpty("OMP_WAIT_POLICY"))qputenv("OMP_WAIT_POLICY","PASSIVE");
    QApplication app(argc, argv);
    // 45MP RGBA16 PNG output inspection requires up to 360MB. Every input
    // reader still checks the explicit 45MP geometry before decoding.
    QImageReader::setAllocationLimit(512);
    QApplication::setApplicationName("LuminaS");
    QStringList paths = app.arguments().mid(1);
    // Reproducible native startup/clean-exit smoke check, using the same window.
    const bool smoke = paths.removeAll("--smoke-test") > 0;
    QString reportPath;const int reportIndex=paths.indexOf("--smoke-report");
    if(reportIndex>=0){
        if(!smoke||reportIndex+1>=paths.size())return 3;
        reportPath=paths[reportIndex+1];paths.removeAt(reportIndex+1);paths.removeAt(reportIndex);
        if(reportPath.isEmpty()||QFileInfo::exists(reportPath))return 3;
    }
    Window window; window.show();
    bool photoCommitted=paths.isEmpty();int nativePaints=0;
    QObject::connect(window.findChild<Canvas *>(),&Canvas::framePainted,&app,[&]{++nativePaints;});
    if (smoke) {
        qInfo() << "Native startup" << app.applicationName() << "Qt" << qVersion() << "platform" << QGuiApplication::platformName();
        if (!paths.isEmpty()) {
            QObject::connect(&window, &Window::previewChanged, &app, [&app,&photoCommitted](const QString &path, bool success) {
                photoCommitted=success;
                qInfo() << "Photo preview committed" << path << "success" << success;
                QTimer::singleShot(300, &app, [&app, success] { app.exit(success ? 0 : 1); });
            });
            QTimer::singleShot(120000, &app, [&app] { app.exit(2); });
        } else QTimer::singleShot(3000, &app, &QCoreApplication::quit);
    }
    window.addFiles(paths);
    const int result=app.exec();
    if(!reportPath.isEmpty()){
        QSaveFile report(reportPath);if(!report.open(QIODevice::WriteOnly))return 4;
        const auto bytes=QJsonDocument(QJsonObject{{"exitCode",result},{"photoCommitted",photoCommitted},{"nativePhotoPaints",nativePaints},{"hasPhoto",!paths.isEmpty()},{"elapsedMs",elapsed.elapsed()},{"qt",QString::fromLatin1(qVersion())},{"platform",QGuiApplication::platformName()}}).toJson();
        if(report.write(bytes)!=bytes.size()||!report.commit())return 4;
    }
    return result;
}
