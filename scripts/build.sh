#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
mkdir -p "$repo/build"
cd "$repo/build"
qmake6 "$repo/omacap.pro"
make -j"${JOBS:-4}"
