#pragma once

#include <QString>

inline constexpr auto kPinSmokeEditorChild = "OMACAP_PIN_SMOKE_EDITOR_CHILD";
[[nodiscard]] bool runPinInteractionSmoke(QString &error);
[[nodiscard]] bool runPinThemeRenderingSmoke(const QString &path, QString &error);
