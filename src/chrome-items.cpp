#include "chrome-items.h"
#include "theme.h"
#include "video-icons.h"
#include <QPainter>
#include <QQmlContext>
#include <QQmlEngine>

namespace {
QBrush borderBrush(const ChromeTheme &theme, const QString &role, const QRectF &bounds) {
    if (role == "tooltip")
        return theme.tooltipBorder;
    const ChromeThemeBorder *border = role == "button"           ? &theme.buttonBorder
                                      : role == "buttonHover"    ? &theme.buttonHoverBorder
                                      : role == "buttonSelected" ? &theme.buttonSelectedBorder
                                      : role == "active"         ? &theme.activeBorder
                                      : role == "inactive"       ? &theme.inactiveBorder
                                      : role == "selection"      ? &theme.selectionBorder
                                      : role == "panel"          ? &theme.panelBorder
                                                                 : nullptr;
    return border ? border->brush(bounds) : QBrush(Qt::NoBrush);
}
} // namespace

ChromeBox::ChromeBox(QQuickItem *parent) : QQuickPaintedItem(parent) {
    setAntialiasing(true);
    connect(this, &ChromeBox::changed, this, [this] { update(); });
}

void ChromeBox::componentComplete() {
    QQuickPaintedItem::componentComplete();
    if (auto context = qmlContext(this))
        theme = context->contextProperty("theme").value<Theme *>();
    if (theme)
        connect(theme, &Theme::changed, this, [this] { update(); });
}

void ChromeBox::paint(QPainter *painter) {
    painter->setRenderHint(QPainter::Antialiasing);
    const qreal inset = border.isEmpty() ? 0 : borderWidth / 2;
    const QRectF bounds = boundingRect().adjusted(inset, inset, -inset, -inset);
    const QBrush stroke = theme && !border.isEmpty() ? borderBrush(theme->chrome(), border, bounds)
                                                     : QBrush(Qt::NoBrush);
    painter->setPen(stroke.style() == Qt::NoBrush ? QPen(Qt::NoPen) : QPen(stroke, borderWidth));
    painter->setBrush(fill);
    painter->drawRoundedRect(bounds, radius, radius);
}

ChromeIcon::ChromeIcon(QQuickItem *parent) : QQuickPaintedItem(parent) {
    setAntialiasing(true);
    setImplicitSize(24, 24);
    connect(this, &ChromeIcon::changed, this, [this] { update(); });
}

void ChromeIcon::paint(QPainter *painter) {
    painter->setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    const QPointF center = boundingRect().center();
    painter->translate(center);
    painter->scale(size / 19, size / 19);
    painter->translate(-center);
    drawVideoIcon(*painter, boundingRect(), name, color);
}

void registerChromeTypes() {
    qmlRegisterType<ChromeBox>("OmaCap", 1, 0, "ChromeBox");
    qmlRegisterType<ChromeIcon>("OmaCap", 1, 0, "ChromeIcon");
}
