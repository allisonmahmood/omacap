#!/usr/bin/python
"""Owned recording worker. JSON events on stdout; SIGINT finalizes only this session."""

import argparse, json, os, signal, sys, threading, time
from pathlib import Path
import dbus, dbus.mainloop.glib
import gi

gi.require_version("Gst", "1.0")
from gi.repository import GLib, Gst

p = argparse.ArgumentParser()
p.add_argument("--output", required=True)
p.add_argument("--mic", default="")
p.add_argument("--desktop", default="")
p.add_argument("--camera", default="")
p.add_argument("--synthetic", action="store_true")
p.add_argument("--seconds", type=int, default=0)
p.add_argument("--probe", action="store_true")
a = p.parse_args()
Gst.init(None)
dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
loop = GLib.MainLoop()
bus = None
portal = None
capture_started = time.monotonic()
pipeline = None
session = None
fd = None
request_path = None
finished = False
stopping = False
first = {}
last_frame = time.monotonic()
ready = False
count = 0
exitcode = 0
timeout_id = None
output = Path(a.output)
output.parent.mkdir(parents=True, exist_ok=True)


def emit(event, **data):
    print(json.dumps(dict(event=event, **data)), flush=True)


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
    emit("finished", frames=count, first=first)
    loop.quit()
    return False


def stop(*_):
    global stopping
    if stopping:
        return False
    stopping = True
    if pipeline:
        pipeline.send_event(Gst.Event.new_eos())
        GLib.timeout_add_seconds(
            6,
            lambda: finish(
                "Recording finalization timed out; recoverable media was retained."
            ),
        )
    else:
        finish()
    return False


signal.signal(signal.SIGINT, lambda *_: GLib.idle_add(stop))
signal.signal(signal.SIGTERM, lambda *_: GLib.idle_add(stop))


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
            finish(
                "Sharing was cancelled."
                if code == 1
                else "The desktop portal rejected recording."
            )
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
    if a.probe:
        emit("probe", ok=True)
        finish()
        return
    request(
        "SelectSources",
        [session],
        {"types": dbus.UInt32(3), "multiple": False, "cursor_mode": dbus.UInt32(2)},
        lambda _: request("Start", [session, ""], {}, start, 600),
    )


def message(_, msg):
    if msg.type == Gst.MessageType.ERROR:
        err, _ = msg.parse_error()
        finish(str(err))
    elif msg.type == Gst.MessageType.EOS:
        finish()


def buffer_probe(pad, info, name):
    global last_frame, ready, count
    buf = info.get_buffer()
    if buf:
        if name not in first:
            first[name] = buf.pts / Gst.SECOND
        if name == "screen":
            last_frame = time.monotonic()
            count += 1
        expected = 1 + bool(a.camera) + bool(a.mic) + bool(a.desktop)
        if len(first) == expected and not ready:
            ready = True
            emit("ready", lead=max(first.values()) + 3)
    return Gst.PadProbeReturn.OK


def watchdog():
    if not ready and time.monotonic() - capture_started > 15:
        finish(
            "A selected audio or camera device did not become ready. Check the device and try again."
        )
        return False
    if not stopping and time.monotonic() - last_frame > 12:
        finish(
            "The selected source stopped supplying frames. The partial recording was kept. Keep the window open and at a fixed size while recording."
        )
        return False
    return not finished


def start(result=None):
    global pipeline, fd, last_frame, capture_started
    width, height = 1280, 720
    if a.synthetic:
        source = "videotestsrc name=screen is-live=true pattern=smpte ! video/x-raw,width=1280,height=720,framerate=30/1"
    else:
        node, props = result["streams"][0]
        width, height = props.get("size", (1920, 1080))
        scale = min(1, 3840 / width, 2160 / height)
        width = max(2, int(width * scale) // 2 * 2)
        height = max(2, int(height * scale) // 2 * 2)
        fd = portal.OpenPipeWireRemote(
            session, dbus.Dictionary({}, signature="sv"), timeout=10
        ).take()
        target = (
            f"target-object={int(props['pipewire-serial'])}"
            if "pipewire-serial" in props
            else f"path={int(node)}"
        )
        source = f"pipewiresrc name=screen fd={fd} {target} do-timestamp=true use-bufferpool=false always-copy=true min-buffers=8 max-buffers=16 on-disconnect=error"
    # Element properties are assigned below; device names and output paths never enter pipeline syntax.
    enc = (
        "nvh264enc preset=p1 bitrate=12000 gop-size=60 ! h264parse"
        if not a.synthetic and Gst.ElementFactory.find("nvh264enc")
        else "videoconvert ! vp8enc deadline=1 cpu-used=6 threads=4 target-bitrate=12000000"
    )
    desc = f'matroskamux name=mux ! filesink name=output {source} ! queue max-size-buffers=3 max-size-bytes=0 max-size-time=0 ! videoconvert ! videoscale ! capsfilter name=screen_caps caps="video/x-raw,format=NV12,width={width},height={height},pixel-aspect-ratio=1/1" ! {enc} ! queue ! mux.video_0 '
    if a.camera:
        cam = (
            "videotestsrc name=camera is-live=true pattern=ball ! video/x-raw,width=640,height=480,framerate=30/1"
            if a.synthetic
            else "v4l2src name=camera do-timestamp=true ! decodebin"
        )
        desc += f"{cam} ! queue ! videoconvert ! videoscale ! video/x-raw,format=I420,width=640,height=480 ! vp8enc deadline=1 cpu-used=8 threads=2 ! queue ! mux.video_1 "
    for i, name in enumerate(n for n in ["mic", "desktop"] if getattr(a, n)):
        source = (
            f"audiotestsrc name={name} is-live=true freq={440 + i * 440}"
            if a.synthetic
            else f"pulsesrc name={name} do-timestamp=true"
        )
        desc += f"{source} ! queue ! audioconvert ! audioresample ! audio/x-raw,rate=48000 ! opusenc ! queue ! mux.audio_{i} "
    try:
        pipeline = Gst.parse_launch(desc)
        pipeline.get_by_name("output").set_property("location", str(output))
        for name in ["screen", "camera", "mic", "desktop"]:
            element = pipeline.get_by_name(name)
            if element:
                if not a.synthetic and name != "screen":
                    element.set_property("device", getattr(a, name))
                element.get_static_pad("src").add_probe(
                    Gst.PadProbeType.BUFFER, buffer_probe, name
                )
        gstbus = pipeline.get_bus()
        gstbus.add_signal_watch()
        gstbus.connect("message", message)
        capture_started = time.monotonic()
        pipeline.set_state(Gst.State.PLAYING)
        last_frame = time.monotonic()
        GLib.timeout_add_seconds(2, watchdog)
        if a.seconds:
            GLib.timeout_add_seconds(a.seconds, stop)
    except Exception as e:
        finish(str(e))


try:
    if a.synthetic:
        start()
    else:
        bus = dbus.SessionBus()
        portal = dbus.Interface(
            bus.get_object("org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop"),
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
sys.exit(exitcode)
