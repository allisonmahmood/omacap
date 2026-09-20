#include "backend.h"
#include "exporter.h"
#include <QApplication>
#include <QDir>
#include <QLockFile>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QStandardPaths>
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setApplicationName("omacap");
    app.setOrganizationName("OmaCap");
    app.setApplicationVersion("0.1.0");
    if (app.arguments().contains("--export")) {
        int i = app.arguments().indexOf("--export");
        return i + 1 < argc ? exportRecording(app.arguments()[i + 1]) : 2;
    }
    QQuickStyle::setStyle("Basic");
    QString data = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(data);
    QLockFile lock(data + "/app.lock");
    if (!lock.tryLock(100)) {
        qWarning("OmaCap is already running.");
        return 1;
    }
    Theme theme;
    QQmlApplicationEngine engine;
    auto frames = new Frames;
    engine.addImageProvider("frames", frames);
    Backend backend(&theme, frames);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/qml/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;
    if (argc > 1)
        backend.testLoad(QFileInfo(app.arguments()[1]).absoluteFilePath());
    int result = app.exec();
    for (auto root : engine.rootObjects())
        delete root;
    return result;
}
