#include "video-ipc.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QStandardPaths>
#include <cerrno>
#include <poll.h>
#include <signal.h>
#include <sys/syscall.h>
#include <unistd.h>

QString omacapRuntimeDir() {
    // OMACAP_RUNTIME_DIR isolates test runs from a running OmaCap.
    const QString override = qEnvironmentVariable("OMACAP_RUNTIME_DIR");
    const QString dir = !override.isEmpty()
                            ? override
                            : QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) +
                                  "/omacap";
    QDir().mkpath(dir);
    QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                   QFileDevice::ExeOwner);
    return dir;
}

QString videoOwnerSocketPath() { return omacapRuntimeDir() + "/video.sock"; }

QString recordingMarkerPath() { return omacapRuntimeDir() + "/recording"; }

QList<qint64> recordingMarkerPids() {
    QFile marker(recordingMarkerPath());
    if (!marker.open(QIODevice::ReadOnly))
        return {};
    QList<qint64> pids;
    for (const QByteArray &line : marker.readAll().split('\n')) {
        bool ok = false;
        if (const qint64 pid = line.trimmed().toLongLong(&ok); ok && pid > 0)
            pids << pid;
    }
    return pids;
}

bool processRunning(qint64 pid) {
    if (pid <= 0)
        return false;
    // A pidfd turns readable once the process exits, even before it is reaped, where kill(pid,
    // 0) would still succeed.
    const int fd = int(::syscall(SYS_pidfd_open, pid, 0));
    if (fd < 0)
        return errno != ESRCH && (::kill(pid_t(pid), 0) == 0 || errno == EPERM);
    pollfd exited{fd, POLLIN, 0};
    const bool running = ::poll(&exited, 1, 0) == 0;
    ::close(fd);
    return running;
}

std::optional<QJsonObject> askVideoOwner(const QJsonObject &command, int timeoutMs) {
    QLocalSocket socket;
    socket.connectToServer(videoOwnerSocketPath());
    if (!socket.waitForConnected(timeoutMs))
        return std::nullopt;
    socket.write(QJsonDocument(command).toJson(QJsonDocument::Compact) + '\n');
    if (!socket.waitForBytesWritten(timeoutMs))
        return std::nullopt;
    QByteArray line;
    while (!line.contains('\n')) {
        if (!socket.waitForReadyRead(timeoutMs))
            return std::nullopt;
        line += socket.readAll();
    }
    const auto reply = QJsonDocument::fromJson(line.left(line.indexOf('\n')));
    return reply.isObject() ? std::optional(reply.object()) : std::nullopt;
}

VideoOwnerServer::VideoOwnerServer(QObject *parent)
    : QObject(parent), server(new QLocalServer(this)) {
    connect(server, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket *client = server->nextPendingConnection()) {
            connect(client, &QLocalSocket::disconnected, client, &QObject::deleteLater);
            connect(client, &QLocalSocket::readyRead, this, [this, client] {
                while (client->canReadLine()) {
                    const auto json = QJsonDocument::fromJson(client->readLine().trimmed());
                    QPointer<QLocalSocket> target = client;
                    const Reply reply = [target](const QJsonObject &answer) {
                        if (!target)
                            return;
                        target->write(QJsonDocument(answer).toJson(QJsonDocument::Compact) +
                                      '\n');
                        // Now, so a reply sent just before quitting still arrives.
                        target->flush();
                    };
                    if (json.isObject())
                        emit command(json.object(), reply);
                    else
                        reply({{"ok", false}, {"error", "Not a JSON object."}});
                }
            });
        }
    });
}

bool VideoOwnerServer::listen() {
    QLocalServer::removeServer(videoOwnerSocketPath());
    return server->listen(videoOwnerSocketPath());
}

QString VideoOwnerServer::errorString() const { return server->errorString(); }
