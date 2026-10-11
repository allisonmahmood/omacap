#pragma once
#include "recording-layout.h"
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <functional>
#include <memory>
#include <vector>

class ChromeSurface;
class ChromeThemeWatcher;
class QPainter;
class QScreen;
class QWindow;

// The windows shown while recording, as layer-shell surfaces in Omasnap's style: the
// countdown, a HUD with the elapsed time and a stop button, and an outline around the area.
// Placement comes from layoutRecording(), for the monitors as they were when it began; when
// the recorded monitor or the HUD's changes scale, size or rotation, displayChanged() says
// so. Without Wayland (tests) nothing is shown.
class RecordingChrome : public QObject {
    Q_OBJECT
  public:
    RecordingChrome(const RecordRequest &request, const QString &stopHint);
    ~RecordingChrome() override;
    // Shows every window with the countdown at `seconds`, or updates the countdown.
    void showCountdown(int seconds);
    void setStopHint(const QString &hint);
    // Makes the countdown fully transparent and returns CLOCK_MONOTONIC nanoseconds once the
    // compositor has applied that: any frame it renders later has no countdown in it.
    // Transparent rather than unmapped, because Hyprland fades unmapped surfaces out.
    qint64 clearCountdown();
    void startClock();
    // Hides everything; the countdown, already transparent, fades out invisibly.
    void hide();
    // Makes everything fully transparent, waits until the compositor has applied that, then
    // hides it: no frame the compositor renders later shows any of it, not even fading out.
    void conceal();
    bool owns(const QWindow *window) const;

  signals:
    void stopClicked();
    void displayChanged();

  private:
    using Paint = std::function<void(QPainter &, const QRect &)>;
    ChromeSurface *add(const QString &monitor, const QRect &global, bool input, Paint paint);
    void watch(QScreen *screen);
    void paintCountdown(QPainter &painter, const QRect &bounds) const;
    void paintHud(QPainter &painter, const QRect &bounds) const;
    RecordingLayout layout;
    QString hint;
    int seconds = 0;
    bool cleared = false, concealed = false, stopHovered = false;
    std::unique_ptr<ChromeThemeWatcher> theme;
    std::vector<std::unique_ptr<ChromeSurface>> surfaces;
    ChromeSurface *countdown = nullptr, *hud = nullptr;
    QElapsedTimer elapsed;
    QTimer tick;
};

// CLOCK_MONOTONIC in nanoseconds: the clock Hyprland stamps captured frames with.
qint64 monotonicNow();
