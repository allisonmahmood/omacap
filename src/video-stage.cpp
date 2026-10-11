#include "video-stage.h"
#include "backend.h"
#include "chrome-items.h"
#include "theme.h"
#include <LayerShellQt/Window>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QProcess>
#include <QQmlContext>
#include <QQmlEngine>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QtMath>
#include <utility>

namespace {
bool onWayland() {
    return QGuiApplication::platformName() == "wayland";
}

// Registered before anything maps, as Omasnap does: without a rule Hyprland tiles the
// floating editor and the independent choosers, and fades them when unfocused.
void registerWindowRules(QObject *owner) {
    auto hyprctl = new QProcess(owner);
    QObject::connect(hyprctl, &QProcess::finished, hyprctl, &QObject::deleteLater);
    QObject::connect(hyprctl, &QProcess::errorOccurred, hyprctl, &QObject::deleteLater);
    hyprctl->start("hyprctl",
                   {"eval", "hl.window_rule({ name = \"omacap-editor\", match = { class = "
                            "\"^omacap$\", title = \"^OmaCap$\" }, float = true, center = true, "
                            "opacity = 1 })\n"
                            "hl.window_rule({ name = \"omacap-choosers\", match = { class = "
                            "\"^omacap$\", title = \"^(Choose background|Background "
                            "color|Export recording)$\" }, float = true, center = true, "
                            "opacity = 1 })"});
}

// Says what went wrong when there is no card or editor to say it, as Omasnap does.
void notify(const QString &headline, const QString &description) {
    QProcess::startDetached("omarchy-notification-send",
                            {"--app-name", "omacap", "-t", "4500", headline, description});
}

QString uniquePath(const QString &stem, const QString &suffix) {
    QString path = stem + suffix;
    for (int i = 2; QFile::exists(path); ++i)
        path = stem + QString("-%1").arg(i) + suffix;
    return path;
}
} // namespace

VideoStage::VideoStage(QQmlEngine &engine, Backend &backend, Theme &theme)
    : engine(engine), backend(backend), editorComponent(&engine), cardComponent(&engine),
      lastPhase(backend.phase()), m_windowed(QSettings().value("editor/window", false).toBool()) {
    registerChromeTypes();
    // The overlay dims the desktop rather than covering it, and the card has round corners.
    QQuickWindow::setDefaultAlphaBuffer(true);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("stage", this);
    connect(&backend, &Backend::changed, this, &VideoStage::phaseChanged);
    // The card follows Backend::recordingFinished(). It must fire no later than the call
    // that moves the phase to "editor"; until it exists, every session opens the editor.
    if (backend.metaObject()->indexOfSignal("recordingFinished()") >= 0)
        connect(&backend, SIGNAL(recordingFinished()), this, SLOT(noteRecordingFinished()));
    if (onWayland())
        registerWindowRules(this);
}

VideoStage::~VideoStage() {
    // Items are deleted while `stage` still resolves in their bindings, including any
    // still waiting on deleteLater().
    qDeleteAll(findChildren<QQuickItem *>(Qt::FindDirectChildrenOnly));
    delete editorHost;
    delete cardHost;
}

bool VideoStage::load() {
    editorComponent.loadUrl(QUrl("qrc:/qml/Editor.qml"));
    cardComponent.loadUrl(QUrl("qrc:/qml/Card.qml"));
    for (auto component : {&editorComponent, &cardComponent})
        if (component->isError()) {
            qWarning().noquote() << component->errorString();
            return false;
        }
    return true;
}

void VideoStage::offerRecovery() {
    if (idle() && backend.recoverable())
        showCard("recovery");
}

bool VideoStage::idle() const {
    // "recorder" is the backend's older name for idle.
    return backend.phase() == "idle" || backend.phase() == "recorder";
}

bool VideoStage::recordingPhase() const {
    static const QStringList hidden{"countdown", "recording", "stopping", "selecting", "capturing"};
    return hidden.contains(backend.phase());
}

void VideoStage::noteRecordingFinished() {
    recordingDone = true;
}

// Backend::changed also reports messages and progress; act on phase changes only.
void VideoStage::phaseChanged() {
    const QString phase = backend.phase();
    if (phase == lastPhase)
        return;
    const QString was = std::exchange(lastPhase, phase);
    // No OmaCap pixels may appear in a recording.
    if (recordingPhase()) {
        hideSurfaces();
        return;
    }
    showSurfaces();
    if (phase == "editor" && was != "exporting") {
        // Decide once the call that loaded the session has returned, so its
        // recordingFinished() has been seen whichever order it came in.
        QTimer::singleShot(0, this, &VideoStage::decideSession);
    } else if (idle()) {
        // The session has ended.
        closeEditor();
        if (m_card == "recording")
            dropCard();
        if (!backend.message().isEmpty() && m_card.isEmpty() && onWayland())
            notify("OmaCap", backend.message());
        finishIfIdle();
    }
}

// A new recording shows the card; an opened file or a recovered edit opens the editor.
void VideoStage::decideSession() {
    if (backend.phase() != "editor" || editorOpen || m_card == "recording")
        return;
    if (std::exchange(recordingDone, false))
        showCard("recording");
    else
        openEditor();
}

void VideoStage::showCard(const QString &kind) {
    if (m_card != kind) {
        m_card = kind;
        emit cardChanged();
    }
    if (!cardHost) {
        cardItem = qobject_cast<QQuickItem *>(cardComponent.create(engine.rootContext()));
        if (!cardItem) {
            qWarning().noquote() << cardComponent.errorString();
            return;
        }
        cardItem->setParent(this);
        cardHost = new QQuickWindow;
        cardHost->setTitle("OmaCap card");
        cardHost->setColor(Qt::transparent);
        cardHost->installEventFilter(this);
        cardHost->resize(qCeil(cardItem->implicitWidth()), qCeil(cardItem->implicitHeight()));
        cardItem->setParentItem(cardHost->contentItem());
        constexpr int margin = 14; // Omasnap's corner margin for previews.
        if (onWayland()) {
            // A layer in the focused monitor's corner that never takes the keyboard on its
            // own; hovering or clicking it does.
            auto layer = LayerShellQt::Window::get(cardHost);
            layer->setScope("omacap-card");
            layer->setLayer(LayerShellQt::Window::LayerTop);
            layer->setAnchors({LayerShellQt::Window::AnchorBottom,
                               LayerShellQt::Window::AnchorRight});
            layer->setMargins({0, 0, margin, margin});
            layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityOnDemand);
            layer->setActivateOnShow(false);
            layer->setWantsToBeOnActiveScreen(true);
        } else {
            cardHost->setFlag(Qt::FramelessWindowHint);
            const QRect area = QGuiApplication::primaryScreen()->availableGeometry();
            cardHost->setPosition(area.right() - margin - cardHost->width(),
                                  area.bottom() - margin - cardHost->height());
        }
    }
    if (!recordingPhase())
        cardHost->show();
}

void VideoStage::closeCard() {
    if (m_card.isEmpty())
        return;
    const bool session = m_card == "recording";
    dropCard();
    // Ending the session brings the phase back to idle, which finishes; the recording stays
    // in Videos.
    if (session && !editorOpen && backend.phase() == "editor")
        backend.discard();
    else
        finishIfIdle();
}

void VideoStage::dropCard() {
    if (m_card.isEmpty())
        return;
    m_card.clear();
    emit cardChanged();
    destroyHost(cardHost);
    if (cardItem)
        cardItem->deleteLater();
    cardItem.clear();
}

void VideoStage::openEditor() {
    if (backend.phase() != "editor" && backend.phase() != "exporting")
        return;
    dropCard();
    editorOpen = true;
    if (!editorItem) {
        editorItem = qobject_cast<QQuickItem *>(editorComponent.create(engine.rootContext()));
        if (!editorItem) {
            qWarning().noquote() << editorComponent.errorString();
            return;
        }
        editorItem->setParent(this);
    }
    if (!editorHost)
        hostEditor();
}

void VideoStage::setWindowed(bool windowed) {
    if (windowed == m_windowed)
        return;
    m_windowed = windowed;
    QSettings().setValue("editor/window", windowed);
    emit windowedChanged();
    // Usually asked from a key press the current host is still delivering; switch hosts
    // once that has returned.
    QTimer::singleShot(0, this, [this] {
        if (editorOpen && editorItem)
            hostEditor();
    });
}

// Moves the editor into a new host for the current presentation. A layer surface can't
// become a window (LayerShellQt configures a window once, before it is shown), so the
// host is replaced, and the editor item, with all its state, moves across.
void VideoStage::hostEditor() {
    destroyHost(editorHost);
    auto window = new QQuickWindow;
    window->setTitle("OmaCap");
    window->setColor(Qt::transparent);
    window->installEventFilter(this);
    if (m_windowed) {
        window->setMinimumSize({1000, 680});
        window->resize(1280, 850);
    } else if (onWayland()) {
        auto layer = LayerShellQt::Window::get(window);
        layer->setScope("omacap");
        layer->setLayer(LayerShellQt::Window::LayerOverlay);
        layer->setAnchors({LayerShellQt::Window::AnchorTop, LayerShellQt::Window::AnchorBottom,
                           LayerShellQt::Window::AnchorLeft, LayerShellQt::Window::AnchorRight});
        layer->setExclusiveZone(-1);
        layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityExclusive);
        layer->setWantsToBeOnActiveScreen(true);
    } else {
        window->setFlag(Qt::FramelessWindowHint);
        window->setGeometry(QGuiApplication::primaryScreen()->geometry());
    }
    editorItem->setParentItem(window->contentItem());
    editorHost = window;
    if (!recordingPhase())
        showSurfaces();
}

void VideoStage::closeEditor() {
    editorOpen = false;
    destroyHost(editorHost);
    // The next session starts with a fresh editor. This often runs from one of its own
    // handlers, such as Discard's.
    if (editorItem)
        editorItem->deleteLater();
    editorItem.clear();
}

void VideoStage::destroyHost(QPointer<QQuickWindow> &host) {
    if (!host)
        return;
    host->removeEventFilter(this);
    host->hide();
    // Hosts are often closed from an event they are still delivering.
    host->deleteLater();
    host.clear();
}

void VideoStage::showSurfaces() {
    if (editorOpen && editorHost && !editorHost->isVisible() && !choosing) {
        editorHost->show();
        editorHost->requestActivate();
        editorItem->forceActiveFocus();
    }
    if (cardHost && !cardHost->isVisible())
        cardHost->show();
}

void VideoStage::hideSurfaces() {
    for (auto host : {editorHost, cardHost})
        if (host)
            host->hide();
}

void VideoStage::finishIfIdle() {
    if (idle() && !editorOpen && m_card.isEmpty())
        emit finished();
}

bool VideoStage::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() != QEvent::Close)
        return false;
    // Closing the editor's window asks as Escape does, so unexported edits are not lost.
    if (watched == editorHost && editorItem)
        QMetaObject::invokeMethod(editorItem, "requestClose");
    else if (watched == cardHost)
        QTimer::singleShot(0, this, &VideoStage::closeCard);
    event->ignore();
    return true;
}

void VideoStage::handOff(const std::function<void()> &choose) {
    // A normal dialog can't appear above an exclusive overlay layer, and Hyprland would hand
    // the keyboard back to the app underneath if the overlay returned while the chooser was
    // still being torn down.
    const bool overlay = editorHost && !m_windowed && onWayland();
    if (overlay) {
        choosing = true;
        LayerShellQt::Window::get(editorHost)
            ->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
        editorHost->hide();
    }
    choose();
    if (!overlay)
        return;
    QTimer::singleShot(0, this, [this] {
        choosing = false;
        if (!editorHost || recordingPhase())
            return;
        LayerShellQt::Window::get(editorHost)
            ->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityExclusive);
        showSurfaces();
    });
}

void VideoStage::chooseBackground() {
    handOff([this] { backend.chooseBackground(); });
}

void VideoStage::chooseColor() {
    handOff([this] { backend.chooseColor(); });
}

void VideoStage::chooseExport(bool gif, int width, int fps, int quality) {
    handOff([=, this] { backend.exportVideo(gif, width, fps, quality); });
}

void VideoStage::exportNow(bool gif, int width, int fps, int quality) {
    const QString videos = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    QDir().mkpath(videos);
    backend.startExport(
        uniquePath(videos + "/Demo-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"),
                   gif ? ".gif" : ".mp4"),
        gif, width, fps, quality);
}

void VideoStage::reveal(const QString &path) {
    if (!path.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
}
