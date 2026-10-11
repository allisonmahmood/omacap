/** @fileoverview Save As atomically exports PNG and returns a saved preview. */
#include "save-as-smoke.hpp"
#include "capture.hpp"
#include "editor.hpp"
#include "recent-snaps.hpp"
#include "pin-file.hpp"
#include "recording-state.hpp"
#include "video-ipc.h"

#include <QApplication>
#include <QCloseEvent>
#include <QColor>
#include <QCoreApplication>
#include <QEvent>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QImage>
#include <QMetaObject>
#include <QObject>
#include <QRectF>
#include <QScopeGuard>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <QPushButton>
#include <QPoint>
#include <QToolTip>
#include <QTimer>
#include <QProcess>
#include <QMessageBox>
#include <QWindow>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QThread>
#include <Qt>
#include <QtTypes>
#include <QtCore/qtenvironmentvariables.h>
#include <QtTest/qtestkeyboard.h>

#include <memory>

namespace {
template <typename Predicate> bool waitUntil(Predicate ready) {
  QElapsedTimer timer;
  timer.start();
  while (!ready() && timer.elapsed() < 3000) {
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QThread::msleep(1);
  }
  return ready();
}
QFileDialog *saveDialog() {
  for (QWidget *widget : QApplication::topLevelWidgets()) {
    if (widget->objectName() == QStringLiteral("omacap-save-as"))
      return qobject_cast<QFileDialog *>(widget);
  }
  return nullptr;
}
} // namespace

bool runSaveAsSmoke(QString &error) {
  const QTemporaryDir directory;
  if (!directory.isValid()) {
    error = QStringLiteral("Could not create Save As test directory");
    return false;
  }
  const QByteArray previousRecents = qgetenv("OMACAP_RECENT_DIR");
  const auto restoreRecents = qScopeGuard([&] {
    if (previousRecents.isNull())
      qunsetenv("OMACAP_RECENT_DIR");
    else
      qputenv("OMACAP_RECENT_DIR", previousRecents);
  });
  qputenv("OMACAP_RECENT_DIR", directory.filePath(QStringLiteral("recent")).toUtf8());
  const QString target = directory.filePath(QStringLiteral("chosen.png"));
  QImage original(100, 80, QImage::Format_ARGB32);
  original.fill(Qt::red);
  if (!savePngFile(original, target, error))
    return false;
  QImage replacement(100, 80, QImage::Format_ARGB32);
  replacement.fill(Qt::blue);
  if (!savePngFile(replacement, target, error) || QImage(target).convertToFormat(QImage::Format_ARGB32) != replacement) {
    error = QStringLiteral("Save As did not atomically replace existing PNG");
    return false;
  }
  if (savePngFile({}, target, error) || error.isEmpty() ||
      QImage(target).convertToFormat(QImage::Format_ARGB32) != replacement) {
    error = QStringLiteral("Failed Save As damaged the existing file");
    return false;
  }
  if (savePngFile(original, directory.path(), error) || error.isEmpty()) {
    error = QStringLiteral("Save As accepted a directory as its destination");
    return false;
  }
  error.clear();

  QStringList previews;
  const auto cleanupPreviews = qScopeGuard([&] {
    for (const QString &path : previews) {
      const PinSnapshotFile owned(path);
    }
  });
  const auto preparePreview = [&](CaptureEditor &editor) {
    editor.setProcessLauncherForTest([&](const QString &, const QStringList &args) {
      if (args.size() != 2 || args.first() != QStringLiteral("--preview"))
        return false;
      previews.push_back(args.last());
      return true;
    });
  };
  CaptureData capture;
  capture.monitor.geometry = QRect(0, 0, 800, 600);
  capture.monitor.pixelSize = QSize(1600, 1200);
  capture.monitor.scale = 2;
  capture.source = QImage(1600, 1200, QImage::Format_ARGB32);
  capture.source.fill(QColor(QStringLiteral("#354f68")));
  capture.previewSize = capture.monitor.geometry.size();
  for (const bool windowed : {false, true}) {
    CaptureEditor editor(capture, CaptureEditor::CaptureMode::File);
    preparePreview(editor);
    editor.setWindowedPresentation(windowed);
    editor.resize(800, 600);
    editor.show();
    QCoreApplication::processEvents();
    editor.beginText(QPointF(120, 160));
    auto *textEditor = editor.findChild<QPlainTextEdit *>();
    if (!textEditor) {
      error = QStringLiteral("Save As fixture has no text editor");
      return false;
    }
    textEditor->setPlainText(QStringLiteral("Keep this draft"));
    textEditor->setFocus();
    const int before = editor.operationIndex();
    const QRectF selection = editor.currentSelection();
    const qreal zoom = editor.viewZoom_;
    const QSignalSpy closed(qApp, &QGuiApplication::lastWindowClosed);
    QTest::keyClick(textEditor, Qt::Key_S,
                     Qt::ControlModifier | Qt::ShiftModifier);
    if (!waitUntil([] { return saveDialog() != nullptr; })) {
      error = QStringLiteral("Ctrl+Shift+S did not open the save chooser");
      return false;
    }
    auto *dialog = saveDialog();
    if (editor.isVisible() != windowed || !editor.busy_) {
      error = QStringLiteral("Save As did not suspend the correct presentation");
      dialog->reject();
      return false;
    }
    dialog->reject();
    if (!waitUntil([&] {
          return !editor.busy_ && editor.isVisible() && textEditor->hasFocus();
        }) ||
        closed.count() != 0 || !editor.textEditing() ||
        textEditor->toPlainText() != QStringLiteral("Keep this draft") ||
        editor.operationIndex() != before || editor.currentSelection() != selection ||
        editor.viewZoom_ != zoom) {
      error = QStringLiteral("Cancelling Save As changed the draft or document");
      return false;
    }
    // Drain deletion before looking for the next dialog.
    if (!waitUntil([] { return saveDialog() == nullptr; })) {
      error = QStringLiteral("Cancelled save chooser was not disposed");
      return false;
    }
    // QFileDialog accepts explicit non-PNG suffixes despite its name filter.
    // Even an approved overwrite must not replace that file with PNG bytes.
    const QString wrongFormat = directory.filePath(QStringLiteral("keep.jpg"));
    QFile existing(wrongFormat);
    if (!existing.open(QIODevice::WriteOnly) || existing.write("keep original") != 13) {
      error = QStringLiteral("Could not prepare Save As format fixture");
      return false;
    }
    existing.close();
    editor.saveAs();
    if (!waitUntil([] { return saveDialog() != nullptr; })) {
      error = QStringLiteral("Format fixture could not open Save As");
      return false;
    }
    dialog = saveDialog();
    dialog->setOption(QFileDialog::DontConfirmOverwrite);
    dialog->findChild<QLineEdit *>(QStringLiteral("fileNameEdit"))->setText(wrongFormat);
    QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
    if (!waitUntil([&] { return !editor.busy_; }) || !editor.textEditing() ||
        textEditor->toPlainText() != QStringLiteral("Keep this draft") ||
        editor.operationIndex() != before ||
        !editor.statusForTest().contains(QStringLiteral("choose a .png")) ||
        !existing.open(QIODevice::ReadOnly) || existing.readAll() != "keep original") {
      error = QStringLiteral("Non-PNG Save As changed its destination or draft");
      return false;
    }
    existing.close();
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_S,
                     Qt::ControlModifier | Qt::ShiftModifier);
    if (!waitUntil([] { return saveDialog() != nullptr; })) {
      error = QStringLiteral("Second Save As did not open");
      return false;
    }
    dialog = saveDialog();
    const QString output = directory.filePath(
        windowed ? QStringLiteral("window.png") : QStringLiteral("overlay.PNG"));
    // Enter the path as a user does; selectFile() can leave the old name in
    // the visible chooser while its asynchronous directory model reloads.
    auto *filename = dialog->findChild<QLineEdit *>(QStringLiteral("fileNameEdit"));
    if (!filename) {
      error = QStringLiteral("Save chooser has no filename input");
      dialog->reject();
      return false;
    }
    filename->selectAll();
    QTest::keyClicks(filename, windowed ? output.chopped(4) : output);
    QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
    if (!waitUntil([&] { return !editor.busy_; }) || editor.isVisible() ||
        editor.textEditing() || QImage(output).convertToFormat(QImage::Format_ARGB32) !=
            editor.renderCurrentOutput().convertToFormat(QImage::Format_ARGB32) ||
        editor.saveAsDirectory_ != directory.path()) {
      error = QStringLiteral("Save As did not export the draft and close: "
                             "visible=%1 draft=%2 busy=%3 output=%4x%5 directory=%6 status=%7")
                  .arg(editor.isVisible()).arg(editor.textEditing()).arg(editor.busy_)
                  .arg(QImage(output).width()).arg(QImage(output).height())
                  .arg(editor.saveAsDirectory_, editor.statusForTest());
      return false;
    }
    const int beforeNudge = editor.operationIndex();
    CaptureData reopened;
    describeFileCapture(reopened, QImage(output), {});
    if (reopened.previewSize != (QSizeF(reopened.source.size()) / 2).toSize()) {
      error = QStringLiteral("Save As lost the capture's logical size in its standalone PNG");
      return false;
    }
    const auto recent = findRecentSnap(editor.recentId_, &error);
    const qsizetype recentCount = listRecentSnaps(false).size();
    OperationLog savedLog;
    if (!recent || !loadOperationLog(recent->logPath, savedLog, error) ||
        savedLog.index != beforeNudge || savedLog.ops.size() != beforeNudge ||
        QImage(recent->sourcePath).convertToFormat(QImage::Format_ARGB32) != capture.source) {
      error = QStringLiteral("Save As did not retain its editable capture");
      return false;
    }
    OperationLog previewLog;
    if (previews.isEmpty() ||
        !loadOperationLog(operationLogPath(previews.last()), previewLog, error) ||
        previewLog.savedPath != output || previewLog.recentId != savedLog.recentId ||
        QImage(previews.last()) != QImage(output)) {
      error = QStringLiteral("Saved preview lost its file, pixels, or capture identity");
      return false;
    }
    editor.show();
    const auto document = copyPinDocument(previews.last(), error);
    if (!document)
      return false;
    editor.setPinDocument(document);
    editor.selectedAnnotation_ = 0;
    editor.selectedAnnotations_ = {0};
    editor.nudgeSelectedAnnotation(QPointF(3, 0));
    const QString nudged = directory.filePath(
        windowed ? QStringLiteral("window-nudged.png") : QStringLiteral("overlay-nudged.png"));
    editor.saveAsToPath(nudged);
    if (!waitUntil([&] { return !editor.busy_; }) ||
        editor.operationIndex() != beforeNudge + 1 ||
        QImage(nudged).convertToFormat(QImage::Format_ARGB32) !=
            editor.renderCurrentOutput().convertToFormat(QImage::Format_ARGB32)) {
      error = QStringLiteral("Save As did not commit an immediate nudge");
      return false;
    }
    const int savedIndex = editor.operationIndex();
    OperationLog completedDocument;
    if (!loadOperationLog(operationLogPath(document->path()), completedDocument, error) ||
        completedDocument.savedPath != nudged || editor.isVisible()) {
      error = QStringLiteral("Save As did not retire the originating preview and close");
      return false;
    }
    const auto updatedRecent = findRecentSnap(editor.recentId_, &error);
    if (!updatedRecent ||
        !loadOperationLog(updatedRecent->logPath, savedLog, error) ||
        savedLog.index != savedIndex || savedLog.recentId != editor.recentId_ ||
        listRecentSnaps(false).size() != recentCount || QFile::exists(recent->sourcePath)) {
      error = QStringLiteral("Repeated Save As did not update the same recent capture");
      return false;
    }
    editor.show();
    editor.setPinDocument({});
    editor.saveAsToPath(directory.filePath(QStringLiteral("missing/file.png")));
    if (!waitUntil([&] { return !editor.busy_; }) || !editor.isVisible() ||
        editor.operationIndex() != savedIndex ||
        !editor.statusForTest().contains(QStringLiteral("Could not"))) {
      error = QStringLiteral("Failed Save As changed or closed the editor");
      return false;
    }
    const QString failedPreview = directory.filePath(QStringLiteral("preview-failed.png"));
    editor.setProcessLauncherForTest([](const QString &, const QStringList &) { return false; });
    editor.saveAsToPath(failedPreview);
    if (!waitUntil([&] { return !editor.busy_; }) || !editor.isVisible() ||
        QImage(failedPreview).isNull() ||
        !editor.statusForTest().contains(QStringLiteral("could not show its preview"))) {
      error = QStringLiteral("A failed preview launch lost its saved PNG or closed the editor");
      return false;
    }
    editor.close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  }
  {
    CaptureEditor editor(capture, CaptureEditor::CaptureMode::File);
    editor.show();
    editor.waitForPictures();
    // A chooser must not steal the mouse release that commits a live gesture,
    // or export before the backdrop pictures have loaded.
    const auto refuses = [&] {
      editor.saveAs();
      return !editor.busy_ && !editor.saveAsActive_;
    };
    for (bool *pending : {&editor.dragging_, &editor.panning_}) {
      *pending = true;
      if (!refuses()) {
        error = QStringLiteral("Save As interrupted an unfinished edit");
        return false;
      }
      *pending = false;
    }
    editor.look_.backdrop = BackgroundStyle::Wallpaper;
    editor.wallpaperPending_ = true;
    if (!refuses()) {
      error = QStringLiteral("Save As did not wait for the wallpaper");
      return false;
    }
    editor.wallpaperPending_ = false;
    editor.close();
  }
  // A recording takes every window off screen: the editor, its chooser and
  // the chooser's overwrite question, still waiting in exec(). Windows shown
  // meanwhile never map, and a tooltip closes. The rest come back as they
  // were when it ends.
  {
    QProcess recorder;
    recorder.start(QStringLiteral("/bin/sleep"), {QStringLiteral("30")});
    const auto stopRecorder = qScopeGuard([&] {
      recorder.kill();
      recorder.waitForFinished();
      QFile::remove(recordingMarkerPath());
    });
    if (!recorder.waitForStarted()) {
      error = QStringLiteral("Could not start the recorder fixture");
      return false;
    }
    RecordingWatcher recording;
    const RecordingConcealment concealment(recording);
    const auto setRecording = [&](bool on) {
      QFile marker(recordingMarkerPath());
      const bool written =
          on ? marker.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
                   marker.write(QByteArray::number(recorder.processId()) + '\n') > 0
             : marker.remove();
      marker.close();
      return written && waitUntil([&] { return recording.recording() == on; });
    };
    const auto onScreen = [](const QWidget *window) {
      return window && window->isVisible() && window->windowHandle() &&
             window->windowHandle()->isVisible();
    };
    for (const bool windowed : {false, true}) {
      CaptureEditor editor(capture, CaptureEditor::CaptureMode::File);
      editor.setWindowedPresentation(windowed);
      editor.resize(800, 600);
      editor.show();
      QCoreApplication::processEvents();
      editor.saveAs();
      if (!waitUntil([] { return saveDialog() != nullptr; })) {
        error = QStringLiteral("Save As did not open before the recording");
        return false;
      }
      QFileDialog *dialog = saveDialog();
      dialog->findChild<QLineEdit *>(QStringLiteral("fileNameEdit"))->setText(target);
      // Accepting an existing file asks in a nested exec(); the recording
      // starts and ends while it waits.
      QString failure;
      bool asked = false;
      QTimer::singleShot(0, [&] {
        auto *question = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        asked = question != nullptr;
        if (!question || !onScreen(question)) {
          failure = QStringLiteral("Save As did not ask before replacing a file");
        } else if (!setRecording(true)) {
          failure = QStringLiteral("The recording did not start");
        } else if (onScreen(&editor) || onScreen(dialog) || onScreen(question)) {
          failure = QStringLiteral("A recording left Save As on screen");
        } else {
          QWidget later;
          later.show();
          QToolTip::showText(QPoint(10, 10), QStringLiteral("tip"));
          QCoreApplication::processEvents();
          QWidget *tip = nullptr;
          for (QWidget *window : QApplication::topLevelWidgets()) {
            if (window->windowType() == Qt::ToolTip)
              tip = window;
          }
          if (onScreen(&later) || onScreen(tip))
            failure = QStringLiteral("A window showed during a recording");
          else if (tip && !waitUntil([&] { return !tip->isVisible(); }))
            failure = QStringLiteral("A tooltip stayed open during a recording");
          else if (!setRecording(false))
            failure = QStringLiteral("The recording did not end");
          else if (!onScreen(dialog) || !onScreen(question) ||
                   onScreen(&editor) != windowed)
            failure = QStringLiteral("Save As did not come back after the recording");
        }
        if (question)
          question->button(QMessageBox::No)->click();
      });
      static_cast<QDialog *>(dialog)->accept();
      if (!asked || !failure.isEmpty()) {
        error = asked ? failure
                      : QStringLiteral("Save As did not ask before replacing a file");
        return false;
      }
      if (saveDialog() != dialog || !onScreen(dialog)) {
        error = QStringLiteral("Declining to replace closed Save As");
        return false;
      }
      // Closing the chooser, and so showing the editor, during a recording
      // puts nothing on screen; the editor comes back when it ends.
      if (!setRecording(true)) {
        error = QStringLiteral("The recording did not start");
        return false;
      }
      dialog->reject();
      if (!waitUntil([&] { return !editor.busy_ && saveDialog() == nullptr; }) ||
          onScreen(&editor)) {
        error = QStringLiteral("Closing a chooser during a recording showed the editor");
        return false;
      }
      if (!setRecording(false) || !onScreen(&editor) || saveDialog() != nullptr) {
        error = QStringLiteral("The editor did not come back after the recording");
        return false;
      }
      editor.close();
    }
  }
  // A finished worker may still have its GUI completion queued when close
  // arrives. Closing must invalidate that completion, even without destruction.
  for (const bool windowed : {false, true}) {
    CaptureEditor editor(capture, CaptureEditor::CaptureMode::File);
    preparePreview(editor);
    editor.setWindowedPresentation(windowed);
    editor.show();
    editor.saveAs();
    for (auto *watcher : editor.findChildren<QFutureWatcherBase *>())
      watcher->waitForFinished();
    editor.close();
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    if (saveDialog() || editor.isVisible()) {
      if (auto *dialog = saveDialog())
        dialog->reject();
      error = QStringLiteral("Closing during Save As preparation reopened UI");
      return false;
    }
    // Close while the chooser is visible, then after its rejection but before
    // deferred deletion/restoration. Neither route may resurrect the editor.
    for (const bool rejectFirst : {false, true}) {
      editor.show();
      editor.saveAs();
      if (!waitUntil([] { return saveDialog() != nullptr; })) {
        error = QStringLiteral("Close-race fixture could not open Save As");
        return false;
      }
      if (rejectFirst)
        saveDialog()->reject();
      editor.close();
      if (!waitUntil([] { return saveDialog() == nullptr; })) {
        error = QStringLiteral("Closing editor left an orphan save chooser");
        return false;
      }
      QCoreApplication::processEvents();
      if (editor.isVisible()) {
        error = QStringLiteral("Closing during Save As cancellation reopened editor");
        return false;
      }
    }
  }
  // Compositor close now routes through the editor's dismissal path. It must
  // still cancel Save As before that path checks whether the editor is busy.
  {
    CaptureEditor editor(capture, CaptureEditor::CaptureMode::File);
    editor.setWindowedPresentation(true);
    editor.show();
    editor.saveAs();
    if (!waitUntil([] { return saveDialog() != nullptr; })) {
      error = QStringLiteral("Compositor-close fixture could not open Save As");
      return false;
    }
    QCloseEvent close;
    QCoreApplication::sendEvent(editor.windowHandle(), &close);
    if (!waitUntil([&] { return !editor.isVisible() && !saveDialog(); })) {
      error = QStringLiteral("Compositor close left Save As or its editor open");
      return false;
    }
  }
  {
    auto editor = std::make_unique<CaptureEditor>(capture, CaptureEditor::CaptureMode::File);
    editor->show();
    editor->saveAs();
    for (auto *watcher : editor->findChildren<QFutureWatcherBase *>())
      watcher->waitForFinished();
    editor.reset();
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    if (saveDialog()) {
      saveDialog()->reject();
      error = QStringLiteral("Destroyed editor left a save chooser");
      return false;
    }
    // Accepted output owns copied pixels/state. Destruction drains it before
    // application teardown, even with its GUI completion still pending.
    editor = std::make_unique<CaptureEditor>(capture, CaptureEditor::CaptureMode::File);
    const QString output = directory.filePath(QStringLiteral("destroyed.png"));
    preparePreview(*editor);
    editor->saveAsToPath(output);
    editor.reset();
    if (QImage(output).isNull()) {
      error = QStringLiteral("Destroying editor interrupted accepted Save As output");
      return false;
    }
  }
  return true;
}
