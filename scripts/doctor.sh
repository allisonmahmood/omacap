#!/usr/bin/env bash
# Checks that this desktop can record and export before anything is tested on it, and prints
# the versions a result depends on. A failing check here explains a failing recording or
# export better than the app's own message does.
#
#   scripts/doctor.sh
#
# Exits 1 when a check fails. It only reads: nothing is started, stopped or changed.
set -uo pipefail
failed=0
pass() { printf 'ok    %s\n' "$1"; }
fail() {
  printf 'FAIL  %s\n      %s\n' "$1" "$2"
  failed=1
}

if hyprctl version >/dev/null 2>&1; then
  pass "Hyprland answers ($(hyprctl version | sed -n 's/^Hyprland \([^ ]*\).*/\1/p'))"
else
  fail "Hyprland does not answer hyprctl" "OmaCap records on Hyprland only; check HYPRLAND_INSTANCE_SIGNATURE."
fi

# Other Hyprland instances are usually a test compositor someone forgot to stop.
others=$(hyprctl instances -j 2>/dev/null | python -c 'import json, sys; print(len(json.load(sys.stdin)) - 1)' 2>/dev/null || echo 0)
if [[ $others -gt 0 ]]; then
  fail "$others more Hyprland instance(s) are running" "Stop a leftover test compositor: scripts/nested-hyprland.sh stop STATE_DIR."
else
  pass "no other Hyprland instance is running"
fi

# The exporter draws through Xwayland. Qt finds the display by its socket file, which can go
# missing while the X server lives on; every export then fails with "No OpenGL context".
display=${DISPLAY:-}
number=${display#*:} # ":0.0" and "host:0" both name display 0
socket=/tmp/.X11-unix/X${number%%.*}
if [[ -n $display ]] && python - "$socket" <<'PY' 2>/dev/null; then
import socket, sys
with socket.socket(socket.AF_UNIX) as s:
    s.settimeout(2)
    s.connect(sys.argv[1])
PY
  pass "Xwayland accepts connections on $display"
else
  fail "nothing can connect to the X display '${display}'" \
    "Exports fail with \"No OpenGL context\". If ${socket}_ exists, 'ln -s $(basename "$socket")_ $socket' restores it until the next login."
fi

if busctl --user status org.freedesktop.portal.Desktop >/dev/null 2>&1; then
  pass "the desktop portal is running"
else
  fail "the desktop portal is not running" "Recording asks xdg-desktop-portal for the screen."
fi

if gst-inspect-1.0 --exists pipewiresrc; then
  pass "GStreamer has pipewiresrc"
else
  fail "GStreamer has no pipewiresrc" "Install gst-plugin-pipewire."
fi
# What a recording here is encoded with; see ENCODERS in scripts/capture.py.
if gst-inspect-1.0 --exists nvh264enc; then
  pass "recordings use NVIDIA's encoder (nvh264enc)"
elif gst-inspect-1.0 --exists vp8enc; then
  pass "recordings use the software encoder (vp8enc)"
else
  fail "GStreamer has no encoder OmaCap uses" "Install gst-plugins-good for vp8enc."
fi

marker=${OMACAP_RUNTIME_DIR:-${XDG_RUNTIME_DIR:-/tmp}/omacap}/recording
if [[ -e $marker ]]; then
  fail "OmaCap is recording, or left its marker behind ($marker)" "Stop the recording first."
else
  pass "no recording is in progress"
fi

echo
echo "Versions:"
pacman -Q omacap hyprland xdg-desktop-portal-hyprland pipewire gst-plugin-pipewire gstreamer \
  gst-plugins-bad-libs qt6-base onnxruntime-cpu nvidia-utils 2>/dev/null | sed 's/^/  /'
exit $failed
