#!/usr/bin/python
"""A camera and microphone that start 400 ms after the screen, with matching pulses."""

import subprocess
from pathlib import Path

out = Path("tests/out")
out.mkdir(parents=True, exist_ok=True)


def run(*args):
    subprocess.run(["ffmpeg", "-v", "error", "-y", *args], check=True)


run(
    "-f",
    "lavfi",
    "-i",
    "color=c=black:s=640x360:r=30:d=5",
    "-vf",
    "drawbox=color=white:t=fill:enable='lt(mod(t+0.6,1),0.08)'",
    "-c:v",
    "libx264",
    "-preset",
    "ultrafast",
    str(out / "sync-screen.mkv"),
)
run(
    "-f",
    "lavfi",
    "-i",
    "color=c=black:s=640x480:r=30:d=5",
    "-vf",
    "drawbox=color=white:t=fill:enable='lt(mod(t,1),0.08)'",
    "-c:v",
    "libx264",
    "-preset",
    "ultrafast",
    str(out / "sync-camera.mkv"),
)
run(
    "-f",
    "lavfi",
    "-i",
    r"aevalsrc=if(lt(mod(t\,1)\,0.08)\,0.3*sin(2*PI*1000*t)\,0):s=48000:d=5",
    str(out / "sync-audio.wav"),
)
run(
    "-i",
    str(out / "sync-screen.mkv"),
    "-itsoffset",
    "0.4",
    "-i",
    str(out / "sync-camera.mkv"),
    "-itsoffset",
    "0.4",
    "-i",
    str(out / "sync-audio.wav"),
    "-map",
    "0:v",
    "-map",
    "1:v",
    "-map",
    "2:a",
    "-c:v",
    "copy",
    "-c:a",
    "pcm_s16le",
    str(out / "delayed.mkv"),
)

run(
    "-i",
    str(out / "delayed.mkv"),
    "-map",
    "0",
    "-c",
    "copy",
    "-output_ts_offset",
    "5",
    str(out / "delayed-origin.mkv"),
)

# Identifiable frame content for cut-boundary and low-frame-rate replay checks.
run(
    "-f", "lavfi", "-i", "testsrc2=s=640x360:r=15:d=2",
    "-c:v", "libx264", "-preset", "ultrafast", str(out / "frames15.mkv"),
)
run(
    "-i", str(out / "frames15.mkv"), "-map", "0:v", "-map", "0:v", "-c", "copy",
    str(out / "camera-wide.mkv"),
)

# Wayland only sends changed frames: a still window can have long timestamp gaps.
run(
    "-f", "lavfi", "-i", "testsrc2=s=640x360:r=30:d=4",
    "-vf", "select='lt(t,1)+gte(t,3)'", "-fps_mode", "vfr",
    "-c:v", "libx264", "-preset", "ultrafast", str(out / "sparse.mkv"),
)

run(
    "-i", str(out / "sparse.mkv"), "-map", "0:v", "-c", "copy",
    "-output_ts_offset", "0.023", str(out / "sparse-offset.mkv"),
)
