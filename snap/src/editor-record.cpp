/** @fileoverview The picker's record mode: the Screenshot | Record switch,
 *  the record bar beside a picked area, and the handoff to the video
 *  process. Nothing is captured or copied in this mode. */
#include "editor.hpp"

#include "chrome-theme.hpp"
#include "icons.hpp"
#include "overlay-chrome.hpp"
#include "video-ipc.h"

#include <QCoreApplication>
#include <QFontMetricsF>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QPainter>
#include <QProcess>
#include <QThread>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>

namespace {
constexpr qreal kBarHeight = 44.0;
constexpr qreal kBarPad = 6.0;
constexpr qreal kItemHeight = 32.0;
constexpr qreal kItemGap = 6.0;
constexpr qreal kMenuRow = 28.0;
constexpr qreal kChipTextWidth = 150.0;

const QString kListingDevices =
    QStringLiteral("Finding microphones and cameras…");

QString deviceLabel(const QVector<RecordDevice> &devices, const QString &id,
                    const QString &none) {
  if (id.isEmpty())
    return none;
  for (const RecordDevice &device : devices)
    if (device.id == id)
      return device.label;
  return none;
}
} // namespace

void CaptureEditor::setRecordMode(bool record) {
  recordMode_ = record;
  recordMenu_.clear();
  if (record) {
    scrollMode_ = false;
    setRecentsOpen(false);
    if (!recordDevicesListed_ && !recordDevicesWatcher_.isRunning()) {
      connect(&recordDevicesWatcher_,
              &QFutureWatcher<std::pair<RecordDevices, RecordChoices>>::finished,
              this, [this] {
                const auto [devices, choices] = recordDevicesWatcher_.result();
                recordDevices_ = devices;
                recordChoices_ = choices;
                recordDevicesListed_ = true;
                update();
              },
              Qt::SingleShotConnection);
      // Starting Qt Multimedia takes hundreds of milliseconds; the user is
      // picking an area meanwhile.
      recordDevicesWatcher_.setFuture(QtConcurrent::run([] {
        RecordDevices devices = listRecordDevices();
        RecordChoices choices =
            availableChoices(loadRecordChoices(devices.defaultMic), devices);
        return std::pair{devices, choices};
      }));
    }
  }
  setStatus(record ? QStringLiteral("Record · drag an area, click a window, or "
                                    "click empty space for the screen")
                   : QStringLiteral("Screenshot · drag an area, click a window, "
                                    "or click empty space for the screen"));
  updatePointerCursor();
  update();
}

void CaptureEditor::waitForRecordDevices() {
  while (recordDevicesWatcher_.isRunning() || !recordDevicesListed_) {
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    QThread::yieldCurrentThread();
  }
  QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

QVector<QRectF> CaptureEditor::modeSwitchRects() const {
  const QFontMetricsF metrics(chromeFont(12, true));
  const QStringList labels{QStringLiteral("Screenshot"), QStringLiteral("Record")};
  qreal total = 4.0;
  QVector<qreal> widths;
  for (const QString &label : labels) {
    // Record also carries its red dot.
    widths.push_back(metrics.horizontalAdvance(label) +
                     (label == QStringLiteral("Record") ? 44.0 : 30.0));
    total += widths.constLast();
  }
  qreal x = (width() - total) / 2.0 + 2.0;
  QVector<QRectF> rects;
  for (const qreal segment : widths) {
    rects.push_back(QRectF(x, 14.0, segment, 28.0));
    x += segment;
  }
  return rects;
}

void CaptureEditor::paintModeSwitch(QPainter &painter) {
  const QVector<QRectF> segments = modeSwitchRects();
  const ChromeTheme &theme = chromeTheme();
  const QRectF panel = segments.constFirst().united(segments.constLast())
                           .adjusted(-2, -2, 2, 2);
  painter.save();
  painter.setPen(QPen(theme.panelBorder.brush(panel), 1));
  painter.setBrush(theme.surface);
  painter.drawRoundedRect(panel, 10, 10);
  painter.setFont(chromeFont(12, true));
  for (qsizetype index = 0; index < segments.size(); ++index) {
    const bool active = (index == 1) == recordMode_;
    const QRectF segment = segments.at(index);
    if (active) {
      painter.setPen(Qt::NoPen);
      painter.setBrush(theme.accent);
      painter.drawRoundedRect(segment, 8, 8);
    } else if (segment.contains(cursor_)) {
      painter.setPen(Qt::NoPen);
      painter.setBrush(theme.buttonHover);
      painter.drawRoundedRect(segment, 8, 8);
    }
    QRectF text = segment;
    if (index == 1) {
      // A recording dot, red whichever segment is lit.
      painter.setPen(Qt::NoPen);
      painter.setBrush(QColor(0xf2, 0x4b, 0x3f));
      painter.drawEllipse(QPointF(segment.left() + 15, segment.center().y()), 4, 4);
      text.adjust(14, 0, 0, 0);
    }
    painter.setPen(active ? theme.accentText : theme.foreground);
    painter.drawText(text, Qt::AlignCenter,
                     index == 0 ? QStringLiteral("Screenshot")
                                : QStringLiteral("Record"));
  }
  painter.restore();
}

QRectF CaptureEditor::recordArea() const {
  return editedMode_ == CaptureMode::Region ? mapWidgetToPreview(selection_)
                                            : selection_;
}

RecordRequest CaptureEditor::recordRequest() const {
  return buildRecordRequest(liveMonitor_, recordArea(), recordChoices_);
}

QVector<CaptureEditor::RecordBarItem> CaptureEditor::recordBarItems() const {
  QVector<RecordBarItem> items;
  if (phase_ != Phase::Record)
    return items;
  const QFontMetricsF metrics(chromeFont(12, true));
  qreal x = 0.0;
  const auto add = [&](const QString &id, const QString &label, qreal width,
                       bool on) {
    items.push_back({QRectF(x, 0, width, kItemHeight), id, label, on});
    x += width + kItemGap;
  };
  const auto chip = [&](const QString &text) {
    return 34.0 + std::min(kChipTextWidth, metrics.horizontalAdvance(text)) + 12.0;
  };
  const QString mic = deviceLabel(recordDevices_.mics, recordChoices_.mic,
                                  QStringLiteral("No microphone"));
  const QString camera = deviceLabel(recordDevices_.cameras,
                                     recordChoices_.camera,
                                     QStringLiteral("No camera"));
  add(QStringLiteral("mic"), mic, chip(mic), !recordChoices_.mic.isEmpty());
  add(QStringLiteral("camera"), camera, chip(camera),
      !recordChoices_.camera.isEmpty());
  add(QStringLiteral("desktop"), QStringLiteral("Desktop audio"),
      chip(QStringLiteral("Desktop audio")), recordChoices_.desktopAudio);
  add(QStringLiteral("record"), QStringLiteral("Record"),
      metrics.horizontalAdvance(QStringLiteral("Record")) + 44.0, true);
  add(QStringLiteral("close"), {}, kItemHeight, false);
  const qreal width = x - kItemGap + 2 * kBarPad;
  // Outside the area: below it, else above it, else (a whole screen) along
  // its bottom. The overlay is gone before anything is recorded.
  const QRectF area = editedMode_ == CaptureMode::Region
                          ? selection_
                          : mapPreviewToWidget(selection_);
  const qreal left = std::clamp(area.center().x() - width / 2.0, 12.0,
                                std::max(12.0, this->width() - width - 12.0));
  qreal top = area.bottom() + 12.0;
  if (top + kBarHeight > height() - 56.0) {
    top = area.top() - 12.0 - kBarHeight;
    if (top < 56.0)
      top = std::min(area.bottom(), height() - 56.0) - 12.0 - kBarHeight;
  }
  for (RecordBarItem &item : items)
    item.rect.translate(left + kBarPad, top + (kBarHeight - kItemHeight) / 2.0);
  return items;
}

QRectF CaptureEditor::recordBarRect() const {
  const QVector<RecordBarItem> items = recordBarItems();
  if (items.isEmpty())
    return {};
  const qreal inset = (kBarHeight - kItemHeight) / 2.0;
  return items.constFirst().rect.united(items.constLast().rect)
      .adjusted(-kBarPad, -inset, kBarPad, inset);
}

QVector<CaptureEditor::RecordBarItem> CaptureEditor::recordMenuItems() const {
  QVector<RecordBarItem> items;
  if (recordMenu_.isEmpty())
    return items;
  QRectF anchor;
  for (const RecordBarItem &item : recordBarItems())
    if (item.id == recordMenu_)
      anchor = item.rect;
  const bool camera = recordMenu_ == QStringLiteral("camera");
  const QVector<RecordDevice> &devices =
      camera ? recordDevices_.cameras : recordDevices_.mics;
  const QString chosen = camera ? recordChoices_.camera : recordChoices_.mic;
  QVector<RecordDevice> rows{
      {QString(), camera ? QStringLiteral("No camera")
                         : QStringLiteral("No microphone")}};
  rows += devices;
  const QFontMetricsF metrics(chromeFont(12));
  qreal width = anchor.width();
  for (const RecordDevice &row : rows)
    width = std::max(width, metrics.horizontalAdvance(row.label) + 40.0);
  width = std::min(width, 380.0);
  const qreal height = rows.size() * kMenuRow + 8.0;
  const QRectF bar = recordBarRect();
  // Below the bar, or above it when it sits low on the screen.
  qreal top = bar.bottom() + 6.0;
  if (top + height > this->height() - 8.0)
    top = bar.top() - 6.0 - height;
  const qreal left = std::clamp(anchor.left(), 8.0,
                                std::max(8.0, this->width() - width - 8.0));
  for (qsizetype index = 0; index < rows.size(); ++index)
    items.push_back({QRectF(left + 4, top + 4 + index * kMenuRow, width - 8,
                            kMenuRow),
                     recordMenu_ + QLatin1Char(':') + rows.at(index).id,
                     rows.at(index).label, rows.at(index).id == chosen});
  return items;
}

QPoint CaptureEditor::recordBarItemCenterForTest(const QString &id) const {
  for (const RecordBarItem &item : recordBarItems() + recordMenuItems())
    if (item.id == id)
      return item.rect.center().toPoint();
  return {-1, -1};
}

void CaptureEditor::enterRecordSetup() {
  phase_ = Phase::Record;
  dragging_ = false;
  windowMode_ = false;
  smartMode_ = false;
  hoveredWindow_ = -1;
  recordMenu_.clear();
  setStatus(QStringLiteral("Enter records · M microphone · C camera · D desktop "
                           "audio · drag or click to pick again · Esc closes"));
  updatePointerCursor();
  update();
}

bool CaptureEditor::pressRecordBar(const QPointF &point) {
  if (!recordMenu_.isEmpty()) {
    const QVector<RecordBarItem> menu = recordMenuItems();
    for (const RecordBarItem &item : menu) {
      if (!item.rect.contains(point))
        continue;
      const QString id = item.id.section(QLatin1Char(':'), 1);
      if (recordMenu_ == QStringLiteral("camera"))
        recordChoices_.camera = id;
      else
        recordChoices_.mic = id;
      recordMenu_.clear();
      update();
      return true;
    }
    const bool insideMenu =
        !menu.isEmpty() &&
        menu.constFirst().rect.united(menu.constLast().rect)
            .adjusted(-4, -4, 4, 4)
            .contains(point);
    if (insideMenu)
      return true;
    recordMenu_.clear();
    update();
  }
  for (const RecordBarItem &item : recordBarItems()) {
    if (!item.rect.contains(point))
      continue;
    // The listing replaces every device choice: none is made before it.
    const bool device = item.id == QStringLiteral("mic") ||
                        item.id == QStringLiteral("camera") ||
                        item.id == QStringLiteral("desktop");
    if (device && !recordDevicesListed_)
      setStatus(kListingDevices);
    else if (item.id == QStringLiteral("mic") || item.id == QStringLiteral("camera"))
      recordMenu_ = item.id;
    else if (item.id == QStringLiteral("desktop"))
      recordChoices_.desktopAudio = !recordChoices_.desktopAudio;
    else if (item.id == QStringLiteral("record"))
      startRecording();
    else if (item.id == QStringLiteral("close"))
      close();
    update();
    return true;
  }
  return recordBarRect().contains(point);
}

void CaptureEditor::cycleRecordDevice(bool camera) {
  const QVector<RecordDevice> &devices =
      camera ? recordDevices_.cameras : recordDevices_.mics;
  QString &chosen = camera ? recordChoices_.camera : recordChoices_.mic;
  // None, then each device in turn.
  QStringList ids{QString()};
  for (const RecordDevice &device : devices)
    ids.push_back(device.id);
  chosen = ids.at((std::max<qsizetype>(0, ids.indexOf(chosen)) + 1) % ids.size());
  const QString name = deviceLabel(devices, chosen, QStringLiteral("none"));
  setStatus(camera ? QStringLiteral("Camera: %1").arg(name)
                   : QStringLiteral("Microphone: %1").arg(name));
  update();
}

bool CaptureEditor::recordKey(QKeyEvent *event) {
  if (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier |
                            Qt::MetaModifier))
    return false;
  const int key = event->key();
  if ((key == Qt::Key_M || key == Qt::Key_C || key == Qt::Key_D) &&
      !recordDevicesListed_) {
    // The listing replaces every device choice: none is made before it.
    setStatus(kListingDevices);
    return true;
  }
  switch (key) {
  case Qt::Key_Return:
  case Qt::Key_Enter:
    startRecording();
    return true;
  case Qt::Key_Escape:
    if (!recordMenu_.isEmpty()) {
      recordMenu_.clear();
      update();
    } else {
      close();
    }
    return true;
  case Qt::Key_M:
    cycleRecordDevice(false);
    return true;
  case Qt::Key_C:
    cycleRecordDevice(true);
    return true;
  case Qt::Key_D:
    recordChoices_.desktopAudio = !recordChoices_.desktopAudio;
    setStatus(recordChoices_.desktopAudio ? QStringLiteral("Desktop audio on")
                                          : QStringLiteral("Desktop audio off"));
    update();
    return true;
  case Qt::Key_V:
    // Screenshot this area instead.
    recordMode_ = false;
    phase_ = Phase::Select;
    enterSelectedCapture(QStringLiteral("Area selected · Select moves layers · "
                                        "wheel zooms · outer handles crop"));
    update();
    return true;
  default:
    return false;
  }
}

void CaptureEditor::startRecording() {
  if (busy_)
    return;
  if (!recordDevicesListed_) {
    setStatus(kListingDevices);
    return;
  }
  const RecordRequest request = recordRequest();
  if (request.area.width() < 2 || request.area.height() < 2) {
    setStatus(QStringLiteral("Pick an area to record"));
    return;
  }
  busy_ = true;
  // The handoff runs on a worker that the destructor drains, so the overlay
  // leaves the screen now, before the video process starts to record it.
  recordHandoffFuture_ = QtConcurrent::run(
      [request, choices = recordChoices_, handoff = recordHandoff_] {
        saveRecordChoices(choices);
        QString error;
        if (handoff) {
          error = handoff(request);
        } else if (const auto reply = askVideoOwner(
                       {{QStringLiteral("cmd"), QStringLiteral("record")},
                        {QStringLiteral("request"), request.toJson()}})) {
          // A running video process took it, or said why not.
          if (!reply->value(QStringLiteral("ok")).toBool())
            error = reply->value(QStringLiteral("error"))
                        .toString(QStringLiteral("the recorder declined"));
        } else if (!QProcess::startDetached(
                       QCoreApplication::applicationFilePath(),
                       {QStringLiteral("--record-request"),
                        QString::fromUtf8(QJsonDocument(request.toJson())
                                              .toJson(QJsonDocument::Compact))})) {
          error = QStringLiteral("could not start OmaCap's recorder");
        }
        if (!error.isEmpty())
          sendCaptureNotification(
              QStringLiteral("Could not start recording: %1").arg(error));
        return error;
      });
  close();
}

void CaptureEditor::paintRecordBar(QPainter &painter) {
  const QRectF bar = recordBarRect();
  if (bar.isEmpty())
    return;
  const ChromeTheme &theme = chromeTheme();
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setPen(QPen(theme.panelBorder.brush(bar), 1));
  painter.setBrush(theme.surface);
  painter.drawRoundedRect(bar, 11, 11);
  const QFont font = chromeFont(12, true);
  const QFontMetricsF metrics(font);
  painter.setFont(font);
  for (const RecordBarItem &item : recordBarItems()) {
    const bool hovered = item.rect.contains(cursor_);
    const bool record = item.id == QStringLiteral("record");
    const bool open = item.id == recordMenu_;
    painter.setPen(QPen((hovered || open ? theme.buttonHoverBorder
                                         : theme.buttonBorder)
                            .brush(item.rect),
                        1));
    painter.setBrush(record ? theme.accent
                     : item.id == QStringLiteral("desktop") && item.on
                         ? theme.buttonSelected
                     : hovered || open ? theme.buttonHover
                                       : theme.button);
    painter.drawRoundedRect(item.rect, 8, 8);
    const QColor text = record ? theme.accentText
                        : item.on || item.id == QStringLiteral("close")
                            ? theme.buttonText
                            : chromeAlpha(theme.buttonText, 140);
    if (item.id == QStringLiteral("close")) {
      drawToolbarIcon(painter, item.rect, QStringLiteral("close"), {}, text);
      continue;
    }
    if (record) {
      painter.setPen(Qt::NoPen);
      painter.setBrush(QColor(0xf2, 0x4b, 0x3f));
      painter.drawEllipse(QPointF(item.rect.left() + 17, item.rect.center().y()),
                          5, 5);
      painter.setPen(text);
      painter.drawText(item.rect.adjusted(30, 0, -10, 0),
                       Qt::AlignLeft | Qt::AlignVCenter, item.label);
      continue;
    }
    const QString icon = item.id == QStringLiteral("mic")      ? QStringLiteral("mic")
                         : item.id == QStringLiteral("camera") ? QStringLiteral("camera")
                                                               : QStringLiteral("speaker");
    drawToolbarIcon(painter,
                    QRectF(item.rect.left() + 4, item.rect.top(), 30,
                           item.rect.height()),
                    item.on ? icon : icon + QStringLiteral("-off"), {}, text);
    painter.setPen(text);
    painter.drawText(item.rect.adjusted(34, 0, -10, 0),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     metrics.elidedText(item.label, Qt::ElideRight,
                                        kChipTextWidth));
  }
  const QVector<RecordBarItem> menu = recordMenuItems();
  if (!menu.isEmpty()) {
    const QRectF panel = menu.constFirst().rect.united(menu.constLast().rect)
                             .adjusted(-4, -4, 4, 4);
    painter.setPen(QPen(theme.panelBorder.brush(panel), 1));
    painter.setBrush(theme.surface);
    painter.drawRoundedRect(panel, 9, 9);
    painter.setFont(chromeFont(12));
    const QFontMetricsF rowMetrics(chromeFont(12));
    for (const RecordBarItem &item : menu) {
      if (item.on || item.rect.contains(cursor_)) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(item.on ? theme.buttonSelected : theme.buttonHover);
        painter.drawRoundedRect(item.rect, 6, 6);
      }
      painter.setPen(item.on ? theme.accent : Qt::transparent);
      if (item.on)
        painter.drawText(QRectF(item.rect.left() + 8, item.rect.top(), 16,
                                item.rect.height()),
                         Qt::AlignCenter, QStringLiteral("✓"));
      painter.setPen(theme.foreground);
      painter.drawText(item.rect.adjusted(28, 0, -8, 0),
                       Qt::AlignLeft | Qt::AlignVCenter,
                       rowMetrics.elidedText(item.label, Qt::ElideRight,
                                             item.rect.width() - 36));
    }
  }
  painter.restore();
}
