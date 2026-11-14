# Releasing OmaCap

GitHub hosts the downloadable Arch packages. AUR distribution is deferred. The release workflow prepares a draft; it never publishes automatically. No extra Actions secrets are needed.

## Prepare a release

1. Set `VERSION` to the next version, such as `0.1.1`, and add `releases/0.1.1.md` with changes, installation steps and known limitations. The application and package take their version from `VERSION`.
2. Merge the changes into `main` after CI passes.
3. Open [Actions > Prepare release](https://github.com/allisonmahmood/omacap/actions/workflows/release.yml), select `main`, and click **Run workflow**. The workflow uses the exact commit selected at dispatch, even if `main` advances during the build.
4. On an Omarchy desktop, check out the commit the workflow summary names and run `./scripts/verify-release.sh --publish <version>`. It downloads the draft, runs its package through [the desktop checks](verifying.md), attaches the evidence as `VERIFICATION.txt` and publishes. Until then the release is visible only to people with repository write access. Don't publish from the release page: CI cannot record a screen, so a draft that only passed CI is untested where it matters.

The workflow refuses branches other than `main`, existing tags, and duplicate releases. It builds the package against Omarchy's stable Arch mirror and runs the tests, then installs the package in a fresh container, opens a video in its editor and exports one with a blurred camera. It downgrades the base container to the stable repository versions too. The same commit must also pass on current Arch before the draft is made, because desktops run newer packages than the stable mirror. The capture overlay and the recorder need Hyprland, which no container has; that is what step 4 covers.

The first draft can also be staged from the successful PR's `release-assets-omarchy-stable` artifact. Use the exact commit in `BUILD-INFO.txt` as its target. That draft contains the tested PR code; merging the PR makes the repeatable workflow available on `main`.

## Release contents

- `omacap-<version>-1-x86_64.pkg.tar.zst`, the installable application.
- `omacap-<version>.tar.gz`, the exact committed source used to build it.
- `PKGBUILD`, a standalone recipe with a checksum for that source archive. Place it alongside the source archive and run `makepkg -si` to rebuild.
- `BUILD-INFO.txt`, the source commit, build channel, workflow run and installed package versions.
- `SHA256SUMS`, checksums for every file above.
- `VERIFICATION.txt`, what `verify-release.sh` ran on a real desktop before publication, and the machine it ran on.

Download the package and `SHA256SUMS`, verify with `sha256sum --check --ignore-missing SHA256SUMS`, then install with `sudo pacman -U ./omacap-<version>-1-x86_64.pkg.tar.zst`. Checksums detect corruption; they are not publisher signatures. The package uses Omarchy's normal local-package policy. Do not disable signature verification to install it.

## Validation and maintenance

CI uses synthetic audio and video, a virtual X display and Omasnap's offscreen smoke tests. `verify-release.sh` adds what a container can't: recordings through the real portal in a nested Hyprland, with the encoder that desktop records with, through display changes, and exports through that session's display. Neither uses your devices or your own session. So before a release, also check by hand: a screenshot from the picker, copied and edited, and a recording with microphone and camera, stopped with the record key. Do not describe container checks as a full Omarchy desktop test.

Omarchy stable uses a delayed Arch mirror. Release binaries must come from the stable build, not the current-Arch CI artifact. `BUILD-INFO.txt` records the dependency versions used; rebuilding later can produce different binaries because the mirror moves. Publish a new version if a library update requires a rebuild. Keep published assets and tags unchanged.

If a draft preparation fails, fix the failure and rerun it. If asset upload fails after draft creation, delete only that unpublished draft before rerunning. Published releases are never overwritten by the workflow. Optional GitHub immutable releases can enforce this after publication.

GitHub downloads have no package update feed. Users install each new release with `pacman -U` and can subscribe to GitHub release notifications. AUR can be added later without changing the installed package name, `omacap`.

Build locally with `./scripts/package.sh`; use `-si` to also install dependencies and the package. Commit tracked changes first. This script packages `HEAD`, not uncommitted edits. `./scripts/build.sh` remains available for working on uncommitted code.
