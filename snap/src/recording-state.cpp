/** @fileoverview Follows OmaCap's recording marker. */
#include "recording-state.hpp"

#include "video-ipc.h"

#include <QApplication>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QMetaObject>
#include <QSocketNotifier>
#include <QWidget>
#include <QWindow>

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <poll.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <utility>

namespace {
int openPidfd(qint64 pid) {
  return static_cast<int>(::syscall(SYS_pidfd_open, pid, 0));
}

/// Whether the process behind `pidfd` is still running. A pidfd says so even
/// for a process that has exited but not been reaped yet, where kill(pid, 0)
/// still succeeds.
bool running(int pidfd) {
  pollfd exited{pidfd, POLLIN, 0};
  return ::poll(&exited, 1, 0) == 0;
}

/// Whether `pid` is still running, where pidfd_open() failed with `error`.
bool runningWithoutPidfd(qint64 pid, int error) {
  if (error == ESRCH)
    return false;
  return ::kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM;
}

bool alive(qint64 pid) {
  const int fd = openPidfd(pid);
  if (fd < 0)
    return runningWithoutPidfd(pid, errno);
  const bool result = running(fd);
  ::close(fd);
  return result;
}

/// Stops and closes an exit notifier. This can run inside its own signal:
/// retire it, then close its pidfd.
void retire(QSocketNotifier *exit) {
  const int fd = static_cast<int>(exit->socket());
  exit->setEnabled(false);
  exit->deleteLater();
  ::close(fd);
}
} // namespace

bool recordingInProgress() { return std::ranges::any_of(recordingMarkerPids(), alive); }

RecordingWatcher::RecordingWatcher(QObject *parent) : QObject(parent) {
  files_.addPath(omacapRuntimeDir());
  connect(&files_, &QFileSystemWatcher::directoryChanged, this,
          [this] { refresh(); });
  connect(&files_, &QFileSystemWatcher::fileChanged, this, [this] { refresh(); });
  refresh();
}

RecordingWatcher::~RecordingWatcher() {
  for (QSocketNotifier *exit : std::as_const(exits_))
    retire(exit);
}

void RecordingWatcher::refresh() {
  // The marker's own writes change no directory entry, so watch the file
  // too. Re-arm that watch each time: one replaced by a rename still points
  // at the file it replaced.
  const QString marker = recordingMarkerPath();
  if (files_.files().contains(marker))
    files_.removePath(marker);
  if (QFileInfo::exists(marker))
    files_.addPath(marker);

  // Each live process keeps a notifier, readable once it exits: a crashed
  // recorder never removes its marker, and nothing else would tell us.
  // running() then sees it exited, so this retires its notifier rather than
  // spinning on it.
  QHash<qint64, QSocketNotifier *> watched;
  bool unwatched = false;
  for (const qint64 pid : recordingMarkerPids()) {
    QSocketNotifier *exit = exits_.take(pid);
    if (!exit) {
      const int fd = openPidfd(pid);
      if (fd < 0) {
        unwatched = runningWithoutPidfd(pid, errno) || unwatched;
        continue;
      }
      exit = new QSocketNotifier(fd, QSocketNotifier::Read, this);
      connect(exit, &QSocketNotifier::activated, this, [this] { refresh(); });
    }
    if (running(static_cast<int>(exit->socket())))
      watched.insert(pid, exit);
    else
      retire(exit);
  }
  // Processes the marker no longer names, or with it gone.
  for (QSocketNotifier *exit : std::as_const(exits_))
    retire(exit);
  exits_ = std::move(watched);

  const bool recording = unwatched || !exits_.isEmpty();
  if (recording != recording_) {
    recording_ = recording;
    emit recordingChanged(recording_);
  }
}

RecordingConcealment::RecordingConcealment(const RecordingWatcher &recording,
                                           QWidget *self)
    : self_(self) {
  qApp->installEventFilter(this);
  connect(&recording, &RecordingWatcher::recordingChanged, this,
          &RecordingConcealment::setConcealed);
  setConcealed(recording.recording());
}

bool RecordingConcealment::eventFilter(QObject *watched, QEvent *event) {
  // A window's Show event comes before it maps; concealed now, it never does.
  if (active_ && event->type() == QEvent::Show && watched->isWidgetType()) {
    auto *widget = static_cast<QWidget *>(watched);
    if (widget->isWindow())
      conceal(widget);
  }
  return QObject::eventFilter(watched, event);
}

void RecordingConcealment::setConcealed(bool concealed) {
  if (concealed == active_)
    return;
  active_ = concealed;
  if (concealed) {
    for (QWidget *window : QApplication::topLevelWidgets()) {
      if (window->isVisible())
        conceal(window);
    }
    return;
  }
  // Parents first: each window comes back over the one it belongs to.
  QList<QPointer<QWidget>> windows = std::exchange(concealed_, {});
  const auto depth = [](const QPointer<QWidget> &window) {
    int levels = 0;
    for (QWidget *parent = window ? window->parentWidget() : nullptr; parent;
         parent = parent->parentWidget())
      ++levels;
    return levels;
  };
  std::ranges::stable_sort(windows, {}, depth);
  for (const QPointer<QWidget> &window : std::as_const(windows)) {
    if (!window)
      continue;
    window->setAttribute(Qt::WA_DontShowOnScreen, false);
    // The widget never stopped being visible: this maps its window again.
    // One hidden meanwhile shows as usual next time.
    if (window->isVisible())
      window->windowHandle()->setVisible(true);
  }
}

void RecordingConcealment::conceal(QWidget *window) {
  // A window kept off screen for its own reasons stays as it is.
  if (window == self_ || window->testAttribute(Qt::WA_DontShowOnScreen))
    return;
  // On a visible window this unmaps it, and nothing else.
  window->setAttribute(Qt::WA_DontShowOnScreen, true);
  concealed_.append(window);
  // A menu or tooltip would be stale by the time the recording ends.
  const Qt::WindowType type = window->windowType();
  if (type == Qt::Popup || type == Qt::ToolTip)
    QMetaObject::invokeMethod(window, &QWidget::close, Qt::QueuedConnection);
}
