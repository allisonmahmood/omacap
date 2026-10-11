#include "video-app.h"
#include "backend.h"
#include "exporter.h"
#include "overlay-chrome.hpp"
#include "video-owner.h"
#include "video-stage.h"
#include <QApplication>
#include <QQmlEngine>
#include <QQuickStyle>

namespace {
// The names QSettings and QStandardPaths file the video process's settings and sessions under.
void nameApp() {
    QCoreApplication::setApplicationName("omacap");
    QCoreApplication::setOrganizationName("OmaCap");
}
} // namespace

int runVideoApp(int argc, char **argv) {
    // As in Omasnap: LayerShellQt::Window::get gives only the overlay and the card a layer
    // role, so the floating editor and choosers keep Qt's xdg-shell integration. Qt's generic
    // platform theme skips loading GTK; choosers are Qt's own, styled from the Omarchy theme.
    qunsetenv("QT_WAYLAND_SHELL_INTEGRATION");
    qputenv("QT_QPA_PLATFORMTHEME", "generic");
    QGuiApplication::setDesktopFileName("omacap");
    nameApp();
    QApplication app(argc, argv);
    app.setApplicationVersion(QStringLiteral(OMACAP_VERSION));
    if (app.arguments().contains("--export")) {
        int i = app.arguments().indexOf("--export");
        return i + 1 < argc ? exportRecording(app.arguments()[i + 1]) : 2;
    }
    QApplication::setFont(chromeDefaultFont());
    QQuickStyle::setStyle("Basic");
    VideoOwner owner;
    if (!owner.acquire()) {
        // Another video process started first: it records the request or opens the file.
        if (const auto forwarded = forwardToVideoOwner(app.arguments().mid(1)))
            return *forwarded;
        qWarning("OmaCap is already running.");
        return 1;
    }
    Theme theme;
    QQmlEngine engine;
    auto frames = new Frames;
    engine.addImageProvider("frames", frames);
    Backend backend(&theme, frames);
    VideoStage stage(engine, backend, theme);
    if (!stage.load())
        return 1;
    // The backend quits the app once no card or editor is open and nothing records, prepares
    // or exports.
    app.setQuitOnLastWindowClosed(false);
    if (!owner.attach(backend))
        stage.offerRecovery();
    return app.exec();
}
