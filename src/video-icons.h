#pragma once

class QColor;
class QPainter;
class QRectF;
class QString;

// Draws a toolbar icon centred in `bounds`, as Omasnap's drawToolbarIcon does: a 19 px
// Lucide-style glyph, 2 px round strokes on a 24-unit grid. Adds the video editor's icons
// (play, pause, scissors, zoom, person, export, mic, speaker, frame, glass, crop, …) and
// falls back to Omasnap's own set (undo, redo, background, close, folder, edit, pin, …).
void drawVideoIcon(QPainter &painter, const QRectF &bounds, const QString &name,
                   const QColor &color);
