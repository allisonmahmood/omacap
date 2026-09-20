#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
"$repo/scripts/build.sh"
install -Dm755 "$repo/build/omacap" "$HOME/.local/bin/omacap"
install -Dm644 "$repo/packaging/omacap.svg" "${XDG_DATA_HOME:-$HOME/.local/share}/icons/hicolor/scalable/apps/omacap.svg"
mkdir -p "${XDG_DATA_HOME:-$HOME/.local/share}/applications"
sed "s|^Exec=.*|Exec=\"$HOME/.local/bin/omacap\" %f|" "$repo/packaging/omacap.desktop" > "${XDG_DATA_HOME:-$HOME/.local/share}/applications/omacap.desktop"
if command -v update-desktop-database >/dev/null; then
  update-desktop-database "${XDG_DATA_HOME:-$HOME/.local/share}/applications"
fi
