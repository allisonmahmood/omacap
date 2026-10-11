#!/usr/bin/python
"""OmaCap's recording worker. Records one monitor through the screen-cast portal, cropped to
the picked area before encoding, plus the chosen microphone, desktop audio and camera.

JSON lines: events on stdout, commands on stdin. Times are CLOCK_MONOTONIC nanoseconds.
  events    token{monitor, token}   the portal's restore token for the monitor it shared
            streaming               every source delivers; the countdown may start
            recording               the first clean screen frame was kept
            stopping                nothing after the stop time will be kept
            error{message}          finished{frames}
  commands  start{after}   keep frames the compositor rendered after `after`, from the first one
            stop{at}       keep nothing captured after `at` (default now), then finish

Every track starts at the first kept screen frame and is saved relative to it, so the file
holds nothing from before `start`. Encoded buffers wait HOLD before reaching the file, so a stop
can still drop frames that passed the start of the pipeline before it arrived. SIGINT and
SIGTERM stop now; stdin closing does too. Once the parent (the video process) dies, nothing
more is kept, even before the main loop runs. The screen must keep the size the crop was made
for: a stream that changes size ends the recording before any frame of the new size is kept,
and one that breaks off (XDPH's does when the monitor's mode changes) ends it too; either way
the file keeps what was recorded before.

The recorded monitor must also keep the shape OmaCap placed its chrome for. A thread asks
Hyprland about it every POLL seconds (--display is how it was when recording was requested), and
a screen frame reaches the file only once an answer asked for after the frame was rendered
still matched. An answer that differs ends the recording before the change. A stop drains what
is held back only once an answer has caught up with its cut. A microphone, desktop audio or
camera that fails ends the recording too, keeping every track up to then.
"""

import argparse, json, os, signal, socket, subprocess, sys, threading, time
from pathlib import Path
import dbus, dbus.mainloop.glib
import gi

gi.require_version("Gst", "1.0")
from gi.repository import GLib, Gst

p = argparse.ArgumentParser()
p.add_argument("--output", required=True)
p.add_argument("--monitor", default="", help="Hyprland name of the monitor to record")
p.add_argument("--size", default="", help="the monitor's native WxH, to validate the stream")
p.add_argument("--crop", default="", help="x,y,w,h in the monitor's native pixels")
p.add_argument("--display", default="", help="the monitor's Display.FIELDS as JSON, to watch")
p.add_argument("--token", default="", help="restore token from an earlier recording")
p.add_argument("--mic", default="")
p.add_argument("--desktop", action="store_true")
p.add_argument("--camera", default="")
p.add_argument("--synthetic", action="store_true", help="test sources instead of devices")
p.add_argument("--seconds", type=int, default=0, help="start at once, stop after this long")
p.add_argument("--break-after", default="", help="synthetic: SOURCE:SECONDS into the recording, it fails")
Gst.init(None)
dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
loop = GLib.MainLoop()
bus = None
portal = None
pipeline = None
session = None
fd = None
request_path = None
finished = False
stopping = False
drained = False
broken = set()  # sources that failed mid-recording; drain() ends their tracks
streaming = False
started = set()
count = 0
exitcode = 0
timeout_id = None
# Reopening a saved recording finds its audio tracks by these titles. Keep them
# single words: matroskamux stores tag values containing spaces escaped and quoted.
TRACK_TITLES = {"mic": "Microphone", "desktop": "Desktop"}
OFFSET_NONE = Gst.BUFFER_OFFSET_NONE
# Videos stay at most 4K; larger areas are scaled down after cropping.
MAX_WIDTH, MAX_HEIGHT = 3840, 2160
# How long encoded buffers wait before the file. A stop's `at` may lie this far in the past.
HOLD = Gst.SECOND
# How often Hyprland is asked about the recorded monitor, and how long an answer may take, in
# seconds. A screen frame needs an answer asked for after it before it leaves the hold, so HOLD
# is comfortably longer than both together.
POLL, POLL_TIMEOUT = 0.25, 0.3
# How long a stop waits for an answer newer than its cut before cutting at the last one.
SETTLE = Gst.SECOND
# The sources besides the screen, as a failure names them.
DEVICES = {"mic": "microphone", "desktop": "desktop audio", "camera": "camera"}
# The video process that started this worker; once it dies, this process is re-parented.
parent = os.getppid()


def emit(event, **data):
    try:
        print(json.dumps(dict(event=event, **data)), flush=True)
    except BrokenPipeError:
        # Nobody reads any more (the parent died); finishing the file still matters.
        os.dup2(os.open(os.devnull, os.O_WRONLY), sys.stdout.fileno())


def now():
    return time.clock_gettime_ns(time.CLOCK_MONOTONIC)


class Cut:
    """Which buffers are saved. The screen opens the cut at the first fresh frame stamped after
    `after`; other tracks keep what falls between that frame and `until`.

    Screen timestamps are the compositor's own (XDPH stamps each frame when Hyprland copies
    it), so a frame stamped after `after` was rendered after the chrome was hidden. Repeated
    frames keep the sequence number of the frame they copy and never open the cut.

    The screen's streaming thread opens and closes the cut; `end` may come from any thread."""

    def __init__(self):
        self.after = None
        self.epoch = None
        self.until = None
        self.newest = -1
        self.last = None  # the newest screen frame kept
        self.lock = threading.Lock()

    def screen(self, pts, seq, opening=None):
        """Whether to keep a screen frame. `opening(epoch)` runs just before the cut opens,
        while every other track still drops all it gets."""
        fresh = seq == OFFSET_NONE or seq > self.newest
        if seq != OFFSET_NONE:
            self.newest = max(self.newest, seq)
        if not self.keeps(pts):
            return False
        if self.epoch is None:
            if self.after is None or not fresh or pts <= self.after:
                return False
            if opening:
                opening(pts)
            self.epoch = pts
        self.last = pts
        return True

    def keeps(self, pts):
        until = self.until
        return until is None or pts <= until

    def track(self, pts):
        return self.epoch is not None and self.epoch <= pts and self.keeps(pts)

    def end(self, at):
        """Keep nothing after `at`. An earlier end stands."""
        with self.lock:
            self.until = at if self.until is None else min(self.until, at)

    def close(self):
        """Keep no screen frame after the last one kept, and none at all if none was."""
        self.end(-1 if self.last is None else self.last)


cut = Cut()


class Display:
    """The recorded monitor as Hyprland describes it, watched from a thread of its own.

    `verified` is when the newest answer that still matched the baseline was asked for, so the
    monitor was unchanged until then; frame timestamps are on the same clock. An answer that
    differs, or a monitor Hyprland no longer lists, ends the recording at `verified`. One that
    fails or comes late leaves `verified` where it was, so frames since wait for a newer one.
    Hyprland sends no event when a monitor's scale, transform or mode changes, hence the polling."""

    FIELDS = ("width", "height", "scale", "transform", "x", "y", "mirrorOf", "disabled")

    def __init__(self, name="", baseline=None):
        self.name = name
        self.baseline = baseline  # None: nothing to watch (synthetic sources)
        self.verified = None
        self.changed = False
        self.wake = threading.Event()  # asks again at once

    def covers(self, pts):
        """Whether the monitor was seen unchanged after `pts`."""
        verified = self.verified
        return self.baseline is None or (verified is not None and pts <= verified)

    def cut_back(self):
        """Keeps nothing the watch hasn't covered."""
        verified = self.verified
        cut.end(-1 if verified is None else verified)

    def state(self):
        """The monitor's FIELDS, {} when Hyprland doesn't list it, None when it didn't answer."""
        path = os.path.join(os.environ.get("XDG_RUNTIME_DIR", ""), "hypr",
                            os.environ.get("HYPRLAND_INSTANCE_SIGNATURE", ""), ".socket.sock")
        try:
            with socket.socket(socket.AF_UNIX) as hyprland:
                hyprland.settimeout(POLL_TIMEOUT)
                hyprland.connect(path)
                hyprland.sendall(b"j/monitors")  # what `hyprctl -j monitors` asks
                monitors = json.loads(b"".join(iter(lambda: hyprland.recv(1 << 16), b"")))
        except (OSError, ValueError):
            return None
        if not isinstance(monitors, list):
            return None
        monitor = next((m for m in monitors if isinstance(m, dict) and m.get("name") == self.name), None)
        return {} if monitor is None else {k: monitor.get(k) for k in self.FIELDS}

    def watch(self):
        while not finished:
            asked = now()
            state = self.state()
            if state == self.baseline:
                self.verified = asked
            elif state is not None:
                self.changed = True
                self.cut_back()
                GLib.idle_add(interrupt, "The display changed, so OmaCap stopped recording.")
                return
            self.wake.wait(POLL)
            self.wake.clear()


display = Display()


def finish(error=""):
    global finished, exitcode
    if finished:
        return False
    finished = True
    exitcode = 1 if error else 0
    if error:
        emit("error", message=error)

    def forced():
        os._exit(exitcode)

    guard = threading.Timer(5, forced)
    guard.daemon = True
    guard.start()
    if pipeline:
        pipeline.set_state(Gst.State.NULL)
    if session:
        try:
            dbus.Interface(
                bus.get_object("org.freedesktop.portal.Desktop", session),
                "org.freedesktop.portal.Session",
            ).Close(timeout=2)
        except dbus.DBusException:
            pass
    elif request_path:
        try:
            dbus.Interface(
                bus.get_object("org.freedesktop.portal.Desktop", request_path),
                "org.freedesktop.portal.Request",
            ).Close(timeout=2)
        except dbus.DBusException:
            pass
    if fd is not None:
        os.close(fd)
    # A cancelled countdown, or a cut before the first frame, records nothing; don't leave a
    # header-only file behind.
    if not count:
        output.unlink(missing_ok=True)
    emit("finished", frames=count)
    loop.quit()
    guard.cancel()
    return False


def stop(at=None):
    global stopping
    cut.end(now() if at is None else at)
    if stopping:
        return False
    stopping = True
    emit("stopping")
    if pipeline:
        settle(drain)
    else:
        finish()
    return False


def settle(then):
    """Runs `then` once the display watch has caught up with the cut's end, or the cut has gone
    back to where the watch got: what is held back is never written unwatched."""
    display.wake.set()
    deadline = now() + SETTLE

    def check():
        if not display.covers(cut.until) and not display.changed:
            if now() < deadline:
                return True
            display.cut_back()
        then()
        return False

    if check():
        GLib.timeout_add(10, check)


def drain():
    """Ends every track, so what is held back reaches the file and the file is finished."""
    global drained
    drained = True
    for source in broken:
        end_track(source)
    pipeline.send_event(Gst.Event.new_eos())
    GLib.timeout_add_seconds(
        6,
        lambda: finish("Recording finalization timed out; recoverable media was retained."),
    )


def end_track(source):
    """Ends a track whose source broke off and so can't end it."""
    gate = pipeline.get_by_name(f"{source}_gate").get_static_pad("sink")
    gate.send_event(Gst.Event.new_eos())


def begin(after):
    cut.after = after
    return False


def command(line):
    try:
        message = json.loads(line)
    except ValueError:
        return
    if message.get("cmd") == "start":
        begin(int(message["after"]))
    elif message.get("cmd") == "stop":
        stop(int(message["at"]) if "at" in message else None)


def read_commands(source, _condition):
    line = sys.stdin.readline()
    if not line:
        GLib.idle_add(stop)
        return False
    command(line)
    return True


def interrupted(*_):
    # The cut ends here, as soon as Python runs the handler; the rest of the stop follows from
    # the main loop.
    cut.end(now())
    GLib.idle_add(stop)


signal.signal(signal.SIGINT, interrupted)
signal.signal(signal.SIGTERM, interrupted)
abandoned = threading.Event()


def orphaned():
    """Whether the parent died. The first streaming thread to notice ends the cut, so nothing
    later is kept however busy the main loop is, and stops."""
    if os.getppid() == parent:
        return False
    if not abandoned.is_set():
        abandoned.set()
        cut.end(now())
        GLib.idle_add(stop)
    return True


def interrupt(message):
    """Ends the recording early, keeping what the cut kept so far, and says why."""
    if not stopping and not finished:
        emit("error", message=message)
        stop()
    return False


def request(name, values, options, done, timeout=25):
    global request_path, timeout_id
    token = f"omacap_{os.getpid()}_{name}"
    request_path = (
        "/org/freedesktop/portal/desktop/request/"
        + bus.get_unique_name()[1:].replace(".", "_")
        + "/"
        + token
    )

    def response(code, result):
        global timeout_id
        match.remove()
        if timeout_id:
            GLib.source_remove(timeout_id)
            timeout_id = None
        if code:
            # Closing the portal's picker is a cancel, not an error.
            finish("" if code == 1 else "The desktop portal rejected recording.")
        else:
            done(result)

    match = bus.add_signal_receiver(
        response,
        signal_name="Response",
        dbus_interface="org.freedesktop.portal.Request",
        path=request_path,
    )
    options["handle_token"] = token
    timeout_id = GLib.timeout_add_seconds(
        timeout,
        lambda: finish(
            f"The desktop screen-sharing portal did not respond during {name}. Close other sharing pickers and try again; signing out and back in may restore the portal."
        ),
    )
    getattr(portal, name)(
        *values,
        dbus.Dictionary(options, signature="sv"),
        reply_handler=lambda _: None,
        error_handler=lambda e: finish(str(e)),
        timeout=timeout,
    )


def created(result):
    global session
    session = result["session_handle"]
    # Monitors only, persistent: a valid restore token skips XDPH's picker.
    options = {
        "types": dbus.UInt32(1),
        "multiple": False,
        "cursor_mode": dbus.UInt32(2),
        "persist_mode": dbus.UInt32(2),
    }
    if a.token:
        options["restore_token"] = a.token
    request(
        "SelectSources",
        [session],
        options,
        lambda _: request("Start", [session, ""], {}, shared, 600),
    )


def stream_problem(props, monitor, size):
    """Why the shared stream isn't the requested monitor, or None. XDPH ignores `types` when
    it restores a token, and its picker can return any monitor or window."""
    if int(props.get("source_type", 0)) != 1:
        return "The screen-sharing picker shared a window. Choose the display to record."
    shared = str(props.get("mapping_id", ""))
    if shared != monitor:
        return f"The screen-sharing picker shared {shared or 'another display'}, not {monitor}. Record again and choose {monitor}."
    if size and "size" in props:
        width, height = (int(v) for v in props["size"])
        if abs(width - size[0]) > 2 or abs(height - size[1]) > 2:
            return f"{monitor} is shared at {width}×{height}, not {size[0]}×{size[1]}. Record again."
    return None


def shared(result):
    global fd
    node, props = result["streams"][0]
    # Always replace the stored token: the portal may issue a new one each time.
    emit(
        "token",
        monitor=str(props.get("mapping_id", "")),
        token=str(result.get("restore_token", "")),
    )
    size = tuple(int(v) for v in a.size.split("x")) if a.size else None
    problem = stream_problem(props, a.monitor, size)
    if problem:
        finish(problem)
        return
    fd = portal.OpenPipeWireRemote(
        session, dbus.Dictionary({}, signature="sv"), timeout=10
    ).take()
    start(node, props)


def source_of(element):
    """The source whose branch holds `element` ("screen", "mic", …), from the names start()
    gives; None for what the tracks share, such as the muxer."""
    while element is not None:
        if (name := element.get_name().split("_")[0]) in ("screen", *DEVICES):
            return name
        element = element.get_parent()
    return None


def message(_, msg):
    if msg.type == Gst.MessageType.EOS:
        finish()
        return
    if msg.type != Gst.MessageType.ERROR:
        return
    err, _ = msg.parse_error()
    source = source_of(msg.src)
    if source is None or cut.epoch is None:
        finish(str(err))
        return
    # A source broke off mid-recording. What was recorded is kept: the recording stops as on a
    # stop, and the broken track ends where its source did.
    broken.add(source)
    if drained:
        end_track(source)
    if source == "screen":
        # XDPH's stream breaks off when the monitor's mode changes, and the held frames may
        # already show the change: the stop drains them once the display watch has caught up.
        cut.close()
        interrupt(
            "The screen stream ended, as it does when the display changes, so OmaCap stopped recording."
        )
    else:
        interrupt(f"The {DEVICES[source]} stopped working, so OmaCap stopped recording.")


def opening(epoch):
    # Every track is saved relative to the first kept frame. The offsets are in place before
    # the cut opens, so no track passes a buffer with the old mapping.
    for gate in gates:
        gate.get_static_pad("src").set_offset(-epoch)
    GLib.idle_add(lambda: emit("recording") and False)


def screen_gate(pad, info, _):
    buf = info.get_buffer()
    first(pad, "screen", buf)
    if orphaned() or not cut.screen(buf.pts, buf.offset, opening):
        return Gst.PadProbeReturn.DROP
    return Gst.PadProbeReturn.OK


def track_gate(pad, info, name):
    buf = info.get_buffer()
    first(pad, name, buf)
    if orphaned() or not cut.track(buf.pts):
        return Gst.PadProbeReturn.DROP
    return Gst.PadProbeReturn.OK


def screen_events(pad, info, size):
    """Keeps the screen stream's renegotiations from the rest of the pipeline.

    videocrop's margins were set for the monitor's size: a larger frame would let pixels
    outside the area in. A stream that changes size keeps nothing more and stops; the new caps
    never reach videocrop. pipewiresrc flushes when XDPH renegotiates, as when the monitor's
    mode changes; a flush would empty the hold and the muxer and truncate the file."""
    event = info.get_event()
    if event.type in (Gst.EventType.FLUSH_START, Gst.EventType.FLUSH_STOP):
        return Gst.PadProbeReturn.DROP
    if event.type != Gst.EventType.CAPS:
        return Gst.PadProbeReturn.OK
    caps = event.parse_caps().get_structure(0)
    if (caps.get_value("width"), caps.get_value("height")) == size:
        return Gst.PadProbeReturn.OK
    cut.close()
    GLib.idle_add(interrupt, "The display changed, so OmaCap stopped recording.")
    return Gst.PadProbeReturn.DROP


def written(pad, info, name):
    """After the hold: drops what a later cut excludes, and counts the screen frames saved. A
    screen frame the display watch hasn't caught up with ends the recording before it: the
    watch has stalled, and the frame might show the chrome moved into the area."""
    global count
    buf = info.get_buffer()
    if buf.pts != Gst.CLOCK_TIME_NONE:
        if name == "screen" and cut.keeps(buf.pts) and not display.covers(buf.pts):
            display.cut_back()
            GLib.idle_add(interrupt, "OmaCap couldn't check the display, so it stopped recording.")
        if not cut.keeps(buf.pts):
            return Gst.PadProbeReturn.DROP
    if name == "screen":
        count += 1
    return Gst.PadProbeReturn.OK


def first(pad, name, buf):
    global streaming
    if name in started:
        return
    started.add(name)
    # Screen frames must carry compositor time, or the cut means nothing.
    if name == "screen" and abs(buf.pts - now()) > 5 * Gst.SECOND:
        GLib.idle_add(lambda: finish("The screen stream has no usable timestamps."))
    if len(started) == len(gates) and not streaming:
        streaming = True
        GLib.idle_add(ready)


def ready():
    emit("streaming")
    if a.seconds:
        begin(now())
        GLib.timeout_add_seconds(a.seconds, stop)
    return False


def watchdog():
    if not streaming and not finished:
        finish(
            "A selected audio or camera device did not become ready. Check the device and try again."
        )
    return False


def crop_box(width, height):
    """videocrop's left, top, right, bottom for --crop, clamped to the frame."""
    if not a.crop:
        return 0, 0, 0, 0
    x, y, w, h = (int(v) for v in a.crop.split(","))
    x, y = max(0, min(x, width - 2)), max(0, min(y, height - 2))
    w, h = max(2, min(w, width - x)), max(2, min(h, height - y))
    return x, y, width - x - w, height - y - h


def encoded_size(width, height):
    """The cropped area, scaled down to fit 4K, with even sides."""
    scale = min(1, MAX_WIDTH / width, MAX_HEIGHT / height)
    return max(2, int(width * scale) // 2 * 2), max(2, int(height * scale) // 2 * 2)


def desktop_monitor():
    result = subprocess.run(
        ["pactl", "get-default-sink"], capture_output=True, text=True, timeout=3
    )
    if result.returncode != 0 or not result.stdout.strip():
        raise RuntimeError("Could not find the desktop audio output.")
    return result.stdout.strip() + ".monitor"


def fault(source):
    """With --break-after naming `source`, where it will fail (see breaks)."""
    return f" ! identity name={source}_fault" if a.break_after.startswith(source + ":") else ""


def breaks(source, seconds):
    """--break-after: a probe that fails `source` `seconds` after `start`, as a stream that
    breaks off or an unplugged device does, and passes nothing from it after."""
    failed = False

    def probe(pad, info):
        nonlocal failed
        after = cut.after
        if not failed and after is not None and info.get_buffer().pts > after + seconds * Gst.SECOND:
            failed = True
            element = pipeline.get_by_name(source)
            problem = GLib.Error.new_literal(
                Gst.ResourceError.quark(), "Broken off", int(Gst.ResourceError.READ))
            element.post_message(Gst.Message.new_error(element, problem, "--break-after"))
        return Gst.PadProbeReturn.DROP if failed else Gst.PadProbeReturn.OK

    return probe


gates = []


def start(node=None, props=None):
    global pipeline
    if a.synthetic:
        width, height = 1280, 720
        source = f"videotestsrc name=screen is-live=true pattern=smpte ! video/x-raw,width={width},height={height},framerate=30/1{fault('screen')}"
    else:
        width, height = (int(v) for v in props.get("size", (1920, 1080)))
        target = (
            f"target-object={int(props['pipewire-serial'])}"
            if "pipewire-serial" in props
            else f"path={int(node)}"
        )
        # Not live, so GStreamer keeps XDPH's frame timestamps instead of restamping them on
        # arrival; the cut depends on them.
        source = f"pipewiresrc name=screen fd={fd} {target} do-timestamp=false stream-properties=props,stream.is-live=false use-bufferpool=false always-copy=true min-buffers=8 max-buffers=16 on-disconnect=error keepalive-time=100 resend-last=true"
    left, top, right, bottom = crop_box(width, height)
    hold = f"queue max-size-buffers=0 max-size-bytes=0 max-size-time={5 * HOLD} min-threshold-time={HOLD}"
    out_width, out_height = encoded_size(width - left - right, height - top - bottom)
    # Element properties are assigned below; device names and output paths never enter pipeline syntax.
    enc = (
        "nvh264enc preset=p1 bitrate=12000 gop-size=60 ! h264parse"
        if not a.synthetic and Gst.ElementFactory.find("nvh264enc")
        else "videoconvert ! vp8enc deadline=1 cpu-used=6 threads=4 target-bitrate=12000000"
    )
    desc = (
        "matroskamux name=mux ! filesink name=output async=false "
        f"{source} ! identity name=screen_gate ! queue max-size-buffers=3 max-size-bytes=0 max-size-time=0 ! "
        f"videocrop left={left} top={top} right={right} bottom={bottom} ! videoconvert ! videoscale ! "
        f'capsfilter caps="video/x-raw,format=NV12,width={out_width},height={out_height},pixel-aspect-ratio=1/1" ! {enc} ! {hold} name=screen_hold ! mux.video_0 '
    )
    if a.camera:
        cam = (
            f"videotestsrc name=camera is-live=true pattern=ball ! video/x-raw,width=640,height=480,framerate=30/1{fault('camera')}"
            if a.synthetic
            else "v4l2src name=camera do-timestamp=true ! decodebin name=camera_decode"
        )
        desc += f"{cam} ! identity name=camera_gate ! queue ! videoconvert ! videoscale ! video/x-raw,format=I420,width=640,height=480 ! vp8enc deadline=1 cpu-used=8 threads=2 ! {hold} name=camera_hold ! mux.video_1 "
    for i, name in enumerate(n for n in ["mic", "desktop"] if getattr(a, n)):
        source = (
            f"audiotestsrc name={name} is-live=true freq={440 + i * 440}{fault(name)}"
            if a.synthetic
            else f"pulsesrc name={name} do-timestamp=true"
        )
        desc += f"{source} ! identity name={name}_gate ! queue ! audioconvert ! audioresample ! audio/x-raw,rate=48000 ! opusenc ! taginject name={name}_title ! {hold} name={name}_hold ! mux.audio_{i} "
    try:
        pipeline = Gst.parse_launch(desc)
        # Running time is CLOCK_MONOTONIC, the clock the compositor and OmaCap stamp with.
        clock = Gst.SystemClock.obtain()
        clock.set_property("clock-type", Gst.ClockType.MONOTONIC)
        pipeline.use_clock(clock)
        pipeline.set_start_time(Gst.CLOCK_TIME_NONE)
        pipeline.set_base_time(0)
        pipeline.get_by_name("output").set_property("location", str(output))
        if a.mic and not a.synthetic:
            pipeline.get_by_name("mic").set_property("device", a.mic)
        if a.desktop and not a.synthetic:
            pipeline.get_by_name("desktop").set_property("device", desktop_monitor())
        if a.camera and not a.synthetic:
            pipeline.get_by_name("camera").set_property("device", a.camera)
        for name in ["screen", "camera", "mic", "desktop"]:
            gate = pipeline.get_by_name(f"{name}_gate")
            if gate:
                gates.append(gate)
                gate.get_static_pad("src").add_probe(
                    Gst.PadProbeType.BUFFER,
                    screen_gate if name == "screen" else track_gate,
                    name,
                )
                pipeline.get_by_name(f"{name}_hold").get_static_pad("src").add_probe(
                    Gst.PadProbeType.BUFFER, written, name
                )
        pipeline.get_by_name("screen_gate").get_static_pad("src").add_probe(
            Gst.PadProbeType.EVENT_DOWNSTREAM | Gst.PadProbeType.EVENT_FLUSH,
            screen_events,
            (width, height),
        )
        if a.break_after:
            source, _, seconds = a.break_after.partition(":")
            pipeline.get_by_name(f"{source}_fault").get_static_pad("src").add_probe(
                Gst.PadProbeType.BUFFER, breaks(source, float(seconds)))
        for name, title in TRACK_TITLES.items():
            element = pipeline.get_by_name(f"{name}_title")
            if element:
                element.set_property("tags", f'title="{title}"')
        gstbus = pipeline.get_bus()
        gstbus.add_signal_watch()
        gstbus.connect("message", message)
        pipeline.set_state(Gst.State.PLAYING)
        GLib.timeout_add_seconds(15, watchdog)
    except Exception as e:
        finish(str(e))


def main():
    global a, output, bus, portal, display
    a = p.parse_args()
    output = Path(a.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    if a.display:
        display = Display(a.monitor, json.loads(a.display))
        threading.Thread(target=display.watch, daemon=True).start()
    GLib.io_add_watch(sys.stdin, GLib.IO_IN | GLib.IO_HUP, read_commands)
    try:
        if a.synthetic:
            start()
        else:
            bus = dbus.SessionBus()
            portal = dbus.Interface(
                bus.get_object(
                    "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop"
                ),
                "org.freedesktop.portal.ScreenCast",
            )
            request(
                "CreateSession",
                [],
                {"session_handle_token": f"omacap_session_{os.getpid()}"},
                created,
            )
        if not finished:
            loop.run()
    except Exception as e:
        finish(str(e))
    return exitcode


if __name__ == "__main__":
    sys.exit(main())
