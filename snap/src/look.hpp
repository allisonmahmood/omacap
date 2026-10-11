/** @fileoverview OmaCap's look for a screenshot (what stands behind it and
 *  how it is framed) and the one layout every painter shares: the editor,
 *  flattened exports, and the canvas spotlights sample. */
#pragma once

#include <QColor>
#include <QImage>
#include <QJsonObject>
#include <QRectF>
#include <QSizeF>
#include <QString>

class QPainter;

/// What fills the canvas around the screenshot. Wallpaper and Image show a
/// stored picture (backdrop-assets.hpp); Solid shows `CaptureLook::color`.
enum class BackgroundStyle {
  Off,
  Wallpaper,
  Slate,
  Aurora,
  Sunset,
  Lagoon,
  Violet,
  Solid,
  Image
};
/// How the canvas makes room for layers that reach past the screenshot.
/// Framed keeps the look's frame and grows it with a little mat beyond such
/// a layer; Overflow drops the frame and grows only as far as the layers;
/// Image clips everything to the screenshot.
enum class CanvasBoundaryMode { Framed, Overflow, Image };
/// The framed canvas's shape. Auto hugs the screenshot plus its padding; the
/// others widen or heighten the canvas, never cropping the screenshot.
enum class CanvasAspect { Auto, Wide, Standard, Square };
/// How see-through the screenshot card is: Light is OmaCap's 96%, Custom
/// uses `CaptureLook::opacity`.
enum class Glass { Opaque, Light, Custom };

struct CaptureLook {
  BackgroundStyle backdrop = BackgroundStyle::Wallpaper;
  /// The Solid backdrop, and what stands in for a missing picture.
  QColor color = QColor(0x18, 0x26, 0x38);
  /// Asset ids of the pictures the Wallpaper and Image backdrops show. The
  /// wallpaper is resolved when a document is created, so later wallpaper
  /// changes never restyle it.
  QString wallpaper = {};
  QString image = {};
  /// Padding on every side and the card's corner radius, as shares of the
  /// framed canvas's width (OmaCap's video look uses the same units).
  qreal padding = 0.07;
  qreal corners = 0.015;
  /// Card shadow strength, 0 (none) to 1.
  qreal shadow = 0.45;
  Glass glass = Glass::Opaque;
  /// Card opacity for Custom glass.
  qreal opacity = 0.85;
  CanvasAspect aspect = CanvasAspect::Auto;

  /// No backdrop: the screenshot alone, as plain Omasnap captures are.
  [[nodiscard]] static CaptureLook plain();
  /// The opacity the card paints at.
  [[nodiscard]] qreal cardOpacity() const;
  /// Asset id of the picture the backdrop shows, or empty.
  [[nodiscard]] QString pictureId() const;
  bool operator==(const CaptureLook &) const = default;
};

/// OmaCap's shadow strength, also what Shift+B turns a shadow back on to.
inline constexpr qreal kDefaultLookShadow = 0.45;
/// Mat kept beyond a layer that grows a Framed canvas, so it never ends flush
/// against the edge.
inline constexpr qreal kFramedLayerMargin = 15.0;
/// A fixed aspect applies while the canvas it gives is no larger than a
/// square with edges this many logical pixels long, or than the Auto canvas
/// if that is larger. Past it (a tall or wide scroll capture, or a layer far
/// beyond the card) the canvas stays Auto: nothing is ever cropped or
/// downscaled, and a 16:9 frame around a 30 000 px stitch would be absurd.
inline constexpr qreal kMaximumAspectEdge = 8192.0;

/// Where everything sits, in annotation space: the screenshot's logical
/// pixels with its top-left at 0,0.
struct CaptureLayout {
  /// The whole output, with integral edges.
  QRectF canvas;
  /// The screenshot.
  QRectF card;
  /// Card corner radius; 0 unless styled.
  qreal radius = 0.0;
  /// The look frames the card: padding, aspect, corners, shadow and glass.
  bool styled = false;
  /// The backdrop paints the canvas.
  bool backdrop = false;
  bool operator==(const CaptureLayout &) const = default;
};

/// The canvas the look alone gives a `source`-sized screenshot, before any
/// layer grows it: padding on every side, then widened or heightened to the
/// look's aspect. Not integral.
[[nodiscard]] QRectF framedCanvas(const QSizeF &source,
                                  const CaptureLook &look);
/// The layout for a screenshot whose layers paint `layers` (null when none
/// paints anything).
[[nodiscard]] CaptureLayout captureLayout(const QSizeF &source,
                                          const QRectF &layers,
                                          CanvasBoundaryMode mode,
                                          const CaptureLook &look);

/// Paints what lies under the annotation layers: the backdrop over `canvas`,
/// the card's shadow outside the card, then `card` (the screenshot with its
/// redactions already applied) in `cardRect`, clipped to its corners at the
/// look's opacity. `canvas` and `cardRect` are the layout's rects on the
/// device and `scale` its pixels per logical pixel.
void paintCaptureBase(QPainter &painter, const CaptureLayout &layout,
                      const CaptureLook &look, const QImage &picture,
                      const QRectF &canvas, const QRectF &cardRect,
                      const QImage &card, const QSizeF &scale);
/// Fills `bounds` with a backdrop. Pictures cover-fit; a missing picture
/// paints `color`.
void paintCaptureBackground(QPainter &painter, const QRectF &bounds,
                            BackgroundStyle style, const QImage &picture = {},
                            const QColor &color = {});

/// Lowercase names used by the op log and the remembered look.
[[nodiscard]] QString backgroundStyleName(BackgroundStyle style);
/// Leaves `style` untouched and returns false for an unknown name.
[[nodiscard]] bool backgroundStyleFromName(const QString &name,
                                           BackgroundStyle &style);
/// What the editor calls a backdrop: "Wallpaper", "Aurora", ….
[[nodiscard]] QString backgroundStyleLabel(BackgroundStyle style);
[[nodiscard]] QString canvasAspectLabel(CanvasAspect aspect);
[[nodiscard]] QJsonObject lookToJson(const CaptureLook &look);
/// Missing or invalid fields keep their defaults; numbers are clamped.
[[nodiscard]] CaptureLook lookFromJson(const QJsonObject &json);
