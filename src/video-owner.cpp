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

QJsonObject result(const QString &error) {
    return error.isEmpty() ? QJsonObject{{"ok", true}} : QJsonObject{{"ok", false}, {"error", error}};
}

// Runs one command against the backend; `reply` is called exactly once. Anything that would
// replace unsaved edits waits for the user's answer first.
void handle(Backend &backend, const QJsonObject &command, const VideoOwnerServer::Reply &reply) {
    const QString cmd = command["cmd"].toString(), phase = backend.phase();
    if (cmd == "record-key") {
        // The record hotkey stops a recording. Otherwise it starts over: the session ends, so
        // nothing of it is under the picker, and the caller opens the picker.
        if (phase == "countdown" || phase == "recording") {
            backend.stop();
            reply({{"action", "stopped"}});
        } else {
            backend.confirmReplace([&backend, reply](bool yes) {
                if (yes)
                    backend.discard();
                reply({{"action", yes ? "pick" : "busy"}});
            });
        }
    } else if (cmd == "record") {
        const auto request = RecordRequest::fromJson(command["request"].toObject());
        if (const QString problem =
                request ? request->unsupported() : "Not a valid recording request.";
            !problem.isEmpty())
            return reply(result(problem));
        backend.confirmReplace([&backend, request = *request, reply](bool yes) {
            reply(result(yes ? backend.startRecording(request) : "OmaCap is busy."));
        });
    } else if (cmd == "open") {
        const QString path = QFileInfo(command["path"].toString()).absoluteFilePath();
        backend.confirmReplace([&backend, path, reply](bool yes) {
            if (yes)
                backend.loadFile(path);
            reply(result(yes ? "" : "OmaCap is busy."));
        });
    } else {
        reply(result("Unknown command: " + cmd));
    }
}
} // namespace

// As Omasnap does, says what went wrong when nothing is open to say it.
void notifyError(const QString &message) {
    QProcess::startDetached("omarchy-notification-send",
                            {"--app-name", "omacap", "-t", "4500", "OmaCap", message});
}

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
    // Queued, so a reply written just before still reaches its caller. A failed startup,
    // such as an invalid request, exits 1.
    connect(&backend, &Backend::exitRequested, qApp, &QCoreApplication::exit, Qt::QueuedConnection);
    connect(&backend, &Backend::backgroundError, this, notifyError);
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
    const auto reply = askVideoOwner(command, answerTimeoutMs);
    if (!reply)
        return std::nullopt;
    if ((*reply)["ok"].toBool())
        return 0;
    // The picker starts this process detached, so stderr alone would go unseen.
    const QString error = (*reply)["error"].toString();
    std::fprintf(stderr, "%s\n", qPrintable(error));
    notifyError(error);
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
