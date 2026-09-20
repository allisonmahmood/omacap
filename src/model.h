#pragma once
#include <QJsonObject>
#include <QRectF>
#include <QVector>
struct Span {
    double start = 0, end = 0;
};
struct Zoom {
    double start = 0, end = 0, x = .5, y = .5, amount = 1.8;
};
struct Edit {
    double duration = 0;
    QVector<Span> spans;
    QVector<Zoom> zooms;
    QRectF crop{0, 0, 1, 1};
    double padding = .07, corners = .015, shadow = .45, cameraSize = .18, cameraX = .79,
           cameraY = .75;
    bool camera = true, mic = true, desktop = true;
    QString background, color = "#182638";
    double length() const;
    double sourceTime(double edited) const;
    double editedTime(double source) const;
    double zoomAt(double source, double &x, double &y) const;
    void remove(double start, double end);
    QJsonObject json() const;
    static Edit fromJson(const QJsonObject &o);
};
