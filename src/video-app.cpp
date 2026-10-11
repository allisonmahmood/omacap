#include "video-app.h"
#include "backend.h"
#include "exporter.h"
#include "overlay-chrome.hpp"
#include "video-owner.h"
#include "video-stage.h"
#include <QApplication>
#include <QFileInfo>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QTextStream>

int runVideoApp(int argc, char **argv) {
    // As in Omasnap: LayerShellQt::Window::get gives only the overlay and the card a layer
    // role, so the floating editor and choosers keep Qt's xdg-shell integration. Qt's generic
    // platform theme skips loading GTK; choosers are Qt's own, styled from the Omarchy theme.
    qunsetenv("QT_WAYLAND_SHELL_INTEGRATION");
    qputenv("QT_QPA_PLATFORMTHEME", "generic");
    QGuiApplication::setDesktopFileName("omacap");
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
    QApplication::setFont(chromeDefaultFont());
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
    QQmlEngine engine;
    auto frames = new Frames;
    engine.addImageProvider("frames", frames);
    Backend backend(&theme, frames);
    owner.attach(backend);
    VideoStage stage(engine, backend, theme);
    if (!stage.load())
        return 1;
    // The process lives as long as its card, editor or recording does.
    app.setQuitOnLastWindowClosed(false);
    QObject::connect(&stage, &VideoStage::finished, &app, &QCoreApplication::quit);
    if (argc > 1)
        backend.testLoad(QFileInfo(app.arguments()[1]).absoluteFilePath());
    else
        stage.offerRecovery();
    return app.exec();
}
