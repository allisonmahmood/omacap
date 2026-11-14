#!/usr/bin/env bash
# Proves that a build of OmaCap records and exports on this Omarchy desktop.
#
#   scripts/verify-desktop.sh PACKAGE.pkg.tar.zst | OMACAP_BINARY
#
# CI tests in containers: no Hyprland, no screen-cast portal, no hardware encoder, and
# whatever the mirrors held that day. This runs the build where OmaCap is used, on this
# machine's packages, and covers what a container can't: a recording through the real portal
# with the encoder recordings use here, and exports through this session's display.
#
# Nothing is installed and your screen is not recorded. A package is unpacked to a temporary
# directory, recordings happen in a hidden nested Hyprland with a portal of its own, and the
# exports render two seconds of test patterns. The evidence is written to VERIFICATION.txt.
set -euo pipefail
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build=$(realpath -- "${1:?a package or an omacap binary}")
# An agent's shell can carry library paths (an AppImage's) that break system binaries.
unset LD_LIBRARY_PATH

work=$(mktemp -d)
state=$work/nested
report=$work/VERIFICATION.txt
cleanup() {
  "$repo/scripts/nested-portal.sh" stop "$state" >/dev/null 2>&1 || true
  "$repo/scripts/nested-hyprland.sh" stop "$state" || echo "stop the nested Hyprland by hand: see $state" >&2
  [[ -e $state/env ]] || rm -rf -- "$work"
}
trap cleanup EXIT
step() { printf '\n== %s\n' "$1" | tee -a "$report"; }
run() { "$@" 2>&1 | tee -a "$report"; }

if [[ $build == *.pkg.tar.zst ]]; then
  mkdir "$work/package"
  bsdtar -xf "$build" -C "$work/package"
  omacap=$work/package/usr/bin/omacap
else
  omacap=$build
fi
{
  echo "OmaCap $("$omacap" --version) ($(basename "$build")), verified $(date -u +%Y-%m-%dT%H:%MZ)"
  echo "Source: $(git -C "$repo" rev-parse HEAD)$(git -C "$repo" diff --quiet HEAD || echo ', with uncommitted changes')"
  echo "Kernel $(uname -r); GPU: $(lspci 2>/dev/null | grep -E 'VGA|3D' | sed 's/.*: //' | paste -sd ';')"
} | tee "$report"

step "This desktop"
run "$repo/scripts/doctor.sh"

step "The recorder with every encoder this machine has"
run /usr/bin/python "$repo/tests/capture-cut.py"

step "Recording through the real portal, in a nested Hyprland"
"$repo/scripts/nested-hyprland.sh" start "$state" >/dev/null
"$repo/scripts/nested-portal.sh" start "$state" >/dev/null
instance=$(sed -n 's/^HYPRLAND_INSTANCE_SIGNATURE=//p' "$state/env")
# The recording needs something that keeps drawing, or no frames arrive.
terminal=$(command -v foot alacritty kitty ghostty | head -1)
[[ -n $terminal ]] || { echo "No terminal found to draw in the nested Hyprland." >&2; exit 1; }
HYPRLAND_INSTANCE_SIGNATURE=$instance hyprctl dispatch \
  "hl.dsp.exec_cmd(\"$terminal -e sh -c 'while :; do date +%N; sleep 0.05; done'\")" >/dev/null
sleep 2
record() { run timeout 180 /usr/bin/python "$repo/tests/record-nested.py" "$state" --omacap "$omacap" "$@"; }
record
# Each of these ends a recording by itself; the file must hold nothing from after it.
for interrupt in scale mode transform kill-owner; do
  record --interrupt "$interrupt"
done

step "Exporting through this session's display"
run "$repo/tests/packaged-export.sh" "$omacap" "$work/export" mp4
run "$repo/tests/packaged-export.sh" "$omacap" "$work/export" gif

step "Result"
echo "PASS: this build records and exports on this desktop." | tee -a "$report"
cp "$report" "$repo/VERIFICATION.txt"
echo "Evidence: $repo/VERIFICATION.txt"
