#include "record-request.h"
#include <cmath>

QRect RecordRequest::nativeArea() const {
    const auto edge = [this](int logical) { return int(std::lround(logical * scale)); };
    return QRect(QPoint(edge(area.left()), edge(area.top())),
                 QPoint(edge(area.left() + area.width()), edge(area.top() + area.height())) -
                     QPoint(1, 1));
}

QSize RecordRequest::nativeSize() const {
    return {int(std::lround(monitorLogical.width() * scale)),
            int(std::lround(monitorLogical.height() * scale))};
}

QString RecordRequest::unsupported() const {
    // The stream is in the panel's orientation; cropping it is not supported yet.
    if (transform != 0)
        return "OmaCap can't record rotated displays yet.";
    return {};
}

QJsonObject RecordRequest::toJson() const {
    return {{"monitor", monitor},
            {"monitorX", monitorLogical.x()},
            {"monitorY", monitorLogical.y()},
            {"monitorWidth", monitorLogical.width()},
            {"monitorHeight", monitorLogical.height()},
            {"scale", scale},
            {"transform", transform},
            {"areaX", area.x()},
            {"areaY", area.y()},
            {"areaWidth", area.width()},
            {"areaHeight", area.height()},
            {"mic", mic},
            {"camera", camera},
            {"desktopAudio", desktopAudio}};
}

std::optional<RecordRequest> RecordRequest::fromJson(const QJsonObject &json) {
    RecordRequest request;
    request.monitor = json["monitor"].toString();
    request.monitorLogical = QRect(json["monitorX"].toInt(), json["monitorY"].toInt(),
                                   json["monitorWidth"].toInt(), json["monitorHeight"].toInt());
    request.scale = json["scale"].toDouble(1);
    request.transform = json["transform"].toInt();
    request.area = QRect(json["areaX"].toInt(), json["areaY"].toInt(), json["areaWidth"].toInt(),
                         json["areaHeight"].toInt());
    request.mic = json["mic"].toString();
    request.camera = json["camera"].toString();
    request.desktopAudio = json["desktopAudio"].toBool();
    const QRect monitorArea(QPoint(), request.monitorLogical.size());
    if (request.monitor.isEmpty() || !(request.scale > 0) || request.area.isEmpty() ||
        !monitorArea.contains(request.area))
        return std::nullopt;
    return request;
}
