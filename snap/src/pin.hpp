#pragma once

#include <QString>

enum class PinLifetime { Timed, Persistent };

/** Runs a detached pinned-image window in this OmaCap process. */
[[nodiscard]] int runPinnedCapture(const QString &path, PinLifetime lifetime);
