/** @fileoverview OmaCap's screenshot look: layout maths and the painter for
 *  everything under the annotation layers. */
#include "look.hpp"

#include <QList>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QRegularExpression>

#include <algorithm>
#include <array>
#include <cmath>

CaptureLook CaptureLook::plain() {
  CaptureLook look;
  look.backdrop = BackgroundStyle::Off;
  return look;
}

qreal CaptureLook::cardOpacity() const {
  switch (glass) {
  case Glass::Opaque:
    return 1.0;
  case Glass::Light:
    return 0.96;
  case Glass::Custom:
    return opacity;
  }
  return 1.0;
}

QString CaptureLook::pictureId() const {
  return backdrop == BackgroundStyle::Wallpaper ? wallpaper
         : backdrop == BackgroundStyle::Image   ? image
                                                : QString();
}

namespace {
struct Frame {
  QRectF rect;
  /// Width over height the frame was shaped to; 0 for Auto, including a
  /// fixed aspect that fell back to it.
  qreal aspect = 0.0;
  /// The Auto frame: the card plus its padding.
  QRectF automatic = rect;
};

/// Whether a fixed aspect may shape the canvas to `shaped` where Auto gives
/// `automatic` (kMaximumAspectEdge).
bool aspectFits(const QRectF &shaped, const QRectF &automatic) {
  return shaped.width() * shaped.height() <=
         std::max(kMaximumAspectEdge * kMaximumAspectEdge,
                  automatic.width() * automatic.height());
}

qreal aspectRatio(CanvasAspect aspect) {
  switch (aspect) {
  case CanvasAspect::Auto:
    return 0.0;
  case CanvasAspect::Wide:
    return 16.0 / 9.0;
  case CanvasAspect::Standard:
    return 4.0 / 3.0;
  case CanvasAspect::Square:
    return 1.0;
  }
  return 0.0;
}

// Padding is a share of the canvas width on every side, and a fixed aspect
// fits the card inside the padded canvas, as OmaCap's video composition does.
// The card keeps its own size; the canvas grows around it.
Frame lookFrame(const QSizeF &source, const CaptureLook &look) {
  const QRectF card(QPointF(), source);
  if (source.isEmpty())
    return {card};
  const qreal padding = std::clamp<qreal>(look.padding, 0.0, 0.25);
  const qreal edge = padding * source.width() / (1.0 - 2.0 * padding);
  const QRectF automatic = card.adjusted(-edge, -edge, edge, edge);
  const qreal aspect = aspectRatio(look.aspect);
  if (aspect <= 0.0)
    return {automatic};
  const qreal ratio = source.width() / source.height();
  const qreal fill = std::min(1.0 - 2.0 * padding,
                              ratio * (1.0 / aspect - 2.0 * padding));
  const qreal width = source.width() / fill;
  const qreal height = width / aspect;
  const QRectF shaped(-(width - source.width()) / 2.0,
                      -(height - source.height()) / 2.0, width, height);
  if (!aspectFits(shaped, automatic))
    return {automatic};
  return {shaped, aspect, automatic};
}

/// Grows `rect` about its center along the short axis until it is `aspect`.
QRectF widenedToAspect(const QRectF &rect, qreal aspect) {
  if (rect.width() < rect.height() * aspect) {
    const qreal width = rect.height() * aspect;
    return rect.adjusted(-(width - rect.width()) / 2.0, 0,
                         (width - rect.width()) / 2.0, 0);
  }
  const qreal height = rect.width() / aspect;
  return rect.adjusted(0, -(height - rect.height()) / 2.0, 0,
                       (height - rect.height()) / 2.0);
}

QRectF integral(const QRectF &rect) {
  const qreal left = std::floor(rect.left());
  const qreal top = std::floor(rect.top());
  return {left, top, std::ceil(rect.right()) - left,
          std::ceil(rect.bottom()) - top};
}

/// Omasnap's soft ambient-plus-key shadow around `card`. Its default
/// strength paints exactly Omasnap's; the caller clips it to outside the card
/// so a see-through card never shows it.
void paintCardShadow(QPainter &painter, const QRectF &card, qreal strength,
                     const QSizeF &scale) {
  const qreal factor = std::max(scale.width(), scale.height());
  const QColor layer =
      QColor::fromRgbF(0, 0, 0,
                       std::clamp<qreal>(strength, 0.0, 1.0) *
                           (6.0 / 255.0) / kDefaultLookShadow);
  painter.save();
  painter.setPen(Qt::NoPen);
  painter.setBrush(layer);
  // A 40 px key shadow 14 px down and a 12 px ambient halo, built from faint
  // rings so stacking never darkens the card's own edge.
  for (int ring = 14; ring > 0; --ring) {
    const qreal spread = ring * factor * (40.0 / 14.0);
    painter.drawRoundedRect(card.adjusted(-spread, -spread + 14 * scale.height(),
                                          spread, spread + 14 * scale.height()),
                            spread, spread);
  }
  for (int ring = 8; ring > 0; --ring) {
    const qreal spread = ring * factor * (12.0 / 8.0);
    painter.drawRoundedRect(card.adjusted(-spread, -spread, spread, spread),
                            spread, spread);
  }
  painter.restore();
}

/// `picture` reduced by halves until it is no more than twice `needed`, so a
/// cover-fit draw never minifies by more than 2× (bilinear sampling aliases
/// past that). The last few levels are cached per thread: every editor
/// repaint draws the canvas and the toolbar's swatch, at different levels.
QImage pictureLevel(const QImage &picture, const QSizeF &needed) {
  if (needed.isEmpty())
    return picture;
  const qreal ratio = std::min(picture.width() / needed.width(),
                               picture.height() / needed.height());
  const int level = ratio >= 2.0 ? static_cast<int>(std::floor(std::log2(ratio)))
                                 : 0;
  if (level == 0)
    return picture;
  const QSize size(std::max(1, picture.width() >> level),
                   std::max(1, picture.height() >> level));
  struct Level {
    qint64 key;
    int level;
    QImage image;
  };
  thread_local QList<Level> cached;
  for (qsizetype index = 0; index < cached.size(); ++index) {
    if (cached.at(index).key == picture.cacheKey() &&
        cached.at(index).level == level) {
      cached.move(index, 0);
      return cached.constFirst().image;
    }
  }
  cached.prepend({picture.cacheKey(), level,
                  picture.scaled(size, Qt::IgnoreAspectRatio,
                                 Qt::SmoothTransformation)});
  if (cached.size() > 4)
    cached.removeLast();
  return cached.constFirst().image;
}
} // namespace

QRectF framedCanvas(const QSizeF &source, const CaptureLook &look) {
  return lookFrame(source, look).rect;
}

CaptureLayout captureLayout(const QSizeF &source, const QRectF &layers,
                            CanvasBoundaryMode mode, const CaptureLook &look) {
  CaptureLayout layout;
  layout.card = QRectF(QPointF(), source);
  layout.canvas = layout.card;
  if (source.isEmpty() || mode == CanvasBoundaryMode::Image)
    return layout;
  const QRectF reach = layers.isNull() ? layout.card
                                       : layout.card.united(layers);
  const bool grows = reach.left() < 0.0 || reach.top() < 0.0 ||
                     reach.right() > source.width() ||
                     reach.bottom() > source.height();
  if (mode == CanvasBoundaryMode::Overflow) {
    if (grows)
      layout.canvas = integral(reach);
    layout.backdrop = grows && look.backdrop != BackgroundStyle::Off;
    return layout;
  }
  layout.styled = look.backdrop != BackgroundStyle::Off;
  layout.backdrop = layout.styled;
  const Frame frame =
      layout.styled ? lookFrame(source, look) : Frame{layout.card};
  QRectF canvas = frame.rect;
  if (grows) {
    const QRectF reached = layers.adjusted(-kFramedLayerMargin,
                                           -kFramedLayerMargin,
                                           kFramedLayerMargin,
                                           kFramedLayerMargin);
    canvas = canvas.united(reached);
    if (frame.aspect > 0.0) {
      // The same limit as for the card alone: a layer dragged far off would
      // otherwise widen 16:9 around it to billions of pixels.
      const QRectF automatic = frame.automatic.united(reached);
      const QRectF widened = widenedToAspect(canvas, frame.aspect);
      canvas = aspectFits(widened, automatic) ? widened : automatic;
    }
  }
  // A bare card keeps its exact, possibly fractional, size.
  if (grows || layout.styled)
    layout.canvas = integral(canvas);
  if (layout.styled)
    layout.radius = std::min(std::max<qreal>(0.0, look.corners) *
                                 frame.rect.width(),
                             std::min(source.width(), source.height()) / 2.0);
  return layout;
}

void paintCaptureBase(QPainter &painter, const CaptureLayout &layout,
                      const CaptureLook &look, const QImage &picture,
                      const QRectF &canvas, const QRectF &cardRect,
                      const QImage &card, const QSizeF &scale) {
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
  if (layout.backdrop) {
    painter.save();
    painter.setClipRect(canvas, Qt::IntersectClip);
    paintCaptureBackground(painter, canvas, look.backdrop, picture, look.color);
    painter.restore();
  }
  QPainterPath cardPath;
  cardPath.addRoundedRect(cardRect, layout.radius * scale.width(),
                          layout.radius * scale.height());
  if (layout.styled && look.shadow > 0.0) {
    // Hollow: a see-through card must show the backdrop, not its own shadow.
    QPainterPath outside;
    outside.addRect(canvas);
    painter.save();
    painter.setClipPath(outside.subtracted(cardPath), Qt::IntersectClip);
    paintCardShadow(painter, cardRect, look.shadow, scale);
    painter.restore();
  }
  if (!card.isNull()) {
    if (layout.styled) {
      if (layout.radius > 0.0)
        painter.setClipPath(cardPath, Qt::IntersectClip);
      painter.setOpacity(look.cardOpacity());
    }
    painter.drawImage(cardRect, card);
  }
  painter.restore();
}

void paintCaptureBackground(QPainter &painter, const QRectF &bounds,
                            BackgroundStyle style, const QImage &picture,
                            const QColor &color) {
  switch (style) {
  case BackgroundStyle::Off:
    return;
  case BackgroundStyle::Solid:
    painter.fillRect(bounds, color);
    return;
  case BackgroundStyle::Slate:
    // Omasnap's window gray, an opaque mat.
    painter.fillRect(bounds, QColor(QStringLiteral("#242424")));
    return;
  case BackgroundStyle::Wallpaper:
  case BackgroundStyle::Image: {
    if (picture.isNull()) {
      painter.fillRect(bounds, color);
      return;
    }
    // Cover-fit: fill `bounds` and center-crop the overhang, the way a
    // wallpaper covers a screen of another aspect.
    const QSizeF size(picture.size());
    const qreal scale =
        std::max(bounds.width() / size.width(), bounds.height() / size.height());
    const QSizeF covered = size * scale;
    const QRectF target(bounds.center() - QPointF(covered.width(),
                                                  covered.height()) / 2.0,
                        covered);
    const qreal device = std::max<qreal>(
        1.0, std::hypot(painter.deviceTransform().m11(),
                        painter.deviceTransform().m12()));
    painter.drawImage(target, pictureLevel(picture, covered * device));
    return;
  }
  case BackgroundStyle::Aurora:
  case BackgroundStyle::Sunset:
  case BackgroundStyle::Lagoon:
  case BackgroundStyle::Violet:
    break;
  }

  struct Blob {
    QPointF center;
    qreal radius;
    QColor color;
  };
  QColor base;
  std::array<Blob, 4> blobs;
  if (style == BackgroundStyle::Aurora) {
    base = QColor(QStringLiteral("#101827"));
    blobs = {Blob{QPointF(bounds.left() + bounds.width() * 0.15,
                          bounds.top() + bounds.height() * 0.18),
                  bounds.width() * 0.75, QColor(QStringLiteral("#2dd4bf"))},
             Blob{bounds.topRight(), bounds.width() * 0.78,
                  QColor(QStringLiteral("#7c3aed"))},
             Blob{QPointF(bounds.center().x(), bounds.bottom()),
                  bounds.width() * 0.72, QColor(QStringLiteral("#2563eb"))},
             Blob{bounds.bottomLeft(), bounds.width() * 0.55,
                  QColor(QStringLiteral("#0f766e"))}};
  } else if (style == BackgroundStyle::Sunset) {
    base = QColor(QStringLiteral("#251328"));
    blobs = {Blob{bounds.topLeft(), bounds.width() * 0.82,
                  QColor(QStringLiteral("#f97316"))},
             Blob{QPointF(bounds.right(), bounds.top() + bounds.height() * 0.2),
                  bounds.width() * 0.7, QColor(QStringLiteral("#ec4899"))},
             Blob{QPointF(bounds.center().x(), bounds.bottom()),
                  bounds.width() * 0.75, QColor(QStringLiteral("#7c3aed"))},
             Blob{bounds.bottomLeft(), bounds.width() * 0.5,
                  QColor(QStringLiteral("#ef4444"))}};
  } else if (style == BackgroundStyle::Lagoon) {
    base = QColor(QStringLiteral("#071c2a"));
    blobs = {
        Blob{QPointF(bounds.left() + bounds.width() * 0.12, bounds.top()),
             bounds.width() * 0.68, QColor(QStringLiteral("#06b6d4"))},
        Blob{QPointF(bounds.right(), bounds.top() + bounds.height() * 0.25),
             bounds.width() * 0.75, QColor(QStringLiteral("#1d4ed8"))},
        Blob{QPointF(bounds.center().x(), bounds.bottom()),
             bounds.width() * 0.75, QColor(QStringLiteral("#0f766e"))},
        Blob{bounds.bottomLeft(), bounds.width() * 0.5,
             QColor(QStringLiteral("#22d3ee"))}};
  } else {
    base = QColor(QStringLiteral("#171225"));
    blobs = {
        Blob{QPointF(bounds.left(), bounds.top() + bounds.height() * 0.15),
             bounds.width() * 0.7, QColor(QStringLiteral("#a855f7"))},
        Blob{bounds.topRight(), bounds.width() * 0.72,
             QColor(QStringLiteral("#4f46e5"))},
        Blob{bounds.bottomRight(), bounds.width() * 0.65,
             QColor(QStringLiteral("#db2777"))},
        Blob{QPointF(bounds.left() + bounds.width() * 0.25, bounds.bottom()),
             bounds.width() * 0.62, QColor(QStringLiteral("#4338ca"))}};
  }

  painter.fillRect(bounds, base);
  for (const Blob &blob : blobs) {
    QRadialGradient gradient(blob.center, blob.radius);
    QColor center = blob.color;
    center.setAlpha(220);
    QColor edge = blob.color;
    edge.setAlpha(0);
    gradient.setColorAt(0, center);
    gradient.setColorAt(1, edge);
    painter.fillRect(bounds, gradient);
  }
}

namespace {
struct StyleName {
  BackgroundStyle style;
  const char *name;
  const char *label;
};
constexpr std::array<StyleName, 9> kStyleNames{{
    {BackgroundStyle::Off, "off", "Off"},
    {BackgroundStyle::Wallpaper, "wallpaper", "Wallpaper"},
    {BackgroundStyle::Slate, "slate", "Slate"},
    {BackgroundStyle::Aurora, "aurora", "Aurora"},
    {BackgroundStyle::Sunset, "sunset", "Sunset"},
    {BackgroundStyle::Lagoon, "lagoon", "Lagoon"},
    {BackgroundStyle::Violet, "violet", "Violet"},
    {BackgroundStyle::Solid, "solid", "Colour"},
    {BackgroundStyle::Image, "image", "Image"},
}};
constexpr std::array<const char *, 4> kAspectNames{"auto", "16:9", "4:3",
                                                   "1:1"};
constexpr std::array<const char *, 3> kGlassNames{"opaque", "light", "custom"};

/// Asset ids become file names, so only a content hash is accepted.
QString assetId(const QJsonValue &value) {
  static const QRegularExpression pattern(QStringLiteral("^[0-9a-f]{16,64}$"));
  const QString id = value.toString();
  return pattern.match(id).hasMatch() ? id : QString();
}
} // namespace

QString backgroundStyleName(BackgroundStyle style) {
  for (const StyleName &entry : kStyleNames)
    if (entry.style == style)
      return QString::fromLatin1(entry.name);
  return QStringLiteral("off");
}

bool backgroundStyleFromName(const QString &name, BackgroundStyle &style) {
  for (const StyleName &entry : kStyleNames) {
    if (name == QLatin1StringView(entry.name)) {
      style = entry.style;
      return true;
    }
  }
  return false;
}

QString backgroundStyleLabel(BackgroundStyle style) {
  for (const StyleName &entry : kStyleNames)
    if (entry.style == style)
      return QString::fromLatin1(entry.label);
  return {};
}

QString canvasAspectLabel(CanvasAspect aspect) {
  return aspect == CanvasAspect::Auto
             ? QStringLiteral("Auto")
             : QString::fromLatin1(kAspectNames.at(static_cast<size_t>(aspect)));
}

QJsonObject lookToJson(const CaptureLook &look) {
  return {{QStringLiteral("backdrop"), backgroundStyleName(look.backdrop)},
          {QStringLiteral("color"), look.color.name()},
          {QStringLiteral("wallpaper"), look.wallpaper},
          {QStringLiteral("image"), look.image},
          {QStringLiteral("padding"), look.padding},
          {QStringLiteral("corners"), look.corners},
          {QStringLiteral("shadow"), look.shadow},
          {QStringLiteral("glass"),
           QString::fromLatin1(kGlassNames.at(static_cast<size_t>(look.glass)))},
          {QStringLiteral("opacity"), look.opacity},
          {QStringLiteral("aspect"),
           QString::fromLatin1(
               kAspectNames.at(static_cast<size_t>(look.aspect)))}};
}

CaptureLook lookFromJson(const QJsonObject &json) {
  CaptureLook look;
  BackgroundStyle backdrop;
  if (backgroundStyleFromName(json.value(QStringLiteral("backdrop")).toString(),
                              backdrop))
    look.backdrop = backdrop;
  const QColor color(json.value(QStringLiteral("color")).toString());
  if (color.isValid())
    look.color = color;
  look.wallpaper = assetId(json.value(QStringLiteral("wallpaper")));
  look.image = assetId(json.value(QStringLiteral("image")));
  const auto number = [&](const char *key, qreal fallback, qreal low,
                          qreal high) {
    const double value =
        json.value(QLatin1StringView(key)).toDouble(fallback);
    return std::isfinite(value) ? std::clamp<qreal>(value, low, high)
                                : fallback;
  };
  look.padding = number("padding", look.padding, 0.0, 0.25);
  look.corners = number("corners", look.corners, 0.0, 0.08);
  look.shadow = number("shadow", look.shadow, 0.0, 1.0);
  look.opacity = number("opacity", look.opacity, 0.0, 1.0);
  const QString glass = json.value(QStringLiteral("glass")).toString();
  for (size_t index = 0; index < kGlassNames.size(); ++index)
    if (glass == QLatin1StringView(kGlassNames.at(index)))
      look.glass = static_cast<Glass>(index);
  const QString aspect = json.value(QStringLiteral("aspect")).toString();
  for (size_t index = 0; index < kAspectNames.size(); ++index)
    if (aspect == QLatin1StringView(kAspectNames.at(index)))
      look.aspect = static_cast<CanvasAspect>(index);
  return look;
}
