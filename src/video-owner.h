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
    // marker left by a crashed owner is removed.
    bool acquire();
    // Starts accepting commands, acts on this process's arguments (`--record-request <json>`
    // or a video file), and quits the app once the backend has nothing left to do.
    void attach(Backend &backend);

  private:
    QLockFile lock;
    VideoOwnerServer server;
    std::unique_ptr<QSocketNotifier> quitting;
    void handleQuitSignals();
};

// Hands `--record-request <json>` or a video file to the running video process. Returns this
// process's exit code, or nothing when no video process answered.
std::optional<int> forwardToVideoOwner(const QStringList &args);
// The first argument naming a video file, made absolute; empty when there is none.
QString videoArgument(const QStringList &args);
