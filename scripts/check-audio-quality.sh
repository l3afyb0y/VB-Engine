#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${REPO_ROOT}"

BASELINE_DIR="${1:?baseline directory required}"
CANDIDATE_DIR="${2:-Samples}"

cargo run --bin vb_engine -- compare-wavs "${BASELINE_DIR}" "${CANDIDATE_DIR}"
