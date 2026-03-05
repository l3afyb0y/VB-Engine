#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build/release-local"
INSTALL_PREFIX="${INSTALL_PREFIX:-/usr/local}"

cmake -S "${REPO_ROOT}" -B "${BUILD_DIR}" \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DVB_ENGINE_BUILD_TESTS=ON \
  -DVB_ENGINE_BUILD_BENCHMARKS=ON

cmake --build "${BUILD_DIR}"
ctest --test-dir "${BUILD_DIR}" --output-on-failure

if [ "${INSTALL_PREFIX}" = "/usr/local" ]; then
  sudo cmake --install "${BUILD_DIR}" --prefix "${INSTALL_PREFIX}"
else
  cmake --install "${BUILD_DIR}" --prefix "${INSTALL_PREFIX}"
fi
