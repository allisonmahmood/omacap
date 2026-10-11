#include "recorder.h"
#include "recording-chrome.h"
#include "recording-layout.h"
#include "video-ipc.h"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QWindow>
#include <signal.h>
#include <sys/prctl.h>

namespace {
QString tokenKey(const QString &monitor) { return "restoreTokens/" + monitor; }

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

Recorder::Recorder(QObject *parent) : QObject(parent) {
    countdownTimer.setInterval(1000);
    connect(&countdownTimer, &QTimer::timeout, this, &Recorder::tick);
    worker.setChildProcessModifier([] { prctl(PR_SET_PDEATHSIG, SIGTERM); });
    connect(&worker, &QProcess::readyReadStandardOutput, this, &Recorder::parseWorker);
    connect(&worker, &QProcess::finished, this, [this] {
        parseWorker();
        countdownTimer.stop();
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
        chrome.reset();
        removeMarker();
        setState(State::Idle);
        emit finished(false, "Could not start the recording worker.");
    });
}

Recorder::~Recorder() {
    if (worker.state() != QProcess::NotRunning) {
        // The worker finalizes the file on SIGINT.
        ::kill(worker.processId(), SIGINT);
        if (!worker.waitForFinished(8000)) {
            worker.kill();
            worker.waitForFinished(2000);
        }
    }
    removeMarker();
}

void Recorder::start(const RecordRequest &next, const QString &path, bool synthetic) {
    if (m_state != State::Idle)
        return;
    request = next;
    output = path;
    error.clear();
    buffer.clear();
    recording = false;
    m_countdown = 3;
    setState(State::Countdown);
    writeMarker();
    stopHint = "Run omacap --record again to stop";
    chrome = std::make_unique<RecordingChrome>(request, stopHint);
    connect(chrome.get(), &RecordingChrome::stopClicked, this, &Recorder::stop);
    if (!synthetic)
        lookUpHotkey();
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
}

void Recorder::stop() {
    if (m_state == State::Countdown) {
        // Nothing has been kept yet, so the worker finishes without a recording.
        countdownTimer.stop();
        setState(State::Stopping);
        send({{"cmd", "stop"}});
    } else if (m_state == State::Recording) {
        setState(State::Stopping);
        send({{"cmd", "stop"}, {"at", monotonicNow()}});
    } else {
        return;
    }
    QTimer::singleShot(10000, this, [this, pid = worker.processId()] {
        if (worker.state() != QProcess::NotRunning && worker.processId() == pid)
            worker.kill();
    });
}

void Recorder::tick() {
    if (--m_countdown > 0) {
        chrome->showCountdown(m_countdown);
        emit stateChanged();
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
            chrome->showCountdown(m_countdown);
            countdownTimer.start();
        } else if (event == "recording") {
            recording = true;
            chrome->startClock();
            if (m_state == State::Countdown)
                setState(State::Recording);
        } else if (event == "stopping") {
            // Nothing captured from now on is kept, so the chrome may go and other OmaCap
            // windows may show again.
            if (chrome)
                chrome->hide();
            removeMarker();
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

void Recorder::writeMarker() {
    QFile marker(recordingMarkerPath());
    if (marker.open(QIODevice::WriteOnly | QIODevice::Truncate))
        marker.write(QByteArray::number(QCoreApplication::applicationPid()) + '\n');
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
