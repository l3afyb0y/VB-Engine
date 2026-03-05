# Packaging and Install Plan

## Arch Linux (primary)
- Official workflow:
  - `./scripts/install-arch.sh`
- Package workflows:
  - top-level `PKGBUILD` for repository-source installs

## Generic Linux
- `./scripts/install-linux.sh`
- Uses CMake configure/build/install.
- Installs headers, shared library, and standalone binary.

## One-and-done Commands
- Arch:
  - `bash scripts/install-arch.sh`
- Generic Linux:
  - `bash scripts/install-linux.sh`

## Dependency Policy
- Prefer system packages for toolchain/runtime dependencies.
- Any asset or dependency must be redistributable or auto-installed via package manager/script.
