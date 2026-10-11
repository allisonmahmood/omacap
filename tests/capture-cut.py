#!/usr/bin/python
"""The recording worker keeps exactly the frames between start and stop, on every track."""

import importlib.util
import json
import subprocess
import time
from pathlib import Path

repo = Path(__file__).resolve().parents[1]
out = repo / "tests/out"
out.mkdir(exist_ok=True)
spec = importlib.util.spec_from_file_location("capture", repo / "scripts/capture.py")
capture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(capture)

# The cut opens at the first fresh frame stamped after `after`, never on a repeated frame.
cut = capture.Cut()
assert not cut.screen(100, 0) and not cut.track(100)  # before start
cut.after = 150
assert not cut.screen(140, 1)  # rendered before the chrome was hidden
assert not cut.screen(160, 1)  # a keepalive copy of frame 1, restamped after
assert cut.screen(170, 2) and cut.epoch == 170
assert not cut.track(165) and cut.track(170)
cut.until = 300
assert cut.screen(300, 3) and not cut.screen(310, 4) and not cut.track(301)

capture.a = capture.p.parse_args(["--output", "x", "--crop", "100,50,640,480"])
assert capture.crop_box(1920, 1080) == (100, 50, 1180, 550)
capture.a.crop = "1800,1000,640,480"  # clamped to the frame
assert capture.crop_box(1920, 1080) == (1800, 1000, 0, 0)
assert capture.encoded_size(5120, 1440) == (3840, 1080)
assert capture.encoded_size(1021, 767) == (1020, 766)

monitor = {"source_type": 1, "mapping_id": "DP-3", "size": (1920, 1080)}
assert capture.stream_problem(monitor, "DP-3", (1920, 1080)) is None
assert "DP-1" in capture.stream_problem(monitor | {"mapping_id": "DP-1"}, "DP-3", None)
assert "window" in capture.stream_problem(monitor | {"source_type": 2}, "DP-3", None)
assert capture.stream_problem(monitor, "DP-3", (2560, 1440))

# A synthetic recording: every track starts at the first kept frame, and the file ends at stop.
output = out / "cut.mkv"
output.unlink(missing_ok=True)
worker = subprocess.Popen(
    ["/usr/bin/python", str(repo / "scripts/capture.py"), "--synthetic", "--output", str(output),
     "--mic", "test", "--desktop", "--camera", "test", "--crop", "100,60,640,360"],
    stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True,
)


def expect(event):
    while True:
        line = worker.stdout.readline()
        assert line, f"worker exited before {event}"
        message = json.loads(line)
        assert message["event"] != "error", message
        if message["event"] == event:
            return message


def send(**command):
    worker.stdin.write(json.dumps(command) + "\n")
    worker.stdin.flush()


expect("streaming")
start = time.clock_gettime_ns(time.CLOCK_MONOTONIC) + 500_000_000
send(cmd="start", after=start)
expect("recording")
time.sleep((start + 1_000_000_000 - time.clock_gettime_ns(time.CLOCK_MONOTONIC)) / 1e9)
send(cmd="stop")
expect("stopping")
expect("finished")
assert worker.wait(timeout=10) == 0
probe = json.loads(subprocess.check_output(
    ["ffprobe", "-v", "error", "-show_streams", "-show_format", "-of", "json", str(output)]))
streams = probe["streams"]
assert [s["codec_type"] for s in streams] == ["video", "video", "audio", "audio"], streams
starts = [float(s["start_time"]) for s in streams]
assert starts[0] == 0 and max(starts) < 0.05, starts
assert (streams[0]["width"], streams[0]["height"]) == (640, 360), streams[0]
duration = float(probe["format"]["duration"])
assert 0.9 < duration < 1.1, duration
# A countdown cancelled before start leaves no file.
cancelled = out / "cancelled-countdown.mkv"
worker = subprocess.Popen(
    ["/usr/bin/python", str(repo / "scripts/capture.py"), "--synthetic", "--output", str(cancelled)],
    stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True,
)
expect("streaming")
send(cmd="stop")
expect("finished")
assert worker.wait(timeout=10) == 0 and not cancelled.exists()
print(f"PASS: worker cut {duration:.3f}s, track starts {starts}")
