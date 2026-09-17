#!/usr/bin/env bash
set -euo pipefail

preset="${1:-release}"
shift || true

cmake --preset "$preset"
cmake --build --preset "$preset" --target storage_engine_bench

exec "build/$preset/bin/storage_engine_bench" "$@"
