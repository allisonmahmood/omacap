#!/usr/bin/env bash
# Renders a two-second edit with a blurred camera through `omacap --export`, the way the
# editor's Export button does, and checks the file it writes.
#
#   tests/packaged-export.sh OMACAP DIR [gif]
#
# OMACAP is the binary to check (an installed or unpacked package, not the build tree's test
# runner) and DIR an empty directory for the job. The exporter needs an X display: Xvfb in CI,
# Xwayland on a desktop. This covers what a package can get wrong without any test noticing:
# its OpenGL context, the person model inside the binary, ONNX Runtime and FFmpeg.
set -euo pipefail
omacap=${1:?the omacap binary}
dir=${2:?an empty directory}
kind=${3:-mp4}
mkdir -p "$dir"
ffmpeg -v error -y -f lavfi -i testsrc2=size=640x360:rate=30:duration=2 -c:v ffv1 "$dir/screen.mkv"
ffmpeg -v error -y -f lavfi -i smptebars=size=640x480:rate=30:duration=2 -c:v ffv1 "$dir/camera.mkv"
gif=false
[[ $kind == gif ]] && gif=true
cat > "$dir/export.json" <<JSON
{"edit": {"duration": 2, "spans": [[0, 2]], "cameraBlur": 0.6},
 "session": "$dir", "output": "$dir/export.$kind", "gif": $gif,
 "width": 640, "fps": 30, "aspect": 1.7777, "sourceWidth": 640, "screenStart": 0, "cameraStart": 0}
JSON
rm -f "$dir/export.$kind"
# The editor starts its exporter with exactly these.
if ! QT_QPA_PLATFORM=offscreen QSG_RHI_BACKEND=opengl QT_QUICK_BACKEND=rhi \
    timeout 180 "$omacap" --export "$dir/export.json" > "$dir/export.log" 2>&1; then
  cat "$dir/export.log" >&2
  echo "FAIL: the $kind export did not finish" >&2
  exit 1
fi
grep -q '"event":"done"' "$dir/export.log"
frames=$(ffprobe -v error -select_streams v:0 -count_frames -show_entries stream=nb_read_frames \
  -of csv=p=0 "$dir/export.$kind")
[[ $frames -eq 60 ]] || { echo "FAIL: $frames frames in the $kind export, not 60" >&2; exit 1; }
echo "PASS: $kind export with a blurred camera, $frames frames"
