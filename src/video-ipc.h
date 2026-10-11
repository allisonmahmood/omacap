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
//                         it stopped a countdown or recording, {"action":"pick"} when it
//                         ended the session and the caller should open the picker, or
//                         {"action":"busy"} to do nothing.
//   {"cmd":"record","request":{...RecordRequest}}  Starts the countdown. Replies {"ok":true}
//                         or {"ok":false,"error":…}, e.g. while recording or exporting.
//   {"cmd":"open","path":"/abs/video.mp4"}         Opens the video in the editor. Replies like
//                         `record`.
//
// A command that would replace unsaved edits asks the user first, so its reply can wait for
// them (answerTimeoutMs).

// Per-session runtime directory for OmaCap's sockets, locks and markers (0700).
// $OMACAP_RUNTIME_DIR overrides it, so tests never meet a running OmaCap.
QString omacapRuntimeDir();
QString videoOwnerSocketPath();
// Exists exactly while a countdown or recording is running. Previews and pins hide while it
// exists, so no OmaCap surface ends up in a recording.
QString recordingMarkerPath();

// Sends one command to the running video process. Returns its reply, or nothing when no
// video process is running or it did not answer in time.
std::optional<QJsonObject> askVideoOwner(const QJsonObject &command, int timeoutMs = 3000);
// How long to wait for a command that may first ask the user about unsaved edits.
constexpr int answerTimeoutMs = 120000;

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
