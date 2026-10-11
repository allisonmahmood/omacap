#include "look-smoke.hpp"

#include "capture-state.hpp"
#include "capture.hpp"
#include "editor.hpp"

#include <QApplication>
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
} // namespace

bool runLookSmoke(QApplication &application, QString &error) {
  return checkLayoutMaths(error) && checkLookPersistence(application, error) &&
         checkCardPainting(error);
}
