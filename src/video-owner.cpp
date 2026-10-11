#include "video-owner.h"
#include "backend.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <cerrno>
#include <fcntl.h>
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

VideoOwner::VideoOwner() : lock(omacapRuntimeDir() + "/video.lock") {}

bool VideoOwner::acquire() {
    if (!lock.tryLock(100))
        return false;
    handleQuitSignals();
    return true;
}

// SIGINT and SIGTERM quit through the event loop rather than killing the process, so the
// backend's destructor still finalizes a running recording.
void VideoOwner::handleQuitSignals() {
    if (::pipe2(quitPipe, O_CLOEXEC | O_NONBLOCK) != 0)
        return;
    quitting = std::make_unique<QSocketNotifier>(quitPipe[0], QSocketNotifier::Read);
    connect(quitting.get(), &QSocketNotifier::activated, this, [this] {
        char bytes[256];
        while (::read(quitPipe[0], bytes, sizeof bytes) > 0) {
        }
        quitting->setEnabled(false);
        QCoreApplication::quit();
    });
    struct sigaction action = {};
    action.sa_handler = requestQuit;
    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);
}

void VideoOwner::attach(Backend &backend) {
    if (!server.listen())
        qWarning("OmaCap cannot accept commands: %s", qPrintable(server.errorString()));
    connect(&server, &VideoOwnerServer::command, &backend,
            [&backend](const QJsonObject &command, const VideoOwnerServer::Reply &reply) {
                const QString cmd = command["cmd"].toString();
                if (cmd == "open") {
                    backend.loadFile(QFileInfo(command["path"].toString()).absoluteFilePath());
                    reply({{"ok", true}});
                } else {
                    reply({{"ok", false}, {"error", "Unknown command: " + cmd}});
                }
            });
}
