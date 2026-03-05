#!/usr/bin/env bash
set -euo pipefail

BASELINE_DIR="${1:-/tmp/vb_engine_baseline_samples}"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [ ! -d "$BASELINE_DIR" ]; then
  echo "Baseline directory not found: $BASELINE_DIR" >&2
  exit 2
fi

cmake --preset debug
cmake --build --preset debug --target vb_engine_sample_generator vb_engine_compare_wavs
"$REPO_ROOT/build/debug/vb_engine_sample_generator"
"$REPO_ROOT/build/debug/vb_engine_compare_wavs" "$BASELINE_DIR" "$REPO_ROOT/Samples"
