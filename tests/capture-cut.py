#!/usr/bin/python
"""The recording worker keeps exactly the frames between start and stop, on every track."""

import importlib.util
import json
import os
import socket
import subprocess
import tempfile
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


class Hyprland:
    """A stand-in for Hyprland's socket that answers `j/monitors` with `monitors`, after
    `delay` seconds; `hang` holds every request without an answer."""

    def __init__(self):
        self.runtime = tempfile.mkdtemp(prefix="hypr-")
        self.env = os.environ | {"XDG_RUNTIME_DIR": self.runtime, "HYPRLAND_INSTANCE_SIGNATURE": "test"}
        os.makedirs(f"{self.runtime}/hypr/test")
        self.monitor = {"name": "TEST-1", "width": 1280, "height": 720, "scale": 1, "transform": 0,
                        "x": 0, "y": 0, "mirrorOf": "none", "disabled": False, "focused": True}
        self.monitors, self.delay, self.hang = [self.monitor], 0, False
        self.server = socket.socket(socket.AF_UNIX)
        self.server.bind(f"{self.runtime}/hypr/test/.socket.sock")
        self.server.listen(16)
        threading.Thread(target=self.serve, daemon=True).start()

    def serve(self):
        while True:
            client, _ = self.server.accept()
            threading.Thread(target=self.answer, args=(client, self.monitors, self.delay,
                                                       self.hang), daemon=True).start()

    def answer(self, client, monitors, delay, hang):
        with client:
            if client.recv(64) != b"j/monitors" or hang:
                time.sleep(2)
                return
            time.sleep(delay)
            try:
                client.sendall(json.dumps(monitors).encode())
            except OSError:
                pass  # the worker gave up waiting

    def baseline(self):
        return json.dumps({k: self.monitor[k] for k in capture.Display.FIELDS})


# The watch reads the recorded monitor from Hyprland's socket.
hyprland = Hyprland()
os.environ |= hyprland.env
watched = capture.Display("TEST-1", json.loads(hyprland.baseline()))
assert watched.state() == watched.baseline
assert capture.Display("DP-9", None).state() == {}  # not listed: gone
hyprland.hang = True
assert watched.state() is None  # no answer in time
hyprland.hang = False
os.environ["HYPRLAND_INSTANCE_SIGNATURE"] = "none"
assert watched.state() is None
os.environ |= hyprland.env


def watched_recording(name, *args, change=None, at=1.5):
    """Records against `hyprland` from 0.3 s after streaming. `at` seconds after streaming,
    `change(hyprland)` runs, or else the recording is stopped. Returns the start, that time,
    the errors, the duration and the file."""
    global worker
    path = out / f"{name}.mkv"
    path.unlink(missing_ok=True)
    hyprland.monitors, hyprland.delay, hyprland.hang = [hyprland.monitor], 0, False
    worker = subprocess.Popen(
        ["/usr/bin/python", str(repo / "scripts/capture.py"), "--synthetic", "--output", str(path),
         "--monitor", "TEST-1", "--display", hyprland.baseline(), *args],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, env=hyprland.env,
    )
    expect("streaming")
    streaming = now()
    start = streaming + 300_000_000
    send(cmd="start", after=start)
    expect("recording")
    changed = streaming + int(at * 1e9)
    sleep_until(changed)
    if change:
        change(hyprland)
    else:
        send(cmd="stop")
    events = [json.loads(line) for line in worker.stdout]
    assert worker.wait(timeout=10) == 0, events
    assert events[-1]["event"] == "finished" and events[-1]["frames"] > 0, events
    errors = [e["message"] for e in events if e["event"] == "error"]
    return start, changed, errors, duration_of(path), path


def rescale(hyprland):
    hyprland.monitors = [hyprland.monitor | {"scale": 2}]


# A stop waits for the watch to catch up, then keeps everything up to it.
start, _, errors, kept, _ = watched_recording("watched-stop", at=1.3)
assert not errors and 0.9 < kept < 1.1, (errors, kept)
# A display change ends the recording before it, though frames since have been encoded.
start, changed, errors, kept, _ = watched_recording("display-change", change=rescale)
assert errors == ["The display changed, so OmaCap stopped recording."], errors
assert (changed - start) / 1e9 - 0.5 < kept < (changed - start) / 1e9 + 0.04, (kept, changed - start)


def stall(hyprland):
    hyprland.hang = True


# A watch that gets no answers keeps nothing it hasn't seen.
start, changed, errors, kept, _ = watched_recording("stalled-watch", change=stall)
assert errors == ["OmaCap couldn't check the display, so it stopped recording."], errors
assert kept < (changed - start) / 1e9 + 0.04, (kept, changed - start)


def unseen_change(hyprland):
    rescale(hyprland)
    hyprland.delay = 0.29  # answers just in time, after the screen breaks


# The screen breaks off (as XDPH's does on a mode change) 0.15 s after a change the watch
# hasn't reported yet: what is held back waits for it, so nothing since the change is kept.
start, changed, errors, kept, _ = watched_recording(
    "screen-break", "--break-after", "screen:1.15", change=unseen_change, at=1.3)
assert errors == ["The screen stream ended, as it does when the display changes, so OmaCap "
                  "stopped recording."], errors
assert kept < (changed - start) / 1e9 + 0.04, (kept, changed - start)

# A microphone that fails ends the recording and keeps every track up to then.
start, _, errors, kept, path = watched_recording(
    "mic-break", "--mic", "test", "--desktop", "--camera", "test", "--break-after", "mic:1.2",
    change=lambda _: None)
assert errors == ["The microphone stopped working, so OmaCap stopped recording."], errors
types = [s["codec_type"] for s in json.loads(subprocess.check_output(
    ["ffprobe", "-v", "error", "-show_streams", "-of", "json", str(path)]))["streams"]]
assert types == ["video", "video", "audio", "audio"] and 1.1 < kept < 1.3, (types, kept)
print(f"PASS: worker cut {duration:.3f}s, track starts {starts}; a late stop "
      f"{late_duration:.3f}s, an orphan {orphan_duration:.3f}s; the display watch")
