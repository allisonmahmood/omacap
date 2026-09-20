#include "theme.h"
#include <QColor>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
Theme::Theme(QObject *p) : QObject(p) {
    root = qEnvironmentVariable("OMACAP_THEME_ROOT");
    if (root.isEmpty()) {
        root = qEnvironmentVariable("XDG_STATE_HOME", QDir::homePath() + "/.local/state") +
               "/omarchy/current";
        if (!QDir(root).exists())
            root = QDir::homePath() + "/.config/omarchy/current";
    }
    debounce.setSingleShot(true);
    debounce.setInterval(100);
    connect(&watcher, &QFileSystemWatcher::directoryChanged, &debounce,
            qOverload<>(&QTimer::start));
    connect(&watcher, &QFileSystemWatcher::fileChanged, &debounce, qOverload<>(&QTimer::start));
    connect(&debounce, &QTimer::timeout, this, &Theme::reload);
    reload();
}
QString Theme::wallpaper() const {
    return QFileInfo(root + "/background").canonicalFilePath();
}
void Theme::reload() {
    QVariantMap c{{"background", "#151719"},      {"foreground", "#e1e4e8"},
                  {"accent", "#89b4fa"},          {"selection", "#29313c"},
                  {"muted", "#777f89"},           {"lighter_background", "#202428"},
                  {"dark_foreground", "#b3b8bf"}, {"red", "#ed8796"}};
    QFile f(root + "/theme/colors.toml");
    if (f.open(QIODevice::ReadOnly)) {
        QRegularExpression re("^\\s*([a-z_]+)\\s*=\\s*\"(#[0-9a-fA-F]{6})\"");
        for (auto line : QString::fromUtf8(f.readAll()).split('\n')) {
            auto m = re.match(line);
            if (m.hasMatch())
                c[m.captured(1)] = m.captured(2);
        }
    }
    if (c != m_colors) {
        m_colors = c;
        emit changed();
    }
    if (!watcher.files().isEmpty())
        watcher.removePaths(watcher.files());
    if (!watcher.directories().isEmpty())
        watcher.removePaths(watcher.directories());
    for (const QString &path : QStringList{QFileInfo(root).absolutePath(), root, root + "/theme",
                                           root + "/theme/colors.toml"})
        if (QFileInfo::exists(path))
            watcher.addPath(path);
}
