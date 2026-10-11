#!/usr/bin/python
"""Record a changing then idle PipeWire source without opening desktop devices."""

import importlib.util
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import threading
import time

seconds = int(sys.argv[1]) if len(sys.argv) > 1 else 18
repo = Path(__file__).resolve().parents[1]
out = repo / "tests/out"
out.mkdir(exist_ok=True)

with tempfile.TemporaryDirectory(prefix="omacap-pipewire-") as runtime:
    os.environ["PIPEWIRE_RUNTIME_DIR"] = runtime
    config = Path(runtime) / "pipewire.conf"
    config.write_text("""
context.properties = { core.daemon = true core.name = pipewire-0 }
context.spa-libs = { support.* = support/libspa-support }
context.modules = [
    { name = libpipewire-module-protocol-native }
    { name = libpipewire-module-client-node }
    { name = libpipewire-module-metadata }
    { name = libpipewire-module-spa-node-factory }
    { name = libpipewire-module-link-factory }
    { name = libpipewire-module-access }
]
context.objects = [
    { factory = spa-node-factory args = {
        factory.name = support.node.driver node.name = Dummy-Driver
        node.group = pipewire.dummy priority.driver = 20000
    } }
]
""")
    with (out / "pipewire-test.log").open("w") as log:
        server = subprocess.Popen(
            ["pipewire"],
            stdout=log,
            stderr=log,
            env=os.environ | {"PIPEWIRE_CONFIG_DIR": runtime},
        )
        producer = None
        capture = None
        try:
            for _ in range(100):
                if (Path(runtime) / "pipewire-0").exists():
                    break
                assert server.poll() is None, "Test PipeWire server exited"
                time.sleep(0.02)
            spec = importlib.util.spec_from_file_location(
                "capture", repo / "scripts/capture.py"
            )
            capture = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(capture)
            Gst, GLib = capture.Gst, capture.GLib
            producer = Gst.parse_launch(
                "videotestsrc is-live=true pattern=ball ! "
                "video/x-raw,format=RGBx,width=640,height=360,framerate=30/1 ! "
                "valve name=activity ! pipewiresink mode=provide "
                'stream-properties="props,node.name=omacap-test,media.class=Video/Source"'
            )
            producer.set_state(Gst.State.PLAYING)
            node = None
            for _ in range(100):
                objects = json.loads(subprocess.check_output(["pw-dump"], timeout=5))
                node = next(
                    (
                        obj["id"]
                        for obj in objects
                        if obj.get("info", {}).get("props", {}).get("node.name")
                        == "omacap-test"
                    ),
                    None,
                )
                if node is not None:
                    break
                time.sleep(0.05)
            assert node is not None, "Synthetic PipeWire source not registered"

            class Remote:
                def take(self):
                    remote = socket.socket(socket.AF_UNIX)
                    remote.connect(str(Path(runtime) / "pipewire-0"))
                    return remote.detach()

            class Portal:
                def OpenPipeWireRemote(self, *_args, **_kwargs):
                    return Remote()

            capture.a = capture.p.parse_args(
                ["--output", str(out / "pipewire-idle.mkv"), "--seconds", str(seconds)]
            )
            capture.output = Path(capture.a.output)
            capture.portal = Portal()
            events = []
            original_emit = capture.emit

            def emit(event, **data):
                events.append(dict(event=event, **data))
                original_emit(event, **data)

            capture.emit = emit
            link_errors = []

            def link():
                try:
                    for _ in range(100):
                        ports = json.loads(
                            subprocess.check_output(["pw-dump"], timeout=5)
                        )
                        outputs = [
                            obj["id"]
                            for obj in ports
                            if obj.get("type") == "PipeWire:Interface:Port"
                            and obj["info"]["direction"] == "output"
                        ]
                        inputs = [
                            obj["id"]
                            for obj in ports
                            if obj.get("type") == "PipeWire:Interface:Port"
                            and obj["info"]["direction"] == "input"
                        ]
                        if outputs and inputs:
                            subprocess.run(
                                ["pw-link", str(outputs[0]), str(inputs[0])],
                                check=True,
                                timeout=5,
                            )
                            return
                        time.sleep(0.05)
                    raise RuntimeError("PipeWire capture ports not registered")
                except Exception as error:
                    link_errors.append(error)

            linker = threading.Thread(target=link, daemon=True)
            linker.start()
            capture.start({"streams": [(node, {"size": (640, 360)})]})
            linker.join(timeout=10)
            assert not link_errors, link_errors

            # Keep the source connected but send no damage, exactly like a still window.
            def idle():
                producer.get_by_name("activity").set_property("drop", True)
                return False

            GLib.timeout_add(max(1000, (seconds - 20) * 1000), idle)
            if not capture.finished:
                capture.loop.run()
            assert capture.exitcode == 0, events
            assert any(event["event"] == "ready" for event in events), events
            result = json.loads(
                subprocess.check_output(
                    [
                        "ffprobe",
                        "-v",
                        "error",
                        "-show_format",
                        "-show_packets",
                        "-show_entries",
                        "format=duration:packet=pts_time",
                        "-of",
                        "json",
                        str(capture.output),
                    ],
                    timeout=10,
                )
            )
            duration = float(result["format"]["duration"])
            stamps = [float(packet["pts_time"]) for packet in result["packets"]]
            gap = max(b - a for a, b in zip(stamps, stamps[1:]))
            assert duration >= seconds - 1, (duration, seconds)
            assert gap < 0.5, f"Still frames stopped arriving: largest gap {gap}"
            subprocess.run(
                [
                    "ffmpeg",
                    "-v",
                    "error",
                    "-xerror",
                    "-i",
                    str(capture.output),
                    "-f",
                    "null",
                    "-",
                ],
                check=True,
                timeout=30,
            )
            print(
                f"PASS: PipeWire idle capture {duration:.3f}s, {len(stamps)} frames, max gap {gap:.3f}s"
            )
        finally:
            if capture and capture.pipeline:
                capture.pipeline.set_state(capture.Gst.State.NULL)
            if producer:
                producer.set_state(capture.Gst.State.NULL)
            server.terminate()
            server.wait(timeout=5)
