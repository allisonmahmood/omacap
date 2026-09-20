#!/usr/bin/python
"""Measure decoded camera/audio pulses after cuts, rather than mirroring edit logic."""

import json, subprocess
from pathlib import Path
import numpy as np

p = "tests/out/delayed-export.mp4"


def decode(args):
    return subprocess.check_output(["ffmpeg", "-v", "error", "-i", p, *args])


a = np.frombuffer(
    decode(["-vn", "-ac", "1", "-ar", "48000", "-f", "f32le", "pipe:1"]),
    dtype=np.float32,
)
a = a[: len(a) // 480 * 480].reshape(-1, 480)
a = np.sqrt(np.mean(a * a, axis=1))
v = (
    np.frombuffer(
        decode(
            ["-an", "-vf", "crop=4:4:562:312,format=gray", "-f", "rawvideo", "pipe:1"]
        ),
        dtype=np.uint8,
    )
    .reshape(-1, 16)
    .mean(1)
)


def onsets(x, threshold, fps):
    active = x > threshold
    return np.flatnonzero(active & ~np.r_[False, active[:-1]]) / fps


av = onsets(a, 0.1, 100)
vv = onsets(v, 220, 30)
assert len(av) == 3 and len(vv) == 3, (av, vv)
expected = np.array([0.9, 2.4, 3.4])
assert np.max(np.abs(av - expected)) <= 1 / 30, ("audio timing", av)
assert np.max(np.abs(vv - expected)) <= 1 / 30, ("camera timing", vv)
error = float(max(min(abs(x - av)) for x in vv))
result = {
    "audio_pulses_s": av.tolist(),
    "camera_pulses_s": vv.tolist(),
    "max_alignment_error_s": error,
}
Path("tests/out/delayed-sync.json").write_text(json.dumps(result, indent=2))
print(json.dumps(result))
assert error <= 1 / 30, result
