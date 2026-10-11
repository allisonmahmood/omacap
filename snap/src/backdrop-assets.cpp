/** @fileoverview Content-addressed store for backdrop pictures. */
#include "backdrop-assets.hpp"

#include "capture.hpp"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QSaveFile>
#include <QStandardPaths>

namespace {
QString assetsDirectory() {
  const QString state =
      QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation);
  if (state.isEmpty())
    return {};
  const QString root = QDir(state).filePath(QStringLiteral("omacap/backdrops"));
  return ensurePrivateDirectory(root) ? root : QString();
}

/// Omarchy's current theme directory, as OmaCap's video side finds it.
QString omarchyCurrent() {
  const QString override = qEnvironmentVariable("OMACAP_THEME_ROOT");
  if (!override.isEmpty())
    return override;
  const QString state = qEnvironmentVariable(
      "XDG_STATE_HOME", QDir::home().filePath(QStringLiteral(".local/state")));
  const QString current = QDir(state).filePath(QStringLiteral("omarchy/current"));
  return QFileInfo::exists(current)
             ? current
             : QDir::home().filePath(QStringLiteral(".config/omarchy/current"));
}
} // namespace

QString currentWallpaperPath() {
  return QFileInfo(QDir(omarchyCurrent()).filePath(QStringLiteral("background")))
      .canonicalFilePath();
}

QString importBackdropAsset(const QString &file, QString &error) {
  QFile input(file);
  if (!input.open(QIODevice::ReadOnly)) {
    error = QStringLiteral("Could not read %1").arg(file);
    return {};
  }
  const QByteArray bytes = input.readAll();
  const QString root = assetsDirectory();
  if (root.isEmpty()) {
    error = QStringLiteral("Could not create the backdrop store");
    return {};
  }
  const QString id = QString::fromLatin1(
      QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().left(40));
  const QString path = QDir(root).filePath(id);
  if (QFile::exists(path)) {
    // Touch it, so pruning sees it is still in use.
    QFile stored(path);
    if (stored.open(QIODevice::ReadWrite))
      static_cast<void>(stored.setFileTime(QDateTime::currentDateTime(),
                                           QFileDevice::FileModificationTime));
    return id;
  }
  // Only pictures Qt can show become backdrops.
  QBuffer buffer;
  buffer.setData(bytes);
  buffer.open(QIODevice::ReadOnly);
  if (!QImageReader(&buffer).canRead()) {
    error = QStringLiteral("%1 is not an image OmaCap can read")
                .arg(QFileInfo(file).fileName());
    return {};
  }
  QSaveFile output(path);
  if (!output.open(QIODevice::WriteOnly) ||
      output.write(bytes) != bytes.size() || !output.commit()) {
    error = QStringLiteral("Could not store the backdrop picture");
    return {};
  }
  return id;
}

QImage loadBackdropAsset(const QString &id) {
  const QString root = assetsDirectory();
  if (id.isEmpty() || root.isEmpty())
    return {};
  QImageReader reader(QDir(root).filePath(id));
  reader.setAutoTransform(true);
  const QSize size = reader.size();
  if (size.width() > 3840 || size.height() > 3840)
    reader.setScaledSize(size.scaled(3840, 3840, Qt::KeepAspectRatio));
  QImage image = reader.read();
  return image.isNull() ? image
                        : image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

void pruneBackdropAssets(const QSet<QString> &keep) {
  const QString root = assetsDirectory();
  if (root.isEmpty())
    return;
  const QDateTime cutoff = QDateTime::currentDateTime().addDays(-1);
  const QDir dir(root);
  for (const QFileInfo &file : dir.entryInfoList(QDir::Files)) {
    if (!keep.contains(file.fileName()) && file.lastModified() < cutoff)
      QFile::remove(file.filePath());
  }
}
