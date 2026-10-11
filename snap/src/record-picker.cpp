/** @fileoverview Record-mode devices and requests. */
#include "record-picker.hpp"

#include <QAudioDevice>
#include <QCameraDevice>
#include <QMediaDevices>

#include <algorithm>
#include <cmath>

RecordDevices listRecordDevices() {
  RecordDevices devices;
  for (const QAudioDevice &device : QMediaDevices::audioInputs()) {
    const QString id = QString::fromUtf8(device.id());
    if (!id.contains(QStringLiteral("monitor")))
      devices.mics.push_back({id, device.description()});
  }
  for (const QCameraDevice &device : QMediaDevices::videoInputs())
    devices.cameras.push_back(
        {QString::fromUtf8(device.id()), device.description()});
  const QString fallback = QString::fromUtf8(QMediaDevices::defaultAudioInput().id());
  if (std::ranges::any_of(devices.mics, [&](const RecordDevice &mic) {
        return mic.id == fallback;
      }))
    devices.defaultMic = fallback;
  return devices;
}

RecordChoices availableChoices(RecordChoices choices,
                               const RecordDevices &devices) {
  const auto offered = [](const QVector<RecordDevice> &list, const QString &id) {
    return std::ranges::any_of(
        list, [&](const RecordDevice &device) { return device.id == id; });
  };
  if (!choices.mic.isEmpty() && !offered(devices.mics, choices.mic))
    choices.mic = devices.defaultMic;
  if (!choices.camera.isEmpty() && !offered(devices.cameras, choices.camera))
    choices.camera.clear();
  return choices;
}

RecordRequest buildRecordRequest(const MonitorInfo &monitor, const QRectF &area,
                                 const RecordChoices &choices) {
  const QRectF bounds(QPointF(), QSizeF(monitor.geometry.size()));
  const QRectF clamped = area.normalized().intersected(bounds);
  RecordRequest request;
  request.monitor = monitor.name;
  request.monitorLogical = monitor.geometry;
  request.scale = monitor.scale;
  request.transform = monitor.transform;
  const QPoint topLeft(qRound(clamped.left()), qRound(clamped.top()));
  const QPoint bottomRight(qRound(clamped.right()), qRound(clamped.bottom()));
  request.area = QRect(topLeft, QSize(bottomRight.x() - topLeft.x(),
                                      bottomRight.y() - topLeft.y()));
  request.mic = choices.mic;
  request.camera = choices.camera;
  request.desktopAudio = choices.desktopAudio;
  return request;
}
