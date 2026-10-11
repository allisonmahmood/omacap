/** @fileoverview What the picker's record mode hands the video process: the
 *  devices it offers and the RecordRequest (src/record-request.h) built from
 *  a picked area. */
#pragma once

#include "capture-state.hpp"
#include "capture.hpp"
#include "record-request.h"

#include <QRectF>
#include <QString>
#include <QVector>

struct RecordDevice {
  /// QAudioDevice/QCameraDevice id as UTF-8 text.
  QString id;
  QString label;
};

struct RecordDevices {
  QVector<RecordDevice> mics;
  QVector<RecordDevice> cameras;
  /// The system's default microphone, or empty.
  QString defaultMic;
};

/// Microphones (never a "monitor" source: that is desktop audio) and cameras.
/// Starts Qt Multimedia, which takes hundreds of milliseconds: worker only.
[[nodiscard]] RecordDevices listRecordDevices();
/// `choices` with any device that is no longer there replaced: a missing
/// microphone by the default one, a missing camera by none.
[[nodiscard]] RecordChoices availableChoices(RecordChoices choices,
                                             const RecordDevices &devices);
/// The request to record `area` (logical pixels relative to the monitor's
/// top-left, clamped to it, edges rounded to whole pixels) of `monitor`.
[[nodiscard]] RecordRequest buildRecordRequest(const MonitorInfo &monitor,
                                               const QRectF &area,
                                               const RecordChoices &choices);
