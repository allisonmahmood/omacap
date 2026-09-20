# OmaCap

[![CI](https://github.com/allisonmahmood/omacap/actions/workflows/ci.yml/badge.svg)](https://github.com/allisonmahmood/omacap/actions/workflows/ci.yml)

A local screen recorder and quick video editor for Omarchy. Record a window or display, clean up a demo, and export an MP4 or GIF.

- Optional microphone, desktop audio and camera recording, with separate audio mute controls.
- Crop, wallpaper or solid backgrounds, padding, rounded video corners and shadows.
- A movable camera overlay, trimming, cuts and deliberate zoom sections.
- One preview for editing and visual treatment. No project library, account or cloud upload.

The controls follow your Omarchy theme. New recordings use your current wallpaper as the video background. Each recording keeps its own copy, so switching themes does not change an edit in progress.

## Install

OmaCap targets current Omarchy on Arch Linux with Hyprland, PipeWire and the Hyprland desktop portal. It requires Qt 6.5 or newer and a working OpenGL driver. Other desktops and distributions are not tested.

Clone the repository and build an Arch package from the checkout:

```sh
git clone https://github.com/allisonmahmood/omacap.git
cd omacap/packaging
makepkg -si
```

Arch's `base-devel` and `git` packages are needed to build. `makepkg -si` installs the dependencies listed in [packaging/PKGBUILD](packaging/PKGBUILD). The recipe builds the parent checkout; it is not a standalone AUR package.

Launch **OmaCap** from the application launcher or run `omacap`. Open an existing video with `omacap /path/to/video.mp4`.

For a local development build, after installing the same dependencies:

```sh
./scripts/build.sh
./build/omacap
```

`./scripts/install-local.sh` installs the binary and launcher for your user under `~/.local`. Use either the package installation or the local installation to avoid competing copies.

## Record and edit

1. Choose your microphone, camera and desktop audio options, then start recording. Select a window or display in the system sharing picker.
2. Stop recording to open the editor. Crop the picture, adjust its background and padding, and place the camera overlay.
3. Drag the timeline selection handles. **Keep selection** trims the ends; **Delete selection** removes an internal section.
4. Add a zoom at the playhead, choose its focus in the preview, and confirm. Drag its timeline block or handles to change its timing. Zoom sections cannot overlap.
5. Export to a local file. Choose MP4 or GIF, resolution, frame rate and quality. GIF has no audio.

Space plays or pauses. Left and Right seek by a frame. Ctrl+Z undoes and Ctrl+Shift+Z redoes.

There is no saved-project format. An unfinished edit has a recovery checkpoint. Export leaves the editor open for another export; closing or discarding removes the temporary session media. Imported originals and exported files are kept.

## Known limitations

- Keep a selected window at a fixed size while recording. Resizing it or moving it between monitors with different scales can interrupt the Hyprland portal stream. OmaCap reports the failure and retains readable partial media. Use display capture when your demo needs window resizing.
- Output uses a landscape 16:9 canvas and targets SDR footage and short demos. There is no cursor tracking or automatic zoom.
- GIF file size estimates are approximate.
- Automated tests use generated camera and audio tracks. Physical-device synchronization and portal capture still need testing on the target desktop.

The app does not restart desktop services or modify desktop configuration.

## Development and tests

The app uses C++20 and Qt Quick for the editor. A Python/GStreamer worker captures through the desktop portal. Exports render the same QML composition in a separate process and encode through system FFmpeg.

```sh
./scripts/build.sh
./scripts/test.sh
```

Tests additionally require `python-numpy`. They generate their own media and theme, and use isolated application data. The suite covers editing, recovery, theme changes, capture with synthetic tracks, MP4/GIF export, cancellation and audio/video timing after cuts. Outputs are written under `tests/out`.

GitHub Actions builds on Arch Linux and runs the suite with a virtual display and software OpenGL. This checks the application without requiring a camera, microphone or sharing picker. It does not certify hardware capture or real-time GPU performance. See [.github/workflows/ci.yml](.github/workflows/ci.yml) for the CI environment.

For desktop validation, record a test window with the microphone and camera you intend to use, then play and export the result. Keep recordings containing private information out of issues and pull requests.

## License

[MIT](LICENSE). Installed Qt and multimedia dependencies retain their own licenses. OmaCap is an independent project and contains no Cap code or assets.
