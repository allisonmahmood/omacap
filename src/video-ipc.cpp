#include "video-ipc.h"
#include <QDir>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QStandardPaths>

QString omacapRuntimeDir() {
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + "/omacap";
    QDir().mkpath(dir);
    QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                   QFileDevice::ExeOwner);
    return dir;
}

QString videoOwnerSocketPath() { return omacapRuntimeDir() + "/video.sock"; }

QString recordingMarkerPath() { return omacapRuntimeDir() + "/recording"; }

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
                        if (target)
                            target->write(QJsonDocument(answer).toJson(QJsonDocument::Compact) +
                                          '\n');
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
