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
  does. Edit opens the editor; Export uses the last export settings and writes to Videos.
  It fades after 10 seconds without a hover, unless it is kept (pin) or exporting. Closing
  or fading ends the session; the recording itself stays in Videos. At start-up without a
  file, the card instead offers an earlier session's unfinished edit for recovery; that
  card fades the same way, and the edit stays recoverable.

Opening a file goes straight to the editor. The process exits once its session has ended
and no surface is open. The chrome is painted with Omasnap's own theme, fonts and icon code
(`src/theme.*`, `src/chrome-items.*`, `src/video-icons.*`), so it follows the Omarchy theme
live and looks like Omasnap.
