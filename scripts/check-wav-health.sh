#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${REPO_ROOT}"

TARGET_DIR="${1:-Samples}"
cargo run --bin vb_engine -- check-wav-health "${TARGET_DIR}"
