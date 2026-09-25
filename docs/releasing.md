# Releasing OmaCap

GitHub hosts the downloadable Arch packages. AUR distribution is deferred. The release workflow prepares a draft; it never publishes automatically. No extra Actions secrets are needed.

## Prepare a release

1. Set `VERSION` to the next version, such as `0.1.1`, and add `releases/0.1.1.md` with changes, installation steps and known limitations. The application and package take their version from `VERSION`.
2. Merge the changes into `main` after CI passes.
3. Open [Actions > Prepare release](https://github.com/allisonmahmood/omacap/actions/workflows/release.yml), select `main`, and click **Run workflow**. The workflow uses the exact commit selected at dispatch, even if `main` advances during the build.
4. Open the draft linked in the workflow summary. Review the notes and assets, then click **Publish release**. Until then the release is visible only to people with repository write access.

The workflow refuses branches other than `main`, existing tags, and duplicate releases. It builds against Omarchy's stable Arch mirror, runs the integration tests, then installs and launches the resulting package in a fresh container before creating the draft. It downgrades the base container to the stable repository versions too. Current-Arch compatibility is checked separately by CI.

The first draft can also be staged from the successful PR's `release-assets-omarchy-stable` artifact. Use the exact commit in `BUILD-INFO.txt` as its target. That draft contains the tested PR code; merging the PR makes the repeatable workflow available on `main`.

## Release contents

- `omacap-<version>-1-x86_64.pkg.tar.zst`, the installable application.
- `omacap-<version>.tar.gz`, the exact committed source used to build it.
- `PKGBUILD`, a standalone recipe with a checksum for that source archive. Place it alongside the source archive and run `makepkg -si` to rebuild.
- `BUILD-INFO.txt`, the source commit, build channel, workflow run and installed package versions.
- `SHA256SUMS`, checksums for every file above.

Download the package and `SHA256SUMS`, verify with `sha256sum --check --ignore-missing SHA256SUMS`, then install with `sudo pacman -U ./omacap-<version>-1-x86_64.pkg.tar.zst`. Checksums detect corruption; they are not publisher signatures. The package uses Omarchy's normal local-package policy. Do not disable signature verification to install it.

## Validation and maintenance

Automated tests use synthetic audio/video and a virtual X display. They do not establish that real Hyprland portal capture, microphone or camera hardware works. Before a release, check those on Omarchy stable, including an MP4 and GIF export. Do not describe container checks as a full Omarchy desktop test.

Omarchy stable uses a delayed Arch mirror. Release binaries must come from the stable build, not the current-Arch CI artifact. `BUILD-INFO.txt` records the dependency versions used; rebuilding later can produce different binaries because the mirror moves. Publish a new version if a library update requires a rebuild. Keep published assets and tags unchanged.

If a draft preparation fails, fix the failure and rerun it. If asset upload fails after draft creation, delete only that unpublished draft before rerunning. Published releases are never overwritten by the workflow. Optional GitHub immutable releases can enforce this after publication.

GitHub downloads have no package update feed. Users install each new release with `pacman -U` and can subscribe to GitHub release notifications. AUR can be added later without changing the installed package name, `omacap`.

Build locally with `./scripts/package.sh`; use `-si` to also install dependencies and the package. Commit tracked changes first. This script packages `HEAD`, not uncommitted edits. `./scripts/build.sh` remains available for working on uncommitted code.
