#!/usr/bin/python
"""The recording worker keeps exactly the frames between start and stop, on every track."""

import importlib.util
import json
import subprocess
import threading
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
cut.end(400)  # an earlier end stands
assert cut.until == 300

# Other tracks only pass buffers once every offset is in place.
cut = capture.Cut()
cut.after = 0
seen = []
assert cut.screen(5, 1, lambda epoch: seen.append((epoch, cut.epoch, cut.track(5))))
assert seen == [(5, None, False)] and cut.track(5)

# A screen stream that changes size keeps nothing more, and never passes the new caps on.
Gst, OK, DROP = capture.Gst, capture.Gst.PadProbeReturn.OK, capture.Gst.PadProbeReturn.DROP


class Info:
    def __init__(self, width, height):
        self.event = Gst.Event.new_caps(
            Gst.Caps.from_string(f"video/x-raw,width={width},height={height}"))

    def get_event(self):
        return self.event


capture.cut = capture.Cut()
capture.cut.after = 0
assert capture.screen_events(None, Info(1920, 1080), (1920, 1080)) == OK
assert capture.cut.screen(10, 1)
assert capture.screen_events(None, Info(2560, 1440), (1920, 1080)) == DROP
assert not capture.cut.screen(20, 2) and not capture.cut.track(11) and capture.cut.until == 10
capture.cut = capture.Cut()  # a change before the first frame keeps nothing at all
capture.cut.after = 0
assert capture.screen_events(None, Info(1280, 720), (1920, 1080)) == DROP
assert not capture.cut.screen(20, 1) and capture.cut.epoch is None
# XDPH's renegotiation flushes; a flush would empty what is held back and truncate the file.
for flush in [Gst.Event.new_flush_start(), Gst.Event.new_flush_stop(True)]:
    info = Info(1920, 1080)
    info.event = flush
    assert capture.screen_events(None, info, (1920, 1080)) == DROP

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


def now():
    return time.clock_gettime_ns(time.CLOCK_MONOTONIC)


def sleep_until(at):
    time.sleep(max(0, (at - now()) / 1e9))


def duration_of(path):
    return float(json.loads(subprocess.check_output(
        ["ffprobe", "-v", "error", "-show_format", "-of", "json", str(path)]))["format"]["duration"])


def exits(process, timeout):
    """Whether the worker behind `process`'s pipes exits (closes stdout) within `timeout`."""
    reader = threading.Thread(target=process.stdout.read, daemon=True)
    reader.start()
    reader.join(timeout)
    return not reader.is_alive()


# A stop can name a moment the worker already passed: frames encoded since are still dropped.
late = out / "late-stop.mkv"
late.unlink(missing_ok=True)
worker = subprocess.Popen(
    ["/usr/bin/python", str(repo / "scripts/capture.py"), "--synthetic", "--output", str(late)],
    stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True,
)
expect("streaming")
start = now() + 300_000_000
send(cmd="start", after=start)
expect("recording")
sleep_until(start + 1_600_000_000)
send(cmd="stop", at=start + 1_000_000_000)
expect("finished")
assert worker.wait(timeout=10) == 0
late_duration = duration_of(late)
assert 0.9 < late_duration < 1.1, late_duration

# Once the parent dies nothing more is kept, and the worker stops by itself: here neither a
# signal nor stdin closing tells it, only being re-parented.
orphan = out / "orphan.mkv"
orphan.unlink(missing_ok=True)
worker = subprocess.Popen(
    ["/usr/bin/python", "-c",
     "import subprocess, sys, time; subprocess.Popen(sys.argv[1:]); time.sleep(60)",
     "/usr/bin/python", str(repo / "scripts/capture.py"), "--synthetic", "--output", str(orphan)],
    stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True,
)
expect("streaming")
start = now() + 300_000_000
send(cmd="start", after=start)
expect("recording")
sleep_until(start + 1_000_000_000)
worker.kill()
killed = now()
expect("stopping")
expect("finished")
assert exits(worker, 10), "the orphaned worker kept running"
orphan_duration = duration_of(orphan)
assert abs(orphan_duration - (killed - start) / 1e9) < 0.1, (orphan_duration, killed - start)
print(f"PASS: worker cut {duration:.3f}s, track starts {starts}; a late stop "
      f"{late_duration:.3f}s, an orphan {orphan_duration:.3f}s")
