/** @fileoverview Follows OmaCap's recording marker. */
#include "recording-state.hpp"

#include "video-ipc.h"

#include <QFile>
#include <QFileInfo>
#include <QSocketNotifier>

#include <cerrno>
#include <csignal>
#include <sys/syscall.h>
#include <unistd.h>

namespace {
/// The process the marker names: its first line, in decimal. 0 when there
/// is no marker or it names nothing.
qint64 markerPid() {
  QFile marker(recordingMarkerPath());
  if (!marker.open(QIODevice::ReadOnly))
    return 0;
  bool ok = false;
  const qint64 pid = marker.readLine(32).trimmed().toLongLong(&ok);
  return ok && pid > 0 ? pid : 0;
}

bool alive(qint64 pid) {
  return pid > 0 && (::kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM);
}
} // namespace

bool recordingInProgress() { return alive(markerPid()); }

RecordingWatcher::RecordingWatcher(QObject *parent) : QObject(parent) {
  files_.addPath(omacapRuntimeDir());
  connect(&files_, &QFileSystemWatcher::directoryChanged, this,
          [this] { refresh(); });
  connect(&files_, &QFileSystemWatcher::fileChanged, this, [this] { refresh(); });
  refresh();
}

RecordingWatcher::~RecordingWatcher() { watchRecorder(0); }

void RecordingWatcher::refresh() {
  // The marker's own writes change no directory entry; watch it too while
  // it exists (a removed path drops out of the watcher by itself).
  const QString marker = recordingMarkerPath();
  if (QFileInfo::exists(marker) && !files_.files().contains(marker))
    files_.addPath(marker);
  const qint64 pid = markerPid();
  const bool recording = alive(pid);
  watchRecorder(recording ? pid : 0);
  if (recording != recording_) {
    recording_ = recording;
    emit recordingChanged(recording_);
  }
}

void RecordingWatcher::watchRecorder(qint64 pid) {
  if (pid == watchedPid_)
    return;
  if (recorderExit_) {
    // This can run inside the notifier's own signal: retire it, then close.
    const int fd = static_cast<int>(recorderExit_->socket());
    recorderExit_->setEnabled(false);
    recorderExit_->deleteLater();
    recorderExit_ = nullptr;
    ::close(fd);
  }
  watchedPid_ = 0;
  if (pid <= 0)
    return;
  // Readable once the process exits: a crashed recorder never removes its
  // marker, and nothing else would tell us.
  const int fd = static_cast<int>(::syscall(SYS_pidfd_open, pid, 0));
  if (fd < 0)
    return;
  watchedPid_ = pid;
  recorderExit_ = new QSocketNotifier(fd, QSocketNotifier::Read, this);
  connect(recorderExit_, &QSocketNotifier::activated, this,
          [this] { refresh(); });
}
