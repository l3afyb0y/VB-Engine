#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="${SCRIPT_DIR}"

PLUGIN_BASENAME="${VB_VST3_BASENAME:-VB-Engine-Piano}"
PLUGIN_BUNDLE_NAME="${PLUGIN_BASENAME}.vst3"
TARGET_DIR="${REPO_ROOT}/target"
PROFILE="release"
ARCH_SUFFIX="$(uname -m)-linux"
STAGE_ROOT="${TARGET_DIR}/vst3-bundles"
STAGED_BUNDLE_DIR="${STAGE_ROOT}/${PLUGIN_BUNDLE_NAME}"
STAGED_BINARY_PATH="${STAGED_BUNDLE_DIR}/Contents/${ARCH_SUFFIX}/${PLUGIN_BASENAME}.so"
BUILD_BINARY_PATH="${TARGET_DIR}/${PROFILE}/libvb_engine_vst3.so"
DEST_ROOT="${1:-${VST3_DIR:-${HOME}/.vst3}}"
DEST_BUNDLE_DIR="${DEST_ROOT}/${PLUGIN_BUNDLE_NAME}"

echo "Building vb-engine-vst3 (${PROFILE})..."
cargo build -p vb-engine-vst3 --release --manifest-path "${REPO_ROOT}/Cargo.toml"

if [[ ! -f "${BUILD_BINARY_PATH}" ]]; then
    echo "Expected built plugin at '${BUILD_BINARY_PATH}', but it was not found." >&2
    exit 1
fi

echo "Staging Linux VST3 bundle..."
rm -rf "${STAGED_BUNDLE_DIR}"
mkdir -p "${STAGED_BUNDLE_DIR}/Contents/${ARCH_SUFFIX}"
mkdir -p "${STAGED_BUNDLE_DIR}/Contents/Resources"
install -m755 "${BUILD_BINARY_PATH}" "${STAGED_BINARY_PATH}"

echo "Installing bundle to '${DEST_ROOT}'..."
mkdir -p "${DEST_ROOT}"
rm -rf "${DEST_BUNDLE_DIR}"
cp -a "${STAGED_BUNDLE_DIR}" "${DEST_BUNDLE_DIR}"

echo
echo "Installed:"
echo "  ${DEST_BUNDLE_DIR}"
echo
echo "Bundle binary:"
echo "  ${DEST_BUNDLE_DIR}/Contents/${ARCH_SUFFIX}/${PLUGIN_BASENAME}.so"
echo
echo "You may need to rescan VST3 plugins in your host."
