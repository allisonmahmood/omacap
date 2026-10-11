/** @fileoverview The look popovers under the editor toolbar: Backdrop
 *  swatches, Frame (padding, corners, shadow, canvas) and Glass, painted in
 *  the editor's own chrome. */
#include "editor.hpp"

#include "chrome-theme.hpp"
#include "icons.hpp"
#include "overlay-chrome.hpp"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace {
// Metrics follow the palette popover: 9 px corners, 4 px insets, 28 px
// controls.
constexpr qreal kPad = 10.0;
constexpr qreal kTitle = 22.0;
constexpr qreal kRow = 28.0;
constexpr qreal kSeparator = 9.0;
constexpr qreal kSwatch = 28.0;
constexpr qreal kSwatchGap = 4.0;
constexpr qreal kLabelWidth = 62.0;
constexpr qreal kValueWidth = 44.0;
constexpr qreal kWidth = 2 * kPad + 9 * kSwatch + 8 * kSwatchGap;

struct Swatch {
  const char *id;
  BackgroundStyle style;
  const char *label;
};
constexpr std::array<Swatch, 9> kSwatches{{
    {"wallpaper", BackgroundStyle::Wallpaper, "Wallpaper"},
    {"slate", BackgroundStyle::Slate, "Slate"},
    {"aurora", BackgroundStyle::Aurora, "Aurora"},
    {"sunset", BackgroundStyle::Sunset, "Sunset"},
    {"lagoon", BackgroundStyle::Lagoon, "Lagoon"},
    {"violet", BackgroundStyle::Violet, "Violet"},
    {"colour", BackgroundStyle::Solid, "Colour…"},
    {"image", BackgroundStyle::Image, "Image…"},
    {"off", BackgroundStyle::Off, "Off"},
}};

/// A slider's range over one look value.
struct Slider {
  const char *id;
  const char *label;
  qreal CaptureLook::*value;
  qreal low;
  qreal high;
};
constexpr std::array<Slider, 4> kSliders{{
    {"padding", "Padding", &CaptureLook::padding, 0.0, 0.25},
    {"corners", "Corners", &CaptureLook::corners, 0.0, 0.08},
    {"shadow", "Shadow", &CaptureLook::shadow, 0.0, 1.0},
    {"opacity", "Opacity", &CaptureLook::opacity, 0.1, 1.0},
}};

const Slider *sliderFor(const QString &id) {
  for (const Slider &slider : kSliders)
    if (id == QLatin1StringView(slider.id))
      return &slider;
  return nullptr;
}

/// The track inside a slider's row rect, where the knob travels.
QRectF sliderTrack(const QRectF &row) {
  return {row.left() + kLabelWidth + 7.0, row.center().y() - 2.0,
          row.width() - kLabelWidth - kValueWidth - 14.0, 4.0};
}
} // namespace

QRectF CaptureEditor::lookPopoverRect() const {
  QString anchorAction;
  qreal height = 0.0;
  switch (lookPopover_) {
  case LookPopover::None:
    return {};
  case LookPopover::Backdrop:
    anchorAction = QStringLiteral("look-backdrop");
    height = kPad + kTitle + kSwatch + kPad;
    break;
  case LookPopover::Frame:
    anchorAction = QStringLiteral("look-frame");
    height = kPad + kTitle + 3 * kRow + kSeparator + kRow + kPad;
    break;
  case LookPopover::Glass:
    anchorAction = QStringLiteral("look-glass");
    height = kPad + kTitle + 2 * kRow + kPad;
    break;
  }
  const QRectF anchor = toolbarButtonRect(anchorAction);
  const qreal x = std::clamp(anchor.center().x() - kWidth / 2.0, 8.0,
                             std::max(8.0, width() - kWidth - 8.0));
  return {x, anchor.bottom() + 4.0, kWidth, height};
}

QVector<CaptureEditor::LookControl> CaptureEditor::lookControls() const {
  QVector<LookControl> controls;
  const QRectF panel = lookPopoverRect();
  if (panel.isEmpty())
    return controls;
  const qreal left = panel.left() + kPad;
  const qreal inner = panel.width() - 2 * kPad;
  qreal y = panel.top() + kPad + kTitle;
  const auto segments = [&](const QString &label,
                            const QVector<QPair<QString, QString>> &items,
                            const QString &selected) {
    controls.push_back({LookControl::Kind::Label,
                        QRectF(left, y, kLabelWidth, kRow), {}, label, false});
    const qreal start = left + kLabelWidth;
    const qreal each = (inner - kLabelWidth) / items.size();
    for (qsizetype index = 0; index < items.size(); ++index)
      controls.push_back({LookControl::Kind::Segment,
                          QRectF(start + index * each, y + 2, each, kRow - 4),
                          items.at(index).first, items.at(index).second,
                          items.at(index).first == selected});
    y += kRow;
  };
  const auto slider = [&](const QString &id) {
    controls.push_back({LookControl::Kind::Slider, QRectF(left, y, inner, kRow),
                        id, QString::fromLatin1(sliderFor(id)->label), false});
    y += kRow;
  };
  switch (lookPopover_) {
  case LookPopover::None:
    break;
  case LookPopover::Backdrop:
    for (size_t index = 0; index < kSwatches.size(); ++index)
      controls.push_back(
          {LookControl::Kind::Swatch,
           QRectF(left + index * (kSwatch + kSwatchGap), y, kSwatch, kSwatch),
           QString::fromLatin1(kSwatches.at(index).id),
           QString::fromUtf8(kSwatches.at(index).label),
           kSwatches.at(index).style == look_.backdrop});
    break;
  case LookPopover::Frame: {
    slider(QStringLiteral("padding"));
    slider(QStringLiteral("corners"));
    slider(QStringLiteral("shadow"));
    y += kSeparator;
    const QString aspect = look_.aspect == CanvasAspect::Wide ? QStringLiteral("aspect-wide")
                           : look_.aspect == CanvasAspect::Standard
                               ? QStringLiteral("aspect-standard")
                           : look_.aspect == CanvasAspect::Square
                               ? QStringLiteral("aspect-square")
                               : QStringLiteral("aspect-auto");
    segments(QStringLiteral("Canvas"),
             {{QStringLiteral("aspect-auto"), QStringLiteral("Auto")},
              {QStringLiteral("aspect-wide"), QStringLiteral("16:9")},
              {QStringLiteral("aspect-standard"), QStringLiteral("4:3")},
              {QStringLiteral("aspect-square"), QStringLiteral("1:1")}},
             aspect);
    break;
  }
  case LookPopover::Glass: {
    const QString glass = look_.glass == Glass::Light ? QStringLiteral("glass-light")
                          : look_.glass == Glass::Custom
                              ? QStringLiteral("glass-custom")
                              : QStringLiteral("glass-opaque");
    segments(QStringLiteral("Window"),
             {{QStringLiteral("glass-opaque"), QStringLiteral("Opaque")},
              {QStringLiteral("glass-light"), QStringLiteral("Light")},
              {QStringLiteral("glass-custom"), QStringLiteral("Custom")}},
             glass);
    slider(QStringLiteral("opacity"));
    break;
  }
  }
  return controls;
}

QPoint CaptureEditor::lookControlCenterForTest(const QString &id) const {
  const QRectF control = lookControlRectForTest(id);
  return control.isEmpty() ? QPoint(-1, -1) : control.center().toPoint();
}

QRectF CaptureEditor::lookControlRectForTest(const QString &id) const {
  for (const LookControl &control : lookControls())
    if (control.id == id)
      return control.kind == LookControl::Kind::Slider
                 ? sliderTrack(control.rect).adjusted(0, -8, 0, 8)
                 : control.rect;
  return {};
}

QString CaptureEditor::lookStatus(const CaptureLook &look) const {
  if (look.backdrop == BackgroundStyle::Off)
    return QStringLiteral("Backdrop: Off · B cycles");
  const QRectF frame = framedCanvas(selection_.size(), look);
  QString status =
      QStringLiteral("%1 · padding %2% · corners %3 · shadow %4%")
          .arg(backgroundStyleLabel(look.backdrop))
          .arg(qRound(look.padding * 100))
          .arg(qRound(look.corners * frame.width()))
          .arg(qRound(look.shadow * 100));
  if (look.glass != Glass::Opaque)
    status += QStringLiteral(" · glass %1%").arg(qRound(look.cardOpacity() * 100));
  if (look.aspect != CanvasAspect::Auto)
    status += QStringLiteral(" · %1").arg(canvasAspectLabel(look.aspect));
  return status;
}

bool CaptureEditor::trackLookPopover() {
  const LookPopover before = lookPopover_;
  if (!lookSlider_.isEmpty())
    return false; // a drag keeps its popover, wherever the pointer goes
  LookPopover hovered = LookPopover::None;
  for (const ToolbarButton &button : toolbarButtons(nullptr, false)) {
    if (!button.rect.contains(cursor_))
      continue;
    if (button.action == QStringLiteral("look-backdrop"))
      hovered = LookPopover::Backdrop;
    else if (button.action == QStringLiteral("look-frame"))
      hovered = LookPopover::Frame;
    else if (button.action == QStringLiteral("look-glass"))
      hovered = LookPopover::Glass;
  }
  if (hovered != LookPopover::None) {
    lookPopover_ = hovered;
    lookIntentOrigin_ = cursor_;
  } else if (lookPopover_ != LookPopover::None) {
    const QRectF panel = lookPopoverRect();
    // Keep it while the pointer travels from the button down into it.
    QPolygonF approach;
    approach << lookIntentOrigin_ << panel.topLeft() + QPointF(-8, 6)
             << panel.topRight() + QPointF(8, 6);
    const bool approaching = !lookIntentOrigin_.isNull() &&
                             cursor_.y() >= lookIntentOrigin_.y() &&
                             approach.containsPoint(cursor_, Qt::OddEvenFill);
    if (!panel.adjusted(0, -4, 0, 0).contains(cursor_) && !approaching) {
      lookPopover_ = LookPopover::None;
      lookIntentOrigin_ = {};
    }
  }
  return lookPopover_ != before;
}

bool CaptureEditor::pressLookPopover(const QPointF &point) {
  const QRectF panel = lookPopoverRect();
  if (panel.isEmpty() || !panel.contains(point))
    return false;
  for (const LookControl &control : lookControls()) {
    if (control.kind == LookControl::Kind::Slider &&
        control.rect.contains(point)) {
      lookSlider_ = control.id;
      lookBeforeSlider_ = look_;
      dragLookSlider(point);
      return true;
    }
    if (!control.id.isEmpty() && control.rect.contains(point)) {
      applyLookControl(control.id);
      return true;
    }
  }
  return true; // the popover's own padding is chrome, not canvas
}

void CaptureEditor::dragLookSlider(const QPointF &point) {
  const Slider *slider = sliderFor(lookSlider_);
  if (!slider)
    return;
  QRectF row;
  for (const LookControl &control : lookControls())
    if (control.id == lookSlider_)
      row = control.rect;
  const QRectF track = sliderTrack(row);
  const qreal t =
      std::clamp((point.x() - track.left()) / track.width(), 0.0, 1.0);
  CaptureLook live = look_;
  live.*(slider->value) = slider->low + t * (slider->high - slider->low);
  if (lookSlider_ == QStringLiteral("opacity"))
    live.glass = Glass::Custom;
  if (live == look_)
    return;
  // A live preview, not yet an operation: release commits it once.
  look_ = live;
  refreshCanvasRect();
  setStatus(lookStatus(look_));
  update();
}

void CaptureEditor::releaseLookSlider() {
  if (lookSlider_.isEmpty())
    return;
  lookSlider_.clear();
  const CaptureLook chosen = look_;
  look_ = lookBeforeSlider_;
  refreshCanvasRect();
  commitLook(chosen, lookStatus(chosen));
  trackLookPopover();
  update();
}

void CaptureEditor::applyLookControl(const QString &id) {
  CaptureLook next = look_;
  for (const Swatch &swatch : kSwatches) {
    if (id != QLatin1StringView(swatch.id))
      continue;
    if (swatch.style == BackgroundStyle::Solid) {
      chooseBackdropColour();
      return;
    }
    if (swatch.style == BackgroundStyle::Image) {
      chooseBackdropImage();
      return;
    }
    next.backdrop = swatch.style;
    if (next.backdrop == BackgroundStyle::Wallpaper && next.wallpaper.isEmpty())
      next.wallpaper = wallpaperId_;
    commitLook(next, next == look_ ? QString() : lookStatus(next));
    if (next.backdrop == BackgroundStyle::Wallpaper && next.wallpaper.isEmpty())
      adoptWallpaper();
    return;
  }
  if (id == QStringLiteral("aspect-auto"))
    next.aspect = CanvasAspect::Auto;
  else if (id == QStringLiteral("aspect-wide"))
    next.aspect = CanvasAspect::Wide;
  else if (id == QStringLiteral("aspect-standard"))
    next.aspect = CanvasAspect::Standard;
  else if (id == QStringLiteral("aspect-square"))
    next.aspect = CanvasAspect::Square;
  else if (id == QStringLiteral("glass-opaque"))
    next.glass = Glass::Opaque;
  else if (id == QStringLiteral("glass-light"))
    next.glass = Glass::Light;
  else if (id == QStringLiteral("glass-custom"))
    next.glass = Glass::Custom;
  CaptureLook automatic = next;
  automatic.aspect = CanvasAspect::Auto;
  if (next.aspect != look_.aspect && next.aspect != CanvasAspect::Auto &&
      framedCanvas(selection_.size(), next) ==
          framedCanvas(selection_.size(), automatic)) {
    // Too tall or wide for that shape (lookFrame's limit): it keeps Auto,
    // and says why.
    commitLook(next, QStringLiteral("Canvas %1 would dwarf this capture · "
                                    "it keeps Auto")
                         .arg(canvasAspectLabel(next.aspect)));
    return;
  }
  commitLook(next, lookStatus(next));
}

void CaptureEditor::paintLookPopover(QPainter &painter) {
  const QRectF panel = lookPopoverRect();
  if (panel.isEmpty())
    return;
  const ChromeTheme &theme = chromeTheme();
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setPen(QPen(theme.panelBorder.brush(panel), 1));
  painter.setBrush(theme.surface);
  painter.drawRoundedRect(panel, 9, 9);

  // Title and the key that does the same without the popover.
  const QRectF title(panel.left() + kPad, panel.top() + kPad - 2,
                     panel.width() - 2 * kPad, kTitle - 4);
  const auto [name, hint] =
      lookPopover_ == LookPopover::Backdrop
          ? std::pair{QStringLiteral("Backdrop"), QStringLiteral("B cycles")}
      : lookPopover_ == LookPopover::Frame
          ? std::pair{QStringLiteral("Frame"), QStringLiteral("Shift+B shadow")}
          : std::pair{QStringLiteral("Glass"), QStringLiteral("window opacity")};
  painter.setFont(chromeFont(11, true));
  painter.setPen(theme.foreground);
  painter.drawText(title, Qt::AlignLeft | Qt::AlignVCenter, name);
  painter.setFont(chromeFont(11));
  painter.setPen(theme.muted);
  painter.drawText(title, Qt::AlignRight | Qt::AlignVCenter, hint);

  const QVector<LookControl> controls = lookControls();
  const LookControl *hoveredSwatch = nullptr;
  for (const LookControl &control : controls) {
    const bool hovered = control.rect.contains(cursor_);
    switch (control.kind) {
    case LookControl::Kind::Swatch: {
      if (hovered)
        hoveredSwatch = &control;
      const QRectF swatch = control.rect;
      QPainterPath shape;
      shape.addRoundedRect(swatch, 6, 6);
      BackgroundStyle style = BackgroundStyle::Off;
      for (const Swatch &entry : kSwatches)
        if (control.id == QLatin1StringView(entry.id))
          style = entry.style;
      painter.save();
      painter.setClipPath(shape);
      if (style == BackgroundStyle::Off) {
        painter.fillRect(swatch, theme.button);
        painter.setPen(QPen(chromeAlpha(theme.foreground, 150), 1.5));
        painter.drawLine(swatch.bottomLeft() + QPointF(6, -6),
                         swatch.topRight() + QPointF(-6, 6));
      } else if (style == BackgroundStyle::Image && look_.image.isEmpty()) {
        painter.fillRect(swatch, theme.button);
        drawToolbarIcon(painter, swatch, QStringLiteral("background"), {},
                        theme.buttonText);
      } else {
        // The wallpaper this document shows, else the one it would get.
        const QImage picture =
            style == BackgroundStyle::Wallpaper
                ? pictures_.value(look_.wallpaper.isEmpty() ? wallpaperId_
                                                            : look_.wallpaper)
            : style == BackgroundStyle::Image ? pictures_.value(look_.image)
                                              : QImage();
        paintCaptureBackground(painter, swatch, style, picture, look_.color);
      }
      painter.restore();
      if (control.selected || hovered) {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(control.selected
                           ? QPen(theme.accent, 2)
                           : QPen(chromeAlpha(theme.foreground, 150), 1));
        painter.drawRoundedRect(swatch.adjusted(-2, -2, 2, 2), 8, 8);
      } else {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(chromeAlpha(theme.foreground, 40), 1));
        painter.drawRoundedRect(swatch, 6, 6);
      }
      break;
    }
    case LookControl::Kind::Slider: {
      const Slider *slider = sliderFor(control.id);
      const QRectF track = sliderTrack(control.rect);
      const qreal value = look_.*(slider->value);
      const qreal t =
          std::clamp((value - slider->low) / (slider->high - slider->low), 0.0, 1.0);
      painter.setFont(chromeFont(11));
      painter.setPen(theme.muted);
      painter.drawText(QRectF(control.rect.left(), control.rect.top(),
                              kLabelWidth, kRow),
                       Qt::AlignLeft | Qt::AlignVCenter, control.label);
      painter.setPen(Qt::NoPen);
      painter.setBrush(chromeAlpha(theme.foreground, 40));
      painter.drawRoundedRect(track, 2, 2);
      // Opacity only applies to Custom glass; elsewhere it reads as idle.
      const bool live = control.id != QStringLiteral("opacity") ||
                        look_.glass == Glass::Custom;
      painter.setBrush(live ? theme.accent : chromeAlpha(theme.foreground, 90));
      painter.drawRoundedRect(QRectF(track.topLeft(),
                                     QSizeF(track.width() * t, track.height())),
                              2, 2);
      const QPointF knob(track.left() + track.width() * t, track.center().y());
      const bool active = lookSlider_ == control.id ||
                          track.adjusted(-6, -10, 6, 10).contains(cursor_);
      painter.setBrush(theme.foreground);
      painter.setPen(QPen(live ? theme.accent : theme.muted, active ? 2.5 : 1.5));
      painter.drawEllipse(knob, 6, 6);
      QString text;
      if (control.id == QStringLiteral("corners"))
        text = QString::number(
            qRound(value * framedCanvas(selection_.size(), look_).width()));
      else
        text = QStringLiteral("%1%").arg(qRound(value * 100));
      painter.setFont(chromeFont(11, true));
      painter.setPen(theme.foreground);
      painter.drawText(QRectF(control.rect.right() - kValueWidth,
                              control.rect.top(), kValueWidth, kRow),
                       Qt::AlignRight | Qt::AlignVCenter, text);
      break;
    }
    case LookControl::Kind::Label:
      painter.setFont(chromeFont(11));
      painter.setPen(theme.muted);
      painter.drawText(control.rect, Qt::AlignLeft | Qt::AlignVCenter,
                       control.label);
      break;
    case LookControl::Kind::Segment: {
      const QRectF item = control.rect.adjusted(1, 0, -1, 0);
      painter.setPen(Qt::NoPen);
      painter.setBrush(control.selected ? theme.accent
                       : hovered        ? theme.buttonHover
                                        : theme.button);
      painter.drawRoundedRect(item, 7, 7);
      painter.setFont(chromeFont(11, true));
      painter.setPen(control.selected ? theme.accentText
                     : hovered        ? theme.buttonHoverText
                                      : theme.buttonText);
      painter.drawText(item, Qt::AlignCenter, control.label);
      break;
    }
    }
  }
  if (lookPopover_ == LookPopover::Frame) {
    // Between the frame's sliders and the canvas shape.
    const qreal y = panel.top() + kPad + kTitle + 3 * kRow + kSeparator / 2.0;
    painter.setPen(QPen(chromeAlpha(theme.foreground, 26), 1));
    painter.drawLine(QPointF(panel.left() + kPad, y),
                     QPointF(panel.right() - kPad, y));
  }
  painter.restore();
  if (hoveredSwatch)
    drawInstantTooltip(painter, rect(), hoveredSwatch->rect,
                       hoveredSwatch->label);
}
