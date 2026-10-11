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
