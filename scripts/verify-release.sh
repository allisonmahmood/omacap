#!/usr/bin/env bash
# Verifies a draft release on a real Omarchy desktop, and publishes it with --publish.
#
#   scripts/verify-release.sh [--publish] VERSION
#
# Downloads the draft's files, checks them, and runs scripts/verify-desktop.sh on the package
# the release will ship. Run it from a checkout of the release's commit, so the tests match
# the code in the package. With --publish, the evidence is attached to the release and the
# release goes public; a failure leaves the draft as it was.
set -euo pipefail
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
publish=false
[[ ${1:-} == --publish ]] && { publish=true; shift; }
version=${1:?the version to verify, e.g. 0.2.1}
tag=v$version

[[ $(gh release view "$tag" --json isDraft --jq .isDraft) == true ]] ||
  { echo "$tag is not a draft; published releases are never changed." >&2; exit 1; }
assets=$(mktemp -d)
trap 'rm -rf -- "$assets"' EXIT
gh release download "$tag" --dir "$assets"
(cd "$assets" && sha256sum --check SHA256SUMS)
commit=$(sed -n 's/^Commit: //p' "$assets/BUILD-INFO.txt")
[[ $(git -C "$repo" rev-parse HEAD) == "$commit" ]] ||
  { echo "Check out the release's commit first: git switch --detach $commit" >&2; exit 1; }
package=$assets/omacap-$version-1-x86_64.pkg.tar.zst

"$repo/scripts/verify-desktop.sh" "$package"
grep -q "^OmaCap $version " "$repo/VERIFICATION.txt"

if $publish; then
  gh release upload "$tag" "$repo/VERIFICATION.txt" --clobber
  gh release edit "$tag" --draft=false --latest >/dev/null
  echo "Published $(gh release view "$tag" --json url --jq .url)"
else
  echo "Verified, not published. Run again with --publish."
fi
