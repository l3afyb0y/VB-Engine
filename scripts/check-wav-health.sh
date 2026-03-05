#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
WAV_DIR="${1:-${REPO_ROOT}/Samples}"
if [[ -f "${WAV_DIR}" ]]; then
  WAV_DIR="$(dirname "${WAV_DIR}")"
fi
if [[ ! -d "${WAV_DIR}" ]]; then
  echo "error: WAV path must be a directory or a file inside a directory: ${WAV_DIR}" >&2
  exit 2
fi

cmake --preset debug
cmake --build --preset debug --target vb_engine_analyze_wavs
"${REPO_ROOT}/build/debug/vb_engine_analyze_wavs" "${WAV_DIR}"
