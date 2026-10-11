#pragma once
#include <QJsonObject>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QUuid>
#include <QVector>
struct Span {
    double start = 0, end = 0;
    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
};
struct Zoom {
    double start = 0, end = 0, x = .5, y = .5, amount = 1.8;
    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
};
// A mark drawn over a screenshot, in source-image pixels so it stays on its target when
// the crop changes.
struct Annotation {
    // box, ellipse, arrow, line, text, step, pen, marker or redact.
    QString kind;
    // Two corners or end points; one anchor for text and steps; a path for pen and marker.
    QVector<QPointF> points;
    QString color = "#ff375f";
    // Stroke width, or the text height and step diameter.
    double size = 4;
    // Boxes and ellipses: outline, solid or tint.
    QString fill = "outline";
    QString text;
    // Text on a readability pill.
    bool pill = true;
    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QJsonObject json() const;
    // An empty kind means the input was not a valid annotation.
    static Annotation fromJson(const QJsonObject &o);
};
struct Edit {
    double duration = 0;
    QVector<Span> spans;
    QVector<Zoom> zooms;
    QRectF crop{0, 0, 1, 1};
    double padding = .07, corners = .015, shadow = .45, cameraSize = .18, cameraX = .79,
           cameraY = .75, cameraCorners = .12, cameraShadow = .45;
    QString cameraShape = "rectangle";
    QString windowTransparency = "off";
    double windowOpacity = .96;
    bool camera = true, mic = true, desktop = true;
    QString background, color = "#182638";
    // Screenshot canvas: "auto" fits the cropped shot plus padding; the others are fixed
    // ratios. Videos always use 16:9.
    QString canvas = "auto";
    QVector<Annotation> annotations;
    double length() const;
    double canvasAspect(double sourceAspect) const;
    QSize stillSize(QSize source) const;
    double sourceTime(double edited) const;
    double editedTime(double source) const;
    double zoomAt(double source, double &x, double &y) const;
    void remove(double start, double end);
    bool split(double at);
    QVector<Span> runs() const;
    QJsonObject json() const;
    static Edit fromJson(const QJsonObject &o);
};
