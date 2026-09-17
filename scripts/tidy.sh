#!/usr/bin/env bash
set -euo pipefail

preset="${1:-debug}"

run-clang-tidy -p "build/$preset" src tests include benchmark
