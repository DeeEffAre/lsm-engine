#!/usr/bin/env bash
set -euo pipefail

preset="${1:-debug}"

cppcheck \
  --enable=warning,performance,portability,style \
  --project="build/$preset/compile_commands.json" \
  --suppress=missingIncludeSystem \
  --inline-suppr \
  -I include \
  src \
  tests \
  benchmark
