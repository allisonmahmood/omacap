#!/usr/bin/env bash
# Run only in a disposable environment, after installing the release package.
set -euo pipefail
expected_version=${1:?Pass the expected application version}
pacman -Qk omacap
desktop-file-validate /usr/share/applications/omacap.desktop
test -s /usr/share/icons/hicolor/scalable/apps/omacap.svg
test -s /usr/share/licenses/omacap/LICENSE
actual_version=$(QT_QPA_PLATFORM=offscreen /usr/bin/omacap --version)
test "$actual_version" = "$expected_version"

test_state=$(mktemp -d)
trap 'rm -rf -- "$test_state"' EXIT
mkdir -p "$test_state"/{data,config,cache,runtime}
chmod 700 "$test_state/runtime"
# A healthy recorder stays open. Missing libraries or QML modules exit early.
status=0
XDG_DATA_HOME="$test_state/data" XDG_CONFIG_HOME="$test_state/config" \
  XDG_CACHE_HOME="$test_state/cache" XDG_RUNTIME_DIR="$test_state/runtime" \
  QT_QPA_PLATFORM=xcb QSG_RHI_BACKEND=opengl LIBGL_ALWAYS_SOFTWARE=1 \
  dbus-run-session -- xvfb-run -a timeout 8 /usr/bin/omacap > "$test_state/launch.log" 2>&1 || status=$?
cat "$test_state/launch.log"
test "$status" -eq 124
