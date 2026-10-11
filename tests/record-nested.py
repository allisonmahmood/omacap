#!/usr/bin/python
"""Record through the real portal in the nested Hyprland and prove no OmaCap pixel is saved.

    scripts/nested-hyprland.sh start STATE && scripts/nested-portal.sh start STATE
    tests/record-nested.py STATE [--area X,Y,W,H] [--monitor NAME] [--omacap PATH]
                                 [--interrupt scale|mode|transform|kill-owner] [--stall-owner]
                                 [--notify closes|keeps|hovered] [--dismiss] [--stale]

Runs `omacap --record-request`, stops with `omacap --record`, and decodes every frame of
the saved recording. A magenta theme paints the countdown, HUD and OmaCap's windows; the
outline and record dot are OmaCap's red. Any pixel of either colour fails the run. Nothing
touches your session: the portal, D-Bus, settings, Videos and runtime dirs are the
harness's. Needs a client drawing in the area (e.g. a terminal) so frames keep arriving.

--interrupt ends the recording another way, and the file must still be clean and end before
it: `scale` doubles the nested monitor's scale (which moves the chrome into the area), `mode`
changes its resolution, `transform` flips it (Qt doesn't report flips), `kill-owner` SIGKILLs
the video process, whose worker must stop. --stall-owner stops the video process for two
seconds around the change, so only the worker can react to it.

--notify shows an OmaCap notification just before the request: a magenta toast over the area,
from tests/notification-daemon.qml on the harness's bus. With `closes` it would stay up for a
minute, so only OmaCap closing it takes it down; with `keeps` a close does nothing and it
expires after five seconds, as Omarchy's shell keeps toasts; with `hovered` a close does nothing
and it never expires, as Omarchy's shell keeps a toast while it is hovered.

`omarchy-shell` is always a stand-in, so the real shell is never asked. It fails as with no
Omarchy shell running, unless --dismiss: then `notifications dismiss` goes to the stand-in
daemon, which takes down the toasts whose summary holds the needle, as Omarchy's shell does.

--stale changes the monitor's scale after the request was made, and OmaCap must refuse it.
"""

import argparse, atexit, json, os, signal, subprocess, sys, tempfile, time
from pathlib import Path
import numpy as np

repo = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser()
p.add_argument("state")
p.add_argument("--area", default="300,200,800,500", help="logical x,y,w,h on the monitor")
p.add_argument("--monitor", default="")
p.add_argument("--omacap", default=str(repo / "build/omacap"))
p.add_argument("--seconds", type=float, default=3)
p.add_argument("--interrupt", choices=["scale", "mode", "transform", "kill-owner"])
p.add_argument("--stall-owner", action="store_true")
p.add_argument("--notify", choices=["closes", "keeps", "hovered"])
p.add_argument("--dismiss", action="store_true")
p.add_argument("--stale", action="store_true")
a = p.parse_args()
state = Path(a.state).resolve()


def read_env(path):
    return dict(line.split("=", 1) for line in path.read_text().splitlines() if "=" in line)


env = {k: v for k, v in os.environ.items() if not k.startswith(("QT_", "DISPLAY", "WAYLAND"))}
env |= read_env(state / "portal/env")
run = Path(tempfile.mkdtemp(prefix="record-", dir=state))
for name in ["videos", "runtime", "theme-root/theme"]:
    (run / name).mkdir(parents=True)
(run / "theme-root/theme/colors.toml").write_text(
    'background = "#ff00ff"\nforeground = "#ffffff"\naccent = "#ff00ff"\n')
config = Path(env["XDG_CONFIG_HOME"])
(config / "user-dirs.dirs").write_text(f'XDG_VIDEOS_DIR="{run}/videos"\n')
env |= {"OMACAP_RUNTIME_DIR": str(run / "runtime"), "OMACAP_THEME_ROOT": str(run / "theme-root"),
        "QT_QPA_PLATFORM": "wayland"}
# Omarchy's omarchy-shell would reach your session's shell.
shell = run / "bin/omarchy-shell"
shell.parent.mkdir()
shell.write_text("""#!/bin/sh
echo "$*" >>"$0.log"
if [ "$1 $2" = "notifications dismiss" ] && [ -n "$OMACAP_TEST_DAEMON_PID" ]; then
  exec quickshell ipc --pid "$OMACAP_TEST_DAEMON_PID" call notifications dismiss "$3"
fi
echo "omarchy-shell is not running" >&2
exit 1
""")
shell.chmod(0o755)
env["PATH"] = f"{shell.parent}:{env['PATH']}"

monitors = json.loads(subprocess.check_output(["hyprctl", "monitors", "-j"], env=env))
monitor = next(m for m in monitors if m["name"] == (a.monitor or monitors[0]["name"]))
scale = monitor["scale"]
x, y, w, h = (int(v) for v in a.area.split(","))
request = {"monitor": monitor["name"], "monitorX": monitor["x"], "monitorY": monitor["y"],
           "monitorWidth": round(monitor["width"] / scale),
           "monitorHeight": round(monitor["height"] / scale), "scale": scale,
           "transform": monitor["transform"], "areaX": x, "areaY": y, "areaWidth": w,
           "areaHeight": h, "mic": "", "camera": "", "desktopAudio": False}
picks = state / "portal/picker.log"
picked = len(picks.read_text().splitlines()) if picks.exists() else 0
marker = run / "runtime/recording"
mode = f'{monitor["width"]}x{monitor["height"]}@{monitor["refreshRate"]:.2f}'
rule = f'{monitor["name"]},{{}},{monitor["x"]}x{monitor["y"]},{{}}'
restore = rule.format(mode, scale)


def set_monitor(value):
    subprocess.run(["hyprctl", "keyword", "monitor", value], env=env, check=True,
                   stdout=subprocess.DEVNULL)


def layers():
    found = json.loads(subprocess.check_output(["hyprctl", "layers", "-j"], env=env))
    return [layer["namespace"] for output in found.values()
            for level in output["levels"].values() for layer in level]


if a.stale:
    # The area was picked before the monitor changed: placing the chrome for the old one could
    # put it in the area, so OmaCap refuses before showing anything.
    try:
        set_monitor(rule.format(mode, scale * 2))
        refused = subprocess.run([a.omacap, "--record-request", json.dumps(request)], env=env,
                                 capture_output=True, text=True, timeout=30)
    finally:
        set_monitor(restore)
    assert refused.returncode == 1, refused
    assert "The display changed; pick the area again." in refused.stderr, refused.stderr
    assert not marker.exists() and not list((run / "videos").glob("*")), refused
    print("PASS: a request for a changed display is refused")
    sys.exit(0)

daemon = None
if a.notify:
    daemon = subprocess.Popen(
        ["quickshell", "-p", str(repo / "tests/notification-daemon.qml")],
        env=env | {"closes": {"OMACAP_TEST_TOAST_MS": "60000"},
                   "keeps": {"OMACAP_TEST_KEEP_CLOSED": "1"},
                   "hovered": {"OMACAP_TEST_KEEP_CLOSED": "1", "OMACAP_TEST_TOAST_MS": "0"}
                   }[a.notify],
        stdout=open(run / "notifications.log", "w"), stderr=subprocess.STDOUT)
    atexit.register(daemon.terminate)
    if a.dismiss:
        env["OMACAP_TEST_DAEMON_PID"] = str(daemon.pid)
    deadline = time.monotonic() + 15
    while b"org.freedesktop.Notifications" not in subprocess.run(
            ["busctl", "--user", "list", "--no-legend"], env=env, capture_output=True).stdout:
        assert daemon.poll() is None and time.monotonic() < deadline, "no notification daemon"
        time.sleep(0.1)
    # A notification of OmaCap's own, just before the request: a refused request's.
    failed = subprocess.run([a.omacap, "--record-request", "{}"], env=env, timeout=30,
                            capture_output=True)
    assert failed.returncode == 1, failed
    assert (run / "runtime/notifications").read_text().strip(), "its id wasn't listed"
    assert "omacap-test-notification" in layers(), "no toast shown"

owner = subprocess.Popen([a.omacap, "--record-request", json.dumps(request)], env=env,
                         stdout=open(run / "owner.log", "w"), stderr=subprocess.STDOUT)
deadline = time.monotonic() + 30
while not marker.exists():
    assert owner.poll() is None and time.monotonic() < deadline, (run / "owner.log").read_text()
    time.sleep(0.05)
requested = time.monotonic()
assert marker.read_text().split()[0] == str(owner.pid), marker.read_text()
# The countdown, then the recording. After a notification the countdown also waits out the
# toast, unless the dismiss was answered.
time.sleep(4 + a.seconds + (3 if a.notify else 0))
# The video process, then its worker: OmaCap stays hidden while either runs.
pids = [int(pid) for pid in marker.read_text().split()]
assert len(pids) == 2 and pids[0] == owner.pid, pids
interrupted = time.monotonic()
if a.interrupt == "kill-owner":
    owner.kill()
    owner.wait()
    deadline = time.monotonic() + 10
    while os.path.exists(f"/proc/{pids[1]}") and time.monotonic() < deadline:
        time.sleep(0.05)
    assert not os.path.exists(f"/proc/{pids[1]}"), "the worker outlived its owner"
elif a.interrupt:
    # Each ends the recording by itself; the monitor is put back afterwards.
    changed = {"scale": rule.format(mode, scale * 2),
               "mode": rule.format(f'{monitor["width"] * 4 // 5}x{monitor["height"] * 4 // 5}',
                                   scale),
               "transform": rule.format(mode, scale) + ",transform,4"}[a.interrupt]
    try:
        if a.stall_owner:
            owner.send_signal(signal.SIGSTOP)
        set_monitor(changed)
        if a.stall_owner:
            time.sleep(2)
            owner.send_signal(signal.SIGCONT)
        deadline = time.monotonic() + 15
        while marker.exists() and time.monotonic() < deadline:
            time.sleep(0.05)
        assert not marker.exists(), "the recording went on after the display changed"
    finally:
        owner.send_signal(signal.SIGCONT)
        set_monitor(restore)
else:
    stop = subprocess.run([a.omacap, "--record"], env=env, timeout=10)
    assert stop.returncode == 0
if owner.poll() is None:
    try:
        # A UI that keeps the card open keeps the process; ours to end.
        owner.wait(timeout=20)
    except subprocess.TimeoutExpired:
        owner.terminate()
        owner.wait(timeout=20)
assert not marker.exists() or a.interrupt == "kill-owner"
if a.notify:
    # Each recording that found a notification listed asked the shell to take OmaCap's down.
    assert (shell.parent / "omarchy-shell.log").read_text() == "notifications dismiss OmaCap\n"
videos = list((run / "videos").glob("omacap-*.mkv"))
assert len(videos) == 1, (videos, (run / "owner.log").read_text())

info = json.loads(subprocess.check_output(
    ["ffprobe", "-v", "error", "-select_streams", "v:0", "-show_entries",
     "stream=width,height:format=duration,start_time", "-of", "json", str(videos[0])]))
width, height = info["streams"][0]["width"], info["streams"][0]["height"]
raw = subprocess.check_output(["ffmpeg", "-v", "error", "-i", str(videos[0]), "-map", "0:v:0",
                               "-f", "rawvideo", "-pix_fmt", "rgb24", "-"])
frames = np.frombuffer(raw, np.uint8).reshape(-1, height, width, 3).astype(np.int16)
r, g, b = frames[..., 0], frames[..., 1], frames[..., 2]
magenta = ((r - g > 90) & (b - g > 90)).reshape(len(frames), -1).sum(1)
red = ((abs(r - 242) < 30) & (abs(g - 107) < 30) & (abs(b - 82) < 30)).reshape(len(frames), -1).sum(1)
result = {"recording": str(videos[0]), "size": [width, height], "frames": len(frames),
          "duration": float(info["format"]["duration"]),
          "start": float(info["format"]["start_time"]),
          "picker_shown": len(picks.read_text().splitlines()) - picked if picks.exists() else 0,
          "frames_with_omacap_magenta": int((magenta > 0).sum()),
          "frames_with_omacap_red": int((red > 0).sum())}
print(json.dumps(result))
# The picked area, in native pixels with even sides (areas here are under 4K).
assert abs(width - w * scale) <= 2 and abs(height - h * scale) <= 2, (width, height, scale)
assert result["frames_with_omacap_magenta"] == 0 and result["frames_with_omacap_red"] == 0, result
assert result["duration"] > a.seconds - 1, result
# Nothing from after an interruption: the countdown took three of the seconds before it.
assert not a.interrupt or result["duration"] < interrupted - requested - 3 + 0.2, (
    result, interrupted - requested)
print("PASS: no OmaCap pixels in", videos[0])
