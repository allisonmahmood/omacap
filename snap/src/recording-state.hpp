/** @fileoverview Whether OmaCap's video process is counting down or
 *  recording, so nothing of the screenshot side shows up in the recording. */
#pragma once

#include <QFileSystemWatcher>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>

class QSocketNotifier;
class QWidget;

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

/// Keeps every window of this process out of OmaCap's recordings. While
/// `recording` says one runs, each top-level window goes off screen, and one
/// shown meanwhile never reaches it; popups and tooltips close. The others
/// come back, parents first, when it ends. Going off screen is not hiding:
/// nothing about the window changes, so a dialog waiting in exec(), such as
/// Save As's overwrite question, keeps waiting, and one closed meanwhile
/// stays closed. `self` conceals itself, if given: a pin stays mapped where
/// it is and paints nothing.
class RecordingConcealment final : public QObject {
  Q_OBJECT
public:
  explicit RecordingConcealment(const RecordingWatcher &recording,
                                QWidget *self = nullptr);

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  void setConcealed(bool concealed);
  void conceal(QWidget *window);
  QPointer<QWidget> self_;
  /// Off screen for the recording, in the order they went.
  QList<QPointer<QWidget>> concealed_;
  bool active_ = false;
};
