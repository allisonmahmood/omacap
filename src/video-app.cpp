#include "video-app.h"
#include "backend.h"
#include "exporter.h"
#include "overlay-chrome.hpp"
#include "video-owner.h"
#include "video-stage.h"
#include <QApplication>
#include <QDeadlineTimer>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QThread>

namespace {
// The names QSettings and QStandardPaths file the video process's settings and sessions under.
void nameApp() {
    QCoreApplication::setApplicationName("omacap");
    QCoreApplication::setOrganizationName("OmaCap");
}
} // namespace

bool videoRecoverable() {
    nameApp();
    return Backend::recoverable();
}

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
    // Another video process started first: it records the request or opens the file. One that
    // is starting listens only once it is ready, and one that is exiting answers nothing and
    // then lets go of the lock, so keep trying to hand the command over, or to take its place.
    const auto command = videoCommand(app.arguments().mid(1));
    const QDeadlineTimer deadline(10000);
    while (!owner.acquire()) {
        if (const auto forwarded = command ? forwardToVideoOwner(*command) : std::nullopt)
            return *forwarded;
        if (!command || deadline.hasExpired()) {
            qWarning("OmaCap is already running.");
            return 1;
        }
        QThread::msleep(50);
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
