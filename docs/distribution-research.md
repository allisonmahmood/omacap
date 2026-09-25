# Publishing OmaCap for Omarchy

Investigated 2026-09-25. The decision is to release on GitHub first and defer AUR. See [the release guide](releasing.md) for the implementation. The original proposal below is retained as research.

AUR package pushes reopened in August, but new account registration remains closed according to [the latest maintainer response found](https://lists.archlinux.org/archives/list/aur-general%40lists.archlinux.org/thread/62O5WXJRV6VZESY7WBAFL7SZKJVTF6DW/). The maintainer does not currently have a confirmed AUR account, so AUR is not a first-release dependency.

## Recommended route

Publish a versioned GitHub release and an AUR `omacap-bin` package. The intended install command is `yay -S omacap-bin`, with no local compilation. Omarchy users can also find it under **Install > AUR** once the package exists. Normal **Update > Omarchy** includes installed AUR packages. There is no need for an OmaCap updater or a separately maintained pacman repository. The install and update behavior is documented in Omarchy's [package manual](https://github.com/omacom/omarchy/blob/93e8cd56b19df756a6435b0c0e8ed5073d192c36/manual/29-other-packages.md) and [update manual](https://github.com/omacom/omarchy/blob/93e8cd56b19df756a6435b0c0e8ed5073d192c36/manual/30-updates.md).

Build and test the binary against the supported Omarchy stable package set. The current manual says its stable Arch mirror runs one month behind latest Arch. A binary built against newer Qt, FFmpeg or system libraries might therefore fail on stable Omarchy. This compatibility risk is an inference from the mirror policy and OmaCap's shared-library dependencies. Keep latest-Arch CI as an additional check, but it does not establish stable-Omarchy compatibility. If stable binary builds need more work, publish a source-building `omacap` AUR package first. [Omarchy update policy](https://github.com/omacom/omarchy/blob/93e8cd56b19df756a6435b0c0e8ed5073d192c36/manual/30-updates.md)

## Current state

The main-agent audit found these at commit `c13d8711b4224d689408d7110bd9b69fec5eff40`:

- The GitHub repository is public and MIT-licensed, with no tags or releases yet.
- [The latest inspected CI run passed](https://github.com/allisonmahmood/omacap/actions/runs/35591301901). CI builds the application but does not build or install the Arch package.
- `packaging/PKGBUILD` builds from `$startdir/..`, with no downloadable `source` or checksum. It needs the whole checkout and cannot be submitted as a standalone AUR recipe.
- The recipe declares `aarch64`, while CI covers only `x86_64`. Start binary releases with `x86_64` unless ARM builds and runtime checks are added.
- `0.1.0` is hardcoded in the application and package recipe. QML and Python capture code are embedded through `resources.qrc`.

The [AUR info API](https://aur.archlinux.org/rpc/v5/info?arg%5B%5D=omacap&arg%5B%5D=omacap-bin) returned zero matches for `omacap` and `omacap-bin` during this investigation. Recheck availability before submitting.

## Release work

1. Make the package recipe build a versioned source archive independently of a checkout. Use a fixed source URL and real checksum, building inside `$srcdir` and installing into `$pkgdir`. These are standard [PKGBUILD mechanisms](https://man.archlinux.org/man/PKGBUILD.5.en).
2. Add a release workflow for version tags. Check that the tag, app version and package version agree. Reuse the existing tests through a callable workflow or shared commands; GitHub supports [reusable workflows](https://docs.github.com/en/actions/how-tos/reuse-automations/reuse-workflows).
3. Build with `makepkg` as an unprivileged user in a clean Arch environment configured for supported Omarchy stable repositories. Install the resulting package in a separate disposable environment. Verify the installed app, launcher/icon, dependencies and existing relevant tests. Run an actual portal recording and MP4/GIF export on stable Omarchy before the first release; virtual-display tests do not cover that interaction.
4. Attach the native `x86_64` `.pkg.tar.zst`, `SHA256SUMS` and concise release notes to `v0.1.0`. Record tested Omarchy version/channel and limitations. A local downloaded package installs with `sudo pacman -U ./<package>.pkg.tar.zst`; this alone does not create an update feed. [pacman operations](https://man.archlinux.org/man/pacman.8.en)
5. Publish only after checks pass. Prefer immutable GitHub releases, staging all assets before final publication. This locks the tag and assets and produces a release attestation. [Immutable releases](https://docs.github.com/en/code-security/concepts/supply-chain-security/immutable-releases)
6. Publish the AUR recipe against those exact assets, then replace the README's clone-and-build instructions with the verified one-command install. Keep source-build instructions for contributors.

## AUR details

Use `omacap` for a release built from source and `omacap-bin` for prebuilt binaries. AUR requires the `-bin` suffix for prebuilts when source is available. Upload the recipe and metadata, not binary packages, to AUR. For the binary variant, declare `provides=("omacap=$pkgver")` and `conflicts=('omacap')`, and install the same executable and desktop entry. [AUR guidelines](https://wiki.archlinux.org/title/AUR_submission_guidelines), [PKGBUILD provides/conflicts](https://man.archlinux.org/man/PKGBUILD.5.en)

The maintainer needs an AUR account with an SSH public key registered. Each package has a separate Git repository. Commit `PKGBUILD`, generated `.SRCINFO` and required helper files to its `master` branch. Regenerate `.SRCINFO` whenever package metadata changes. For each release, update version and checksum; increment `pkgrel` for packaging-only changes. Publishing a GitHub release alone does not update AUR. Start with a reviewed manual AUR push; automate it later if useful. [Submission/authentication](https://wiki.archlinux.org/title/AUR_submission_guidelines), [.SRCINFO generation](https://wiki.archlinux.org/title/.SRCINFO)

Use fixed checksums in the binary recipe. SHA-256 checksums detect changed bytes but are not publisher signatures. If adding PGP-signed upstream assets, `makepkg` can verify detached signatures with pinned `validpgpkeys`. Direct binary installation uses pacman's local/remote signature policy; do not instruct users to disable it. AUR packaging avoids requiring a custom repository/keyring setup. [Source verification](https://man.archlinux.org/man/PKGBUILD.5.en), [pacman signature policy](https://man.archlinux.org/man/pacman.conf.5.en)

## Reaching Omarchy users

After someone else verifies a clean install, share a short real recording, the repository link and the install command in Omarchy's existing [Show and tell discussions](https://github.com/omacom/omarchy/discussions/categories/show-and-tell). That category is specifically for things people have made. No post has been sent.

An optional later step is to propose a package in [Omarchy's package repository](https://github.com/omacom/omarchy-pkgs). Its documented tooling imports AUR recipes, maintains Omarchy-owned recipes and follows upstream releases directly. Maintainers would decide whether to accept OmaCap. This is a distribution improvement after the first release, not a prerequisite for other Omarchy users to install it. [Package repository workflow](https://github.com/omacom/omarchy-pkgs/blob/master/README.md)

Do not advertise `omarchy pkg add omacap-bin` for an AUR-only package. The inspected implementation uses pacman; AUR has a separate helper which uses yay. [Repository package helper](https://github.com/omacom/omarchy/blob/93e8cd56b19df756a6435b0c0e8ed5073d192c36/bin/omarchy-pkg-add), [AUR helper](https://github.com/omacom/omarchy/blob/93e8cd56b19df756a6435b0c0e8ed5073d192c36/bin/omarchy-pkg-aur-add)
