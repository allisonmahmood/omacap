# How OmaCap fits together

One `omacap` binary runs one of two apps, chosen in `src/main.cpp` before any
`QApplication` exists:

- **Capture and screenshots** (`snap/`, derived from [Omasnap](../snap/VENDORED.md)):
  the frozen-screen picker, the screenshot editor, previews and pins. Short-lived
  processes; previews and pins run as their own processes, as in Omasnap.
- **Video** (`src/`, `qml/`): one long-lived process that owns recording, the card shown
  after a recording, the video editor and exports. It holds the video lock and listens
  on the video socket (`src/video-owner.*`, `src/video-ipc.*`).

## Command line

| Command | Runs |
| --- | --- |
| `omacap`, `omacap --screenshot` | the capture overlay's smart capture |
| `omacap shot.png` | the screenshot editor |
| `omacap --record` | `record-key` to the video process; the picker in record mode (`--record-picker`) unless it stopped a recording or is busy. With no video process and an unfinished edit to recover, starts one on the recovery card instead |
| `omacap --record-request <json>` | `record` to the video process, or starts one that records the request |
| `omacap video.mp4` | `open` to the video process, or starts one with the video |
| `omacap --export <job>` | the offscreen export worker |
| `omacap --version` | prints the version |

A video process that loses the race for the video lock forwards its arguments to the one
that won, so a request or file is acted on once. It keeps trying for up to ten seconds, since
the winner listens only once it is ready, and one that is exiting answers nothing and then
lets go of the lock (the loser takes its place). A video process exits 1 when the last thing
it was asked to do failed, such as an invalid `--record-request` or a file that won't open,
and 0 otherwise.

## Recording, end to end

1. `omacap --record` asks the video process first (`record-key`). If it is recording it
   stops and the command exits. Otherwise the session ends (asking first when that loses
   unsaved edits) and the command opens the picker in record mode. With no video process
   running and an earlier session left unfinished, the command starts one on the recovery
   card instead; pressing the record key again picks as usual.
2. The picker hands a `RecordRequest` (`src/record-request.h`: monitor, area, devices) to
   the video process with `record`, starting one if none runs.
3. The video process refuses a request whose monitor has changed since the area was picked,
   then records the monitor through the portal, cropped to the area before encoding, so
   nothing outside the area is ever saved. Its recording chrome stays outside the area, or on
   another monitor, or is not shown. From the request until the capture worker has exited,
   `recordingMarkerPath()` lists both processes and no other OmaCap surface shows.
4. After stopping, the video process shows its card, then the editor on request.

## Keeping OmaCap out of its own recordings

Hyprland can't leave a surface out of a screen capture, so nothing OmaCap paints may be
in the encoded pixels. `Recorder` (`src/recorder.*`) and its worker (`scripts/capture.py`)
make that hold:

- **Crop at capture.** The worker records the whole monitor and crops it to the area in
  GStreamer, before encoding. The original never holds anything outside the area. The crop
  is made for the monitor's size: a stream that changes size, or breaks off as XDPH's does
  when the monitor's mode changes, ends the recording and keeps what came before. No frame
  of a new size is kept, and the stream's flushes never reach the file.
- **Chrome outside the area.** The HUD and outline sit outside a guard around the encoded
  pixels, else on another monitor, else are not shown (`src/recording-layout.*`). A HUD
  whose monitor disappears is hidden, never moved. Stopping uses the record hotkey, which
  the countdown names (from `hyprctl binds -j`).
- **Display changes.** The chrome keeps its place in logical pixels, so a new scale, size,
  rotation or flip of the recorded monitor (or the HUD's) can move it into the area. A
  request is checked against `hyprctl -j monitors` first: one whose monitor changed since the
  area was picked is refused ("The display changed; pick the area again."), and the state it
  matched is what the worker watches. Hyprland sends no event for these changes, so the
  worker asks its socket about the monitor every 100 ms (about 0.2 ms of work each, on either
  side). It holds encoded frames back for a second, and a screen frame leaves that hold only
  once an answer asked for after the frame was rendered still matched: an answer that differs
  ends the recording at the last one that didn't, and answers that stop coming end it there
  too. Qt's notice of a change, when it comes, makes the chrome transparent and hides it at
  once. A notification says why the recording ended once the capture has ended. One window
  remains: a change undone between two answers, within 100 ms, is never seen by the worker,
  and the frames from it are kept. Qt's notice still hides the chrome and ends the recording
  for any change it sees, but it misses flips.
- **A clean first frame.** The countdown is drawn over the area. When it ends, it is
  repainted fully transparent (an unmapped surface would fade out on screen), and once the
  compositor has applied that, its monotonic time is the cut. XDPH stamps every frame with
  the time Hyprland copied it, and the worker keeps frames from the first one stamped after
  the cut; every track starts there. Earlier frames, including the countdown and the
  capture overlay's fade, are never written.
- **A clean last frame.** Stopping cuts at the moment of the request; the chrome hides
  only after the worker confirms it will keep nothing later. The worker writes out what it
  holds only once the display watch has caught up with the cut, also when the screen stream
  breaks off (XDPH's does when the mode changes). A microphone, desktop audio or camera that
  fails ends the recording as a stop does, keeping every track up to then.
- **A dying video process.** Its worker gets SIGTERM (`PR_SET_PDEATHSIG`) and finishes the
  file, but stops keeping frames as soon as a pad probe sees it was re-parented, whatever its
  main loop is doing. The marker lists the worker, so other surfaces stay hidden until it
  has exited.
- **Notifications.** None is sent while the marker lists a running process. A `record` or
  `open` refused during a capture is shown by the video process once it has ended. One shown
  just before a capture could still be up, so every OmaCap process lists each in
  `notificationIdsPath()` under a lock on that file (`sendNotification()`): `?` before sending
  with `omarchy-notification-send -p`, which is given two seconds, then the id it prints. A
  recording takes the list as soon as its marker is written. A sender that holds the lock past
  its bound leaves the list unknown, so the recording refuses to start instead ("OmaCap is busy
  showing a notification; try again."). Then it takes the toasts down before showing anything.
  Omarchy's shell ignores a close, and keeps a toast up for as long as it is hovered, so the
  recording runs `omarchy-shell notifications dismiss OmaCap`: it takes down every toast whose
  summary holds "OmaCap", hovered or not, and every OmaCap notification's summary is "OmaCap".
  Each id is also closed over D-Bus, for other daemons. Only when the dismiss isn't answered
  does the countdown also last until six seconds after the newest, when Omarchy's shell lets a
  toast nobody hovers expire. A `?` with no id after it (a send that timed out) may yet reach a
  slow daemon, so it extends the countdown either way. A toast hovered through the countdown
  under a daemon that neither answers the dismiss nor honours a close can still be recorded.
- **Other windows.** Starting a recording ends the session, which closes the editor and
  card, and ends an offer to recover an earlier edit. `VideoStage` hides anything still
  open when the phase becomes `countdown`, at least three seconds before the first kept
  frame. As a safety net, `Recorder` hides any window still visible then, and any shown
  later, before it draws, and logs a warning; in normal use it never fires.

The portal shares a monitor only after XDPH's picker, once per monitor: its restore token
is kept in the settings under `restoreTokens/<monitor>` and replaced after every
recording. XDPH may still share something else, so the worker checks the stream (monitor
`mapping_id`, source type and size) and fails clearly when it isn't the requested
monitor. Rotated monitors are refused for now.

## Video process commands

`src/video-ipc.h` defines the socket protocol; `src/video-owner.cpp` answers by phase:

| Command | `idle` | `editor` | `countdown`, `recording` | `stopping`, `preparing`, `exporting` |
| --- | --- | --- | --- | --- |
| `record-key` | `pick` | ends the session, `pick`; with unsaved edits asks first, and `busy` if they are kept | stops, `stopped` | `busy` |
| `record` | starts | starts; with unsaved edits asks first, and errors if they are kept | error | error |
| `open` | opens | opens; asks like `record` | error | error |

A command that asks waits for the user's answer, so its caller waits too (up to two
minutes, `answerTimeoutMs`). During `countdown`, `recording` and `stopping`, a refused
`record` or `open` replies with `"deferred":true`: the video process notifies once the
capture has ended, and the caller only prints the error. A video process that has decided to
exit answers no command; its caller starts a video process of its own.

`recordingMarkerPath()` lists one decimal PID per line: the video process, then its capture
worker once that runs. It exists from the moment a recording is requested until the worker
has exited, and is written whole (aside, then renamed over the old one); a recording that
can't write it fails. While any process it lists runs, no other OmaCap surface shows: the
picker, previews, pins and notifications. Only the video lock's holder writes it, so a new
owner removes one left behind by a crash once nothing it lists runs any more.

## Video process phases

`Backend::phase()` is the contract between recording and the editor UI:

| Phase | Shown |
| --- | --- |
| `idle` | nothing, or the recovery card; the process exits once nothing is open |
| `countdown`, `recording`, `stopping` | only the recording chrome, never inside the recorded area |
| `preparing` | nothing new: a recording or file is being opened, and whatever was open stays (the recovery card while it recovers). A recording's card appears once it has loaded, with its first frame |
| `editor` | a session is loaded: the card after a recording, or the editor |
| `exporting` | export progress in whichever of card or editor is open |

Signals and calls between `Backend` and the UI:

- `recordingFinished()` fires when a new recording is loaded, so the UI shows the card;
  opening a file goes straight to the editor.
- `replaceRequested()` asks the UI to confirm discarding unsaved edits for a new recording
  or file; the editor asks in its discard dialog. The UI answers with `answerReplace(bool)`;
  yes discards the session at once. Unsaved edits (`dirty`) are changes since the last
  export; an untouched recording has none, so replacing or closing it never asks.
- `setUiOpen(bool)` tells the backend whether a card or editor is open, counting one hidden
  for a recording. The backend alone ends the process: `exitRequested(code)` follows once
  that settles with nothing open and the phase `idle`, so it never exits while recording,
  preparing or exporting, or while a session passes through `idle` on its way to another.
  The process exits there and then, answering no command after. `code` is 1 when the last
  session failed (`fail()`), else 0.
- A failure ends the session. One that never loaded anything, such as a file that won't
  open, is removed; one holding a recording or an edit stays recoverable. Failures with
  nothing open to show them, and why a kept recording ended early, are reported as desktop
  notifications (`backgroundError()`, sent with `omarchy-notification-send` once no capture
  runs).

## Video UI

`VideoStage` (`src/video-stage.*`) presents the video process the way Omasnap presents a
capture. Each surface is a QML item it hosts in a window it owns:

- **Editor** (`qml/Editor.qml`): a full-screen layer over the dimmed desktop on the focused
  monitor, with the keyboard, or a floating window. `W` switches between them and is
  remembered; the editor item moves to the new host, so nothing about the edit is lost.
  Choosers (background image, colour, export file) are handed off as Omasnap's Save As: the
  overlay hides and releases the keyboard while the floating dialog is open.
- **Card** (`qml/Card.qml`): after a recording, the styled first frame in the corner of the
  focused monitor. It never takes the keyboard when it appears; hovering or clicking it
  does. Edit (E) opens the editor; Export (Enter) uses the last export settings and writes
  to Videos; P keeps it; X or Esc closes it. It fades after 10 seconds without a hover,
  unless it is kept or exporting, and can't be closed while exporting. Closing or fading
  ends the session; the recording itself stays in Videos.
- **Recovery card**: a video process started without a request or file (as
  `omacap --record` does when one is left) offers the newest earlier session's unfinished
  edit on the same card. Recover (E or Enter) opens it in the editor. Discard (D or Delete)
  deletes it; a recording it edits stays in Videos. Not now (X or Esc), fading, or starting
  a new recording ends the offer; the edit stays recoverable until it is recovered and its
  session ends, or is discarded.

Opening a file goes straight to the editor. The process exits once its session has ended
and no surface is open. The chrome is painted with Omasnap's own theme, fonts and icon code
(`src/theme.*`, `src/chrome-items.*`, `src/video-icons.*`), so it follows the Omarchy theme
live and looks like Omasnap.
