#pragma once
#include "frames.h"
#include "model.h"
#include "theme.h"
#include "waveforms.h"
#include <QAudioOutput>
#include <QElapsedTimer>
#include <QMediaPlayer>
#include <QObject>
#include <QProcess>
#include <QSettings>
#include <QTimer>
#include <QVideoFrame>
#include <QVideoSink>
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
    Q_PROPERTY(double zoom READ zoom NOTIFY positionChanged)
    Q_PROPERTY(double focusX READ focusX NOTIFY positionChanged)
    Q_PROPERTY(double focusY READ focusY NOTIFY positionChanged)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    Q_PROPERTY(int countdown READ countdown NOTIFY changed)
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
    Q_PROPERTY(bool recoverable READ recoverable NOTIFY changed)
    Q_PROPERTY(QVariantList microphones READ microphones CONSTANT)
    Q_PROPERTY(QVariantList cameras READ cameras CONSTANT)
    Q_PROPERTY(QString lastExport READ lastExport NOTIFY changed)
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
    int countdown() const { return m_countdown; }
    bool dirty() const { return m_dirty; }
    bool recoverable() const;
    QVariantList microphones() const;
    QVariantList cameras() const;
    QString lastExport() const { return m_lastExport; }
    Q_INVOKABLE void record(QString mic, QString camera, bool desktop, bool synthetic = false);
    Q_INVOKABLE void stop();
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
    Q_INVOKABLE void exportVideo(bool gif, int width, int fps, int quality, double seconds);
    Q_INVOKABLE void cancelExport();
    Q_INVOKABLE void revealExport();
    Q_INVOKABLE void openExport();
    Q_INVOKABLE void discard();
    Q_INVOKABLE void recover();
    Q_INVOKABLE QVariant preference(QString key, QVariant fallback = QVariant()) const;
    Q_INVOKABLE void remember(QString key, QVariant value);
    void testLoad(QString path);
    void startExport(QString path, bool gif, int width, int fps, int quality, double seconds);
    QString sessionPath() const { return session; }
  signals:
    void waveformsChanged();
    void displayPositionChanged();
    void changed();
    void editChanged();
    void positionChanged();
    void frameChanged();
    void cameraFrameChanged();

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
    QString m_phase = "recorder", m_message, session, recoveryRoot, m_lastExport, pendingExport,
            partialExport;
    double m_position = 0, m_aspect = 16. / 9, m_progress = 0, lead = 0;
    int sourceWidth = 1920, m_countdown = 0, screenRevision = 0, cameraRevision = 0;
    bool m_dirty = false, exportCancelled = false, hasMic = false, hasDesktop = false,
         hasCamera = false;
    QString captureError;
    QByteArray workerBuffer;
    QMediaPlayer screen, camera, mic, desktop;
    QAudioOutput micOutput, desktopOutput;
    QVideoSink screenSink, cameraSink;
    QProcess worker;
    Waveforms peaks;
    QTimer countdownTimer, seekTimer, displayTimer, seekTimeout;
    QElapsedTimer presentationClock;
    double presentedSource = 0, m_displayPosition = 0;
    bool seeking = false;
    QVideoFrame latestCameraFrame;
    quint64 latestCameraSerial = 0, shownCameraSerial = 0;
    void presentCamera();
    void applySeek();
    void updateDisplay();
    double pendingSeek = 0;
    QSettings settings;
    void status(QString phase, QString message = {});
    void newSession();
    void checkpoint();
    void push();
    void edited();
    void prepare(QString path, bool capture);
    void ready();
    void snapshotWallpaper();
    void syncPlayers(bool force = false);
    void parseWorker();

    void clearPlayers();
};
