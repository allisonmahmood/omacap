#pragma once
#include "video-ipc.h"
#include <QLockFile>
#include <QObject>
#include <QSocketNotifier>
#include <QStringList>
#include <memory>
#include <optional>

class Backend;

// Single-instance ownership of recording and video editing: the video lock, the command
// socket, and SIGINT/SIGTERM handling that quits through the event loop.
class VideoOwner : public QObject {
    Q_OBJECT
  public:
    VideoOwner();
    // Takes the video lock. False when another video process already owns it. A recording
    // marker left by an owner that died goes once nothing it lists runs any more.
    bool acquire();
    // Starts accepting commands and quits the app when the backend asks to exit, then acts on
    // this process's arguments: records `--record-request <json>` or opens a video file.
    // Returns false when they name neither.
    bool attach(Backend &backend);

  private:
    QLockFile lock;
    VideoOwnerServer server;
    std::unique_ptr<QSocketNotifier> quitting;
    Backend *backend = nullptr;
    // Errors to show once the capture has ended.
    QStringList pending;
    void handleQuitSignals();
    void clearStaleMarker();
    bool capturing() const;
    void notify(const QString &message);
};

// Shows an error as a desktop notification, unless a capture is running (it would be
// recorded); then it goes to stderr only.
void notifyError(const QString &message);
// The command `args` ask of the video process: record `--record-request <json>`, or open a
// video file. Nothing when they ask for neither.
std::optional<QJsonObject> videoCommand(const QStringList &args);
// Sends `command` to the running video process, waiting while it asks about unsaved edits.
// Returns this process's exit code, or nothing when no video process answered. A refusal is
// also shown as a notification, unless the video process shows it itself.
std::optional<int> forwardToVideoOwner(const QJsonObject &command);
// The first argument naming a video file, made absolute; empty when there is none.
QString videoArgument(const QStringList &args);
