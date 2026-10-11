/** @fileoverview Whether OmaCap's video process is counting down or
 *  recording, so nothing of the screenshot side shows up in the recording. */
#pragma once

#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>

class QSocketNotifier;

/// The recording marker (recordingMarkerPath()) exists and names a live
/// process. It names one per line: the video process and, once it runs, its
/// capture worker, which can outlive it. A marker whose processes are all
/// gone, as a crashed recorder leaves it, does not count.
[[nodiscard]] bool recordingInProgress();

/// Follows recordingInProgress() without polling: inotify for the marker
/// coming, changing and going, and a pidfd for each process it names, which
/// can die without removing it.
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
  QFileSystemWatcher files_;
  /// An exit notifier per live process the marker names, by PID.
  QHash<qint64, QSocketNotifier *> exits_;
  bool recording_ = false;
};
