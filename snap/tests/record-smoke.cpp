#include "record-smoke.hpp"

#include "capture-state.hpp"
#include "editor.hpp"
#include "record-picker.hpp"

#include <QApplication>
#include <QScopeGuard>
#include <QtTest/QTest>

namespace {
/// Logical pixels relative to the monitor, edges rounded, clamped to it;
/// scale and transform travel so the video process can map the area onto
/// its stream.
bool checkRequestBuilding(QString &error) {
  MonitorInfo monitor;
  monitor.name = QStringLiteral("DP-3");
  monitor.geometry = {2560, 0, 1440, 2560};
  monitor.scale = 1.5;
  monitor.transform = 1;
  RecordChoices choices{QStringLiteral("mic-1"), QStringLiteral("/dev/video0"), true};
  const RecordRequest request = buildRecordRequest(
      monitor, QRectF(-10.0, 20.2, 300.4, 200.6), choices);
  if (request.monitor != QStringLiteral("DP-3") ||
      request.monitorLogical != monitor.geometry || request.scale != 1.5 ||
      request.transform != 1 || request.area != QRect(0, 20, 290, 201) ||
      request.nativeArea() != QRect(0, 30, 435, 302) ||
      request.mic != choices.mic || request.camera != choices.camera ||
      !request.desktopAudio) {
    error = QStringLiteral("The record request lost its monitor, area or "
                           "devices: %1,%2 %3x%4")
                .arg(request.area.x())
                .arg(request.area.y())
                .arg(request.area.width())
                .arg(request.area.height());
    return false;
  }
  const auto parsed = RecordRequest::fromJson(request.toJson());
  if (!parsed || parsed->area != request.area ||
      parsed->transform != request.transform || parsed->mic != request.mic) {
    error = QStringLiteral("The record request did not survive JSON");
    return false;
  }
  // A device that went away falls back rather than failing the recording.
  RecordDevices devices;
  devices.mics = {{QStringLiteral("mic-2"), QStringLiteral("Desk mic")}};
  devices.defaultMic = QStringLiteral("mic-2");
  const RecordChoices kept = availableChoices(choices, devices);
  if (kept.mic != QStringLiteral("mic-2") || !kept.camera.isEmpty() ||
      !kept.desktopAudio) {
    error = QStringLiteral("Missing record devices were not replaced");
    return false;
  }
  return true;
}

/// Picking an area in record mode shows the bar and captures nothing; Enter
/// hands the request off, remembers the choices and closes the overlay.
bool checkPicker(QApplication &application, QString &error) {
  CaptureData capture;
  capture.monitor.name = QStringLiteral("TEST");
  capture.monitor.geometry = {0, 0, 800, 600};
  capture.monitor.pixelSize = {1200, 900};
  capture.monitor.scale = 1.5;
  capture.monitor.transform = 0;
  capture.previewSize = {800, 600};
  capture.source = QImage(1200, 900, QImage::Format_ARGB32_Premultiplied);
  capture.source.fill(Qt::darkGray);
  capture.windows = {{QRect(420, 300, 300, 200), QStringLiteral("w"),
                      QStringLiteral("Terminal"), QStringLiteral("foot")}};

  CaptureEditor editor(capture, CaptureEditor::CaptureMode::Smart,
                       QuickOutputMode::CopyAndPreview);
  QVector<RecordRequest> handed;
  editor.setRecordHandoffForTest([&handed](const RecordRequest &request) {
    handed.push_back(request);
    return QString();
  });
  editor.resize(800, 600);
  editor.show();
  editor.setRecordMode(true);
  editor.waitForRecordDevices();
  application.processEvents();

  QTest::mousePress(&editor, Qt::LeftButton, Qt::NoModifier, QPoint(100, 120));
  QTest::mouseMove(&editor, QPoint(400, 320), 10);
  QTest::mouseRelease(&editor, Qt::LeftButton, Qt::NoModifier, QPoint(400, 320));
  application.processEvents();
  const RecordRequest dragged = editor.recordRequestForTest();
  if (!editor.recordBarShownForTest() || editor.exportingForTest() ||
      editor.editingForTest() || dragged.area != QRect(100, 120, 300, 200) ||
      dragged.scale != 1.5 || dragged.monitor != QStringLiteral("TEST")) {
    error = QStringLiteral("Dragging in record mode did not pick the area "
                           "for the bar");
    return false;
  }
  const QRectF bar = editor.recordBarRectForTest();
  if (bar.intersects(QRectF(100, 120, 300, 200))) {
    error = QStringLiteral("The record bar covers the picked area");
    return false;
  }

  // Pick again: a click on a window takes the window.
  QTest::mouseClick(&editor, Qt::LeftButton, Qt::NoModifier, QPoint(500, 380));
  application.processEvents();
  if (!editor.recordBarShownForTest() ||
      editor.recordRequestForTest().area != QRect(420, 300, 300, 200)) {
    error = QStringLiteral("Clicking a window in record mode did not pick it");
    return false;
  }

  QTest::keyClick(&editor, Qt::Key_D);
  QTest::keyClick(&editor, Qt::Key_Return);
  const QFuture<QString> handoff = editor.recordHandoffForTest();
  while (!handoff.isFinished())
    application.processEvents();
  if (handed.size() != 1 || handed.constFirst().area != QRect(420, 300, 300, 200) ||
      !handed.constFirst().desktopAudio || editor.isVisible() ||
      !loadRecordChoices().desktopAudio) {
    error = QStringLiteral("Enter did not hand off the request, remember the "
                           "choices and close");
    return false;
  }
  saveRecordChoices({});

  // V turns a picked area into an ordinary screenshot instead.
  CaptureEditor instead(capture, CaptureEditor::CaptureMode::Smart,
                        QuickOutputMode::CopyAndPreview);
  instead.setProcessLauncherForTest([](const QString &, const QStringList &) {
    return true;
  });
  instead.resize(800, 600);
  instead.show();
  application.processEvents();
  // A click on the switch changes mode; a drag from it would select.
  QTest::mouseClick(&instead, Qt::LeftButton, Qt::NoModifier,
                    instead.modeSwitchCenterForTest(true));
  if (!instead.recordModeForTest() || !instead.selectingForTest()) {
    error = QStringLiteral("Clicking Record did not switch to record mode");
    return false;
  }
  instead.waitForRecordDevices();
  QTest::mouseClick(&instead, Qt::LeftButton, Qt::NoModifier, QPoint(60, 60));
  application.processEvents();
  QTest::keyClick(&instead, Qt::Key_V);
  if (instead.recordModeForTest() || !instead.exportingForTest()) {
    error = QStringLiteral("V did not screenshot the picked area instead");
    return false;
  }
  instead.waitForExport();
  return true;
}
} // namespace

bool runRecordSmoke(QApplication &application, QString &error) {
  return checkRequestBuilding(error) && checkPicker(application, error);
}
