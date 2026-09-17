#!/usr/bin/env bash
set -euo pipefail

preset="${1:-debug}"

cmake --preset "$preset"

ln -sfn "build/$preset/compile_commands.json" compile_commands.json

echo "OK: compile_commands.json -> build/$preset/compile_commands.json"
