#!/usr/bin/env bash
# Build the committed source, producing a standalone recipe and source archive too.
set -euo pipefail
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$repo"
if [[ -n $(git status --porcelain --untracked-files=no) ]]; then
  echo 'Commit tracked changes before packaging so the source matches its commit.' >&2
  exit 1
fi
version=$(cat VERSION)
if [[ ! $version =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo 'VERSION must contain a version such as 0.1.0.' >&2
  exit 1
fi
if [[ $(uname -m) != x86_64 ]]; then
  echo 'Release packages currently support x86_64 only.' >&2
  exit 1
fi
mkdir -p build/package dist
archive="omacap-$version.tar.gz"
git archive --format=tar --prefix="omacap-$version/" HEAD | gzip -n > "dist/$archive"
checksum=$(sha256sum "dist/$archive")
sed -e "s/@VERSION@/$version/g" -e "s/@SHA256@/${checksum%% *}/g" \
  packaging/PKGBUILD.in > dist/PKGBUILD
cp "dist/$archive" dist/PKGBUILD build/package/
cd build/package
PKGDEST="$repo/dist" makepkg --cleanbuild --force "$@"
