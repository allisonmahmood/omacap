#pragma once
#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>
#include <QVariantMap>
class Theme : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap colors READ colors NOTIFY changed)
  public:
    explicit Theme(QObject *p = nullptr);
    QVariantMap colors() const { return m_colors; }
    QString wallpaper() const;
    void reload();
  signals:
    void changed();

  private:
    QString root;
    QVariantMap m_colors;
    QFileSystemWatcher watcher;
    QTimer debounce;
};
