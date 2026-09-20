#include "model.h"
#include <QJsonArray>
#include <algorithm>
#include <cmath>
double Edit::length() const {
    double n = 0;
    for (auto s : spans)
        n += s.end - s.start;
    return n;
}
double Edit::sourceTime(double t) const {
    t = std::max(0., t);
    for (auto s : spans) {
        if (t < s.end - s.start)
            return s.start + t;
        t -= s.end - s.start;
    }
    return spans.isEmpty() ? 0 : spans.last().end;
}
double Edit::editedTime(double t) const {
    double n = 0;
    for (auto s : spans) {
        if (t < s.start)
            return n;
        if (t < s.end)
            return n + t - s.start;
        n += s.end - s.start;
    }
    return n;
}
double Edit::zoomAt(double t, double &x, double &y) const {
    for (auto z : zooms)
        if (t >= z.start && t < z.end) {
            x = z.x;
            y = z.y;
            double ramp = std::min(.55, (z.end - z.start) / 3);
            double p = std::clamp(std::min((t - z.start) / ramp, (z.end - t) / ramp), 0., 1.);
            return 1 + (z.amount - 1) * p * p * p * (p * (p * 6 - 15) + 10);
        }
    return 1;
}
// Cuts are represented in source time. Every track and zoom uses this mapping.
void Edit::remove(double a, double b) {
    a = std::clamp(a, 0., length());
    b = std::clamp(b, a, length());
    if (b - a < .01 || length() - (b - a) < .1)
        return;
    QVector<Span> out;
    double pos = 0;
    for (auto s : spans) {
        double len = s.end - s.start;
        double lo = std::clamp(a - pos, 0., len), hi = std::clamp(b - pos, 0., len);
        if (lo > 0)
            out.append({s.start, s.start + lo});
        if (hi < len)
            out.append({s.start + hi, s.end});
        pos += len;
    }
    spans = out;
}
QJsonObject Edit::json() const {
    QJsonArray ss, zs;
    for (auto s : spans)
        ss.append(QJsonArray{s.start, s.end});
    for (auto z : zooms)
        zs.append(QJsonObject{
            {"start", z.start}, {"end", z.end}, {"x", z.x}, {"y", z.y}, {"amount", z.amount}});
    return {{"duration", duration},
            {"spans", ss},
            {"zooms", zs},
            {"crop", QJsonArray{crop.x(), crop.y(), crop.width(), crop.height()}},
            {"padding", padding},
            {"corners", corners},
            {"shadow", shadow},
            {"cameraSize", cameraSize},
            {"cameraX", cameraX},
            {"cameraY", cameraY},
            {"camera", camera},
            {"mic", mic},
            {"desktop", desktop},
            {"background", background},
            {"color", color}};
}
Edit Edit::fromJson(const QJsonObject &o) {
    Edit e;
    e.duration = std::clamp(o["duration"].toDouble(), 0., 86400.);
    double last = 0;
    for (auto v : o["spans"].toArray()) {
        auto a = v.toArray();
        double s = std::clamp(a[0].toDouble(), last, e.duration),
               t = std::clamp(a[1].toDouble(), s, e.duration);
        if (t > s) {
            e.spans.append({s, t});
            last = t;
        }
    }
    if (e.spans.isEmpty() && e.duration > 0)
        e.spans.append({0, e.duration});
    for (auto v : o["zooms"].toArray()) {
        auto z = v.toObject();
        double s = std::clamp(z["start"].toDouble(), 0., e.duration),
               t = std::clamp(z["end"].toDouble(), s, e.duration);
        if (t - s > .05)
            e.zooms.append({s, t, std::clamp(z["x"].toDouble(.5), 0., 1.),
                            std::clamp(z["y"].toDouble(.5), 0., 1.),
                            std::clamp(z["amount"].toDouble(1.8), 1., 4.)});
    }
    auto c = o["crop"].toArray();
    if (c.size() == 4) {
        double x = std::clamp(c[0].toDouble(), 0., .95), y = std::clamp(c[1].toDouble(), 0., .95);
        e.crop = {x, y, std::clamp(c[2].toDouble(1), .05, 1 - x),
                  std::clamp(c[3].toDouble(1), .05, 1 - y)};
    }
#define NUM(k, lo, hi) e.k = std::clamp(o[#k].toDouble(e.k), lo, hi)
    NUM(padding, 0., .25);
    NUM(corners, 0., .08);
    NUM(shadow, 0., 1.);
    NUM(cameraSize, .08, .4);
    NUM(cameraX, 0., 1.);
    NUM(cameraY, 0., 1.);
#undef NUM
    e.camera = o["camera"].toBool(true);
    e.mic = o["mic"].toBool(true);
    e.desktop = o["desktop"].toBool(true);
    e.background = o["background"].toString();
    e.color = o["color"].toString(e.color);
    return e;
}
