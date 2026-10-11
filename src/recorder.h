#pragma once
#include "record-request.h"
#include <QObject>
#include <QProcess>
#include <QSettings>
#include <QTimer>
#include <memory>

class RecordingChrome;

// Records one monitor area at a time, so that nothing OmaCap shows ends up in the file.
//
// The worker (scripts/capture.py) records the monitor through the screen-cast portal and crops
// it to the area before encoding. Its restore token is kept per monitor, so the portal's
// picker appears once per monitor. Once every source delivers, a three-second countdown runs
// over the area; then the countdown turns transparent and the worker keeps frames from the
// first one the compositor rendered after that. The HUD and outline never touch the encoded
// area. While a countdown or recording runs, recordingMarkerPath() holds this process's PID.
class Recorder : public QObject {
    Q_OBJECT
  public:
    enum class State { Idle, Countdown, Recording, Stopping };
    explicit Recorder(QObject *parent = nullptr);
    ~Recorder() override;
    // Records `request` into `output`. Synthetic sources stand in for the portal and devices.
    void start(const RecordRequest &request, const QString &output, bool synthetic = false);
    // Cancels a countdown, or ends the recording.
    void stop();
    State state() const { return m_state; }
    // Seconds left in the countdown; 3 until every source delivers.
    int countdown() const { return m_countdown; }

  signals:
    void stateChanged();
    // The worker exited. `recorded` means `output` holds a recording; otherwise `error` says
    // why, and is empty when the user cancelled.
    void finished(bool recorded, const QString &error);

  private:
    void setState(State state);
    void parseWorker();
    void tick();
    void send(const QJsonObject &command);
    void writeMarker();
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
    QSettings settings;
    std::unique_ptr<RecordingChrome> chrome;
    QString stopHint;
};
