#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
mkdir -p "$repo/tests/out"
"$repo/scripts/build.sh"
cd "$repo"
# Omasnap-derived capture and screenshot editor checks.
(cd "$repo/build/snap" && QT_QPA_PLATFORM=offscreen ./omasnap-smoke)
# Exports from an earlier run must never satisfy an assertion after a failed export.
rm -f -- "$repo/tests/out/"{delayed-export.mp4,demo.mp4,demo60.mp4,demo.gif,cancelled.mp4,after-cancel.mp4,portal-export.mp4,metrics30.json,metrics60.json,studio.png}
/usr/bin/python "$repo/tests/make-sync-fixture.py"
/usr/bin/python "$repo/tests/capture-pipewire.py"
/usr/bin/python "$repo/tests/capture-cut.py"
# A fresh state directory prevents recovery/preferences from affecting later runs.
test_state=$(mktemp -d)
trap 'rm -rf -- "$test_state"' EXIT
mkdir -p "$test_state/data" "$test_state/config" "$test_state/cache" "$test_state/tmp" \
    "$test_state/videos" "$test_state/pictures" "$test_state/runtime"
# Recordings and screenshots are saved to Videos and Pictures; keep test output out of the
# real ones.
printf 'XDG_VIDEOS_DIR="%s/videos"\nXDG_PICTURES_DIR="%s/pictures"\n' "$test_state" "$test_state" \
    >"$test_state/config/user-dirs.dirs"
unset OMARCHY_SCREENSHOT_DIR
# Synthetic audio/video and a generated wallpaper; no desktop devices are opened. A private
# runtime directory keeps the recording marker and video socket away from a running OmaCap.
XDG_DATA_HOME="$test_state/data" XDG_CONFIG_HOME="$test_state/config" \
    XDG_CACHE_HOME="$test_state/cache" TMPDIR="$test_state/tmp" QT_FORCE_STDERR_LOGGING=1 \
    OMACAP_RUNTIME_DIR="$test_state/runtime" \
    "$repo/build/omacap-tests" -o -,txt \
    -o "$repo/tests/out/results.xml",junitxml \
    2>&1 | tee "$repo/tests/out/results.txt"
/usr/bin/python "$repo/tests/verify-sync.py"
