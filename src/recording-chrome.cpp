#include "recording-chrome.h"
#include "chrome-theme.hpp"
#include "overlay-chrome.hpp"
#include "video-icons.h"
#include <LayerShellQt/Window>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QWidget>
#include <ctime>
#include <optional>
#include <utility>
#include <wayland-client.h>

qint64 monotonicNow() {
    timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return qint64(now.tv_sec) * 1'000'000'000 + now.tv_nsec;
}

namespace {
const QColor recordRed(242, 107, 82);
constexpr int radius = 10;
const QSize hudSize(124, 40), countdownSize(300, 150);
// The HUD's stop button, in HUD coordinates.
const QRect stopButton(89, 5, 30, 30);

QScreen *screenNamed(const QString &name) {
    for (QScreen *screen : QGuiApplication::screens())
        if (screen->name() == name)
            return screen;
    return nullptr;
}

QString themeDirectory() {
    const QString root = qEnvironmentVariable("OMACAP_THEME_ROOT");
    return root.isEmpty() ? chromeThemeDirectory() : root + "/theme";
}

// Waits until the compositor has processed every request sent so far, without dispatching
// Qt's own event queue.
void roundTrip() {
    auto *wayland = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    if (!wayland || !wayland->display())
        return;
    wl_event_queue *queue = wl_display_create_queue(wayland->display());
    wl_display_roundtrip_queue(wayland->display(), queue);
    wl_event_queue_destroy(queue);
}

void panel(QPainter &painter, const QRect &bounds, const ChromeTheme &theme) {
    const QRectF box = QRectF(bounds).adjusted(.5, .5, -.5, -.5);
    QPainterPath path;
    path.addRoundedRect(box, radius, radius);
    painter.fillPath(path, theme.surface);
    painter.setPen(QPen(theme.panelBorder.brush(box), 1));
    painter.drawPath(path);
}
} // namespace

// A small layer-shell surface at a fixed spot on one monitor.
class ChromeSurface : public QWidget {
  public:
    ChromeSurface(QScreen *screen, const QRect &global, bool input,
                  std::function<void(QPainter &, const QRect &)> paint)
        : paint(std::move(paint)) {
        setAttribute(Qt::WA_TranslucentBackground);
        if (input)
            setMouseTracking(true);
        else
            setWindowFlag(Qt::WindowTransparentForInput);
        resize(global.size());
        winId();
        windowHandle()->setScreen(screen);
        auto *layer = LayerShellQt::Window::get(windowHandle());
        layer->setScope("omacap-recording");
        layer->setLayer(LayerShellQt::Window::LayerOverlay);
        layer->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop |
                                                        LayerShellQt::Window::AnchorLeft));
        const QPoint at = global.topLeft() - screen->geometry().topLeft();
        layer->setMargins(QMargins(at.x(), at.y(), 0, 0));
        layer->setExclusiveZone(-1);
        layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
        layer->setScreen(screen);
    }
    std::function<void(QPoint)> clicked;
    // The pointer's position as it moves over the surface, and nothing once it leaves.
    std::function<void(std::optional<QPoint>)> hovered;

  protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.fillRect(rect(), Qt::transparent);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        painter.setRenderHint(QPainter::Antialiasing);
        paint(painter, rect());
    }
    void mousePressEvent(QMouseEvent *event) override {
        if (clicked)
            clicked(event->position().toPoint());
    }
    void mouseMoveEvent(QMouseEvent *event) override {
        if (hovered)
            hovered(event->position().toPoint());
    }
    void leaveEvent(QEvent *) override {
        if (hovered)
            hovered(std::nullopt);
    }

  private:
    std::function<void(QPainter &, const QRect &)> paint;
};

RecordingChrome::RecordingChrome(const RecordRequest &request, const QString &stopHint)
    : hint(stopHint) {
    if (QGuiApplication::platformName() != "wayland")
        return;
    QList<Monitor> monitors;
    for (QScreen *screen : QGuiApplication::screens())
        monitors.append({screen->name(), screen->geometry()});
    layout = layoutRecording(request, monitors, hudSize, countdownSize);
    theme = std::make_unique<ChromeThemeWatcher>(themeDirectory());
    connect(theme.get(), &ChromeThemeWatcher::changed, this, [this] {
        for (auto &surface : surfaces)
            surface->update();
    });
    for (const QRect &strip : layout.outline)
        add(request.monitor, strip, false, [](QPainter &painter, const QRect &bounds) {
            QPen pen(recordRed, bounds.height() < bounds.width() ? bounds.height() : bounds.width(),
                     Qt::DashLine, Qt::FlatCap);
            painter.setRenderHint(QPainter::Antialiasing, false);
            painter.setPen(pen);
            const QPointF middle = QRectF(bounds).center();
            if (bounds.width() >= bounds.height())
                painter.drawLine(QPointF(bounds.left(), middle.y()),
                                 QPointF(bounds.right() + 1, middle.y()));
            else
                painter.drawLine(QPointF(middle.x(), bounds.top()),
                                 QPointF(middle.x(), bounds.bottom() + 1));
        });
    if (layout.hud) {
        hud = add(layout.hudMonitor, *layout.hud, true,
                  [this](QPainter &painter, const QRect &bounds) { paintHud(painter, bounds); });
        if (hud) {
            hud->clicked = [this](QPoint at) {
                if (stopButton.contains(at))
                    emit stopClicked();
            };
            hud->hovered = [this](std::optional<QPoint> at) {
                const bool over = at && stopButton.contains(*at);
                if (std::exchange(stopHovered, over) != over)
                    hud->update();
            };
        }
        // A HUD whose monitor goes away is gone, never moved onto the recorded area.
        connect(qGuiApp, &QGuiApplication::screenRemoved, this, [this](QScreen *screen) {
            if (hud && hud->windowHandle()->screen() == screen) {
                std::erase_if(surfaces, [this](const auto &surface) { return surface.get() == hud; });
                hud = nullptr;
            }
        });
    }
    // Surfaces keep their place in logical pixels, relative to their monitor. A new scale,
    // size or rotation maps that place to other native pixels, perhaps in the encoded area. A
    // monitor that only moves maps it to the same ones.
    watch(screenNamed(request.monitor));
    if (layout.hudMonitor != request.monitor)
        watch(screenNamed(layout.hudMonitor));
    countdown = add(request.monitor, layout.countdown, false,
                    [this](QPainter &painter, const QRect &bounds) {
                        if (!cleared)
                            paintCountdown(painter, bounds);
                    });
    tick.setInterval(1000);
    connect(&tick, &QTimer::timeout, this, [this] {
        if (hud)
            hud->update();
    });
}

RecordingChrome::~RecordingChrome() = default;

ChromeSurface *RecordingChrome::add(const QString &monitor, const QRect &global, bool input,
                                    Paint paint) {
    QScreen *screen = screenNamed(monitor);
    if (!screen)
        return nullptr;
    surfaces.push_back(std::make_unique<ChromeSurface>(
        screen, global, input,
        [this, paint = std::move(paint)](QPainter &painter, const QRect &bounds) {
            if (!concealed)
                paint(painter, bounds);
        }));
    return surfaces.back().get();
}

void RecordingChrome::watch(QScreen *screen) {
    if (!screen)
        return;
    const auto shape = [screen] {
        return std::tuple(screen->geometry().size(), screen->devicePixelRatio(),
                          screen->orientation());
    };
    const auto check = [this, shape, before = shape()] {
        if (shape() != before)
            emit displayChanged();
    };
    connect(screen, &QScreen::geometryChanged, this, check);
    connect(screen, &QScreen::orientationChanged, this, check);
    connect(screen, &QScreen::logicalDotsPerInchChanged, this, check);
    connect(screen, &QScreen::physicalDotsPerInchChanged, this, check);
}

void RecordingChrome::showCountdown(int remaining) {
    seconds = remaining;
    for (auto &surface : surfaces) {
        surface->show();
        surface->update();
    }
}

void RecordingChrome::setStopHint(const QString &stopHint) {
    hint = stopHint;
    if (countdown)
        countdown->update();
}

qint64 RecordingChrome::clearCountdown() {
    cleared = true;
    if (countdown && countdown->isVisible()) {
        countdown->repaint();
        roundTrip();
    }
    return monotonicNow();
}

void RecordingChrome::startClock() {
    if (countdown)
        countdown->hide();
    elapsed.start();
    tick.start();
    if (hud)
        hud->update();
}

void RecordingChrome::hide() {
    tick.stop();
    for (auto &surface : surfaces)
        surface->hide();
}

void RecordingChrome::conceal() {
    concealed = true;
    bool painted = false;
    for (auto &surface : surfaces)
        if (surface->isVisible()) {
            surface->repaint();
            painted = true;
        }
    if (painted)
        roundTrip();
    hide();
}

bool RecordingChrome::owns(const QWindow *window) const {
    return std::ranges::any_of(surfaces, [window](const auto &surface) {
        return surface->windowHandle() == window;
    });
}

void RecordingChrome::paintCountdown(QPainter &painter, const QRect &bounds) const {
    const ChromeTheme &colors = theme->theme();
    panel(painter, bounds, colors);
    painter.setPen(colors.foreground);
    painter.setFont(chromeFont(64, true));
    painter.drawText(bounds.adjusted(0, 12, 0, -44), Qt::AlignCenter, QString::number(seconds));
    painter.setPen(colors.muted);
    painter.setFont(chromeFont(13));
    painter.drawText(bounds.adjusted(16, 0, -16, -18), Qt::AlignHCenter | Qt::AlignBottom, hint);
}

void RecordingChrome::paintHud(QPainter &painter, const QRect &bounds) const {
    const ChromeTheme &colors = theme->theme();
    panel(painter, bounds, colors);
    painter.setPen(Qt::NoPen);
    painter.setBrush(recordRed);
    painter.drawEllipse(QRectF(12, bounds.height() / 2. - 5, 10, 10));
    const qint64 seconds = elapsed.isValid() ? elapsed.elapsed() / 1000 : 0;
    painter.setPen(colors.foreground);
    painter.setFont(chromeMonoFont(13, true));
    painter.drawText(QRect(30, 0, 54, bounds.height()), Qt::AlignVCenter | Qt::AlignLeft,
                     QString("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QChar('0')));
    // Omasnap's toolbar button, with the stop glyph in the recording red at 18 px.
    const QRectF face = QRectF(stopButton).adjusted(.5, .5, -.5, -.5);
    const ChromeThemeBorder &edge = stopHovered ? colors.buttonHoverBorder : colors.buttonBorder;
    painter.setPen(QPen(edge.brush(face), 1));
    painter.setBrush(stopHovered ? colors.buttonHover : colors.button);
    painter.drawRoundedRect(face, 8, 8);
    painter.save();
    painter.translate(face.center());
    painter.scale(18. / 19, 18. / 19);
    painter.translate(-face.center());
    drawVideoIcon(painter, face, "stop", recordRed);
    painter.restore();
}
