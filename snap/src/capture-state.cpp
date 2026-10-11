/** @fileoverview Remembered capture-side state. */
#include "capture-state.hpp"

#include <QDir>
#include <atomic>
#include <QJsonDocument>
#include <QSettings>
#include <QStandardPaths>

namespace {
std::atomic_bool rememberLooks = true;

QString statePath() {
  return QDir(QStandardPaths::writableLocation(
                  QStandardPaths::GenericStateLocation))
      .filePath(QStringLiteral("omacap/capture.ini"));
}
} // namespace

CaptureLook loadLastLook() {
  const QSettings settings(statePath(), QSettings::IniFormat);
  CaptureLook look = lookFromJson(
      QJsonDocument::fromJson(settings.value(QStringLiteral("look/json"))
                                  .toByteArray())
          .object());
  look.wallpaper.clear();
  return look;
}

void saveLastLook(const CaptureLook &look) {
  CaptureLook remembered = look;
  remembered.wallpaper.clear();
  QSettings settings(statePath(), QSettings::IniFormat);
  settings.setValue(QStringLiteral("look/json"),
                    QJsonDocument(lookToJson(remembered))
                        .toJson(QJsonDocument::Compact));
}

void setRememberLooks(bool remember) { rememberLooks = remember; }

bool remembersLooks() { return rememberLooks; }

RecordChoices loadRecordChoices(const QString &defaultMic) {
  const QSettings settings(statePath(), QSettings::IniFormat);
  RecordChoices choices;
  choices.mic =
      settings.value(QStringLiteral("record/mic"), defaultMic).toString();
  choices.camera = settings.value(QStringLiteral("record/camera")).toString();
  choices.desktopAudio =
      settings.value(QStringLiteral("record/desktopAudio"), false).toBool();
  return choices;
}

void saveRecordChoices(const RecordChoices &choices) {
  QSettings settings(statePath(), QSettings::IniFormat);
  settings.setValue(QStringLiteral("record/mic"), choices.mic);
  settings.setValue(QStringLiteral("record/camera"), choices.camera);
  settings.setValue(QStringLiteral("record/desktopAudio"), choices.desktopAudio);
}
