#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BASELINE_DIR="${1:-/tmp/vb_engine_baseline_samples}"

mkdir -p "${BASELINE_DIR}"
rm -f "${BASELINE_DIR}"/*.wav
cp "${REPO_ROOT}"/Samples/*.wav "${BASELINE_DIR}"/

echo "Captured audio baseline in ${BASELINE_DIR}"
