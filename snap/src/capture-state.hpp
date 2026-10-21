/** @fileoverview What the capture side remembers between runs, in
 *  `~/.local/state/omacap/capture.ini`: the look fresh captures start with
 *  and the record bar's device choices. Reading is a tiny file and fine at
 *  startup; write from a worker. */
#pragma once

#include "look.hpp"

#include <QString>

/// The record bar's choices. Ids are QAudioDevice/QCameraDevice ids; empty
/// records without that device.
struct RecordChoices {
  QString mic;
  QString camera;
  bool desktopAudio = false;
  bool operator==(const RecordChoices &) const = default;
};

/// The look last used in the editor, or OmaCap's: the Omarchy wallpaper with
/// its default padding, corners and shadow. The wallpaper id is always empty;
/// each capture resolves the wallpaper it was taken under.
[[nodiscard]] CaptureLook loadLastLook();
void saveLastLook(const CaptureLook &look);
/// Whether editors remember the looks they use (on by default). The smoke
/// suite turns it off so one test's look never styles the next test's
/// captures.
void setRememberLooks(bool remember);
[[nodiscard]] bool remembersLooks();
/// Choices from the last recording; nothing remembered means the default
/// microphone (`defaultMic`), no camera and no desktop audio.
[[nodiscard]] RecordChoices loadRecordChoices(const QString &defaultMic = {});
void saveRecordChoices(const RecordChoices &choices);
