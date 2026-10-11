#include "video-app.h"
#include "backend.h"
#include "exporter.h"
#include "video-owner.h"
#include <QApplication>
#include <QFileInfo>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTextStream>

int runVideoApp(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setApplicationName("omacap");
    app.setOrganizationName("OmaCap");
    app.setApplicationVersion(QStringLiteral(OMACAP_VERSION));
    if (app.arguments() == QStringList{app.arguments().first(), "--version"}) {
        QTextStream(stdout) << app.applicationVersion() << Qt::endl;
        return 0;
    }
    if (app.arguments().contains("--export")) {
        int i = app.arguments().indexOf("--export");
        return i + 1 < argc ? exportRecording(app.arguments()[i + 1]) : 2;
    }
    QQuickStyle::setStyle("Basic");
    VideoOwner owner;
    if (!owner.acquire()) {
        // A video opened while OmaCap is busy goes to the running video process.
        if (argc > 1 && askVideoOwner({{"cmd", "open"},
                                       {"path", QFileInfo(app.arguments()[1]).absoluteFilePath()}}))
            return 0;
        qWarning("OmaCap is already running.");
        return 1;
    }
    Theme theme;
    QQmlApplicationEngine engine;
    auto frames = new Frames;
    engine.addImageProvider("frames", frames);
    Backend backend(&theme, frames);
    owner.attach(backend);
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
