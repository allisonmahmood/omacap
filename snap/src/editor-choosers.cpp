/** @fileoverview Nonblocking choosers over the overlay and windowed editors:
 *  Save As, and the backdrop's colour and image. */
#include "editor.hpp"
#include "backdrop-assets.hpp"
#include "capture.hpp"
#include "overlay-chrome.hpp"
#include "pin.hpp"
#include "recent-snaps.hpp"
#include "pin-file.hpp"

#include <LayerShellQt/Window>
#include <QColorDialog>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QObject>
#include <QProcess>
#include <QStandardPaths>
#include <QPromise>
#include <QString>
#include <QTimer>
#include <Qt>
#include <QtTypes>
#include <QtLogging>
#include <QtConcurrent/QtConcurrentRun>

namespace {
void prepareChooserWindow() {
  // The overlay has no xdg-shell parent for its choosers. Without a rule,
  // Hyprland tiles the independent dialog and rearranges the user's windows.
  // Register before mapping, on a worker.
  QProcess process;
  process.start(QStringLiteral("hyprctl"),
                {QStringLiteral("eval"),
                 QStringLiteral("hl.window_rule({ name = \"omacap-chooser\", "
                                "match = { class = \"^omacap$\", "
                                "title = \"^(Save screenshot as|Choose a "
                                "backdrop (image|colour))$\" }, "
                                "float = true, center = true, opacity = 1 })")});
  if (!process.waitForFinished(500)) {
    process.kill();
    process.waitForFinished(500);
    qWarning("Could not configure the floating chooser window");
  } else if (process.exitStatus() != QProcess::NormalExit ||
             process.exitCode() != 0 ||
             process.readAllStandardOutput().contains("error")) {
    qWarning("Could not configure the floating chooser window");
  }
}
/// Registers the floating rule, then opens the chooser: a chooser that maps
/// before its rule exists tiles for a frame.
void afterChooserRule(QObject *context, std::function<void()> open) {
  if (QGuiApplication::platformName() != QStringLiteral("wayland")) {
    open();
    return;
  }
  auto *watcher = new QFutureWatcher<void>(context);
  QObject::connect(watcher, &QFutureWatcher<void>::finished, context,
                   [watcher, open = std::move(open)] {
                     watcher->deleteLater();
                     open();
                   });
  watcher->setFuture(QtConcurrent::run(prepareChooserWindow));
}
} // namespace

void CaptureEditor::cancelSaveAs() {
  // QObject stays alive after close(), and destruction can drain queued events.
  // Invalidate every continuation before closing the independent chooser.
  ++saveAsRequest_;
  if (saveAsActive_) {
    saveAsActive_ = false;
    busy_ = false;
  }
  if (QDialog *chooser = chooser_.data()) {
    chooser_.clear();
    chooser->reject();
    chooser->deleteLater();
  }
}

void CaptureEditor::setVisible(bool visible) {
  shown_ = visible;
  QWidget::setVisible(visible && !concealed_);
}

void CaptureEditor::setConcealed(bool concealed) {
  if (concealed == concealed_)
    return;
  concealed_ = concealed;
  // The chooser is its own window: hiding the editor leaves it mapped. It
  // goes first and comes back last, over its editor.
  if (concealed && chooser_)
    chooser_->hide();
  QWidget::setVisible(shown_ && !concealed);
  if (!concealed && chooser_)
    chooser_->open();
}

void CaptureEditor::openChooser(QDialog *dialog,
                                std::function<void(bool accepted)> done) {
  chooser_ = dialog;
  lookPopover_ = LookPopover::None;
  const quint64 request = ++saveAsRequest_;
  dialog->setScreen(screen());
  dialog->setFont(chromeFont(13));
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  // The overlay is hidden until the chooser has finished closing.
  dialog->setAttribute(Qt::WA_QuitOnClose, false);
  dialog->setWindowModality(Qt::ApplicationModal);
  connect(this, &QObject::destroyed, dialog, &QObject::deleteLater);
  connect(dialog, &QDialog::finished, this,
          [this, dialog, request, done = std::move(done)](int result) {
    // Closed: the end of a recording must not open it again.
    if (chooser_ == dialog)
      chooser_.clear();
    if (request != saveAsRequest_)
      return;
    const bool accepted = result == QDialog::Accepted;
    // The chooser's xdg-shell unmap and modal focus teardown must complete
    // before the exclusive layer returns, otherwise Hyprland can hand focus
    // back to the underlying client after the overlay has already mapped.
    connect(dialog, &QObject::destroyed, this, [this, accepted, request, done] {
      QTimer::singleShot(0, this, [this, accepted, request, done] {
        if (request != saveAsRequest_)
          return;
        if (!windowedPresentation_) {
          if (layer_)
            layer_->setKeyboardInteractivity(
                LayerShellQt::Window::KeyboardInteractivityExclusive);
          show();
        }
        raise();
        activateWindow();
        restoreSaveAsFocus();
        done(accepted);
      });
    });
    dialog->deleteLater();
  });
  if (!windowedPresentation_) {
    if (layer_)
      layer_->setKeyboardInteractivity(
          LayerShellQt::Window::KeyboardInteractivityNone);
    hide();
  }
  // A recording that started meanwhile opens it once it ends.
  if (!concealed_)
    dialog->open();
}

void CaptureEditor::saveAs() {
  if (busy_ || dragging_ || panning_ || !picturesReady() ||
      phase_ != Phase::Edit || selection_.isEmpty())
    return;
  busy_ = true;
  saveAsActive_ = true;
  const quint64 request = ++saveAsRequest_;
  setStatus(QStringLiteral("Choosing a save destination…"));
  const QString appSlug =
      appFilenameSlug(dominantAppClass(capture_.windows, selection_));
  auto *watcher = new QFutureWatcher<QString>(this);
  connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, request] {
    const QString suggested = watcher->result();
    watcher->deleteLater();
    if (request == saveAsRequest_)
      showSaveAsDialog(suggested);
  });
  const bool wayland = QGuiApplication::platformName() == QStringLiteral("wayland");
  watcher->setFuture(QtConcurrent::run([appSlug, directory = saveAsDirectory_, wayland] {
    if (wayland)
      prepareChooserWindow();
    const QString suggested = suggestedScreenshotPath(appSlug);
    return directory.isEmpty()
               ? suggested
               : QDir(directory).filePath(QFileInfo(suggested).fileName());
  }));
}

void CaptureEditor::showSaveAsDialog(const QString &suggested) {
  // A normal dialog cannot appear above an exclusive layer-shell overlay.
  // Keep it independent of that hidden widget so Wayland can map/focus it.
  auto *dialog = new QFileDialog(windowedPresentation_ ? this : nullptr,
                                 QStringLiteral("Save screenshot as"), suggested,
                                 QStringLiteral("PNG image (*.png)"));
  dialog->setObjectName(QStringLiteral("omacap-save-as"));
  dialog->resize(760, 520);
  dialog->setOption(QFileDialog::DontUseNativeDialog);
  dialog->setAcceptMode(QFileDialog::AcceptSave);
  dialog->setFileMode(QFileDialog::AnyFile);
  dialog->setDefaultSuffix(QStringLiteral("png"));
  // Read on acceptance: the dialog is gone by the time the overlay returns.
  const auto chosen = std::make_shared<QString>();
  connect(dialog, &QFileDialog::fileSelected, this,
          [chosen](const QString &file) { *chosen = file; });
  openChooser(dialog, [this, chosen](bool accepted) {
    const QString path = accepted ? *chosen : QString();
    if (path.isEmpty()) {
      saveAsActive_ = false;
      busy_ = false;
      setStatus(QStringLiteral("Save As cancelled"));
      return;
    }
    // The chooser's filter/default suffix does not prevent an explicit
    // .jpg name. Never write PNG bytes under another format's extension.
    if (QFileInfo(path).suffix().compare(QStringLiteral("png"),
                                        Qt::CaseInsensitive) != 0) {
      saveAsActive_ = false;
      busy_ = false;
      setStatus(QStringLiteral("Save As only writes PNG; choose a .png filename"));
      return;
    }
    // Only accepting output commits a text draft; cancelling the chooser
    // preserves both its content and the operation-log cursor.
    if (textEditing())
      acceptText();
    saveAsToPath(path);
  });
}

void CaptureEditor::saveAsToPath(const QString &path) {
  if (dragging_ || panning_ || !picturesReady())
    return;
  endNudgeRun();
  busy_ = true;
  saveAsActive_ = true;
  const quint64 request = saveAsRequest_;
  setStatus(QStringLiteral("Saving screenshot…"));
  auto *watcher = new QFutureWatcher<QString>(this);
  connect(watcher, &QFutureWatcher<QString>::resultReadyAt, this,
          [this, watcher, request] {
    const QString error = watcher->result();
    watcher->deleteLater();
    if (request != saveAsRequest_)
      return;
    saveAsActive_ = false;
    busy_ = false;
    if (!error.isEmpty()) {
      setStatus(error);
      return;
    }
    close();
  });
  saveAsDirectory_ = QFileInfo(path).absolutePath();
  saveAsFuture_ = QtConcurrent::run(
      [capture = capture_, render = renderer(),
       source = pristineSource_, log = currentOperationLog(),
       previous = editingRecent_, document = pinDocument_,
       launcher = processLauncher_, path](QPromise<QString> &completion) {
    RecentSnapWriter recent(log.recentId);
    const QImage image = render();
    QString error;
    if (savePngFile(image, path, error)) {
      const QString preview = launchPinnedCapture(
          image, renderedCaptureLogicalSize(capture, image.size()), false,
          PinLifetime::Timed, error, launcher, log.recentId, path);
      if (!preview.isEmpty()) {
        if (document)
          document->finishSavedPreview(log, path);
      } else {
        error = QStringLiteral("Saved to %1, but could not show its preview: %2")
                    .arg(path, error);
      }
    }
    // Close as soon as the saved preview is ready; retain the editable source
    // and log afterward, just as other completed captures do.
    completion.addResult(error);
    QString recentError;
    if (!recent.record(source, log, image, recentError,
                       previous ? &*previous : nullptr))
      qWarning("Could not retain Save As capture: %s", qPrintable(recentError));
  });
  watcher->setFuture(saveAsFuture_);
}

void CaptureEditor::chooseBackdropColour() {
  if (busy_ || phase_ != Phase::Edit)
    return;
  busy_ = true;
  setStatus(QStringLiteral("Choosing a backdrop colour…"));
  afterChooserRule(this, [this] {
    busy_ = false;
    auto *dialog =
        new QColorDialog(look_.color, windowedPresentation_ ? this : nullptr);
    dialog->setWindowTitle(QStringLiteral("Choose a backdrop colour"));
    dialog->setOption(QColorDialog::DontUseNativeDialog);
    const auto chosen = std::make_shared<QColor>();
    connect(dialog, &QColorDialog::colorSelected, this,
            [chosen](const QColor &color) { *chosen = color; });
    openChooser(dialog, [this, chosen](bool accepted) {
      if (!accepted || !chosen->isValid()) {
        setStatus(QStringLiteral("Backdrop unchanged"));
        return;
      }
      CaptureLook next = look_;
      next.backdrop = BackgroundStyle::Solid;
      next.color = *chosen;
      commitLook(next, lookStatus(next));
    });
  });
}

void CaptureEditor::chooseBackdropImage() {
  if (busy_ || phase_ != Phase::Edit)
    return;
  busy_ = true;
  setStatus(QStringLiteral("Choosing a backdrop image…"));
  afterChooserRule(this, [this] {
  busy_ = false;
  auto *dialog = new QFileDialog(
      windowedPresentation_ ? this : nullptr,
      QStringLiteral("Choose a backdrop image"),
      QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
      QStringLiteral("Images (*.png *.jpg *.jpeg *.webp *.bmp)"));
  dialog->resize(760, 520);
  dialog->setOption(QFileDialog::DontUseNativeDialog);
  dialog->setFileMode(QFileDialog::ExistingFile);
  const auto chosen = std::make_shared<QString>();
  connect(dialog, &QFileDialog::fileSelected, this,
          [chosen](const QString &file) { *chosen = file; });
  openChooser(dialog, [this, chosen](bool accepted) {
    if (!accepted || chosen->isEmpty()) {
      setStatus(QStringLiteral("Backdrop unchanged"));
      return;
    }
    // Storing and decoding the picture are file work: a worker does both.
    struct Imported {
      QString id;
      QImage picture;
      QString error;
    };
    busy_ = true;
    setStatus(QStringLiteral("Loading backdrop image…"));
    auto *watcher = new QFutureWatcher<Imported>(this);
    connect(watcher, &QFutureWatcher<Imported>::finished, this, [this, watcher] {
      const Imported imported = watcher->result();
      watcher->deleteLater();
      busy_ = false;
      if (imported.id.isEmpty() || imported.picture.isNull()) {
        setStatus(imported.error.isEmpty()
                      ? QStringLiteral("Could not load that image")
                      : imported.error);
        return;
      }
      pictures_.insert(imported.id, imported.picture);
      CaptureLook next = look_;
      next.backdrop = BackgroundStyle::Image;
      next.image = imported.id;
      commitLook(next, lookStatus(next));
    });
    watcher->setFuture(QtConcurrent::run([path = *chosen] {
      Imported imported;
      imported.id = importBackdropAsset(path, imported.error);
      if (!imported.id.isEmpty())
        imported.picture = loadBackdropAsset(imported.id);
      return imported;
    }));
  });
  });
}
