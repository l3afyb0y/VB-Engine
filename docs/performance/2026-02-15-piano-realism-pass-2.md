# Piano Realism Pass 2: CPU + Stability Loop (2026-02-15)

## Goal
Increase acoustic realism (less synth/organ character) without introducing pops/crackles and while keeping realtime performance.

## Loop Summary
1. Added realism features:
- strike-position weighting
- transient brightness bloom/decay
- filtered hammer/key-off noise
- body coloration
- smarter voice selection/retrigger behavior

2. First performance measurement:
- release benchmark dropped to `x0.57` median (failed realtime gate).

3. Optimization loop to recover CPU:
- removed expensive unconditional retrigger scans
- switched to constant-time mapped retrigger path
- reduced hot-loop math in modal render path
- folded static strike weighting into precomputed mode amplitudes
- limited 3-string zone to top register only

4. Final performance measurement:
- release benchmark median `x1.57806` (3-run gate), pass.

## Verification Evidence
- `ctest --test-dir build/debug --output-on-failure`: pass
- `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ctest --test-dir build/asan-ubsan --output-on-failure`: pass
- `ctest --test-dir build/tsan --output-on-failure`: pass
- `MIN_MEDIAN_RTF=1.00 BENCHMARK_BIN=./build/release/vb_engine_benchmark ./scripts/check-performance.sh 3`: pass
- `bash scripts/generate-samples.sh && bash scripts/check-wav-health.sh`: pass

## Audio Safety Snapshot (Samples)
- no clipped samples (`clip_samples=0`) across generated piano files
- bounded sample-to-sample jumps (max around `0.0947`)
- DC offsets near zero

## Notes
- Legacy baseline-compare script (`scripts/check-audio-quality.sh`) is not directly usable against older baseline packs when file format/duration layouts differ.
