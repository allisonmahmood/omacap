/** @fileoverview Whether OmaCap's video process is counting down or
 *  recording, so nothing of the screenshot side shows up in the recording. */
#pragma once

#include <QFileSystemWatcher>
#include <QObject>

class QSocketNotifier;

/// The recording marker (recordingMarkerPath()) exists and names a live
/// process. A marker a crashed recorder left behind does not count.
[[nodiscard]] bool recordingInProgress();

/// Follows recordingInProgress() without polling: inotify for the marker
/// coming, changing and going, and a pidfd for a recorder that dies without
/// removing it.
class RecordingWatcher final : public QObject {
  Q_OBJECT
public:
  explicit RecordingWatcher(QObject *parent = nullptr);
  ~RecordingWatcher() override;
  [[nodiscard]] bool recording() const { return recording_; }

signals:
  void recordingChanged(bool recording);

private:
  void refresh();
  void watchRecorder(qint64 pid);
  QFileSystemWatcher files_;
  QSocketNotifier *recorderExit_ = nullptr;
  qint64 watchedPid_ = 0;
  bool recording_ = false;
};
