#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
mkdir -p "$repo/build/tests" "$repo/tests/out"
cd "$repo/build/tests"
qmake6 "$repo/tests/tests.pro"
make -j"${JOBS:-4}"
cd "$repo"
# Exports from an earlier run must never satisfy an assertion after a failed export.
rm -f -- "$repo/tests/out/"{delayed-export.mp4,demo.mp4,demo60.mp4,demo.gif,cancelled.mp4,after-cancel.mp4,portal-export.mp4,metrics30.json,metrics60.json,studio.png}
/usr/bin/python "$repo/tests/make-sync-fixture.py"
# A fresh state directory prevents recovery/preferences from affecting later runs.
test_state=$(mktemp -d)
trap 'rm -rf -- "$test_state"' EXIT
mkdir -p "$test_state/data" "$test_state/config" "$test_state/cache"
# Synthetic audio/video and a generated wallpaper; no desktop devices are opened.
XDG_DATA_HOME="$test_state/data" XDG_CONFIG_HOME="$test_state/config" \
    XDG_CACHE_HOME="$test_state/cache" QT_FORCE_STDERR_LOGGING=1 \
    "$repo/build/tests/omacap-tests" -o -,txt \
    -o "$repo/tests/out/results.xml",junitxml \
    2>&1 | tee "$repo/tests/out/results.txt"
/usr/bin/python "$repo/tests/verify-sync.py"
