#include "video-owner.h"
#include "backend.h"
#include "record-request.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <cerrno>
#include <cstdio>
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

QJsonObject requestArgument(const QStringList &args) {
    const qsizetype i = args.indexOf("--record-request");
    return i >= 0 && i + 1 < args.size()
               ? QJsonDocument::fromJson(args[i + 1].toUtf8()).object()
               : QJsonObject();
}

// Runs one command against the backend; `reply` is called exactly once.
void handle(Backend &backend, const QJsonObject &command, const VideoOwnerServer::Reply &reply) {
    const QString cmd = command["cmd"].toString(), phase = backend.phase();
    if (cmd == "record-key") {
        // The record hotkey stops a recording, or asks for the picker when one may start.
        if (phase == "countdown" || phase == "recording") {
            backend.stop();
            reply({{"action", "stopped"}});
        } else {
            backend.confirmReplace(
                [reply](bool yes) { reply({{"action", yes ? "pick" : "busy"}}); });
        }
    } else if (cmd == "record") {
        const auto request = RecordRequest::fromJson(command["request"].toObject());
        const QString error =
            request ? backend.startRecording(*request) : "Not a valid recording request.";
        reply(error.isEmpty() ? QJsonObject{{"ok", true}}
                              : QJsonObject{{"ok", false}, {"error", error}});
    } else if (cmd == "open") {
        const QString path = QFileInfo(command["path"].toString()).absoluteFilePath();
        backend.confirmReplace([&backend, path, reply](bool yes) {
            if (yes)
                backend.loadFile(path);
            reply(yes ? QJsonObject{{"ok", true}}
                      : QJsonObject{{"ok", false}, {"error", "OmaCap is busy."}});
        });
    } else {
        reply({{"ok", false}, {"error", "Unknown command: " + cmd}});
    }
}
} // namespace

VideoOwner::VideoOwner() : lock(omacapRuntimeDir() + "/video.lock") {}

bool VideoOwner::acquire() {
    if (!lock.tryLock(100))
        return false;
    // Only the lock holder writes the marker, so one found now outlived its owner.
    QFile::remove(recordingMarkerPath());
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

bool VideoOwner::attach(Backend &backend) {
    if (!server.listen())
        qWarning("OmaCap cannot accept commands: %s", qPrintable(server.errorString()));
    connect(&server, &VideoOwnerServer::command, &backend,
            [&backend](const QJsonObject &command, const VideoOwnerServer::Reply &reply) {
                handle(backend, command, reply);
            });
    // Queued, so a reply written just before still reaches its caller.
    connect(&backend, &Backend::exitRequested, qApp, &QCoreApplication::quit,
            Qt::QueuedConnection);
    // As Omasnap says what went wrong when nothing is open to say it.
    connect(&backend, &Backend::backgroundError, this, [](const QString &message) {
        QProcess::startDetached("omarchy-notification-send",
                                {"--app-name", "omacap", "-t", "4500", "OmaCap", message});
    });
    const QStringList args = QCoreApplication::arguments().mid(1);
    if (args.contains("--record-request")) {
        const auto request = RecordRequest::fromJson(requestArgument(args));
        const QString error = request ? backend.startRecording(*request)
                                      : "OmaCap was given an invalid recording request.";
        if (!error.isEmpty())
            backend.fail(error);
        return true;
    }
    const QString video = videoArgument(args);
    if (video.isEmpty())
        return false;
    backend.loadFile(video);
    return true;
}

std::optional<int> forwardToVideoOwner(const QStringList &args) {
    QJsonObject command;
    if (args.contains("--record-request"))
        command = {{"cmd", "record"}, {"request", requestArgument(args)}};
    else if (const QString video = videoArgument(args); !video.isEmpty())
        command = {{"cmd", "open"}, {"path", video}};
    else
        return std::nullopt;
    const auto reply = askVideoOwner(command);
    if (!reply)
        return std::nullopt;
    if ((*reply)["ok"].toBool())
        return 0;
    std::fprintf(stderr, "%s\n", qPrintable((*reply)["error"].toString()));
    return 1;
}

QString videoArgument(const QStringList &args) {
    static const QStringList extensions{".mp4", ".mkv", ".webm", ".mov", ".m4v", ".avi", ".gif"};
    for (const auto &arg : args)
        if (!arg.startsWith("--") && std::ranges::any_of(extensions, [&](const QString &e) {
                return arg.endsWith(e, Qt::CaseInsensitive);
            }))
            return QFileInfo(arg).absoluteFilePath();
    return {};
}
