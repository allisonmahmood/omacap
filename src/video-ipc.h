#pragma once
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <functional>
#include <optional>

class QLocalServer;

// The video process owns recording, the post-recording card and the video editor. Other
// omacap processes talk to it over a local socket: one JSON object per line, and exactly
// one reply line per command.
//
// Commands:
//   {"cmd":"record-key"}  The record hotkey was pressed. Replies {"action":"stopped"} when
//                         it stopped a countdown or recording, {"action":"pick"} when the
//                         caller should open the picker, or {"action":"busy"} to do nothing.
//   {"cmd":"record","request":{...RecordRequest}}  Replies {"ok":true} or {"ok":false,"error":…}.
//   {"cmd":"open","path":"/abs/video.mp4"}         Replies {"ok":true} or {"ok":false,"error":…}.

// Per-session runtime directory for OmaCap's sockets, locks and markers (0700).
QString omacapRuntimeDir();
QString videoOwnerSocketPath();
// Exists exactly while a countdown or recording is running. Previews and pins hide while it
// exists, so no OmaCap surface ends up in a recording.
QString recordingMarkerPath();

// Sends one command to the running video process. Returns its reply, or nothing when no
// video process is running or it did not answer in time.
std::optional<QJsonObject> askVideoOwner(const QJsonObject &command, int timeoutMs = 3000);

// Listens on videoOwnerSocketPath() and hands each framed command to a handler, which must
// call `reply` exactly once (now or later).
class VideoOwnerServer : public QObject {
    Q_OBJECT
  public:
    using Reply = std::function<void(const QJsonObject &)>;
    explicit VideoOwnerServer(QObject *parent = nullptr);
    // Call only while holding the video lock: it removes a stale socket first.
    bool listen();
    QString errorString() const;
  signals:
    void command(const QJsonObject &command, const VideoOwnerServer::Reply &reply);

  private:
    QLocalServer *server;
};
