#include "look-smoke.hpp"

#include "capture-state.hpp"
#include "capture.hpp"
#include "editor.hpp"

#include <QApplication>
#include <QDir>
#include <QMouseEvent>
#include <QTemporaryDir>
#include <QtEnvironmentVariables>
#include <QJsonObject>
#include <QScopeGuard>
#include <QThreadPool>
#include <QtTest/QTest>

#include <cmath>

namespace {
CaptureData solidCapture(const QSize &logical, qreal scale, const QColor &fill) {
  CaptureData capture;
  capture.monitor.name = QStringLiteral("TEST");
  capture.monitor.scale = scale;
  capture.monitor.geometry = {QPoint(), logical};
  capture.monitor.pixelSize = (QSizeF(logical) * scale).toSize();
  capture.previewSize = logical;
  capture.source = QImage(capture.monitor.pixelSize,
                          QImage::Format_ARGB32_Premultiplied);
  capture.source.fill(fill);
  return capture;
}

bool near(const QRectF &a, const QRectF &b) {
  return std::abs(a.left() - b.left()) < 0.01 &&
         std::abs(a.top() - b.top()) < 0.01 &&
         std::abs(a.width() - b.width()) < 0.01 &&
         std::abs(a.height() - b.height()) < 0.01;
}

/// Padding is a share of the canvas width; a fixed aspect grows the canvas
/// around the card; layers grow it further; a stitch too tall for a fixed
/// aspect keeps Auto.
bool checkLayoutMaths(QString &error) {
  CaptureLook look;
  look.backdrop = BackgroundStyle::Aurora;
  // 860 / (1 - 2 × 7%) = 1000 wide, so 70 px of padding on every side.
  const QSizeF wide(860, 400);
  if (!near(framedCanvas(wide, look), QRectF(-70, -70, 1000, 540))) {
    error = QStringLiteral("Auto padding is not 7% of the canvas width");
    return false;
  }
  const CaptureLayout automatic =
      captureLayout(wide, QRectF(), CanvasBoundaryMode::Framed, look);
  if (!automatic.styled || !automatic.backdrop ||
      std::abs(automatic.radius - 15.0) > 0.01) {
    error = QStringLiteral("Corners are not 1.5% of the canvas width");
    return false;
  }
  look.aspect = CanvasAspect::Wide;
  if (!near(framedCanvas(wide, look), QRectF(-70, -81.25, 1000, 562.5))) {
    error = QStringLiteral("16:9 did not heighten the canvas around the card");
    return false;
  }
  look.aspect = CanvasAspect::Square;
  if (!near(framedCanvas(wide, look), QRectF(-70, -300, 1000, 1000))) {
    error = QStringLiteral("1:1 did not square the canvas around the card");
    return false;
  }
  // A portrait window into 16:9 widens; the card keeps its own size and the
  // padding stays a share of the (now wider) canvas.
  look.aspect = CanvasAspect::Wide;
  const QRectF portrait = framedCanvas(QSizeF(400, 860), look);
  if (std::abs(portrait.width() / portrait.height() - 16.0 / 9.0) > 0.001 ||
      std::abs((portrait.height() - 860) / 2 - 0.07 * portrait.width()) > 0.01) {
    error = QStringLiteral("16:9 around a portrait card is not padded by width");
    return false;
  }
  // A 20 000 px stitch keeps Auto rather than a 35 000 px wide canvas.
  CaptureLook automaticLook = look;
  automaticLook.aspect = CanvasAspect::Auto;
  if (framedCanvas(QSizeF(1000, 20000), look) !=
      framedCanvas(QSizeF(1000, 20000), automaticLook)) {
    error = QStringLiteral("A tall stitch took a fixed aspect");
    return false;
  }
  // A layer past the frame grows it, then the canvas widens back to 16:9.
  const QRectF layer(900, 100, 80, 40);
  const CaptureLayout grown =
      captureLayout(wide, layer, CanvasBoundaryMode::Framed, look);
  if (grown.canvas.right() < layer.right() + kFramedLayerMargin ||
      std::abs(grown.canvas.width() / grown.canvas.height() - 16.0 / 9.0) >
          0.01 ||
      grown.radius != captureLayout(wide, QRectF(), CanvasBoundaryMode::Framed,
                                    look).radius) {
    error = QStringLiteral("Growth did not keep the aspect and the corners");
    return false;
  }
  // So does a wide one; a layer dragged far off keeps the grown canvas Auto
  // rather than heightening it to 16:9 as well.
  if (framedCanvas(QSizeF(30000, 500), look) !=
      framedCanvas(QSizeF(30000, 500), automaticLook)) {
    error = QStringLiteral("A wide stitch took a fixed aspect");
    return false;
  }
  const QRectF distant(30000, 100, 80, 40);
  if (captureLayout(wide, distant, CanvasBoundaryMode::Framed, look).canvas !=
      captureLayout(wide, distant, CanvasBoundaryMode::Framed, automaticLook)
          .canvas) {
    error = QStringLiteral("A far layer widened a fixed aspect past its limit");
    return false;
  }
  // Overflow and Image never frame; Off frames nothing.
  const CaptureLayout overflow =
      captureLayout(wide, layer, CanvasBoundaryMode::Overflow, look);
  const CaptureLayout image =
      captureLayout(wide, layer, CanvasBoundaryMode::Image, look);
  const CaptureLayout off = captureLayout(wide, QRectF(),
                                          CanvasBoundaryMode::Framed,
                                          CaptureLook::plain());
  if (overflow.styled || overflow.canvas != QRectF(0, 0, 980, 400) ||
      !overflow.backdrop || image.canvas != QRectF(QPointF(), wide) ||
      image.backdrop || off.styled || off.canvas != QRectF(QPointF(), wide)) {
    error = QStringLiteral("Overflow, Image or Off kept a frame");
    return false;
  }
  // Export keeps native pixels: the canvas at the monitor's scale.
  look.aspect = CanvasAspect::Auto;
  const CaptureData capture = solidCapture({430, 200}, 2.0, Qt::white);
  const QImage output =
      renderCapture(capture, QRectF(0, 0, 430, 200), {}, look);
  const QRectF canvas =
      captureLayout(QSizeF(430, 200), QRectF(), CanvasBoundaryMode::Framed, look)
          .canvas;
  if (output.size() != (canvas.size() * 2.0).toSize()) {
    error = QStringLiteral("A styled export lost the native scale: %1x%2")
                .arg(output.width())
                .arg(output.height());
    return false;
  }
  return true;
}

/// The look is an op-log entry: it round-trips through JSON, undoes and
/// redoes, survives restoring the log, and is remembered for the next
/// capture.
bool checkLookPersistence(QApplication &application, QString &error) {
  CaptureLook look;
  look.backdrop = BackgroundStyle::Solid;
  look.color = QColor(QStringLiteral("#3a5f2b"));
  look.image = QStringLiteral("0123456789abcdef0123");
  look.padding = 0.12;
  look.corners = 0.04;
  look.shadow = 0.8;
  look.glass = Glass::Custom;
  look.opacity = 0.7;
  look.aspect = CanvasAspect::Standard;
  QJsonObject hostile = lookToJson(look);
  hostile.insert(QStringLiteral("image"), QStringLiteral("../../etc/passwd"));
  hostile.insert(QStringLiteral("padding"), 3.0);
  const CaptureLook parsed = lookFromJson(hostile);
  if (lookFromJson(lookToJson(look)) != look || !parsed.image.isEmpty() ||
      parsed.padding != 0.25) {
    error = QStringLiteral("The look did not round-trip, or accepted a bad "
                           "asset id or padding");
    return false;
  }

  const CaptureData capture = solidCapture({320, 200}, 1.0, Qt::darkCyan);
  CaptureEditor editor(capture, CaptureEditor::CaptureMode::File);
  editor.resize(800, 600);
  editor.show();
  editor.waitForPictures();
  application.processEvents();
  const QImage plain = editor.renderCurrentOutput();
  editor.setLookForTest(look);
  const QImage styled = editor.renderCurrentOutput();
  if (editor.operationLog().constLast().type != Operation::Type::Look ||
      editor.operationLog().constLast().look != look || styled == plain ||
      styled != renderCapture(capture, editor.currentSelection(), {}, look)) {
    error = QStringLiteral("Setting a look did not commit one Look operation");
    return false;
  }
  QTest::keyClick(&editor, Qt::Key_Z, Qt::ControlModifier);
  if (editor.currentLookForTest() != CaptureLook::plain() ||
      editor.renderCurrentOutput() != plain) {
    error = QStringLiteral("Undo did not restore the previous look");
    return false;
  }
  QTest::keyClick(&editor, Qt::Key_Y, Qt::ControlModifier);
  if (!editor.waitForSnapshot() || editor.workingLogPath().isEmpty()) {
    error = QStringLiteral("The look was not persisted");
    return false;
  }
  CaptureEditor restored(capture, CaptureEditor::CaptureMode::File);
  QString restoreError;
  if (!restored.restoreOperationLog(editor.workingLogPath(), restoreError) ||
      restored.currentLookForTest() != look ||
      restored.renderCurrentOutput() != styled) {
    error = QStringLiteral("Restoring the log changed the look: %1")
                .arg(restoreError);
    return false;
  }
  static_cast<void>(restored.waitForSnapshot());
  restored.close();

  // Remembered for the next capture, which starts with it as its first,
  // undoable operation.
  setRememberLooks(true);
  const auto forget = qScopeGuard([] {
    QThreadPool::globalInstance()->waitForDone();
    saveLastLook(CaptureLook::plain());
    setRememberLooks(false);
  });
  CaptureLook remembered = look;
  remembered.backdrop = BackgroundStyle::Lagoon;
  editor.setLookForTest(remembered);
  QThreadPool::globalInstance()->waitForDone();
  remembered.wallpaper.clear();
  if (loadLastLook() != remembered) {
    error = QStringLiteral("The editor did not remember its look");
    return false;
  }
  // A quick run of looks is remembered in order: the last one wins.
  for (int step = 1; step <= 24; ++step) {
    remembered.padding = step / 100.0;
    editor.setLookForTest(remembered);
  }
  QThreadPool::globalInstance()->waitForDone();
  if (loadLastLook() != remembered) {
    error = QStringLiteral("Quick looks were remembered out of order");
    return false;
  }
  static_cast<void>(editor.waitForSnapshot());
  editor.close();
  CaptureEditor next(capture, CaptureEditor::CaptureMode::Smart);
  if (next.operationLog().isEmpty() ||
      next.operationLog().constFirst().type != Operation::Type::Look ||
      next.operationLog().constFirst().look != remembered ||
      next.currentLookForTest() != remembered) {
    error = QStringLiteral("A fresh capture did not start with the last look");
    return false;
  }
  next.close();
  return true;
}

/// Glass shows the backdrop through the card, never its shadow; layers
/// paint at full opacity; redaction destroys pixels before any resampling.
bool checkCardPainting(QString &error) {
  const CaptureData capture = solidCapture({200, 120}, 1.0, QColor(0, 0, 0));
  CaptureLook look;
  look.backdrop = BackgroundStyle::Solid;
  look.color = Qt::white;
  look.glass = Glass::Custom;
  look.opacity = 0.5;
  look.shadow = 1.0;
  CaptureLook shadowless = look;
  shadowless.shadow = 0.0;
  Annotation box;
  box.kind = Annotation::Kind::Rectangle;
  box.filled = true;
  box.start = {120, 20};
  box.end = {180, 100};
  box.color = QColor(255, 0, 0);
  box.size = 4;
  const QImage shadowed =
      renderCapture(capture, QRectF(0, 0, 200, 120), {box}, look);
  const QImage flat =
      renderCapture(capture, QRectF(0, 0, 200, 120), {box}, shadowless);
  const QPoint origin = (-captureLayout(QSizeF(200, 120), {box},
                                        CanvasBoundaryMode::Framed, look)
                              .canvas.topLeft())
                            .toPoint();
  const QColor card = shadowed.pixelColor(origin + QPoint(60, 60));
  if (std::abs(card.red() - 128) > 2 || card != flat.pixelColor(origin + QPoint(60, 60))) {
    error = QStringLiteral("A see-through card showed its shadow: %1")
                .arg(card.name());
    return false;
  }
  if (shadowed.pixelColor(origin + QPoint(150, 60)) != QColor(255, 0, 0)) {
    error = QStringLiteral("A layer did not paint at full opacity over glass");
    return false;
  }
  if (shadowed.pixelColor(origin + QPoint(100, 125)) ==
      flat.pixelColor(origin + QPoint(100, 125))) {
    error = QStringLiteral("The shadow did not paint outside the card");
    return false;
  }

  // A secret under a redaction at a fractional scale, behind glass: no red
  // survives the resample anywhere in the output.
  CaptureData secret = solidCapture({100, 100}, 1.25, Qt::white);
  for (int y = 13; y <= 38; ++y)
    for (int x = 13; x <= 38; ++x)
      secret.source.setPixelColor(x, y, Qt::red);
  Annotation redaction;
  redaction.kind = Annotation::Kind::Redaction;
  redaction.redactionStyle = RedactionStyle::Solid;
  redaction.start = {10, 10};
  redaction.end = {30, 30};
  look.color = QColor(40, 40, 40);
  const QImage redacted =
      renderCapture(secret, QRectF(1, 1, 81, 81), {redaction}, look);
  for (int y = 0; y < redacted.height(); ++y) {
    for (int x = 0; x < redacted.width(); ++x) {
      const QColor pixel = redacted.pixelColor(x, y);
      if (pixel.red() > pixel.green() + 24) {
        error = QStringLiteral("Redacted pixels leaked through a styled export");
        return false;
      }
    }
  }
  return true;
}
/// With zero padding the canvas is the card, yet glass still shows the
/// backdrop through it: a spotlight magnifies that, on screen as in the
/// export.
bool checkSpotlightOverGlass(QApplication &application, QString &error) {
  const CaptureData capture = solidCapture({200, 120}, 1.0, Qt::black);
  Operation styled;
  styled.type = Operation::Type::Look;
  styled.look.backdrop = BackgroundStyle::Solid;
  styled.look.color = Qt::white;
  styled.look.padding = 0.0;
  styled.look.shadow = 0.0;
  styled.look.glass = Glass::Custom;
  styled.look.opacity = 0.5;
  Annotation spotlight;
  spotlight.id = 1;
  spotlight.kind = Annotation::Kind::Spotlight;
  spotlight.start = {40, 30};
  spotlight.end = {160, 90};
  spotlight.magnification = 2.0;
  Operation annotate;
  annotate.type = Operation::Type::Annotate;
  annotate.annotations = {spotlight};
  OperationLog log;
  log.ops = {styled, annotate};
  log.index = 2;
  log.nextId = 2;
  log.previewSize = capture.previewSize;
  CaptureEditor editor(capture, CaptureEditor::CaptureMode::File,
                       QuickOutputMode::None, log);
  editor.setSuppressSnapshots(true);
  editor.resize(800, 600);
  editor.show();
  application.processEvents();
  const QPointF center(100, 60);
  const QColor exported = editor.renderCurrentOutput().pixelColor(center.toPoint());
  const QImage shown = editor.grab().toImage();
  const QColor seen = shown.pixelColor(
      (editor.annotationPointToWidgetForTest(center) * shown.devicePixelRatio())
          .toPoint());
  editor.close();
  if (std::abs(exported.red() - 128) > 3 ||
      std::abs(seen.red() - exported.red()) > 3) {
    error = QStringLiteral("A spotlight over glass magnified %1 in the editor "
                           "but %2 in the export")
                .arg(seen.name(), exported.name());
    return false;
  }
  return true;
}

/// The toolbar's look popovers: hovering a look button opens its popover;
/// a slider drag previews live and is one undo step; segments and swatches
/// commit at once.
bool checkPopovers(QApplication &application, QString &error) {
  const CaptureData capture = solidCapture({480, 300}, 1.0, Qt::darkCyan);
  CaptureLook start;
  start.backdrop = BackgroundStyle::Slate;
  Operation look;
  look.type = Operation::Type::Look;
  look.look = start;
  OperationLog log;
  log.ops = {look};
  log.index = 1;
  log.previewSize = capture.previewSize;
  CaptureEditor editor(capture, CaptureEditor::CaptureMode::File,
                       QuickOutputMode::None, log);
  int pins = 0;
  editor.setProcessLauncherForTest([&pins](const QString &, const QStringList &) {
    ++pins;
    return true;
  });
  editor.resize(800, 600);
  editor.show();
  editor.waitForPictures();
  application.processEvents();
  const auto hover = [&](const QString &action) {
    QTest::mouseMove(&editor, editor.toolbarButtonCenterForTest(action));
    application.processEvents();
  };

  // No tooltip covers an open popover: neither its own button's nor the
  // armed tool's, which sits beside it.
  QTest::keyClick(&editor, Qt::Key_A);
  for (const QString &action : {QStringLiteral("look-frame"), QStringLiteral("palette")}) {
    hover(QStringLiteral("look-glass"));
    hover(action);
    if (!editor.toolbarTooltipForTest().isEmpty()) {
      error = QStringLiteral("A tooltip covered the open %1 popover: %2")
                  .arg(action, editor.toolbarTooltipForTest());
      return false;
    }
  }
  QTest::mouseMove(&editor, QPoint(600, 560));
  if (editor.toolbarTooltipForTest().isEmpty()) {
    error = QStringLiteral("The armed tool lost its tooltip");
    return false;
  }
  QTest::keyClick(&editor, Qt::Key_V);
  hover(QStringLiteral("look-frame"));
  const QRectF track = editor.lookControlRectForTest(QStringLiteral("padding"));
  if (editor.lookPopoverForTest() != CaptureEditor::LookPopover::Frame ||
      track.isEmpty()) {
    error = QStringLiteral("Hovering Frame did not open its popover");
    return false;
  }
  // Down into the popover, as a pointer would travel.
  QTest::mouseMove(&editor, QPoint(qRound(track.center().x()),
                                   qRound(track.top()) - 30));
  QTest::mouseMove(&editor, track.center().toPoint());
  const qsizetype before = editor.operationLog().size();
  QTest::mousePress(&editor, Qt::LeftButton, Qt::NoModifier,
                    QPoint(qRound(track.left() + track.width() * 0.2),
                           qRound(track.center().y())));
  for (const qreal along : {0.4, 0.6, 0.8})
    QTest::mouseMove(&editor, QPoint(qRound(track.left() + track.width() * along),
                                     qRound(track.center().y())));
  const qreal live = editor.currentLookForTest().padding;
  // Pinning mid-drag would pin the live look with a log that lacks it.
  QTest::keyClick(&editor, Qt::Key_P, Qt::ControlModifier);
  QThreadPool::globalInstance()->waitForDone();
  if (pins != 0) {
    error = QStringLiteral("Pinning during a slider drag did not wait for it");
    return false;
  }
  QTest::mouseRelease(&editor, Qt::LeftButton, Qt::NoModifier,
                      QPoint(qRound(track.left() + track.width() * 0.8),
                             qRound(track.center().y())));
  application.processEvents();
  if (std::abs(live - 0.2) > 0.01 || editor.operationLog().size() != before + 1 ||
      std::abs(editor.currentLookForTest().padding - 0.2) > 0.01) {
    error = QStringLiteral("A padding drag was not one live, undoable step (%1)")
                .arg(editor.currentLookForTest().padding);
    return false;
  }
  QTest::keyClick(&editor, Qt::Key_Z, Qt::ControlModifier);
  if (editor.currentLookForTest() != start) {
    error = QStringLiteral("Undo did not take back the whole padding drag");
    return false;
  }

  QTest::mouseClick(&editor, Qt::LeftButton, Qt::NoModifier,
                    editor.lookControlCenterForTest(QStringLiteral("aspect-wide")));
  if (editor.currentLookForTest().aspect != CanvasAspect::Wide) {
    error = QStringLiteral("The 16:9 segment did not set the canvas");
    return false;
  }

  hover(QStringLiteral("look-backdrop"));
  const QPoint aurora = editor.lookControlCenterForTest(QStringLiteral("aurora"));
  QTest::mouseMove(&editor, QPoint(aurora.x(), aurora.y() - 30));
  QTest::mouseClick(&editor, Qt::LeftButton, Qt::NoModifier, aurora);
  if (editor.lookPopoverForTest() != CaptureEditor::LookPopover::Backdrop ||
      editor.currentLookForTest().backdrop != BackgroundStyle::Aurora) {
    error = QStringLiteral("The Aurora swatch did not set the backdrop");
    return false;
  }

  hover(QStringLiteral("look-glass"));
  const QRectF opacity = editor.lookControlRectForTest(QStringLiteral("opacity"));
  QTest::mouseMove(&editor, QPoint(qRound(opacity.center().x()),
                                   qRound(opacity.top()) - 40));
  QTest::mousePress(&editor, Qt::LeftButton, Qt::NoModifier,
                    opacity.center().toPoint());
  QTest::mouseRelease(&editor, Qt::LeftButton, Qt::NoModifier,
                      opacity.center().toPoint());
  if (editor.currentLookForTest().glass != Glass::Custom) {
    error = QStringLiteral("Moving the opacity slider did not pick Custom glass");
    return false;
  }

  // Leaving the popover closes it; nothing drew on the canvas meanwhile.
  QTest::mouseMove(&editor, QPoint(600, 560));
  application.processEvents();
  if (editor.lookPopoverForTest() != CaptureEditor::LookPopover::None ||
      editor.annotationCountForTest() != 0) {
    error = QStringLiteral("The look popover stayed open or drew a layer");
    return false;
  }
  // Esc mid-drag drops the drag's look before the editor closes.
  const CaptureLook settled = editor.currentLookForTest();
  hover(QStringLiteral("look-frame"));
  const QRectF shadow = editor.lookControlRectForTest(QStringLiteral("shadow"));
  QTest::mouseMove(&editor, QPoint(qRound(shadow.center().x()),
                                   qRound(shadow.top()) - 30));
  QTest::mousePress(&editor, Qt::LeftButton, Qt::NoModifier,
                    QPoint(qRound(shadow.left() + 2), qRound(shadow.center().y())));
  QTest::mouseMove(&editor, QPoint(qRound(shadow.right() - 2),
                                   qRound(shadow.center().y())));
  const bool dragged = editor.currentLookForTest() != settled;
  QTest::keyClick(&editor, Qt::Key_Escape);
  if (!dragged || editor.currentLookForTest() != settled) {
    error = QStringLiteral("Esc during a slider drag kept its look");
    return false;
  }
  editor.close();
  return true;
}

/// A slider drag's look is live, not yet in the log. The wallpaper arriving
/// meanwhile gives it the picture rather than replacing it with the log's
/// look, so releasing keeps the drag and cancelling returns to a look that
/// has the wallpaper.
bool checkSliderSurvivesWallpaper(QString &error) {
  const QTemporaryDir theme;
  const QByteArray previousTheme = qgetenv("OMACAP_THEME_ROOT");
  const auto restore = qScopeGuard([&] {
    previousTheme.isNull() ? qunsetenv("OMACAP_THEME_ROOT")
                           : qputenv("OMACAP_THEME_ROOT", previousTheme);
    saveLastLook(CaptureLook::plain());
  });
  QImage wallpaper(64, 64, QImage::Format_RGB32);
  wallpaper.fill(QColor(200, 30, 40));
  if (!theme.isValid() ||
      !wallpaper.save(QDir(theme.path()).filePath(QStringLiteral("background")), "PNG")) {
    error = QStringLiteral("Could not prepare the wallpaper fixture");
    return false;
  }
  qputenv("OMACAP_THEME_ROOT", theme.path().toUtf8());
  CaptureLook remembered;
  remembered.backdrop = BackgroundStyle::Wallpaper;
  saveLastLook(remembered);

  for (const bool release : {true, false}) {
    CaptureEditor editor(solidCapture({480, 300}, 1.0, Qt::darkCyan),
                         CaptureEditor::CaptureMode::Fullscreen);
    editor.resize(800, 600);
    editor.show();
    // Sent directly, so no event loop runs and the wallpaper, resolved on a
    // worker, can only arrive when the test lets it.
    const auto send = [&](QEvent::Type type, const QPointF &at,
                          Qt::MouseButtons held) {
      QMouseEvent event(type, at, editor.mapToGlobal(at),
                        type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                        held, Qt::NoModifier);
      QCoreApplication::sendEvent(&editor, &event);
    };
    send(QEvent::MouseMove,
         editor.toolbarButtonCenterForTest(QStringLiteral("look-frame")), Qt::NoButton);
    const QRectF track = editor.lookControlRectForTest(QStringLiteral("padding"));
    const qsizetype before = editor.operationLog().size();
    const auto along = [&](qreal t) {
      return QPointF(track.left() + track.width() * t, track.center().y());
    };
    send(QEvent::MouseButtonPress, along(0.2), Qt::LeftButton);
    send(QEvent::MouseMove, along(0.8), Qt::LeftButton);
    const qreal dragged = editor.currentLookForTest().padding;
    if (track.isEmpty() || !editor.currentLookForTest().wallpaper.isEmpty()) {
      error = QStringLiteral("The drag did not start before the wallpaper");
      return false;
    }
    editor.waitForPictures();
    const QString resolved = editor.operationLog().value(0).look.wallpaper;
    if (resolved.isEmpty() || editor.currentLookForTest().padding != dragged ||
        editor.currentLookForTest().wallpaper != resolved) {
      error = QStringLiteral("The wallpaper replaced a slider drag's live look");
      return false;
    }
    if (release) {
      send(QEvent::MouseButtonRelease, along(0.8), Qt::NoButton);
      const auto log = editor.operationLog();
      if (log.size() != before + 1 || log.last().look.padding != dragged ||
          log.last().look.wallpaper != resolved) {
        error = QStringLiteral("Releasing the drag lost it to the wallpaper");
        return false;
      }
    } else {
      QTest::keyClick(&editor, Qt::Key_Escape);
      if (editor.currentLookForTest() != editor.operationLog().value(0).look ||
          editor.currentLookForTest().wallpaper != resolved) {
        error = QStringLiteral("Cancelling the drag lost the wallpaper");
        return false;
      }
    }
    editor.close();
  }
  return true;
}
} // namespace

bool runLookSmoke(QApplication &application, QString &error) {
  return checkLayoutMaths(error) && checkLookPersistence(application, error) &&
         checkCardPainting(error) &&
         checkSpotlightOverGlass(application, error) &&
         checkPopovers(application, error) && checkSliderSurvivesWallpaper(error);
}
