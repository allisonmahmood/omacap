/** @fileoverview Pictures a look can stand a screenshot on: Omarchy's
 *  wallpaper as it was when the capture was taken, or a chosen image. Each is
 *  stored once, by content, under `~/.local/state/omacap/backdrops`, and
 *  documents refer to it by id, so undo, reopening and the recents shelf keep
 *  their backdrop after the wallpaper changes. Everything here touches files:
 *  call it on a worker. */
#pragma once

#include <QImage>
#include <QSet>
#include <QString>

/// The picture Omarchy shows as the desktop background now, or empty.
[[nodiscard]] QString currentWallpaperPath();
/// Stores `file` unless an identical picture is stored already, and returns
/// its id; empty with `error` set when it cannot be read or stored.
[[nodiscard]] QString importBackdropAsset(const QString &file, QString &error);
/// Decodes a stored picture at backdrop resolution (longest edge at most
/// 3840 px). Null when the id is unknown or the file is gone.
[[nodiscard]] QImage loadBackdropAsset(const QString &id);
/// Deletes stored pictures that are not in `keep` and have not been touched
/// for a day; younger ones may belong to a document still being edited.
void pruneBackdropAssets(const QSet<QString> &keep);
