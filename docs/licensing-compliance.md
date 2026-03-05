# Licensing and Compliance

## Project License
- VB-Engine source code is licensed under **MIT**.
- Canonical license file: `LICENSE`.

## Third-Party Components

### `dr_flac`
- Location: `src/third_party/dr_flac.h`
- Upstream: https://github.com/mackron/dr_libs
- License model in header: **public domain OR MIT-0** (upstream dual grant)
- Internal note: `src/third_party/README.md`
- Purpose: FLAC decoding for SFZ-referenced samples.

## Redistribution Guidance
- Keep `LICENSE` in redistributions of VB-Engine source/binaries.
- When redistributing builds that include `dr_flac.h`, preserve upstream attribution/license notice as provided in that header.
- If packaging for Linux distributions, install:
  - project license to `/usr/share/licenses/<pkg>/LICENSE`
  - third-party notice/readme to documentation path.

## Sample Library Licensing
The engine supports external SFZ libraries. Those assets are **not** covered by VB-Engine's MIT license.

Before shipping sample content, verify the library license separately:
- redistributable rights,
- commercial-use terms,
- attribution requirements,
- share-alike obligations (if any).

## Compliance Checklist for Maintainers
1. Verify repo still includes `LICENSE`.
2. Verify third-party notice file is present (`src/third_party/README.md`).
3. Ensure packaging scripts install license files.
4. Ensure sample assets in release bundles have explicit redistributable licenses.
5. Document any newly added third-party dependencies in this file.
