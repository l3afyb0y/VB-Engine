# Piano Realism Iteration 9 - Waveform Forensics + Onset Scheduling (2026-02-16)

## Goal
Investigate user-reported crackles in generated Fur Elise samples with sample-accurate waveform analysis, then apply targeted fixes to onset-energy behavior.

## What Changed

### 1) Resonance Note-On Injection Smoothing
Files:
- `src/instruments/piano/resonance_matrix.hpp`
- `src/instruments/piano/resonance_matrix.cpp`

Changes:
- Added `pending_energies_` accumulator per key.
- `note_on` now writes excitation into pending state instead of directly stepping `energies_`.
- `render_sample` applies pending excitation via a short slew (`excitation_alpha_`, ~1.9 ms time constant).

Rationale:
- Reduces abrupt shared-resonance energy steps at note onset while preserving total injected energy.

### 2) Sample-Accurate Event Scheduling in Showcase Renderer
File:
- `tools/generate_piano_samples.cpp`

Changes:
- Reworked render loop so MIDI events are applied at exact sample frames, not only at block boundaries.
- The generator now splits processing chunks at the next event frame when needed.

Rationale:
- Prevents onset bunching from block-quantized event dispatch in generated showcase WAVs.

## Waveform Forensics (Fur Elise)
Target file:
- `Samples/Piano-Fur-Elise-Excerpt.wav`

Measured with local sample-accurate scanners:
- Before this pass (forensics baseline):
  - `max_jump=0.027771` at `0.727396s`
  - `max_second_derivative=0.034058` at `14.363896s`
- After this pass:
  - `max_jump=0.029205` at `0.722042s`
  - `max_second_derivative=0.032043` at `17.045437s`

Probe windows (after pass):
- around `0s`: strongest local transient near `0.031792s`
- around `1s`: no extreme discontinuity signature
- around `4s`: no extreme discontinuity signature
- around `13s`: transient cluster near `12.954562s`, still below clip/overflow behavior

Interpretation:
- No hard clipping and no one-channel corruption.
- Remaining artifacts are onset-transient clusters, not full-scale digital clipping.

## Verification
- `ctest --test-dir build/debug --output-on-failure`: pass
- `bash scripts/run-full-verification.sh`: pass
- `MIN_MEDIAN_RTF=1.00 BENCHMARK_BIN=./build/release/vb_engine_benchmark ./scripts/check-performance.sh 7`: pass
  - median realtime factor: `x20.9612`
- `bash scripts/check-wav-health.sh`: pass
  - all showcase files: `clip_samples=0`

## Notes
- This pass improves event-timing correctness in generated samples and smooths resonance excitation ramps.
- Transient maxima shifted rather than universally dropping; further crackle reduction should target attack-shape balancing and onset-energy distribution under dense low-register note clusters.
