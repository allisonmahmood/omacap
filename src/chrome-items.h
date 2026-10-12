#pragma once
#include <QColor>
#include <QQuickPaintedItem>

class Theme;

// QML items that paint Omasnap's chrome with Omasnap's own QPainter code, so OmaCap's video
// UI matches it pixel for pixel: gradient theme borders and the toolbar icon set. Both repaint
// only when a property or the theme changes. Registered in the `OmaCap` QML module by
// registerChromeTypes(); they read the `theme` context property.

// A rounded panel, button face or pill: `fill`, and a 1 px border in one of the theme's
// border roles ("button", "buttonHover", "buttonSelected", "active", "inactive",
// "selection", "panel", "tooltip"), or none when `border` is empty.
class ChromeBox : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QColor fill MEMBER fill NOTIFY changed)
    Q_PROPERTY(QString border MEMBER border NOTIFY changed)
    Q_PROPERTY(qreal borderWidth MEMBER borderWidth NOTIFY changed)
    Q_PROPERTY(qreal radius MEMBER radius NOTIFY changed)
  public:
    explicit ChromeBox(QQuickItem *parent = nullptr);
    void paint(QPainter *painter) override;
    void componentComplete() override;
  signals:
    void changed();

  private:
    Theme *theme = nullptr;
    QColor fill = Qt::transparent;
    QString border;
    qreal borderWidth = 1, radius = 8;
};

// One toolbar glyph from drawVideoIcon, `size` px tall (Omasnap's toolbar uses 19).
class ChromeIcon : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QString name MEMBER name NOTIFY changed)
    Q_PROPERTY(QColor color MEMBER color NOTIFY changed)
    Q_PROPERTY(qreal size MEMBER size NOTIFY changed)
  public:
    explicit ChromeIcon(QQuickItem *parent = nullptr);
    void paint(QPainter *painter) override;
  signals:
    void changed();

  private:
    QString name;
    QColor color = Qt::white;
    qreal size = 19;
};

void registerChromeTypes();
