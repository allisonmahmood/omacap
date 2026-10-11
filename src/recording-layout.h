#pragma once
#include "record-request.h"
#include <QByteArray>
#include <QList>
#include <QRect>
#include <QSize>
#include <QString>
#include <optional>

// Where the recording chrome may go. Once a recording starts, nothing OmaCap paints may land
// in the encoded area, so the HUD and outline stay outside a guard around it: beside the area
// on its monitor, else on another monitor, else nowhere. The countdown is centred on the area;
// it is gone before the first kept frame (see Recorder).

struct Monitor {
    QString name;
    QRect logical; // global logical coordinates
};

struct RecordingLayout {
    QRect encoded;            // the encoded area in the monitor's native pixels, even-sized
    QRect guard;              // global logical; contains every encoded pixel, plus a margin
    QRect countdown;          // global logical
    std::optional<QRect> hud; // global logical
    QString hudMonitor;
    QList<QRect> outline; // global logical strips around the guard
};

// The picked area in native pixels, shrunk to even sides and kept inside the monitor.
QRect encodedArea(const RecordRequest &request);

// Places the chrome for `request`. `monitors` lists every monitor, including the recorded one.
RecordingLayout layoutRecording(const RecordRequest &request, const QList<Monitor> &monitors,
                                QSize hudSize, QSize countdownSize);

// The key that runs `omacap --record`, from `hyprctl binds -j`, e.g. "Super + Alt + R".
// Binds made in Lua configs only expose their description, so one naming OmaCap and
// recording counts too. Empty when there is none.
QString recordHotkey(const QByteArray &bindsJson);
