#pragma once
#include "video-ipc.h"
#include <QLockFile>
#include <QObject>
#include <QSocketNotifier>
#include <memory>

class Backend;

// Single-instance ownership of recording and video editing: the video lock, the command
// socket, and SIGINT/SIGTERM handling that quits through the event loop.
class VideoOwner : public QObject {
    Q_OBJECT
  public:
    VideoOwner();
    // Takes the video lock. False when another video process already owns it.
    bool acquire();
    // Starts accepting commands and routes them to the backend.
    void attach(Backend &backend);

  private:
    QLockFile lock;
    VideoOwnerServer server;
    std::unique_ptr<QSocketNotifier> quitting;
    void handleQuitSignals();
};
