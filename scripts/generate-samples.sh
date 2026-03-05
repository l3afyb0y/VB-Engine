#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

cmake --preset release
cmake --build --preset release --target vb_engine_sample_generator
"${REPO_ROOT}/build/release/vb_engine_sample_generator"
