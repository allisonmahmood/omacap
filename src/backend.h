#pragma once
#include "frames.h"
#include "model.h"
#include "record-request.h"
#include "recorder.h"
#include "theme.h"
#include "waveforms.h"
#include <QAudioOutput>
#include <QElapsedTimer>
#include <QMediaDevices>
#include <QMediaPlayer>
#include <QObject>
#include <QProcess>
#include <QSettings>
#include <QTimer>
#include <QVideoFrame>
#include <QVideoSink>
#include <functional>
#include <memory>
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap waveforms READ waveformData NOTIFY waveformsChanged)
    Q_PROPERTY(double displayPosition READ displayPosition NOTIFY displayPositionChanged)
    Q_PROPERTY(QString phase READ phase NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(QVariantMap edit READ edit NOTIFY editChanged)
    Q_PROPERTY(double duration READ duration NOTIFY editChanged)
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY positionChanged)
    Q_PROPERTY(QString screenSource READ screenSource NOTIFY frameChanged)
    Q_PROPERTY(QString cameraSource READ cameraSource NOTIFY cameraFrameChanged)
    Q_PROPERTY(double aspect READ aspect NOTIFY changed)
    Q_PROPERTY(double zoom READ zoom NOTIFY displayPositionChanged)
    Q_PROPERTY(double focusX READ focusX NOTIFY displayPositionChanged)
    Q_PROPERTY(double focusY READ focusY NOTIFY displayPositionChanged)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
    Q_PROPERTY(bool recoverable READ recoverable NOTIFY changed)
    Q_PROPERTY(QString lastExport READ lastExport NOTIFY changed)
    Q_PROPERTY(QString original READ original NOTIFY changed)
    Q_PROPERTY(QSize sourceSize READ sourceSize NOTIFY changed)
  public:
    Backend(Theme *, Frames *, QObject *p = nullptr);
    ~Backend();
    QVariantMap waveformData() const { return peaks.data; }
    double displayPosition() const { return m_displayPosition; }
    QString phase() const { return m_phase; }
    QString message() const { return m_message; }
    QVariantMap edit() const { return state.json().toVariantMap(); }
    double duration() const { return state.length(); }
    double position() const { return m_position; }
    bool playing() const { return screen.playbackState() == QMediaPlayer::PlayingState; }
    QString screenSource() const {
        return screenRevision ? QString("image://frames/screen/%1").arg(screenRevision) : QString();
    }
    QString cameraSource() const {
        return cameraRevision ? QString("image://frames/camera/%1").arg(cameraRevision) : QString();
    }
    double aspect() const { return m_aspect; }
    double zoom() const;
    double focusX() const;
    double focusY() const;
    double progress() const { return m_progress; }
    bool dirty() const { return m_dirty; }
    bool recoverable() const;
    QString lastExport() const { return m_lastExport; }
    // The recording this session edits. Discarding the session keeps it.
    QString original() const { return m_original; }
    QSize sourceSize() const { return {sourceWidth, sourceHeight}; }
    // Starts the countdown for a recording, replacing the session. Returns why it can't start
    // (it would lose edits the user hasn't agreed to replace, or can't record the request);
    // empty when it started.
    QString startRecording(const RecordRequest &request, bool synthetic = false);
    // Cancels the countdown, or stops the recording.
    Q_INVOKABLE void stop();
    // The UI calls this once neither the card nor the editor is open. The process then exits
    // as soon as nothing records or exports.
    Q_INVOKABLE void uiClosed();
    // Answers replaceRequested(): true discards the edit for the new recording or file.
    Q_INVOKABLE void answerReplace(bool replace);
    // Calls `answer` with whether a new recording or file may replace the session. Asks the UI
    // first (replaceRequested) when that would lose edits; false while busy.
    void confirmReplace(std::function<void(bool)> answer);
    Q_INVOKABLE void importVideo();
    Q_INVOKABLE void loadFile(QString path);
    Q_INVOKABLE void seek(double seconds);
    Q_INVOKABLE double toSourceTime(double t) const { return state.sourceTime(t); }
    Q_INVOKABLE double toEditedTime(double t) const { return state.editedTime(t); }
    Q_INVOKABLE void togglePlay();
    Q_INVOKABLE void setValue(QString key, QVariant value);
    Q_INVOKABLE void beginEdit();
    Q_INVOKABLE void endEdit();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void split(double at);
    Q_INVOKABLE void cancelEdit();
    Q_INVOKABLE void removeRange(double a, double b);
    Q_INVOKABLE void trim(double a, double b);
    Q_INVOKABLE int addZoom(double at);
    Q_INVOKABLE void updateZoom(int index, double a, double b, double x, double y, double amount);
    Q_INVOKABLE void deleteZoom(int index);
    Q_INVOKABLE void chooseBackground();
    Q_INVOKABLE void chooseColor();
    Q_INVOKABLE void useWallpaper();
    Q_INVOKABLE void exportVideo(bool gif, int width, int fps, int quality);
    Q_INVOKABLE void cancelExport();
    Q_INVOKABLE void revealExport();
    Q_INVOKABLE void openExport();
    Q_INVOKABLE void discard();
    Q_INVOKABLE void recover();
    Q_INVOKABLE QVariant preference(QString key, QVariant fallback = QVariant()) const;
    Q_INVOKABLE void remember(QString key, QVariant value);
    void testLoad(QString path);
    void startExport(QString path, bool gif, int width, int fps, int quality);
    QString sessionPath() const { return session; }
  signals:
    void waveformsChanged();
    void displayPositionChanged();
    void changed();
    void editChanged();
    void positionChanged();
    void frameChanged();
    void cameraFrameChanged();
    // A new recording is loaded: show the card.
    void recordingFinished();
    // A new recording or file would replace unsaved edits. Ask, then call answerReplace().
    void replaceRequested();
    // Nothing is open, recording or exporting: the process may exit.
    void exitRequested();
    // Something failed while no card or editor is open to say so.
    void backgroundError(const QString &message);

  private:
    Theme *theme;
    Frames *frames;
    Edit state;
    QVector<Edit> past, future;
    bool grouping = false;
    QJsonObject groupBefore, exportedEdit;
    quint64 workerRun = 0;
    double screenStart = 0, cameraStart = 0;
    friend class Integration;
    QString m_phase = "idle", m_message, session, recoveryRoot, m_lastExport, pendingExport,
            partialExport, m_original;
    double m_position = 0, m_aspect = 16. / 9, m_progress = 0;
    int sourceWidth = 1920, sourceHeight = 1080, screenRevision = 0, cameraRevision = 0;
    bool m_dirty = false, exportCancelled = false, hasMic = false, hasDesktop = false,
         hasCamera = false, uiOpen = false, announceRecording = false, replaceConfirmed = false,
         recoveredEdits = false;
    std::function<void(bool)> pendingReplace;
    QString captureError;
    QByteArray workerBuffer;
    QMediaPlayer screen, camera, mic, desktop;
    QAudioOutput micOutput, desktopOutput;
    QMediaDevices devices;
    Recorder recorder;
    QVideoSink screenSink, cameraSink;
    QProcess worker;
    Waveforms peaks;
    QTimer seekTimer, displayTimer, seekTimeout;
    QElapsedTimer presentationClock;
    double presentedSource = 0, m_displayPosition = 0;
    bool seeking = false, seekInFlight = false;
    double appliedSeek = 0;
    void presentScreen(const QVideoFrame &frame);
    QVideoFrame latestCameraFrame;
    quint64 latestCameraSerial = 0, shownCameraSerial = 0;
    void presentCamera();
    void applySeek();
    void updateDisplay();
    double pendingSeek = 0;
    QSettings settings;
    void status(QString phase, QString message = {});
    void fail(const QString &message);
    void exitIfUnused();
    bool mayReplace() const;
    bool unsavedEdits() const;
    void newSession();
    void checkpoint();
    void push();
    void edited();
    void prepare(QString path, bool capture);
    void runExporter(const QJsonObject &job);
    void ready();
    void openPlayers();
    qint64 screenSeekEnd = 0;
    void snapshotWallpaper();
    void syncPlayers(bool force = false);
    void parseWorker();

    void clearPlayers();
};
