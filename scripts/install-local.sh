#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
"$repo/scripts/build.sh" --target omacap
install -Dm755 "$repo/build/omacap" "$HOME/.local/bin/omacap"
install -Dm644 "$repo/packaging/omacap.svg" "${XDG_DATA_HOME:-$HOME/.local/share}/icons/hicolor/scalable/apps/omacap.svg"
mkdir -p "${XDG_DATA_HOME:-$HOME/.local/share}/applications"
# Every Exec= line (the app's and each action's) runs this binary, keeping its arguments.
sed "s|^Exec=omacap\b|Exec=\"$HOME/.local/bin/omacap\"|" "$repo/packaging/omacap.desktop" > "${XDG_DATA_HOME:-$HOME/.local/share}/applications/omacap.desktop"
if command -v update-desktop-database >/dev/null; then
  update-desktop-database "${XDG_DATA_HOME:-$HOME/.local/share}/applications"
fi
echo "Installed OmaCap $("$HOME/.local/bin/omacap" --version) to $HOME/.local/bin/omacap"
