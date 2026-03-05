# Sampled Piano Refactor Verification Report (2026-02-15)

## Scope
Validation report for the sampled-piano foundational refactor:
- SFZ/WAV sample playback path
- resonance matrix module
- post-processor module
- C API config expansion with legacy compatibility

## Verification Results
- `ctest --test-dir build/debug --output-on-failure`: PASS
- `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ctest --test-dir build/asan-ubsan --output-on-failure`: PASS
- `ctest --test-dir build/tsan --output-on-failure`: PASS
- `MIN_MEDIAN_RTF=1.00 BENCHMARK_BIN=./build/release/vb_engine_benchmark ./scripts/check-performance.sh 3`: PASS
  - latest full-pipeline median: `x92.8149`
  - note: this benchmark pattern is currently optimized for sustained synthesized voices; with short sampled voices and early silence, realtime factor is inflated and should be interpreted cautiously.
- `bash scripts/generate-samples.sh && bash scripts/check-wav-health.sh`: PASS

## Added Test Coverage
- `test_sfz_velocity_layers_load_and_respond`
  - verifies SFZ layer loading and velocity-dependent output differences
- `test_create_with_legacy_config_size`
  - verifies ABI behavior when callers pass pre-extension config struct size

## Audio Health Snapshot
Generated `Samples/Piano-*.wav` files all passed:
- no clipping (`clip_samples=0`)
- bounded sample jumps
- near-zero DC offset

## Notes
- When no sample library is configured, engine falls back to modeled piano voice for continuity.
- For realistic sampled rendering, set `Samples/Piano-Library/default.sfz` or `VB_PIANO_SFZ_PATH`.
