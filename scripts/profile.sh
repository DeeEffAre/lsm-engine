#!/usr/bin/env bash
set -euo pipefail

cmake --preset profile
cmake --build --preset profile

BIN_PATH="build/profile/bin/myapp"

if [ ! -f "$BIN_PATH" ]; then
    echo "Error: Binary not found at $BIN_PATH" >&2
    exit 1
fi

echo "Starting perf record..."
# 997 Hz sampling frequency avoids timer interrupt aliasing/lockstep artifacts
perf record -F 997 --call-graph fp -e cycles,instructions,cache-misses,branch-misses:u -o perf.data -- "$BIN_PATH" "$@"

echo "Recording finished. Generating report..."
perf report
