#include "recorder.h"
#include "recording-chrome.h"
#include "recording-layout.h"
#include "video-ipc.h"
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QEvent>
#include <QSaveFile>
#include <QWindow>
#include <signal.h>
#include <sys/prctl.h>
#include <algorithm>
#include <unistd.h>

namespace {
QString tokenKey(const QString &monitor) { return "restoreTokens/" + monitor; }

// A stop after Qt tells of a display change cuts this far back; the worker holds encoded frames
// back for longer (HOLD). The worker's own watch of the recorded monitor is what bounds the cut;
// Qt's notice hides the chrome at once, and also covers the HUD's monitor.
constexpr qint64 changeNoticeNs = 500'000'000;

// `hyprctl args`'s output, or nothing when Hyprland didn't answer in time.
std::optional<QByteArray> hyprctl(const QStringList &args) {
    QProcess process;
    process.start("hyprctl", args);
    if (!process.waitForFinished(2000) || process.exitStatus() != QProcess::NormalExit ||
        process.exitCode() != 0)
        return std::nullopt;
    return process.readAllStandardOutput();
}

// How long Omarchy's shell may keep a toast up: five seconds, for the low urgency OmaCap
// sends, plus its fade. It keeps a toast a sender has closed until then.
constexpr qint64 toastLifetimeMs = 6000;

// Closes the notifications OmaCap processes have shown since the last recording (`shown`): one
// sent just before the request may still be on screen, and must be gone before the first kept
// frame. Returns once every close is answered, within a second, with when the last may still
// show all the same: a notification daemon need not take a toast down when its sender closes it.
QDeadlineTimer closeNotifications(const ShownNotifications &shown) {
    QList<QDBusPendingCall> closing;
    for (const quint32 id : shown.ids) {
        QDBusMessage close = QDBusMessage::createMethodCall(
            "org.freedesktop.Notifications", "/org/freedesktop/Notifications",
            "org.freedesktop.Notifications", "CloseNotification");
        close << id;
        closing << QDBusConnection::sessionBus().asyncCall(close, 1000);
    }
    for (QDBusPendingCall &call : closing)
        call.waitForFinished();
    if (!shown.newest.isValid())
        return QDeadlineTimer(0);
    // One listed without an id may have appeared as late as its send's bound.
    const qint64 lasts = toastLifetimeMs + (shown.unnamed ? notificationSendMs : 0);
    return QDeadlineTimer(
        std::max<qint64>(0, QDateTime::currentDateTime().msecsTo(shown.newest.addMSecs(lasts))));
}

// Nothing but the recording chrome may be on screen once a recording starts. The editor and
// card hide on the countdown phase; this makes sure.
void hideOtherWindows(const RecordingChrome &chrome) {
    for (QWindow *window : QGuiApplication::topLevelWindows())
        if (window->isVisible() && !chrome.owns(window)) {
            qWarning("Hiding a window that was still visible when recording started.");
            window->hide();
        }
}
} // namespace

// While a countdown or recording runs, hides any other window of this process as soon as it
// is shown. The hide is queued, so it runs before the compositor configures the new surface
// and the window never draws a frame.
class WindowGuard : public QObject {
  public:
    explicit WindowGuard(const RecordingChrome &chrome) : chrome(chrome) {
        qApp->installEventFilter(this);
    }

  protected:
    bool eventFilter(QObject *object, QEvent *event) override {
        auto *window = qobject_cast<QWindow *>(object);
        if (event->type() == QEvent::Show && window && !window->parent() && !chrome.owns(window)) {
            qWarning("Hiding a window shown while recording.");
            QMetaObject::invokeMethod(window, &QWindow::hide, Qt::QueuedConnection);
        }
        return false;
    }

  private:
    const RecordingChrome &chrome;
};

Recorder::Recorder(QObject *parent) : QObject(parent) {
    countdownTimer.setInterval(1000);
    connect(&countdownTimer, &QTimer::timeout, this, &Recorder::tick);
    // The worker stops when this process dies, also when it died before the signal was armed.
    worker.setChildProcessModifier([parent = ::getpid()] {
        prctl(PR_SET_PDEATHSIG, SIGTERM);
        if (::getppid() != parent)
            _exit(1);
    });
    connect(&worker, &QProcess::readyReadStandardOutput, this, &Recorder::parseWorker);
    connect(&worker, &QProcess::finished, this, [this] {
        parseWorker();
        countdownTimer.stop();
        guard.reset();
        chrome.reset();
        removeMarker();
        // A recording cut short keeps whatever media was written, for the editor to recover.
        const bool recorded = recording && QFileInfo(output).size() > 1024;
        if (!recorded)
            QFile::remove(output);
        setState(State::Idle);
        emit finished(recorded, error);
    });
    connect(&worker, &QProcess::errorOccurred, this, [this](QProcess::ProcessError problem) {
        if (problem != QProcess::FailedToStart)
            return;
        guard.reset();
        chrome.reset();
        removeMarker();
        setState(State::Idle);
        emit finished(false, "Could not start the recording worker.");
    });
}

Recorder::~Recorder() {
    // The owner is going away: nobody is left to open the recording, which stays for recovery.
    worker.disconnect(this);
    if (worker.state() != QProcess::NotRunning) {
        // The worker finalizes the file on SIGINT.
        ::kill(worker.processId(), SIGINT);
        if (!worker.waitForFinished(8000)) {
            worker.kill();
            worker.waitForFinished(2000);
        }
    }
    if (worker.state() == QProcess::NotRunning)
        removeMarker();
}

QString Recorder::start(const RecordRequest &next, const QString &path, bool synthetic) {
    if (m_state != State::Idle)
        return "OmaCap is already recording.";
    // The chrome and the crop are placed for the monitor as the area was picked on it. The
    // worker records only while it stays that way, so one that changed since gets neither.
    QJsonObject display;
    if (!synthetic) {
        const auto monitors = hyprctl({"-j", "monitors"});
        if (!monitors)
            return "OmaCap couldn't ask Hyprland about the displays.";
        const auto current = requestedDisplay(next, *monitors);
        if (!current)
            return "The display changed; pick the area again.";
        display = *current;
    }
    // First of all: while the marker lists this process, no other OmaCap surface shows.
    if (const QString problem = publishMarker({QCoreApplication::applicationPid()});
        !problem.isEmpty())
        return "OmaCap can't record: " + problem;
    // Then no notification is shown any more, and those still on screen go before the countdown
    // ends. A sender that holds on to the list past its bound leaves them unknown.
    const auto shown = takeShownNotifications();
    if (!shown) {
        removeMarker();
        return "OmaCap is busy showing a notification; try again.";
    }
    // Synthetic recordings (tests) leave the desktop's notifications alone.
    toastsGone = synthetic ? QDeadlineTimer() : closeNotifications(*shown);
    request = next;
    output = path;
    error.clear();
    buffer.clear();
    recording = false;
    m_countdown = 3;
    setState(State::Countdown);
    stopHint = "Run omacap --record again to stop";
    chrome = std::make_unique<RecordingChrome>(request, stopHint);
    connect(chrome.get(), &RecordingChrome::stopClicked, this, &Recorder::stop);
    connect(chrome.get(), &RecordingChrome::displayChanged, this, &Recorder::displayChanged);
    if (!synthetic) {
        guard = std::make_unique<WindowGuard>(*chrome);
        lookUpHotkey();
    }
    const QString script = omacapRuntimeDir() + "/capture.py";
    QFile::remove(script);
    QFile::copy(":/scripts/capture.py", script);
    const QRect crop = encodedArea(request);
    const QSize native = request.nativeSize();
    QStringList args{script,
                     "--output",
                     output,
                     "--monitor",
                     request.monitor,
                     "--size",
                     QString("%1x%2").arg(native.width()).arg(native.height()),
                     "--crop",
                     QString("%1,%2,%3,%4")
                         .arg(crop.x())
                         .arg(crop.y())
                         .arg(crop.width())
                         .arg(crop.height())};
    if (!display.isEmpty())
        args << "--display" << QJsonDocument(display).toJson(QJsonDocument::Compact);
    if (const QString token = settings.value(tokenKey(request.monitor)).toString();
        !token.isEmpty())
        args << "--token" << token;
    if (!request.mic.isEmpty())
        args << "--mic" << request.mic;
    if (!request.camera.isEmpty())
        args << "--camera" << request.camera;
    if (request.desktopAudio)
        args << "--desktop";
    if (synthetic)
        args << "--synthetic";
    hideWindows = !synthetic;
    worker.setProcessEnvironment(QProcessEnvironment::systemEnvironment());
    worker.start("/usr/bin/python", args);
    // The worker may keep frames until it exits, so it is listed too. Nothing shows before it
    // streams, so one that can't be listed ends before anything has.
    if (const qint64 pid = worker.processId(); pid > 0) {
        if (const QString problem = publishMarker({QCoreApplication::applicationPid(), pid});
            !problem.isEmpty()) {
            error = "OmaCap can't record: " + problem;
            worker.kill();
        }
    }
    return {};
}

void Recorder::stop() { stopAt(monotonicNow()); }

// Keeps nothing captured after `at` (CLOCK_MONOTONIC nanoseconds). Stopped during the
// countdown, the worker finishes without a recording.
void Recorder::stopAt(qint64 at) {
    if (m_state != State::Countdown && m_state != State::Recording)
        return;
    countdownTimer.stop();
    setState(State::Stopping);
    send({{"cmd", "stop"}, {"at", at}});
    QTimer::singleShot(10000, this, [this, pid = worker.processId()] {
        if (worker.state() != QProcess::NotRunning && worker.processId() == pid)
            worker.kill();
    });
}

// The recorded monitor or the HUD's changed scale, size or rotation, so the chrome may now be
// in the area. It goes first; then the recording ends before the change.
void Recorder::displayChanged() {
    if (m_state != State::Countdown && m_state != State::Recording)
        return;
    const qint64 noticed = monotonicNow();
    chrome->conceal();
    error = "The display changed, so OmaCap stopped recording.";
    stopAt(noticed - changeNoticeNs);
}

void Recorder::tick() {
    if (--m_countdown > 0) {
        chrome->showCountdown(m_countdown);
        return;
    }
    countdownTimer.stop();
    if (hideWindows)
        hideOtherWindows(*chrome);
    send({{"cmd", "start"}, {"after", chrome->clearCountdown()}});
}

void Recorder::parseWorker() {
    buffer += worker.readAllStandardOutput();
    while (buffer.contains('\n')) {
        const QByteArray line = buffer.left(buffer.indexOf('\n'));
        buffer.remove(0, line.size() + 1);
        const QJsonObject message = QJsonDocument::fromJson(line).object();
        const QString event = message["event"].toString();
        if (event == "token") {
            // Tokens are per monitor, and replaced every time: the portal may issue a new one.
            const QString monitor = message["monitor"].toString();
            const QString token = message["token"].toString();
            if (monitor != request.monitor)
                settings.remove(tokenKey(request.monitor));
            if (!monitor.isEmpty() && token.isEmpty())
                settings.remove(tokenKey(monitor));
            else if (!monitor.isEmpty())
                settings.setValue(tokenKey(monitor), token);
        } else if (event == "streaming" && m_state == State::Countdown) {
            if (hideWindows)
                hideOtherWindows(*chrome);
            // The countdown outlasts any OmaCap notification still on screen.
            m_countdown = std::max<int>(m_countdown, (toastsGone.remainingTime() + 999) / 1000);
            chrome->showCountdown(m_countdown);
            countdownTimer.start();
        } else if (event == "recording") {
            recording = true;
            chrome->startClock();
            if (m_state == State::Countdown)
                setState(State::Recording);
        } else if (event == "stopping") {
            // Nothing captured from now on is kept, so the chrome may go and this process's
            // windows may show again. The marker stays until the worker has exited. The worker
            // also stops by itself, e.g. when the display changed, perhaps mid-countdown.
            countdownTimer.stop();
            if (m_state == State::Countdown || m_state == State::Recording)
                setState(State::Stopping);
            guard.reset();
            if (chrome)
                chrome->hide();
        } else if (event == "error") {
            error = message["message"].toString();
        }
    }
}

void Recorder::send(const QJsonObject &command) {
    worker.write(QJsonDocument(command).toJson(QJsonDocument::Compact) + '\n');
}

void Recorder::setState(State next) {
    if (m_state == next)
        return;
    m_state = next;
    emit stateChanged();
}

// Replaces the marker in one step, so a reader sees the old one or all of the new one. Returns
// what went wrong, or nothing.
QString Recorder::publishMarker(const QList<qint64> &pids) {
    QSaveFile marker(recordingMarkerPath());
    if (marker.open(QIODevice::WriteOnly)) {
        for (const qint64 pid : pids)
            marker.write(QByteArray::number(pid) + '\n');
        if (marker.commit())
            return {};
    }
    return QString("%1 could not be written (%2).").arg(marker.fileName(), marker.errorString());
}

void Recorder::removeMarker() { QFile::remove(recordingMarkerPath()); }

void Recorder::lookUpHotkey() {
    auto *hyprctl = new QProcess(this);
    connect(hyprctl, &QProcess::finished, this, [this, hyprctl] {
        hyprctl->deleteLater();
        const QString key = recordHotkey(hyprctl->readAllStandardOutput());
        if (key.isEmpty())
            return;
        stopHint = "Press " + key + " to stop";
        if (chrome)
            chrome->setStopHint(stopHint);
    });
    connect(hyprctl, &QProcess::errorOccurred, hyprctl, &QObject::deleteLater);
    QTimer::singleShot(2000, hyprctl, &QProcess::kill);
    hyprctl->start("hyprctl", {"binds", "-j"});
}
