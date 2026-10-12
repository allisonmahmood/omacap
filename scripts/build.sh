#!/usr/bin/env bash
# Configures and builds into build/. Arguments go to the build step, e.g. `--target omacap`.
set -euo pipefail
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cmake -S "$repo" -B "$repo/build" -G Ninja -DCMAKE_BUILD_TYPE="${BUILD_TYPE:-RelWithDebInfo}" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON --log-level=NOTICE
cmake --build "$repo/build" --parallel "${JOBS:-$(nproc)}" "$@"
