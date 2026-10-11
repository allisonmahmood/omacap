// Icons are adapted from Lucide (ISC; snap/assets/Lucide-ISC.txt), drawn like Omasnap's.
#include "video-icons.h"
#include "icons.hpp"
#include <QPainter>
#include <QPainterPath>
#include <QTransform>

namespace {
// The bottom half of a microphone: capsule, cradle and stand.
void microphone(QPainter &p) {
    p.drawRoundedRect(QRectF(9, 2, 6, 13), 3, 3);
    QPainterPath cradle;
    cradle.moveTo(19, 10);
    cradle.lineTo(19, 12);
    cradle.arcTo(QRectF(5, 5, 14, 14), 0, -180);
    cradle.lineTo(5, 10);
    p.drawPath(cradle);
    p.drawLine(QPointF(12, 19), QPointF(12, 22));
}

void speaker(QPainter &p) {
    p.drawPolygon(QPolygonF{{11, 4.7},
                            {9.8, 4.2},
                            {6.4, 7.6},
                            {5.4, 8},
                            {3, 8},
                            {2, 9},
                            {2, 15},
                            {3, 16},
                            {5.4, 16},
                            {6.4, 16.4},
                            {9.8, 19.8},
                            {11, 19.3}});
}

// A box with a 2-unit rounded bottom, open at the top: trays and bins.
QPainterPath openBox(qreal left, qreal top, qreal right, qreal bottom) {
    QPainterPath path;
    path.moveTo(right, top);
    path.lineTo(right, bottom - 2);
    path.arcTo(QRectF(right - 4, bottom - 4, 4, 4), 0, -90);
    path.lineTo(left + 2, bottom);
    path.arcTo(QRectF(left, bottom - 4, 4, 4), 270, -90);
    path.lineTo(left, top);
    return path;
}

// Draws `name` on Lucide's 24-unit grid. False when the icon is Omasnap's.
bool drawLucide(QPainter &p, const QString &name, const QColor &color) {
    if (name == "play" || name == "pause") {
        p.setBrush(color);
        if (name == "play")
            p.drawPolygon(QPolygonF{{6.5, 4}, {19.5, 12}, {6.5, 20}});
        else {
            p.drawRoundedRect(QRectF(6, 4, 4, 16), 1, 1);
            p.drawRoundedRect(QRectF(14, 4, 4, 16), 1, 1);
        }
    } else if (name == "frame") {
        p.drawRoundedRect(QRectF(2.5, 2.5, 19, 19), 4, 4);
        p.drawRoundedRect(QRectF(7, 7, 10, 10), 1.5, 1.5);
    } else if (name == "glass") {
        p.drawEllipse(QPointF(9, 9), 6.5, 6.5);
        p.drawEllipse(QPointF(15, 15), 6.5, 6.5);
    } else if (name == "person") {
        p.drawEllipse(QPointF(12, 8), 4.5, 4.5);
        QPainterPath shoulders;
        shoulders.moveTo(20, 21);
        shoulders.arcTo(QRectF(4, 13, 16, 16), 0, 180);
        p.drawPath(shoulders);
    } else if (name == "crop") {
        QPainterPath path;
        path.moveTo(6, 2);
        path.lineTo(6, 16);
        path.arcTo(QRectF(6, 14, 4, 4), 180, 90);
        path.lineTo(22, 18);
        path.moveTo(18, 22);
        path.lineTo(18, 8);
        path.arcTo(QRectF(14, 6, 4, 4), 0, 90);
        path.lineTo(2, 6);
        p.drawPath(path);
    } else if (name == "zoom") {
        p.drawEllipse(QPointF(11, 11), 7.5, 7.5);
        p.drawLine(QPointF(21, 21), QPointF(16.7, 16.7));
        p.drawLine(QPointF(11, 8), QPointF(11, 14));
        p.drawLine(QPointF(8, 11), QPointF(14, 11));
    } else if (name == "scissors") {
        p.drawEllipse(QPointF(6, 6), 3, 3);
        p.drawEllipse(QPointF(6, 18), 3, 3);
        p.drawLine(QPointF(8.12, 8.12), QPointF(12, 12));
        p.drawLine(QPointF(20, 4), QPointF(8.12, 15.88));
        p.drawLine(QPointF(14.8, 14.8), QPointF(20, 20));
    } else if (name == "export") {
        p.drawLine(QPointF(12, 3), QPointF(12, 15));
        p.drawPolyline(QPolygonF{{17, 8}, {12, 3}, {7, 8}});
        p.drawPath(openBox(3, 15, 21, 21));
    } else if (name == "mic" || name == "mic-off") {
        microphone(p);
        if (name == "mic-off")
            p.drawLine(QPointF(2, 2), QPointF(22, 22));
    } else if (name == "speaker" || name == "speaker-off") {
        speaker(p);
        if (name == "speaker") {
            QPainterPath waves;
            waves.moveTo(16, 9);
            waves.arcTo(QRectF(7, 7, 10, 10), 36.87, -73.74);
            waves.moveTo(19.4, 18.4);
            waves.arcTo(QRectF(4.07, 3, 18, 18), -45.3, 90.6);
            p.drawPath(waves);
        } else {
            p.drawLine(QPointF(22, 9), QPointF(16, 15));
            p.drawLine(QPointF(16, 9), QPointF(22, 15));
        }
    } else if (name == "plus") {
        p.drawLine(QPointF(12, 5), QPointF(12, 19));
        p.drawLine(QPointF(5, 12), QPointF(19, 12));
    } else if (name == "trash") {
        p.drawLine(QPointF(3, 6), QPointF(21, 6));
        p.drawPath(openBox(5, 6, 19, 22));
        QPainterPath lid;
        lid.moveTo(8, 6);
        lid.lineTo(8, 4);
        lid.arcTo(QRectF(8, 2, 4, 4), 180, -90);
        lid.lineTo(14, 2);
        lid.arcTo(QRectF(12, 2, 4, 4), 90, -90);
        lid.lineTo(16, 6);
        p.drawPath(lid);
    } else if (name == "keep") {
        p.drawLine(QPointF(4, 4), QPointF(4, 20));
        p.drawLine(QPointF(20, 4), QPointF(20, 20));
        p.drawLine(QPointF(8, 12), QPointF(16, 12));
    } else if (name == "check") {
        p.drawPolyline(QPolygonF{{20, 6}, {9, 17}, {4, 12}});
    } else if (name == "open") {
        p.drawPolyline(QPolygonF{{15, 3}, {21, 3}, {21, 9}});
        p.drawLine(QPointF(10, 14), QPointF(21, 3));
        QPainterPath frame;
        frame.moveTo(18, 13);
        frame.lineTo(18, 19);
        frame.arcTo(QRectF(14, 17, 4, 4), 0, -90);
        frame.lineTo(5, 21);
        frame.arcTo(QRectF(3, 17, 4, 4), 270, -90);
        frame.lineTo(3, 8);
        frame.arcTo(QRectF(3, 6, 4, 4), 180, -90);
        frame.lineTo(11, 6);
        p.drawPath(frame);
    } else if (name == "window") {
        p.drawRoundedRect(QRectF(2, 4, 20, 16), 2, 2);
        p.drawLine(QPointF(2, 8), QPointF(22, 8));
        p.drawLine(QPointF(6, 4), QPointF(6, 8));
        p.drawLine(QPointF(10, 4), QPointF(10, 8));
    } else if (name.startsWith("corner-")) {
        // Lucide's arrow-up-left, turned to point at the corner it moves the camera to.
        static const QStringList corners{"corner-tl", "corner-tr", "corner-br", "corner-bl"};
        p.translate(12, 12);
        p.rotate(90 * std::max(0, int(corners.indexOf(name))));
        p.translate(-12, -12);
        p.drawPolyline(QPolygonF{{7, 17}, {7, 7}, {17, 7}});
        p.drawLine(QPointF(17, 17), QPointF(7, 7));
    } else {
        return false;
    }
    return true;
}
} // namespace

void drawVideoIcon(QPainter &painter, const QRectF &bounds, const QString &name,
                   const QColor &color) {
    // The same placement, scale and pen as Omasnap's drawToolbarIcon.
    constexpr qreal iconSize = 19, grid = 24;
    painter.save();
    painter.translate(bounds.center().x() - iconSize / 2, bounds.center().y() - iconSize / 2);
    painter.scale(iconSize / grid, iconSize / grid);
    painter.setPen(QPen(color, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    const bool drawn = drawLucide(painter, name, color);
    painter.restore();
    if (!drawn)
        drawToolbarIcon(painter, bounds, name, {}, color);
}
