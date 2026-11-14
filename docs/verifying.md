# Verifying OmaCap

Passing tests are not proof that OmaCap records. 0.2.0 shipped with every check green and could not record on a machine with an NVIDIA encoder, because no test ran that encoder. A change is proven when the build that ships has done the thing, on the path a user takes, and left evidence.

## Three levels of proof

| Level | Run | Proves | Cannot see |
| --- | --- | --- | --- |
| Tests | `./scripts/test.sh` | The recorder's cut on every encoder path, editing, preview against export, with synthetic sources | Hyprland, the portal, a hardware encoder you don't have |
| CI | every PR, every night, every release | The same on Omarchy stable and on current Arch, then the package installed in a fresh container: the editor opens and the exporter renders a blurred camera | The same; it runs in containers |
| Desktop | `./scripts/verify-desktop.sh PACKAGE_OR_BINARY` | A recording through the real portal with the encoder this machine records with, display changes mid-recording, exports through this session's display | Your real camera and microphone, the picker on your own session |

The nightly run exists because OmaCap breaks without changing: a dependency update did it twice in one week. When it fails it opens an issue.

`scripts/doctor.sh` checks the desktop first. Run it when a recording or export fails for no visible reason; its messages name causes the app's own errors don't.

## What each change needs

Match the change to its row and run the proof before calling it done. CI runs the first two levels by itself; the desktop level is yours to run.

| Change touches | Proof |
| --- | --- |
| `scripts/capture.py`, `src/recorder*`, `src/recording-*` | Desktop. The encoder, the portal and Hyprland's answers are all outside CI. |
| `src/exporter.cpp`, `src/background-blur.*`, `models/` | Tests, plus `tests/packaged-export.sh` on the packaged binary (CI does this). Desktop if the OpenGL setup changed. |
| `packaging/`, `CMakeLists.txt`, a new dependency | CI's fresh-container install. A dependency missing from `PKGBUILD.in` only shows there. |
| `snap/`, the picker, the screenshot editor | `build/snap/omasnap-smoke`. The picker on a real session is checked by hand. |
| `qml/`, the video editor | Tests: the preview is compared with the export. |
| A release | `./scripts/verify-release.sh --publish VERSION`, see [Releasing](releasing.md). Never publish a draft that hasn't passed it. |

## The proof bar

- **Run what ships.** Verify the package from CI or `./scripts/package.sh`, or `build/omacap`; say which in the evidence.
- **A stand-in counts only behind the boundary it replaces.** Synthetic sources replace the portal and the devices, nothing after them: the encoder, the hold and the muxer in a test are the ones a recording uses. When a path is chosen at runtime (the encoder is), the tests run every choice, and a software stand-in covers the ones CI has no hardware for.
- **Check that the trigger happened.** Hyprland answers `ok`, or exits 0 with `unknown request`, to requests it didn't carry out. A test that changes the display asserts the answer, or it proves nothing.
- **Show the result, not the launch.** Evidence is a file with the expected frames, a decoded recording with no OmaCap pixel in it, `VERIFICATION.txt`. "It opened" is not evidence.
- **Name what you could not reach**, and why, in the same message as the result.

## Working on a desktop someone is using

- Record and drive overlays in the nested Hyprland (`scripts/nested-hyprland.sh`, `scripts/nested-portal.sh`), never on the live session. `verify-desktop.sh` does this for you and stops the nested compositor when it ends.
- After stopping it, `hyprctl instances` lists one instance. A leftover one keeps rendering on a hidden workspace.
- The camera and microphone are the user's. Turn them on only when asked.

## Gotchas no config confesses

- Encoders may restamp buffers. `nvh264enc` and every gst-libav encoder start at 1000 hours; `vp8enc` keeps the input's time. Code after an encoder reads capture time from running time (`captured()` in `scripts/capture.py`).
- The exporter needs Xwayland, and finds it by the socket file `/tmp/.X11-unix/X0`. If that file is gone, every export fails with "No OpenGL context" while the X server is still running. `doctor.sh` detects it and prints the repair.
- An agent's shell may carry `LD_LIBRARY_PATH` from an AppImage, which breaks system binaries such as `hyprctl`. Unset it.
- Omarchy stable lags Arch, and desktops run ahead of both. A release is built on stable and must pass on all three, which is why the release workflow also builds on current Arch and the last check runs on a desktop.
