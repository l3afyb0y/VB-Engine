#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

sudo pacman -Sy --needed --noconfirm base-devel cmake ninja git
cd "${REPO_ROOT}"
makepkg -si --noconfirm
