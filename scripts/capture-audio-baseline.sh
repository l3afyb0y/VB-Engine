#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${REPO_ROOT}"

OUT_DIR="${1:?output directory required}"
mkdir -p "${OUT_DIR}"
cargo run --bin render_showcase -- "${OUT_DIR}"
