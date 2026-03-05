#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ASSET_ROOT="${REPO_ROOT}/Samples/Piano-Library"
mkdir -p "${ASSET_ROOT}/downloads" "${ASSET_ROOT}/libraries"

usage() {
  cat <<USAGE
Usage:
  bash scripts/fetch-piano-assets.sh salamander
  bash scripts/fetch-piano-assets.sh upright-kw
  bash scripts/fetch-piano-assets.sh all
  bash scripts/fetch-piano-assets.sh --force salamander

This script downloads free/redistributable SFZ libraries and prepares
Samples/Piano-Library/default.sfz for VB-Engine.
USAGE
}

force=0
if [[ "${1:-}" == "--force" ]]; then
  force=1
  shift
fi

if [[ $# -ne 1 ]]; then
  usage
  exit 2
fi

choice="$1"

require_bin() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "Required command not found: $1" >&2
    exit 3
  fi
}

pick_default_sfz() {
  local library_root="$1"
  local preferred=""
  case "${library_root}" in
    *salamander-v3*)
      preferred="$(find "${library_root}" -type f -iname 'SalamanderGrandPiano-V3*.sfz' | head -n 1 || true)"
      ;;
    *upright-piano-kw*)
      preferred="$(find "${library_root}" -type f -iname '*Upright*.sfz' | head -n 1 || true)"
      ;;
  esac
  if [[ -n "${preferred}" ]]; then
    printf '%s\n' "${preferred}"
    return 0
  fi
  find "${library_root}" -type f -iname '*.sfz' | head -n 1
}

fetch_one() {
  local lib="$1"
  local archive_url=""
  local archive_path=""
  local extract_dir=""

  case "${lib}" in
    salamander)
      # Source: FreePats mirror, license CC BY 3.0.
      archive_url="https://freepats.zenvoid.org/Piano/SalamanderGrandPiano/SalamanderGrandPiano-SFZ+FLAC-V3+20200602.tar.gz"
      archive_path="${ASSET_ROOT}/downloads/salamander-v3.tar.gz"
      extract_dir="${ASSET_ROOT}/libraries/salamander-v3"
      ;;
    upright-kw)
      # Source: FreePats mirror, license CC0.
      archive_url="https://freepats.zenvoid.org/Piano/UprightPianoKW/UprightPianoKW-SFZ-20220221.7z"
      archive_path="${ASSET_ROOT}/downloads/upright-piano-kw.7z"
      extract_dir="${ASSET_ROOT}/libraries/upright-piano-kw"
      ;;
    *)
      echo "Unsupported library: ${lib}" >&2
      return 2
      ;;
  esac

  local existing_sfz
  existing_sfz="$(find "${extract_dir}" -type f -iname '*.sfz' | head -n 1 || true)"
  if [[ "${force}" -eq 0 && -n "${existing_sfz}" ]]; then
    echo "Already present, skipping download: ${lib}" >&2
    printf '%s\n' "${existing_sfz}"
    return 0
  fi

  require_bin curl
  if [[ "${archive_path}" == *.tar.gz ]]; then
    require_bin tar
  fi
  if [[ "${archive_path}" == *.7z ]]; then
    require_bin 7z
  fi

  echo "Downloading ${lib}..." >&2
  curl -L --fail --output "${archive_path}" "${archive_url}"

  rm -rf "${extract_dir}"
  mkdir -p "${extract_dir}"

  if [[ "${archive_path}" == *.tar.gz ]]; then
    tar -xzf "${archive_path}" -C "${extract_dir}"
  elif [[ "${archive_path}" == *.7z ]]; then
    7z x "${archive_path}" -o"${extract_dir}" >/dev/null
  else
    echo "Unsupported archive format: ${archive_path}" >&2
    return 3
  fi

  local sfz_file
  sfz_file="$(pick_default_sfz "${extract_dir}" || true)"
  if [[ -z "${sfz_file}" ]]; then
    echo "No .sfz file found after extraction for ${lib}" >&2
    return 4
  fi

  printf '%s\n' "${sfz_file}"
}

set_default_link() {
  local sfz_file="$1"
  ln -sfn "${sfz_file}" "${ASSET_ROOT}/default.sfz"
  echo "Prepared default SFZ: ${ASSET_ROOT}/default.sfz -> ${sfz_file}"
}

if [[ "${choice}" == "all" ]]; then
  salamander_sfz="$(fetch_one salamander)"
  upright_sfz="$(fetch_one upright-kw)"
  if [[ -L "${ASSET_ROOT}/default.sfz" || -f "${ASSET_ROOT}/default.sfz" ]]; then
    echo "Kept existing default SFZ target: ${ASSET_ROOT}/default.sfz"
  else
    set_default_link "${salamander_sfz}"
  fi
  echo "Fetched libraries:"
  echo "  - salamander: ${salamander_sfz}"
  echo "  - upright-kw: ${upright_sfz}"
  echo "You can now run: bash scripts/generate-samples.sh"
  exit 0
fi

selected_sfz="$(fetch_one "${choice}")"
set_default_link "${selected_sfz}"
echo "You can now run: bash scripts/generate-samples.sh"
