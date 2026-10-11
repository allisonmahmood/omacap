#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cmake -S "$repo" -B "$repo/build" -G Ninja -DCMAKE_BUILD_TYPE="${BUILD_TYPE:-RelWithDebInfo}" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build "$repo/build" --parallel "${JOBS:-$(nproc)}"
