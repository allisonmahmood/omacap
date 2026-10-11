#include "recent-persistence-smoke.hpp"

#include "backdrop-assets.hpp"
#include "capture.hpp"
#include "capture-state.hpp"
#include "cli-path.hpp"
#include "editor.hpp"
#include "recent-snaps.hpp"

#include <QApplication>
#include <QByteArray>
#include <QCommandLineParser>
#include <QChar>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QImage>
#include <QObject>
#include <QPromise>
#include <QScopeGuard>
#include <QSemaphore>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QTimer>
#include <QWheelEvent>
#include <QtCore/qnamespace.h>
#include <QtCore/qtenvironmentvariables.h>
#include <QtCore/qtestsupport_core.h>
#include <QtCore/qtypes.h>
#include <QtConcurrentRun>
#include <QtTest/QTest>

#include <memory>
#include <QtTest/qtestkeyboard.h>
#include <QtTest/qtestmouse.h>

namespace {
bool checkPublication(const QImage &image, const QDir &, QString &error) {
  QString failure;
  if (recordRecentSnap({}, {}, image, failure) || failure.isEmpty() ||
      !listRecentSnaps().isEmpty()) {
    error = QStringLiteral("Invalid source published a partial recent");
    return false;
  }
  return true;
}

bool checkReplacement(const QImage &image, const QDir &, QString &error) {
  for (int index = 0; index < kRecentSnapLimit; ++index) {
    if (!recordRecentSnap(image, {}, image, error))
      return false;
  }
  const auto original = listRecentSnaps();
  const RecentSnap replaced = original.at(2);
  QString failure;
  {
    RecentSnapWriter writer({});
    if (writer.record({}, {}, image, failure, &replaced) ||
        !QFile::exists(replaced.sourcePath)) {
      error = QStringLiteral("Failed replacement removed a published recent");
      return false;
    }
  }
  {
    RecentSnapWriter writer({});
    if (!writer.record(image, {}, image, error, &replaced))
      return false;
  }
  if (QFile::exists(replaced.sourcePath) ||
      listRecentSnaps().size() != kRecentSnapLimit) {
    error = QStringLiteral("Replacement pruned an extra recent capture");
    return false;
  }
  for (const auto &snap : original) {
    if (snap.sourcePath != replaced.sourcePath &&
        (!QFile::exists(snap.sourcePath) || !QFile::exists(snap.thumbPath))) {
      error = QStringLiteral("Replacement removed an unrelated recent capture");
      return false;
    }
  }
  for (const auto &snap : listRecentSnaps())
    removeRecentSnap(snap);
  return true;
}

/// Recording a capture prunes stored pictures a day old that nothing shows
/// any more, never one a pin or an open editor's document still shows.
bool checkBackdropPruning(const QImage &image, QString &error) {
  const QTemporaryDir pictures;
  // Its own shelf, so the other checks see theirs unchanged.
  const QByteArray shelf = qgetenv("OMACAP_RECENT_DIR");
  const auto restoreShelf = qScopeGuard([&] { qputenv("OMACAP_RECENT_DIR", shelf); });
  qputenv("OMACAP_RECENT_DIR", pictures.filePath(QStringLiteral("recent")).toUtf8());
  const auto storeAged = [&](const QColor &color) {
    QImage picture(8, 8, QImage::Format_RGB32);
    picture.fill(color);
    const QString path = pictures.filePath(color.name().mid(1) + QStringLiteral(".png"));
    QString ignored;
    const QString id = picture.save(path) ? importBackdropAsset(path, ignored) : QString();
    QFile stored(QDir(QStandardPaths::writableLocation(
                          QStandardPaths::GenericStateLocation))
                     .filePath(QStringLiteral("omacap/backdrops/") + id));
    const bool aged = !id.isEmpty() && stored.open(QIODevice::ReadWrite) &&
                      stored.setFileTime(QDateTime::currentDateTime().addDays(-2),
                                         QFileDevice::FileModificationTime);
    return std::pair{aged ? id : QString(), stored.fileName()};
  };
  const auto [pinned, pinnedFile] = storeAged(Qt::red);
  const auto [unused, unusedFile] = storeAged(Qt::blue);
  Operation look;
  look.type = Operation::Type::Look;
  look.look.backdrop = BackgroundStyle::Image;
  look.look.image = pinned;
  OperationLog pinLog;
  pinLog.ops = {look};
  pinLog.index = 1;
  const QString pinLogPath = QDir(secureRuntimeDirectory())
                                 .filePath(QStringLiteral("pin-1-1-0123456789abcdef.json"));
  const auto removePin = qScopeGuard([&] { QFile::remove(pinLogPath); });
  if (pinned.isEmpty() || unused.isEmpty() ||
      !saveOperationLog(pinLogPath, pinLog, error)) {
    if (error.isEmpty())
      error = QStringLiteral("Could not prepare the pruning fixture");
    return false;
  }
  if (!recordRecentSnap(image, {}, image, error))
    return false;
  if (!QFile::exists(pinnedFile) || QFile::exists(unusedFile)) {
    error = QStringLiteral("Pruning removed a pinned document's backdrop, or "
                           "kept an unused one");
    return false;
  }
  return true;
}

/// A fresh capture is styled with the wallpaper it was taken under: the
/// picture is stored with the document, so a later wallpaper change never
/// restyles it, and output waits until that picture is ready.
bool checkWallpaperBackdrop(const CaptureData &capture, QString &error) {
  const QTemporaryDir theme;
  const QByteArray previousTheme = qgetenv("OMACAP_THEME_ROOT");
  const auto restore = qScopeGuard([&] {
    previousTheme.isNull() ? qunsetenv("OMACAP_THEME_ROOT")
                           : qputenv("OMACAP_THEME_ROOT", previousTheme);
    saveLastLook(CaptureLook::plain());
  });
  QImage wallpaper(64, 64, QImage::Format_RGB32);
  wallpaper.fill(QColor(200, 30, 40));
  const QString background = QDir(theme.path()).filePath(QStringLiteral("background"));
  if (!theme.isValid() || !wallpaper.save(background, "PNG")) {
    error = QStringLiteral("Could not prepare the wallpaper fixture");
    return false;
  }
  qputenv("OMACAP_THEME_ROOT", theme.path().toUtf8());
  CaptureLook look;
  look.backdrop = BackgroundStyle::Wallpaper;
  look.shadow = 0.0;
  saveLastLook(look);

  CaptureEditor editor(capture, CaptureEditor::CaptureMode::Fullscreen);
  editor.setProcessLauncherForTest([](const QString &, const QStringList &) { return true; });
  // No event has been processed: the wallpaper is still being stored.
  QTest::keyClick(&editor, Qt::Key_S, Qt::ControlModifier);
  if (editor.statusForTest().contains(QStringLiteral("Saving screenshot"))) {
    editor.waitForExport();
    error = QStringLiteral("Output did not wait for the wallpaper");
    return false;
  }
  editor.waitForPictures();
  const auto log = editor.operationLog();
  if (log.isEmpty() || log.first().type != Operation::Type::Look ||
      log.first().look.backdrop != BackgroundStyle::Wallpaper ||
      log.first().look.wallpaper.isEmpty() ||
      editor.currentLookForTest().wallpaper != log.first().look.wallpaper) {
    error = QStringLiteral("The capture did not record its wallpaper");
    return false;
  }
  wallpaper.fill(QColor(20, 40, 220));
  if (!wallpaper.save(background, "PNG")) {
    error = QStringLiteral("Could not change the wallpaper fixture");
    return false;
  }
  const QImage output = editor.renderCurrentOutput();
  if (output.pixelColor(2, 2) != QColor(200, 30, 40)) {
    error = QStringLiteral("A wallpaper change restyled an existing capture: %1")
                .arg(output.pixelColor(2, 2).name());
    return false;
  }
  return true;
}

/// A pin taken before the wallpaper is resolved waits for it, rather than
/// keeping a log whose look has none for good; picking Wallpaper in a
/// document that never showed one resolves it.
bool checkKeptLooksGetTheirWallpaper(const CaptureData &capture, QString &error) {
  const QTemporaryDir directory;
  const QByteArray previousTheme = qgetenv("OMACAP_THEME_ROOT");
  const QByteArray shelf = qgetenv("OMACAP_RECENT_DIR");
  const auto restore = qScopeGuard([&] {
    previousTheme.isNull() ? qunsetenv("OMACAP_THEME_ROOT")
                           : qputenv("OMACAP_THEME_ROOT", previousTheme);
    qputenv("OMACAP_RECENT_DIR", shelf);
    saveLastLook(CaptureLook::plain());
  });
  const QColor green(30, 160, 90);
  QImage wallpaper(64, 64, QImage::Format_RGB32);
  wallpaper.fill(green);
  if (!directory.isValid() ||
      !wallpaper.save(directory.filePath(QStringLiteral("background")), "PNG")) {
    error = QStringLiteral("Could not prepare the wallpaper fixture");
    return false;
  }
  qputenv("OMACAP_THEME_ROOT", directory.path().toUtf8());
  qputenv("OMACAP_RECENT_DIR", directory.filePath(QStringLiteral("recent")).toUtf8());
  CaptureLook look;
  look.backdrop = BackgroundStyle::Wallpaper;
  look.shadow = 0.0;
  saveLastLook(look);
  {
    CaptureEditor editor(capture, CaptureEditor::CaptureMode::Fullscreen);
    editor.setProcessLauncherForTest([](const QString &, const QStringList &) { return true; });
    // No event has been processed: the wallpaper is still being stored.
    QTest::keyClick(&editor, Qt::Key_P, Qt::ControlModifier);
    editor.waitForPictures();
    QThreadPool::globalInstance()->waitForDone();
    const auto kept = listRecentSnaps(false);
    OperationLog keptLog;
    if (kept.isEmpty() || !loadOperationLog(kept.constFirst().logPath, keptLog, error) ||
        keptLog.ops.isEmpty() || keptLog.ops.constFirst().look.wallpaper.isEmpty() ||
        keptLog.ops.constFirst().look.wallpaper != editor.currentLookForTest().wallpaper) {
      error = QStringLiteral("A pin taken before the wallpaper was resolved kept "
                             "no wallpaper");
      return false;
    }
  }

  Operation off;
  off.type = Operation::Type::Look;
  off.look = CaptureLook::plain();
  OperationLog reopened;
  reopened.ops = {off};
  reopened.index = 1;
  reopened.previewSize = capture.previewSize;
  CaptureEditor editor(capture, CaptureEditor::CaptureMode::File,
                       QuickOutputMode::None, reopened);
  editor.setSuppressSnapshots(true);
  editor.waitForPictures();
  QTest::keyClick(&editor, Qt::Key_B); // Off cycles round to Wallpaper
  editor.waitForPictures();
  if (editor.currentLookForTest().backdrop != BackgroundStyle::Wallpaper ||
      editor.currentLookForTest().wallpaper.isEmpty() ||
      editor.operationLog().constLast().look.wallpaper.isEmpty() ||
      editor.renderCurrentOutput().pixelColor(2, 2) != green) {
    error = QStringLiteral("Picking Wallpaper in a reopened document showed no "
                           "wallpaper");
    return false;
  }
  editor.close();
  return true;
}

bool checkDestruction(const CaptureData &capture, QString &error) {
  auto editor = std::make_unique<CaptureEditor>(
      capture, CaptureEditor::CaptureMode::Fullscreen);
  if (!editor->waitForSnapshot()) {
    error = QStringLiteral("Could not persist destruction fixture");
    return false;
  }
  const QString source = temporarySnapshotPath();
  const QString log = operationLogPath(source);
  QSemaphore started;
  QSemaphore destroyed;
  // The old destructor unlinks the files and wakes this worker. A destructor
  // that drains output must wait for the bounded worker instead, retaining
  // both files throughout that interval. No GUI event loop releases the wait.
  auto pending = QtConcurrent::run([&] {
    started.release();
    static_cast<void>(destroyed.tryAcquire(1, 100));
    return QFile::exists(source) && QFile::exists(log);
  });
  QObject::connect(editor.get(), &QObject::destroyed,
                    [&] { destroyed.release(); });
  editor->setFinishFutureForTest(pending);
  started.acquire();
  editor.reset();
  pending.waitForFinished();
  if (!pending.result()) {
    error = QStringLiteral("Destruction removed files still owned by output");
    return false;
  }
  if (QFile::exists(source) || QFile::exists(log)) {
    error = QStringLiteral("Destruction did not clean drained working files");
    return false;
  }
  return true;
}
bool checkRecentHandoff(QApplication &application, const CaptureData &capture,
                        const QDir &, QString &error) {
  const auto original = listRecentSnaps(false);
  if (original.size() != kRecentSnapLimit) {
    error = QStringLiteral("Recent handoff fixture did not fill the shelf");
    return false;
  }
  const QString replaced = original.at(2).sourcePath;
  const auto launchArguments = std::make_shared<QStringList>();
  {
    CaptureEditor editor(capture);
    editor.resize(800, 600);
    editor.show();
    application.processEvents();
    if (!editor.waitForRecents())
      return false;
    // Move away first: the preceding editor may have left the system pointer
    // at this same stack position, in which case Qt sends no move event.
    QTest::mouseMove(&editor, QPoint(100, 300), 20);
    QTest::mouseMove(&editor, editor.recentCardRectForTest(0).center().toPoint(), 20);
    if (!editor.recentsOpenForTest()) {
      error = QStringLiteral("Could not fan the recent handoff fixture shelf");
      return false;
    }
    QTest::mouseClick(&editor, Qt::LeftButton, Qt::NoModifier,
                      editor.recentCardRectForTest(2).center().toPoint());
    editor.waitForReopen();
    if (!editor.editingForTest()) {
      error = QStringLiteral("Could not reopen the handoff fixture");
      return false;
    }
    editor.setProcessLauncherForTest(
        [launchArguments](const QString &, const QStringList &arguments) {
          *launchArguments = arguments;
          return false;
        });
    QTest::keyClick(&editor, Qt::Key_W);
    for (int attempt = 0; attempt < 500 && !editor.isEnabled(); ++attempt)
      QTest::qWait(10);
    if (!editor.isEnabled() || !editor.isVisible() ||
        !QFile::exists(replaced) ||
        QFile::exists(launchArguments->value(1)) ||
        listRecentSnaps(false).size() != kRecentSnapLimit) {
      error = QStringLiteral("Failed presentation handoff lost its recent capture");
      return false;
    }
    editor.setProcessLauncherForTest(
        [launchArguments](const QString &, const QStringList &arguments) {
          *launchArguments = arguments;
          return true;
        });
    QTest::keyClick(&editor, Qt::Key_W);
    for (int attempt = 0; attempt < 500 && editor.isVisible(); ++attempt)
      QTest::qWait(10);
    if (editor.isVisible()) {
      error = QStringLiteral("Recent presentation handoff did not finish");
      return false;
    }
  }
  QCommandLineParser parser;
  configureCaptureCommandLine(parser);
  if (!parser.parse(QStringList{QStringLiteral("omasnap")} + *launchArguments)) {
    error = parser.errorText();
    return false;
  }
  const QString path = parser.value(QStringLiteral("file"));
  const QString token = parser.value(QStringLiteral("handoff-token"));
  const QImage source(path);
  OperationLog log;
  OperationLog originalLog;
  if (source.isNull() || !loadOperationLog(operationLogPath(path), log, error) ||
      !loadOperationLog(original.at(2).logPath, originalLog, error))
    return false;
  if (log.recentId.isEmpty() || log.recentId != originalLog.recentId) {
    error = QStringLiteral("Presentation handoff lost its capture identity");
    return false;
  }
  if (removeEditorHandoff(path, QString(32, QLatin1Char('0'))) ||
      !QFile::exists(path) || !QFile::exists(replaced) ||
      !removeEditorHandoff(path, token)) {
    error = QStringLiteral("Presentation handoff ownership validation failed");
    return false;
  }
  CaptureData reopened;
  describeFileCapture(reopened, source, log);
  CaptureEditor receiver(reopened, CaptureEditor::CaptureMode::File,
                         QuickOutputMode::None, log);
  receiver.setWindowedPresentation(true);
  receiver.resize(800, 600);
  receiver.show();
  receiver.setProcessLauncherForTest([](const QString &, const QStringList &) { return true; });
  QTest::keyClick(&receiver, Qt::Key_S, Qt::ControlModifier);
  receiver.waitForExport();
  if (receiver.isVisible() || QFile::exists(replaced) ||
      listRecentSnaps(false).size() != kRecentSnapLimit) {
    error = QStringLiteral("Saving after handoff failed to replace the recent entry");
    return false;
  }
  for (const auto &snap : original) {
    if (snap.sourcePath != replaced && !QFile::exists(snap.sourcePath)) {
      error = QStringLiteral("Saving after handoff evicted an unrelated recent");
      return false;
    }
  }
  return true;
}
bool checkFreshSelection(QApplication &application, const CaptureData &capture,
                         QString &error) {
  const auto shelf = listRecentSnaps(false);
  if (shelf.size() < 3)
    return false;
  const RecentSnap previous = shelf.at(1);
  OperationLog previousLog;
  if (!loadOperationLog(previous.logPath, previousLog, error))
    return false;
  CaptureEditor editor(capture, CaptureEditor::CaptureMode::Fullscreen,
                        QuickOutputMode::None, previousLog);
  editor.resize(800, 600);
  editor.show();
  application.processEvents();
  editor.returnToSelectForTest();
  QTest::mousePress(&editor, Qt::LeftButton, Qt::NoModifier, QPoint(150, 150));
  QTest::mouseMove(&editor, QPoint(400, 350), 20);
  QTest::mouseRelease(&editor, Qt::LeftButton, Qt::NoModifier, QPoint(400, 350));
  editor.setProcessLauncherForTest([](const QString &, const QStringList &) { return true; });
  QTest::keyClick(&editor, Qt::Key_S, Qt::ControlModifier);
  editor.waitForExport();
  const auto after = listRecentSnaps(false);
  OperationLog freshLog;
  if (editor.isVisible() || !QFile::exists(previous.sourcePath) || after.isEmpty() ||
      !loadOperationLog(after.first().logPath, freshLog, error) ||
      freshLog.recentId.isEmpty() || freshLog.recentId == previousLog.recentId) {
    error = QStringLiteral("Fresh selection reused the previous capture identity");
    return false;
  }
  return true;
}
} // namespace

bool runRecentPersistenceSmoke(QApplication &application, QString &error) {
  const QTemporaryDir directory;
  if (!directory.isValid()) {
    error = QStringLiteral("Could not create recent persistence directory");
    return false;
  }
  const QDir root(directory.path());
  const QByteArray previousShelf = qgetenv("OMACAP_RECENT_DIR");
  const QByteArray previousOutput = qgetenv("OMACAP_SCREENSHOT_DIR");
  const auto restore = qScopeGuard([&] {
    qputenv("OMACAP_RECENT_DIR", previousShelf);
    qputenv("OMACAP_SCREENSHOT_DIR", previousOutput);
  });
  const QString shelf = root.filePath(QStringLiteral("recent"));
  const QString output = root.filePath(QStringLiteral("output"));
  qputenv("OMACAP_RECENT_DIR", shelf.toUtf8());
  qputenv("OMACAP_SCREENSHOT_DIR", output.toUtf8());

  CaptureData capture;
  capture.monitor.name = QStringLiteral("TEST");
  capture.monitor.geometry = {0, 0, 800, 600};
  capture.monitor.pixelSize = {800, 600};
  capture.monitor.scale = 1.0;
  capture.source = QImage(800, 600, QImage::Format_ARGB32_Premultiplied);
  capture.source.fill(Qt::white);
  capture.previewSize = capture.source.size();
  if (!checkPublication(capture.source, root, error) ||
      !checkReplacement(capture.source, root, error) ||
      !checkBackdropPruning(capture.source, error) ||
      !checkDestruction(capture, error) ||
      !checkWallpaperBackdrop(capture, error) ||
      !checkKeptLooksGetTheirWallpaper(capture, error))
    return false;

  auto editorOwner = std::make_unique<CaptureEditor>(
      capture, CaptureEditor::CaptureMode::Fullscreen);
  CaptureEditor &editor = *editorOwner;
  editor.resize(800, 600);
  editor.show();
  application.processEvents();
  if (!editor.waitForSnapshot()) {
    error = QStringLiteral("Could not persist recent editor fixture");
    return false;
  }

  QPromise<bool> pending;
  pending.start();
  bool released = false;
  const auto release = [&] {
    if (!released) {
      released = true;
      pending.addResult(true);
      pending.finish();
    }
  };
  const auto finishPending = qScopeGuard(release);
  editor.setSnapshotFutureForTest(pending.future());
  // Coalesce an edit behind the held autosave. The final shelf must include it.
  QTest::keyClick(&editor, Qt::Key_A);
  QTest::mousePress(&editor, Qt::LeftButton, Qt::NoModifier, QPoint(200, 200));
  QTest::mouseMove(&editor, QPoint(400, 320), 20);
  const auto unfinishedLog = editor.operationLog();
  editor.setProcessLauncherForTest([](const QString &, const QStringList &) { return true; });
  QTest::keyClick(&editor, Qt::Key_S, Qt::ControlModifier);
  QTest::qWait(30);
  if (editor.operationLog() != unfinishedLog ||
      editor.statusForTest().contains(QStringLiteral("Saving screenshot"))) {
    release();
    editor.waitForExport();
    error = QStringLiteral("Output started before the drawing gesture committed");
    return false;
  }
  QTest::mouseRelease(&editor, Qt::LeftButton, Qt::NoModifier,
                      QPoint(400, 320));
  if (editor.annotationCountForTest() != 1) {
    error = QStringLiteral("Could not draw recent persistence fixture arrow");
    return false;
  }

  QTest::keyClick(&editor, Qt::Key_V);
  const Annotation beforeNudge =
      editor.currentAnnotationsForTest().constFirst();
  const QPoint hit =
      editor.annotationPointToWidgetForTest(beforeNudge.start).toPoint();
  QTest::mouseClick(&editor, Qt::LeftButton, Qt::NoModifier, hit);
  QTest::keyClick(&editor, Qt::Key_Right);
  if (editor.currentAnnotationsForTest().constFirst().start ==
      beforeNudge.start) {
    error = QStringLiteral("Could not nudge recent persistence fixture arrow");
    return false;
  }

  // A file where the shelf directory should be makes publication fail without
  // relying on Unix permissions (which behave differently when run as root).
  const QString blockedShelf = root.filePath(QStringLiteral("blocked"));
  QFile blocker(blockedShelf);
  if (!blocker.open(QIODevice::WriteOnly)) {
    error = QStringLiteral("Could not block the recent shelf directory");
    return false;
  }
  blocker.close();
  qputenv("OMACAP_RECENT_DIR", blockedShelf.toUtf8());
  QTimer watchdog;
  watchdog.setSingleShot(true);
  QObject::connect(&watchdog, &QTimer::timeout, &application, release);
  watchdog.start(1500);
  editor.setProcessLauncherForTest([](const QString &, const QStringList &) { return true; });
  QTest::keyClick(&editor, Qt::Key_S, Qt::ControlModifier);
  const auto lockedAnnotations = editor.currentAnnotationsForTest();
  const auto lockedLog = editor.operationLog();
  QWheelEvent wheel(QPointF(hit), QPointF(editor.mapToGlobal(hit)), {},
                    {0, 120}, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase,
                    false);
  application.sendEvent(&editor, &wheel);
  if (editor.currentAnnotationsForTest() != lockedAnnotations ||
      editor.operationLog() != lockedLog) {
    release();
    editor.waitForExport();
    error =
        QStringLiteral("Wheel input changed layers during a pending export");
    return false;
  }
  QTest::qWait(200);
  // The old completion handler entered a nested event loop until the watchdog
  // fired. This wait must return while persistence is still held on the worker.
  if (released) {
    error = QStringLiteral(
        "Export completion blocked the GUI waiting for autosave");
    return false;
  }
  watchdog.stop();
  release();
  editor.waitForExport();
  const QString source = temporarySnapshotPath();
  const QString logPath = operationLogPath(source);
  if (editor.isVisible() || !QFile::exists(source) ||
      !QFile::exists(logPath) ||
      QDir(output).entryList({QStringLiteral("*.png")}, QDir::Files).size() !=
          1) {
    error = QStringLiteral("Failed shelving discarded the editable document or "
                           "hid successful output");
    return false;
  }
  OperationLog log;
  if (!loadOperationLog(logPath, log, error))
    return false;
  const qsizetype operations = log.ops.size();
  if (operations < 2 || log.ops.last().annotations.size() != 1 ||
      log.ops.last().annotations.first().start !=
          editor.currentAnnotationsForTest().first().start) {
    error =
        QStringLiteral("Final persistence dropped the coalesced annotation");
    return false;
  }
  editorOwner.reset();
  if (!QFile::exists(source) || !QFile::exists(logPath)) {
    error = QStringLiteral("Closed editor removed the retained recovery document");
    return false;
  }
  qputenv("OMACAP_RECENT_DIR", shelf.toUtf8());
  CaptureData recoveryCapture;
  describeFileCapture(recoveryCapture, QImage(source), log);
  CaptureEditor recovered(recoveryCapture, CaptureEditor::CaptureMode::File,
                          QuickOutputMode::None, log);
  recovered.resize(800, 600);
  recovered.show();
  const QImage beforeUndo = recovered.renderCurrentOutput();
  QTest::keyClick(&recovered, Qt::Key_Z, Qt::ControlModifier);
  const QImage afterUndo = recovered.renderCurrentOutput();
  QTest::keyClick(&recovered, Qt::Key_Z,
                  Qt::ControlModifier | Qt::ShiftModifier);
  if (afterUndo == beforeUndo || recovered.renderCurrentOutput() != beforeUndo ||
      recovered.operationLog() != log.ops || recovered.operationIndex() != log.index) {
    error = QStringLiteral("Recovery did not preserve undo and redo of final edits");
    return false;
  }
  recovered.setProcessLauncherForTest([](const QString &, const QStringList &) { return true; });
  QTest::keyClick(&recovered, Qt::Key_S, Qt::ControlModifier);
  recovered.waitForExport();
  const auto recent = listRecentSnaps();
  if (recovered.isVisible() || recent.size() != 1 ||
      recent.first().logPath.isEmpty() || QFile::exists(source) ||
      QFile::exists(logPath) ||
      !loadOperationLog(recent.first().logPath, log, error) ||
      log.ops.size() != operations) {
    error = QStringLiteral(
        "Retry did not publish the complete editable recent capture");
    return false;
  }
  // If the source needs to be recreated after cutting, it must still hold
  // pristine pixels. Saving the composed image would apply the cut twice when
  // this same operation log is reopened.
  Operation cut;
  cut.type = Operation::Type::Cut;
  cut.cut = {Qt::Horizontal, 100, 200, 100, 200};
  OperationLog cutLog;
  cutLog.ops = {cut};
  cutLog.index = 1;
  cutLog.previewSize = capture.previewSize;
  const QString cutLogPath = root.filePath(QStringLiteral("cut.json"));
  if (!saveOperationLog(cutLogPath, cutLog, error))
    return false;
  CaptureEditor cutEditor(capture, CaptureEditor::CaptureMode::Fullscreen);
  cutEditor.resize(800, 600);
  cutEditor.show();
  application.processEvents();
  if (!cutEditor.waitForSnapshot() || !QFile::remove(source) ||
      !cutEditor.restoreOperationLog(cutLogPath, error) ||
      !cutEditor.waitForSnapshot()) {
    error = QStringLiteral("Could not recreate a working source after cutting");
    return false;
  }
  if (QImage(source).size() != capture.source.size()) {
    error = QStringLiteral(
        "Working snapshot baked in a cut still present in history");
    return false;
  }
  cutEditor.setProcessLauncherForTest([](const QString &, const QStringList &) { return true; });
  QTest::keyClick(&cutEditor, Qt::Key_S, Qt::ControlModifier);
  cutEditor.waitForExport();
  const auto withCut = listRecentSnaps();
  if (withCut.size() != 2 || cutEditor.isVisible() ||
      QImage(withCut.first().sourcePath).size() != capture.source.size() ||
      !loadOperationLog(withCut.first().logPath, log, error) ||
      log.ops != cutLog.ops || log.index != cutLog.index ||
      log.previewSize != cutLog.previewSize) {
    error = QStringLiteral(
        "Shelving a cut document lost its pristine source or history");
    return false;
  }
  // A successful pending save can still belong to an earlier document. Cover
  // both coalesced autosave and immediate output after adopting a stitched
  // page.
  for (const bool immediateSave : {false, true}) {
    CaptureEditor replaced(capture, CaptureEditor::CaptureMode::Fullscreen);
    replaced.resize(800, 600);
    replaced.show();
    application.processEvents();
    if (!replaced.waitForSnapshot()) {
      error = QStringLiteral("Could not persist the old document fixture");
      return false;
    }
    QPromise<bool> oldSave;
    oldSave.start();
    bool oldReleased = false;
    const auto releaseOld = [&] {
      if (!oldReleased) {
        oldReleased = true;
        oldSave.addResult(true);
        oldSave.finish();
      }
    };
    const auto finishOld = qScopeGuard(releaseOld);
    replaced.setSnapshotFutureForTest(oldSave.future());
    QImage stitched(520, 920, QImage::Format_RGB32);
    stitched.fill(Qt::cyan);
    replaced.adoptStitchedForTest(stitched);
    if (QImage(source).size() != capture.source.size()) {
      error =
          QStringLiteral("Old document fixture was not held before adoption");
      return false;
    }
    replaced.setProcessLauncherForTest([](const QString &, const QStringList &) { return true; });
    if (immediateSave)
      QTest::keyClick(&replaced, Qt::Key_S, Qt::ControlModifier);
    releaseOld();
    if (!immediateSave) {
      if (!replaced.waitForSnapshot() ||
          QImage(source).convertToFormat(QImage::Format_RGB32) != stitched) {
        error =
            QStringLiteral("Old autosave completion hid the adopted source");
        return false;
      }
      replaced.setProcessLauncherForTest([](const QString &, const QStringList &) { return true; });
      QTest::keyClick(&replaced, Qt::Key_S, Qt::ControlModifier);
    }
    replaced.waitForExport();
    const auto adopted = listRecentSnaps();
    if (replaced.isVisible() || adopted.isEmpty() ||
        QImage(adopted.first().sourcePath)
                .convertToFormat(QImage::Format_RGB32) != stitched ||
        !loadOperationLog(adopted.first().logPath, log, error) ||
        log.previewSize != stitched.size()) {
      error = QStringLiteral(
          "Old autosave completion shelved pixels from another document");
      return false;
    }
  }
  // Reopening a shelf document makes Save replace that entry. Adopting a new
  // stitched document ends that ownership, even in the same editor window.
  const auto beforeFresh = listRecentSnaps();
  const QString reopenedSource = beforeFresh.first().sourcePath;
  CaptureEditor fresh(capture, CaptureEditor::CaptureMode::Region,
                      QuickOutputMode::CopyAndPreview);
  fresh.resize(800, 600);
  fresh.show();
  application.processEvents();
  if (!fresh.waitForRecents()) {
    error = QStringLiteral("Could not list recents for fresh document fixture");
    return false;
  }
  QTest::mouseMove(&fresh, fresh.recentCardRectForTest(0).center().toPoint(), 20);
  QTest::mouseClick(&fresh, Qt::LeftButton, Qt::NoModifier,
                    fresh.recentCardRectForTest(0).center().toPoint());
  fresh.waitForReopen();
  if (!fresh.editingForTest() || !fresh.isVisible()) {
    error = QStringLiteral("Reopening a recent inherited fresh-capture quick output");
    return false;
  }
  QImage newDocument(300, 400, QImage::Format_RGB32);
  newDocument.fill(Qt::yellow);
  fresh.adoptStitchedForTest(newDocument);
  fresh.setProcessLauncherForTest([](const QString &, const QStringList &) { return true; });
  QTest::keyClick(&fresh, Qt::Key_S, Qt::ControlModifier);
  fresh.waitForExport();
  if (fresh.isVisible() || !QFile::exists(reopenedSource) ||
      listRecentSnaps().size() != beforeFresh.size() + 1) {
    error = QStringLiteral("A fresh document replaced a previously reopened recent");
    return false;
  }
  return checkRecentHandoff(application, capture, root, error) &&
         checkFreshSelection(application, capture, error);
}
