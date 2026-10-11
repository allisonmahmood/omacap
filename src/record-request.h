#pragma once
#include <QJsonObject>
#include <QRect>
#include <QString>
#include <optional>

// What the capture overlay's record mode hands to the video process: which monitor, which
// part of it, and the audio and camera choices. Coordinates are Hyprland logical pixels.
struct RecordRequest {
    QString monitor;      // Hyprland monitor name, e.g. "DP-3"
    QRect monitorLogical; // the monitor in global logical coordinates
    double scale = 1;     // the monitor's scale; native pixels = logical * scale
    int transform = 0;    // Hyprland transform, 0-7
    QRect area;           // the picked area, logical, relative to the monitor's top-left
    QString mic;          // QAudioDevice id; empty records no microphone
    QString camera;       // QCameraDevice id; empty records no camera
    bool desktopAudio = false;

    // The picked area in the monitor's native pixels, as the screen stream delivers them.
    QRect nativeArea() const;
    // The monitor's size in native pixels.
    QSize nativeSize() const;
    QJsonObject toJson() const;
    static std::optional<RecordRequest> fromJson(const QJsonObject &json);
};
