# Vendored Omasnap

`snap/` started as a copy of [Omasnap](https://github.com/omacom/omasnap), MIT licensed,
© 2026 Tobi Lütke. See `LICENSE` in this directory.

- Upstream: https://github.com/omacom/omasnap
- Commit: 6771b19de8bcead2b17c16b49f1a8a97445dd8e3 (2026-10-03)
- Left out: the demo `assets/omasnap.gif` and `assets/omasnap.mp4`.

OmaCap modifies this code freely. The bundled fonts keep their OFL licences and the Lucide
icons their ISC licence (`assets/`).

## What OmaCap changed

OmaCap runs beside a system Omasnap, so nothing runtime-visible is shared with it:

| | Omasnap | OmaCap |
| --- | --- | --- |
| App id, layer namespace, window class | `omasnap` | `omacap` |
| Runtime files (snapshots, pins, handoffs, region memory) | `$XDG_RUNTIME_DIR/omasnap/` | `omacapRuntimeDir()/snap/` |
| Capture instance lock | `…/omasnap.instance` | `omacapRuntimeDir()/snap/capture.lock` |
| Recents | `~/.local/state/omasnap/recent` | `~/.local/state/omacap/recent` |
| Config | `~/.config/omasnap/omasnap.conf` | `~/.config/omacap/omacap.conf` (same keys) |
| Environment | `OMASNAP_*` | `OMACAP_*`; `OMARCHY_SCREENSHOT_DIR` is honoured after `[output] directory` |
| Default filename | `screenshot-{date}_{time}-{app}` | `omacap-{date}_{time}-{app}` |
| Self-spawns, notification "Click to edit" | `omasnap` | this executable |

### OmaCap's look

Screenshots wear OmaCap's look rather than Omasnap's fixed frame. `CaptureLook`
(`src/look.hpp`) holds the backdrop (Omarchy wallpaper, Omasnap's Slate and gradients,
a colour, a chosen image, or Off), padding and corner radius as shares of the canvas
width (OmaCap's video units: 7% and 1.5% by default), shadow strength (45%), glass
(opaque, light 96%, or a custom opacity) and the canvas aspect (Auto, 16:9, 4:3, 1:1).
`captureLayout()` is the one place the canvas, card and radius are computed, and
`paintCaptureBase()` the one painter for backdrop, shadow and card, so the editor,
exports and spotlight sampling cannot disagree.

- Looks are op-log operations (`Operation::Type::Look`), one undo step each; a slider
  drag commits once, on release.
- Fresh captures start with the last-used look, remembered in
  `~/.local/state/omacap/capture.ini`; the first default is the wallpaper. Opened files
  start plain. Omasnap's `[background]` config is gone.
- The wallpaper is resolved when a document is created. Pictures are stored once, by
  content, in `~/.local/state/omacap/backdrops/` and documents refer to them by id, so
  undo, reopening and recents survive a wallpaper change. Recording a recent prunes
  pictures nothing refers to any more.
- A fixed aspect grows the canvas around the card and never crops or downscales. It
  applies while the canvas is no larger than 8192 × 8192 logical px (or the Auto
  canvas, if larger); a tall scroll stitch, or a layer dragged far off, keeps Auto.
  Framed growth adds 15 px beyond a layer, then widens back to the aspect. Overflow and
  Image never frame.
- The toolbar's style group has Backdrop, Frame and Glass popovers (`src/editor-look.cpp`).
  Colour… and Image… use the Save As chooser handoff (`src/editor-choosers.cpp`).

### Record mode and recordings

- The picker has a Screenshot | Record switch (a click or V) and `--record-picker`
  (internal; `omacap --record` uses it). In record mode a picked area gets a record bar
  (`src/editor-record.cpp`) and Enter hands a `RecordRequest` to the video process
  (`src/record-picker.hpp`), starting one with `--record-request` if none answers. The
  overlay closes before the handoff completes. Microphone, camera and desktop audio
  choices are remembered in `capture.ini`.
- While the recording marker exists and names a live process (`src/recording-state.hpp`:
  inotify plus the recorder's pidfd, no polling), previews and pins paint nothing, take
  no input and pause their countdown; other screenshot UI refuses to open, and an open
  editor steps aside.

`README.md` in this directory is upstream's, kept for reference.
