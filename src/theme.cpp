#include "theme.h"
#include "overlay-chrome.hpp"
#include <QApplication>
#include <QDir>
#include <QFileInfo>

namespace {
QString themeRoot() {
    QString root = qEnvironmentVariable("OMACAP_THEME_ROOT");
    if (!root.isEmpty())
        return root;
    root = qEnvironmentVariable("XDG_STATE_HOME", QDir::homePath() + "/.local/state") +
           "/omarchy/current";
    return QDir(root).exists() ? root : QDir::homePath() + "/.config/omarchy/current";
}
} // namespace

Theme::Theme(QObject *parent)
    : QObject(parent), root(themeRoot()), watcher(root + "/theme") {
    connect(&watcher, &ChromeThemeWatcher::changed, this, [this] {
        // Choosers are Qt widget dialogs; style them as Omasnap does.
        if (auto app = qobject_cast<QApplication *>(QCoreApplication::instance()))
            app->setStyleSheet(chrome().tooltipStyleSheet() + chrome().fileDialogStyleSheet());
        emit changed();
    });
}

QString Theme::wallpaper() const {
    return QFileInfo(root + "/background").canonicalFilePath();
}

QFont Theme::font(int pixelSize, bool bold) const {
    return chromeFont(pixelSize, bold);
}

QFont Theme::mono(int pixelSize, bool bold) const {
    return chromeMonoFont(pixelSize, bold);
}

QColor Theme::alpha(const QColor &color, int alpha) const {
    return chromeAlpha(color, alpha);
}
