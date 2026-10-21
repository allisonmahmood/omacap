#pragma once
#include "chrome-theme.hpp"
#include <QColor>
#include <QFont>
#include <QObject>

// The live Omarchy theme, as Omasnap paints its chrome: the one source of colours and
// fonts for OmaCap's video UI. QML reads it as the `theme` context property; painted
// chrome items (src/chrome-items.h) read the full ChromeTheme, gradient borders included.
// $OMACAP_THEME_ROOT replaces ~/.local/state/omarchy/current, for tests.
class Theme : public QObject {
    Q_OBJECT
    Q_PROPERTY(QColor surface READ surface NOTIFY changed)
    Q_PROPERTY(QColor foreground READ foreground NOTIFY changed)
    Q_PROPERTY(QColor scrim READ scrim NOTIFY changed)
    Q_PROPERTY(QColor muted READ muted NOTIFY changed)
    Q_PROPERTY(QColor accent READ accent NOTIFY changed)
    Q_PROPERTY(QColor accentText READ accentText NOTIFY changed)
    Q_PROPERTY(QColor warning READ warning NOTIFY changed)
    Q_PROPERTY(QColor button READ button NOTIFY changed)
    Q_PROPERTY(QColor buttonHover READ buttonHover NOTIFY changed)
    Q_PROPERTY(QColor buttonSelected READ buttonSelected NOTIFY changed)
    Q_PROPERTY(QColor buttonText READ buttonText NOTIFY changed)
    Q_PROPERTY(QColor buttonHoverText READ buttonHoverText NOTIFY changed)
    Q_PROPERTY(QColor buttonSelectedText READ buttonSelectedText NOTIFY changed)
    Q_PROPERTY(QColor tooltipBackground READ tooltipBackground NOTIFY changed)
    Q_PROPERTY(QColor tooltipText READ tooltipText NOTIFY changed)
    Q_PROPERTY(QColor tooltipBorder READ tooltipBorder NOTIFY changed)
    Q_PROPERTY(QColor toastBackground READ toastBackground NOTIFY changed)
    Q_PROPERTY(QColor toastText READ toastText NOTIFY changed)
  public:
    explicit Theme(QObject *parent = nullptr);
    const ChromeTheme &chrome() const { return watcher.theme(); }
    QColor surface() const { return chrome().surface; }
    QColor foreground() const { return chrome().foreground; }
    QColor scrim() const { return chrome().scrim; }
    QColor muted() const { return chrome().muted; }
    QColor accent() const { return chrome().accent; }
    QColor accentText() const { return chrome().accentText; }
    QColor warning() const { return chrome().warning; }
    QColor button() const { return chrome().button; }
    QColor buttonHover() const { return chrome().buttonHover; }
    QColor buttonSelected() const { return chrome().buttonSelected; }
    QColor buttonText() const { return chrome().buttonText; }
    QColor buttonHoverText() const { return chrome().buttonHoverText; }
    QColor buttonSelectedText() const { return chrome().buttonSelectedText; }
    QColor tooltipBackground() const { return chrome().tooltipBackground; }
    QColor tooltipText() const { return chrome().tooltipText; }
    QColor tooltipBorder() const { return chrome().tooltipBorder; }
    QColor toastBackground() const { return chrome().toastBackground; }
    QColor toastText() const { return chrome().toastText; }
    // The current Omarchy wallpaper, the default video background.
    QString wallpaper() const;
    // Omasnap's chrome faces: Adwaita Sans (Noto Sans fallback), and monospace for readouts.
    Q_INVOKABLE QFont font(int pixelSize, bool bold = false) const;
    Q_INVOKABLE QFont mono(int pixelSize, bool bold = false) const;
    // `color` with its alpha scaled by alpha/255, as Omasnap's chromeAlpha.
    Q_INVOKABLE QColor alpha(const QColor &color, int alpha) const;
  signals:
    void changed();

  private:
    QString root;
    ChromeThemeWatcher watcher;
};
