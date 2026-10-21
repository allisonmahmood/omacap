#include "recording-layout.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <algorithm>
#include <cmath>

namespace {
// Logical pixels kept clear around the encoded area, so neither rounding nor the compositor's
// filtering at fractional scales can spill chrome into it.
constexpr int guardMargin = 2;
constexpr int outlineWidth = 2;
// Space between the outline and the HUD, and the HUD's inset on another monitor.
constexpr int hudGap = 10, hudInset = 24;

// `inner` moved by the least amount that puts it inside `outer`.
QRect clampInside(QRect inner, const QRect &outer) {
    inner.moveLeft(std::clamp(inner.left(), outer.left(), outer.right() - inner.width() + 1));
    inner.moveTop(std::clamp(inner.top(), outer.top(), outer.bottom() - inner.height() + 1));
    return inner;
}
} // namespace

QRect encodedArea(const RecordRequest &request) {
    const QRect native = request.nativeArea().intersected(QRect(QPoint(), request.nativeSize()));
    return {native.topLeft(), QSize(native.width() / 2 * 2, native.height() / 2 * 2)};
}

RecordingLayout layoutRecording(const RecordRequest &request, const QList<Monitor> &monitors,
                                QSize hudSize, QSize countdownSize) {
    RecordingLayout layout;
    layout.encoded = encodedArea(request);
    const QRect monitor = request.monitorLogical;
    const QRect &e = layout.encoded;
    // Every logical pixel touching an encoded pixel, plus the margin.
    const QPoint topLeft(int(std::floor(e.left() / request.scale)),
                         int(std::floor(e.top() / request.scale)));
    const QPoint end(int(std::ceil((e.left() + e.width()) / request.scale)),
                     int(std::ceil((e.top() + e.height()) / request.scale)));
    layout.guard = QRect(monitor.topLeft() + topLeft, monitor.topLeft() + end - QPoint(1, 1))
                       .adjusted(-guardMargin, -guardMargin, guardMargin, guardMargin);
    const QRect &g = layout.guard;

    const QRect area = request.area.translated(monitor.topLeft());
    layout.countdown = clampInside(QRect(QPoint(), countdownSize).translated(
                                       area.center() - QPoint(countdownSize.width() / 2,
                                                              countdownSize.height() / 2)),
                                   monitor);

    const int w = outlineWidth;
    for (const QRect &strip : {QRect(g.left() - w, g.top() - w, g.width() + 2 * w, w),
                               QRect(g.left() - w, g.bottom() + 1, g.width() + 2 * w, w),
                               QRect(g.left() - w, g.top(), w, g.height()),
                               QRect(g.right() + 1, g.top(), w, g.height())})
        if (const QRect visible = strip.intersected(monitor); !visible.isEmpty())
            layout.outline << visible;

    // Below, above, right of or left of the area, clear of the outline; the first that fits.
    const QRect clear = g.adjusted(-w - hudGap, -w - hudGap, w + hudGap, w + hudGap);
    const int centreX = g.center().x() - hudSize.width() / 2,
              centreY = g.center().y() - hudSize.height() / 2;
    const QList<QRect> beside{
        QRect(QPoint(centreX, clear.bottom() + 1), hudSize),
        QRect(QPoint(centreX, clear.top() - hudSize.height()), hudSize),
        QRect(QPoint(clear.right() + 1, centreY), hudSize),
        QRect(QPoint(clear.left() - hudSize.width(), centreY), hudSize),
    };
    for (int side = 0; side < beside.size(); ++side) {
        // Slide along the area's edge to stay on the monitor, never across the area.
        QRect hud = beside[side];
        if (side < 2)
            hud.moveLeft(std::clamp(hud.left(), monitor.left(), monitor.right() - hud.width() + 1));
        else
            hud.moveTop(std::clamp(hud.top(), monitor.top(), monitor.bottom() - hud.height() + 1));
        if (monitor.contains(hud) && !hud.intersects(clear)) {
            layout.hud = hud;
            layout.hudMonitor = request.monitor;
            return layout;
        }
    }
    for (const Monitor &other : monitors)
        if (other.name != request.monitor && other.logical.width() >= hudSize.width() &&
            other.logical.height() >= hudSize.height() + hudInset) {
            const QRect &m = other.logical;
            layout.hud = QRect(QPoint(m.center().x() - hudSize.width() / 2,
                                      m.bottom() + 1 - hudInset - hudSize.height()),
                               hudSize);
            layout.hudMonitor = other.name;
            break;
        }
    return layout;
}

std::optional<QJsonObject> requestedDisplay(const RecordRequest &request,
                                            const QByteArray &monitorsJson) {
    for (const auto &value : QJsonDocument::fromJson(monitorsJson).array()) {
        const QJsonObject monitor = value.toObject();
        if (monitor["name"].toString() != request.monitor)
            continue;
        // Derived as the picker derives the request from the same output.
        const double scale = monitor["scale"].toDouble();
        const int transform = monitor["transform"].toInt();
        if (!(scale > 0))
            return std::nullopt;
        QSize logical(qRound(monitor["width"].toInt() / scale),
                      qRound(monitor["height"].toInt() / scale));
        if (transform % 2)
            logical.transpose();
        if (!qFuzzyCompare(scale, request.scale) || transform != request.transform ||
            QRect(QPoint(monitor["x"].toInt(), monitor["y"].toInt()), logical) !=
                request.monitorLogical)
            return std::nullopt;
        // The worker's Display.FIELDS.
        QJsonObject state;
        for (const char *field :
             {"width", "height", "scale", "transform", "x", "y", "mirrorOf", "disabled"})
            state[field] = monitor[field];
        return state;
    }
    return std::nullopt;
}

QString recordHotkey(const QByteArray &bindsJson) {
    for (const auto &value : QJsonDocument::fromJson(bindsJson).array()) {
        const QJsonObject bind = value.toObject();
        const QString description = bind["description"].toString();
        const bool runs = bind["dispatcher"].toString() == "exec" &&
                          bind["arg"].toString().contains("omacap --record");
        const bool named = description.contains("omacap", Qt::CaseInsensitive) &&
                           description.contains("record", Qt::CaseInsensitive);
        if (!(runs || named) || !bind["submap"].toString().isEmpty())
            continue;
        QStringList keys;
        const int mods = bind["modmask"].toInt();
        for (const auto &[bit, name] : {std::pair{64, "Super"}, std::pair{4, "Ctrl"},
                                        std::pair{8, "Alt"}, std::pair{1, "Shift"}})
            if (mods & bit)
                keys << name;
        QString key = bind["key"].toString();
        if (key.size() == 1)
            key = key.toUpper();
        else if (!key.isEmpty())
            key = key.left(1).toUpper() + key.mid(1).toLower();
        keys << key;
        return keys.join(" + ");
    }
    return {};
}
