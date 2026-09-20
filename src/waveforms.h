#pragma once
#include <QObject>
#include <QProcess>
#include <QVariantMap>

// One bounded peak cache per source, independent of edits and playback.
class Waveforms : public QObject {
    Q_OBJECT
  public:
    explicit Waveforms(QObject *parent = nullptr) : QObject(parent) {}
    ~Waveforms() override { clear(); }
    QVariantMap data;
    void load(const QString &directory, double duration);
    void clear();
  signals:
    void changed();

  private:
    QList<QProcess *> workers;
    quint64 generation = 0;
};
