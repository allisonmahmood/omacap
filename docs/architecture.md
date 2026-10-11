# How OmaCap fits together

One `omacap` binary runs one of two apps, chosen in `src/main.cpp` before any
`QApplication` exists:

- **Capture and screenshots** (`snap/`, derived from [Omasnap](../snap/VENDORED.md)):
  the frozen-screen picker, the screenshot editor, previews and pins. Short-lived
  processes; previews and pins run as their own processes, as in Omasnap.
- **Video** (`src/`, `qml/`): one long-lived process that owns recording, the card shown
  after a recording, the video editor and exports. It holds the video lock and listens
  on the video socket (`src/video-owner.*`, `src/video-ipc.*`).

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

## Video process phases

`Backend::phase()` is the contract between recording and the editor UI:

| Phase | Shown |
| --- | --- |
| `idle` | nothing; the process exits once no card or editor is open |
| `countdown`, `recording`, `stopping` | only the recording HUD, never inside the recorded area |
| `editor` | a session is loaded: the card after a recording, or the editor |
| `exporting` | export progress in whichever of card or editor is open |

`Backend::recordingFinished()` fires when a new recording is loaded, so the UI shows the
card; opening a file goes straight to the editor.
