#pragma once
#include "record-request.h"
#include <QDeadlineTimer>
#include <QObject>
#include <QProcess>
#include <QSettings>
#include <QTimer>
#include <memory>

class RecordingChrome;
class WindowGuard;

// Records one monitor area at a time, so that nothing OmaCap shows ends up in the file.
//
// The worker (scripts/capture.py) records the monitor through the screen-cast portal and crops
// it to the area before encoding. Its restore token is kept per monitor, so the portal's
// picker appears once per monitor. Once every source delivers, a three-second countdown runs
// over the area; then the countdown turns transparent and the worker keeps frames from the
// first one the compositor rendered after that. The HUD and outline never touch the encoded
// area. A request for a monitor that changed since its area was picked is refused; while
// recording, the worker watches the monitor (and Qt it and the HUD's), and a change hides the
// chrome and ends the recording before it. From start() until the worker has exited,
// recordingMarkerPath() lists this process and the worker.
class Recorder : public QObject {
    Q_OBJECT
  public:
    enum class State { Idle, Countdown, Recording, Stopping };
    explicit Recorder(QObject *parent = nullptr);
    ~Recorder() override;
    // Records `request` into `output`. Synthetic sources stand in for the portal and devices.
    // Returns why it can't, e.g. when the display changed since the area was picked or the
    // recording marker can't be written; empty when it started.
    QString start(const RecordRequest &request, const QString &output, bool synthetic = false);
    // Cancels a countdown, or ends the recording.
    void stop();
    State state() const { return m_state; }

  signals:
    void stateChanged();
    // The worker exited. `recorded` means `output` holds a recording; otherwise `error` says
    // why, and is empty when the user cancelled.
    void finished(bool recorded, const QString &error);

  private:
    void setState(State state);
    void parseWorker();
    void tick();
    void stopAt(qint64 at);
    void displayChanged();
    void send(const QJsonObject &command);
    QString publishMarker(const QList<qint64> &pids);
    void removeMarker();
    void lookUpHotkey();
    State m_state = State::Idle;
    int m_countdown = 0;
    bool recording = false, hideWindows = true;
    RecordRequest request;
    QString output, error;
    QByteArray buffer;
    QProcess worker;
    QTimer countdownTimer;
    QDeadlineTimer toastsGone;
    QSettings settings;
    std::unique_ptr<RecordingChrome> chrome;
    std::unique_ptr<WindowGuard> guard;
    QString stopHint;
};
