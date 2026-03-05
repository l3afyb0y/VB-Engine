#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${REPO_ROOT}"

cmake --preset debug
cmake --build --preset debug
ctest --test-dir build/debug --output-on-failure

cmake --preset release
cmake --build --preset release
MIN_MEDIAN_RTF="${MIN_MEDIAN_RTF:-1.00}" BENCHMARK_BIN=./build/release/vb_engine_benchmark ./scripts/check-performance.sh 3

cmake --preset asan-ubsan
cmake --build --preset asan-ubsan
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ctest --test-dir build/asan-ubsan --output-on-failure

cmake --preset tsan
cmake --build --preset tsan
ctest --test-dir build/tsan --output-on-failure

bash ./scripts/generate-samples.sh
bash ./scripts/check-wav-health.sh
