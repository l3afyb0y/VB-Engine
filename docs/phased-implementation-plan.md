# Phased Implementation Plan (Arch-first, integration-first)

## Phase 1: Foundation (Current)
- C++20 CMake scaffold.
- Core engine skeleton with realtime-safe constraints.
- C ABI for embeddable integration.
- Basic tests + benchmark + sanitizer presets.
- Packaging baseline (`PKGBUILD`, install scripts).

Gate:
- Build/test pass on Arch.
- C ABI can be loaded and process silence/audio safely.

## Phase 2: Piano Player Integration Path
- Add a thin adapter library for the Piano Player runtime.
- Validate note-on/off, pedal, velocity mapping, audio latency.
- Add migration shim to swap existing sample engine behind current APIs.

Gate:
- Piano Player can run with VB-Engine in dev mode.
- Measured latency and CPU tracked per buffer size.

## Phase 3: Piano v1 Realism
- Implement resonance, pedal behavior, key-off/release realism.
- Calibrate velocity curves and dynamic response.
- Add golden audio snapshots and listening test protocol.

Gate:
- User audition round completed with iterative parameter tuning.

## Phase 4: Guitar v1
- Build clean electric voice model and articulation controls.
- Add test vectors for pluck/transient/decay characteristics.

Gate:
- User audition + CPU budget within target.

## Phase 5: Plugin Wrappers
- CLAP + VST3 wrappers first.
- LV2 second.
- AU/AAX planning and feasibility documents.

Gate:
- Host smoke tests on Linux major DAWs/hosts.

## Phase 6: Hardening
- Soak tests, memory/resource trend checks.
- Profiling + optimization passes.
- API/ABI stabilization and semantic versioning policy.
