#pragma once
#include <QDateTime>
#include <QJsonObject>
#include <QList>
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
// them (answerTimeoutMs). A refusal while a capture runs also carries "deferred":true: the
// video process tells the user itself once the capture has ended, and the caller must not
// show it (a notification now would be recorded). A video process that is exiting closes a
// command's connection without a reply; the caller then starts a video process of its own.

// Per-session runtime directory for OmaCap's sockets, locks and markers (0700).
// $OMACAP_RUNTIME_DIR overrides it, so tests never meet a running OmaCap.
QString omacapRuntimeDir();
QString videoOwnerSocketPath();
// Exists from the moment a recording is requested until its capture worker has exited, and
// holds one decimal PID per line: the video process's, then the worker's once it runs. While
// any process it lists is running, no other OmaCap surface may show (pins, previews, the
// picker, notifications), so none ends up in a recording. The video process writes it whole
// (written aside, then renamed over it); one listing only exited processes is left over from
// a crash and means nothing.
QString recordingMarkerPath();
// The processes recordingMarkerPath() lists; empty when there is none.
QList<qint64> recordingMarkerPids();
// Whether `pid` is running. One that has exited but not been reaped is not.
bool processRunning(qint64 pid);

// The desktop notifications OmaCap processes have shown since the last recording started, so it
// can take them down before it records. Under an exclusive flock(2) on the file, and only while
// the marker lists no running process, a sender appends `?`, sends with
// `omarchy-notification-send -p` within notificationSendMs, and appends the id that prints: a
// `?` with no id after it stands for one that may be up without an id to close it by (the send
// timed out, or its id couldn't be read or listed). A send that failed takes its `?` back.
// sendNotification() does this; the recorder takes the list under the same lock, after writing
// the marker.
QString notificationIdsPath();
// How long a sender may take to show a notification, holding notificationIdsPath()'s lock.
constexpr int notificationSendMs = 2000;
// Shows `omarchy-notification-send args` and lists it, unless a recording runs (it would be
// recorded). Waits for the notification daemon, up to notificationSendMs. True once shown; false
// when it wasn't, or may not have been.
bool sendNotification(const QStringList &args);
// What notificationIdsPath() listed: the ids, whether one may be up without an id, and when the
// newest was listed (invalid when nothing was).
struct ShownNotifications {
    QList<quint32> ids;
    bool unnamed = false;
    QDateTime newest;
};
// Takes what notificationIdsPath() lists, emptying it. Nothing when its lock stays held past any
// sender's bound: what it lists is then unknown.
std::optional<ShownNotifications> takeShownNotifications();

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
    // Stops accepting connections and removes the socket.
    void close();
    QString errorString() const;
  signals:
    void command(const QJsonObject &command, const VideoOwnerServer::Reply &reply);

  private:
    QLocalServer *server;
};
