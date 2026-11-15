#!/usr/bin/env bash
# Verifies a draft release on a real Omarchy desktop, and publishes it with --publish.
#
#   scripts/verify-release.sh [--publish] VERSION
#
# Downloads the draft's files, checks them, and runs scripts/verify-desktop.sh on the package
# the release will ship. Run it from a clean checkout of the release's commit, so the tests are
# the ones that commit has. With --publish, the evidence is attached and the release goes
# public. What is published is the release that was verified: a tag can come to name another
# draft while the checks run, so the draft is held by its id and its files by their checksums.
# A failure leaves the draft as it was.
set -euo pipefail
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$repo"
publish=false
[[ ${1:-} == --publish ]] && { publish=true; shift; }
version=${1:?the version to verify, e.g. 0.2.1}
tag=v$version
evidence=VERIFICATION.txt

release=$(gh release view "$tag" --json databaseId,isDraft --jq 'select(.isDraft) | .databaseId')
[[ -n $release ]] || { echo "$tag is not a draft; published releases are never changed." >&2; exit 1; }
api="repos/{owner}/{repo}/releases/$release"
# The draft's files as GitHub holds them, one "checksum  name" a line as sha256sum prints
# them, without the evidence. Nothing once it is no longer a draft.
held() {
  gh api "$api" --jq "select(.draft) | .assets[] | select(.name != \"$evidence\")
    | \"\\(.digest | ltrimstr(\"sha256:\"))  \\(.name)\"" | sort
}

assets=$(mktemp -d)
trap 'rm -rf -- "$assets"' EXIT
gh release download "$tag" --dir "$assets"
rm -f -- "$assets/$evidence"
verified=$(cd "$assets" && sha256sum -- * | sort)
# Whether the draft still holds exactly the verified files. A failed request is a no.
unchanged() { now=$(held) && [[ $now == "$verified" ]]; }
unchanged || { echo "The files downloaded for $tag are not the ones draft $release holds." >&2; exit 1; }
(cd "$assets" && sha256sum --check SHA256SUMS)
commit=$(sed -n 's/^Commit: //p' "$assets/BUILD-INFO.txt")
[[ $(git rev-parse HEAD) == "$commit" ]] ||
  { echo "Check out the release's commit first: git switch --detach $commit" >&2; exit 1; }
# Edited tests or scripts would verify something the commit doesn't.
changes=$(git status --porcelain)
[[ -z $changes ]] || { echo "This checkout has changes; verify from a clean checkout of $commit." >&2; exit 1; }

"$repo/scripts/verify-desktop.sh" "$assets/omacap-$version-1-x86_64.pkg.tar.zst"
grep -q "^OmaCap $version " "$evidence"

if ! $publish; then
  echo "Verified, not published. Run again with --publish."
  exit 0
fi
unchanged || { echo "$tag changed while it was being verified; nothing was published." >&2; exit 1; }
gh release upload "$tag" "$evidence" --clobber
# The upload went by tag: check it reached this draft, and that nothing else moved meanwhile.
attached=$(gh api "$api" --jq ".assets[] | select(.name == \"$evidence\") | .digest")
local_digest=$(sha256sum "$evidence" | cut -d' ' -f1)
[[ $attached == "sha256:$local_digest" ]] && unchanged ||
  { echo "$tag changed while it was being verified; nothing was published." >&2; exit 1; }
gh api --method PATCH "$api" -F draft=false -f make_latest=true --jq '"Published \(.html_url)"'
