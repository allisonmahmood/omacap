#include "video-app.h"
#include "backend.h"
#include "exporter.h"
#include <QApplication>
#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QSocketNotifier>
#include <QStandardPaths>
#include <QTextStream>
#include <cerrno>
#include <fcntl.h>
#include <memory>
#include <signal.h>
#include <unistd.h>
namespace {
int quitPipe[2] = {-1, -1};
// Async-signal-safe: the event loop sees the byte and quits. The pipe never blocks; when
// it's full, a quit is already pending.
void requestQuit(int) {
    const int saved = errno;
    const char byte = 1;
    [[maybe_unused]] const auto written = ::write(quitPipe[1], &byte, 1);
    errno = saved;
}
} // namespace
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
    QString data = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(data);
    const bool screenshot = app.arguments().contains("--screenshot");
    const QString requests = data + "/app.sock";
    QLockFile lock(data + "/app.lock");
    if (!lock.tryLock(100)) {
        // A screenshot hotkey still works while OmaCap is open: the running window takes it,
        // and closes the connection once it has the request.
        if (screenshot) {
            QLocalSocket socket;
            socket.connectToServer(requests);
            if (socket.waitForConnected(1000) && socket.write("screenshot\n") > 0 &&
                socket.waitForBytesWritten(1000) &&
                (socket.state() == QLocalSocket::UnconnectedState ||
                 socket.waitForDisconnected(2000)))
                return 0;
        }
        qWarning("OmaCap is already running.");
        return 1;
    }
    // SIGINT and SIGTERM quit through the event loop rather than killing the process, so
    // the backend's destructor still ends a screen freeze left by an interrupted capture.
    std::unique_ptr<QSocketNotifier> quitting;
    if (::pipe2(quitPipe, O_CLOEXEC | O_NONBLOCK) == 0) {
        quitting = std::make_unique<QSocketNotifier>(quitPipe[0], QSocketNotifier::Read);
        QObject::connect(quitting.get(), &QSocketNotifier::activated, &app, [&] {
            char bytes[256];
            while (::read(quitPipe[0], bytes, sizeof bytes) > 0) {
            }
            quitting->setEnabled(false);
            app.quit();
        });
        struct sigaction action = {};
        action.sa_handler = requestQuit;
        sigaction(SIGINT, &action, nullptr);
        sigaction(SIGTERM, &action, nullptr);
    }
    // The lock is ours, so a socket left at this path belongs to a crashed instance.
    QLocalServer::removeServer(requests);
    QLocalServer server;
    if (!server.listen(requests))
        qWarning("OmaCap cannot accept screenshot requests: %s", qPrintable(server.errorString()));
    Theme theme;
    QQmlApplicationEngine engine;
    auto frames = new Frames;
    engine.addImageProvider("frames", frames);
    Backend backend(&theme, frames);
    QObject::connect(&server, &QLocalServer::newConnection, &backend, [&server, &backend] {
        while (auto client = server.nextPendingConnection()) {
            QObject::connect(client, &QLocalSocket::disconnected, client, &QObject::deleteLater);
            QObject::connect(client, &QLocalSocket::readyRead, &backend, [client, &backend] {
                if (client->readAll().startsWith("screenshot"))
                    emit backend.screenshotRequested();
                client->disconnectFromServer();
            });
        }
    });
    // Start the pick before the window loads, so it opens hidden.
    if (screenshot)
        backend.screenshotAtLaunch();
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/qml/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;
    if (!screenshot && argc > 1)
        backend.testLoad(QFileInfo(app.arguments()[1]).absoluteFilePath());
    int result = app.exec();
    for (auto root : engine.rootObjects())
        delete root;
    return result;
}
