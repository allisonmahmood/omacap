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
| `omacap --record` | `record-key` to the video process; the picker in record mode (`--record-picker`) unless it stopped a recording or is busy |
| `omacap --record-request <json>` | `record` to the video process, or starts one that records the request |
| `omacap video.mp4` | `open` to the video process, or starts one with the video |
| `omacap --export <job>` | the offscreen export worker |
| `omacap --version` | prints the version |

## Recording, end to end

1. `omacap --record` asks the video process first (`record-key`). If it is recording it
   stops and the command exits. Otherwise the command opens the picker in record mode.
2. The picker hands a `RecordRequest` (`src/record-request.h`: monitor, area, devices) to
   the video process with `record`, starting one if none runs.
3. The video process records the monitor through the portal, cropped to the area before
   encoding, so nothing outside the area is ever saved. Its recording chrome stays
   outside the area, or on another monitor, or is not shown. While the countdown or
   recording runs, `recordingMarkerPath()` exists and every other OmaCap surface hides.
4. After stopping, the video process shows its card, then the editor on request.

## Keeping OmaCap out of its own recordings

Hyprland can't leave a surface out of a screen capture, so nothing OmaCap paints may be
in the encoded pixels. `Recorder` (`src/recorder.*`) and its worker (`scripts/capture.py`)
make that hold:

- **Crop at capture.** The worker records the whole monitor and crops it to the area in
  GStreamer, before encoding. The original never holds anything outside the area.
- **Chrome outside the area.** The HUD and outline sit outside a guard around the encoded
  pixels, else on another monitor, else are not shown (`src/recording-layout.*`). A HUD
  whose monitor disappears is hidden, never moved. Stopping uses the record hotkey, which
  the countdown names (from `hyprctl binds -j`).
- **A clean first frame.** The countdown is drawn over the area. When it ends, it is
  repainted fully transparent (an unmapped surface would fade out on screen), and once the
  compositor has applied that, its monotonic time is the cut. XDPH stamps every frame with
  the time Hyprland copied it, and the worker keeps frames from the first one stamped after
  the cut; every track starts there. Earlier frames, including the countdown and the
  capture overlay's fade, are never written.
- **A clean last frame.** Stopping cuts at the moment of the request; the chrome hides
  only after the worker confirms it will keep nothing later.
- **Other windows.** The editor and card hide when the phase becomes `countdown`, at least
  three seconds before the first kept frame; `Recorder` hides any still visible.

The portal shares a monitor only after XDPH's picker, once per monitor: its restore token
is kept in the settings under `restoreTokens/<monitor>` and replaced after every
recording. XDPH may still share something else, so the worker checks the stream (monitor
`mapping_id`, source type and size) and fails clearly when it isn't the requested
monitor. Rotated monitors are refused for now.

## Video process commands

`src/video-ipc.h` defines the socket protocol; `src/video-owner.cpp` answers by phase:

| Command | `idle` | `editor` | `countdown`, `recording` | `stopping`, `preparing`, `exporting` |
| --- | --- | --- | --- | --- |
| `record-key` | `pick` | `pick`; with unsaved edits, asks the UI first and replies `pick` or `busy` | stops, `stopped` | `busy` |
| `record` | starts | starts, if nothing unsaved is lost | error | error |
| `open` | opens | opens, asking first like `record-key` | error | error |

`recordingMarkerPath()` holds the video process's PID, one line, from the moment a
recording is requested until the worker confirms the stop (or exits). The capture overlay
hides previews and pins while it exists with a live PID. Only the video lock's holder
writes it, so a new owner removes one left behind by a crash.

## Video process phases

`Backend::phase()` is the contract between recording and the editor UI:

| Phase | Shown |
| --- | --- |
| `idle` | nothing; the process exits once no card or editor is open |
| `countdown`, `recording`, `stopping` | only the recording chrome, never inside the recorded area |
| `preparing` | a recording or file is being opened |
| `editor` | a session is loaded: the card after a recording, or the editor |
| `exporting` | export progress in whichever of card or editor is open |

Signals and calls between `Backend` and the UI:

- `recordingFinished()` fires when a new recording is loaded, so the UI shows the card;
  opening a file goes straight to the editor.
- `replaceRequested()` asks the UI to confirm discarding an edited session for a new
  recording or file. The UI answers with `answerReplace(bool)`.
- `uiClosed()` tells the backend neither card nor editor is open any more. The process
  then exits once nothing is recording or exporting. A session shown in the editor
  counts as open until then.
- Failures with nothing open to show them are reported as desktop notifications.
