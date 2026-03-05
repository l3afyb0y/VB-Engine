# Piano Realism Iteration 10 - Realtime De-Crackle + Onset Stress Hardening (2026-02-16)

## Goal
Eliminate remaining crackle/static vectors under dense low-register note onsets and pedal-heavy passages by reducing realtime spike risk and smoothing onset/release transient behavior.

## What Changed

### 1) Note-On CPU Spike Mitigation
Files:
- `src/instruments/piano/sample_library.hpp`
- `src/instruments/piano/sample_library.cpp`
- `src/core/voice_pool.hpp`
- `src/core/voice_pool.cpp`

Changes:
- Added precomputed layer-selection cache for all `(release_trigger, note, velocity)` combinations.
  - Removes per-note runtime candidate allocation and sort work from `select_layers` during audio processing.
- Refactored slot handling:
  - `pick_slot_index()` now returns indices directly.
  - Added `active_slot_flags_` to avoid linear duplicate checks on `active_voice_indices_`.

Rationale:
- Reduces callback-time bursts when many notes start together.

### 2) Transient Shaping Refinement
Files:
- `src/instruments/piano/sampled_piano_voice.cpp`
- `src/core/voice_pool.cpp`
- `src/instruments/piano/post_processor.cpp`
- `src/instruments/piano/resonance_matrix.cpp`

Changes:
- Sampled voice attack now uses velocity/note-dependent attack time with cosine shaper.
- Boundary fade-in/out uses raised-cosine windows.
- Release-trigger playback is gentler:
  - capped release velocity,
  - reduced gain-jitter scalar for release starts.
- Post output guard is adaptive (envelope-based) rather than fixed hard step limit.
- Resonance note-on energy injection stays slewed via pending-energy state.

Rationale:
- Reduces impulsive crackle-like transients while preserving articulation.

### 3) Realtime Stress Instrumentation
Files:
- `benchmarks/onset_stress_benchmark.cpp`
- `CMakeLists.txt`

Changes:
- Added `vb_engine_onset_stress_benchmark`:
  - 64-frame blocks @ 48 kHz,
  - repeated 20-note low-register burst with sustain cycles,
  - reports max/p95/p99/p99.9 block processing time and budget overruns.

## Verification
- `bash scripts/run-full-verification.sh`: pass
- `MIN_MEDIAN_RTF=1.00 BENCHMARK_BIN=./build/release/vb_engine_benchmark ./scripts/check-performance.sh 11`: pass
  - median realtime factor: `x15.5542` (thermal/load-sensitive run, still comfortably > x1)
- `./build/release/vb_engine_onset_stress_benchmark`: pass-style metrics
  - `block_budget_ms=1.3333`
  - `max_block_ms=0.3900`
  - `p99_block_ms=0.2005`
  - `p99.9_block_ms=0.2073`
  - `over_budget_blocks=0/6000`
- `bash scripts/check-wav-health.sh`: pass (`clip_samples=0` across generated samples)

## Fur Elise Waveform Snapshot
Target:
- `Samples/Piano-Fur-Elise-Excerpt.wav`

Current scan:
- `max_jump=0.031799` at `0.728083s`
- `max_second_derivative=0.021210` at `7.851708s`

Interpretation:
- No clipping/DC corruption.
- Remaining large values are clustered onset transients rather than isolated full-scale discontinuities.
- Combined with zero budget overruns in the onset stress benchmark, primary realtime crackle vectors are substantially reduced on this system.
