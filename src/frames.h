#pragma once
#include <QMutex>
#include <QMutexLocker>
#include <QQuickImageProvider>
class Frames : public QQuickImageProvider {
  public:
    Frames() : QQuickImageProvider(Image) {}
    QImage screen, camera;
    QMutex mutex;
    void put(bool cam, const QImage &im) {
        QMutexLocker lock(&mutex);
        (cam ? camera : screen) = im;
    }
    QImage requestImage(const QString &id, QSize *size, const QSize &) override {
        QMutexLocker lock(&mutex);
        QImage im = id.startsWith("camera") ? camera : screen;
        if (size)
            *size = im.size();
        return im;
    }
};
