# Piano Realism Iteration 6 - Realtime Stability + Crackle Mitigation (2026-02-16)

## Scope
This pass targeted user-reported residual micro-crackles under realtime use and suspected CPU spikes.

Primary goals:
- reduce render-path CPU pressure,
- remove likely crackle sources from tail/release transitions,
- and harden denormal-prone DSP states.

## Implemented Changes

### 1) CPU Optimization: Sparse Convolution Tap Evaluation
- Replaced dense per-tap FIR accumulation in the post-processor with sparse tap lists for body and room partitions.
- Tap lists are built at init from IR values above threshold and used during realtime rendering.
- Mid/late partition cadence interpolation is preserved.

Result: lower convolution multiply count in the render callback while keeping the same room model structure.

### 2) Crackle Mitigation: Release Tail Handling
- `SampledPianoVoice` now applies end-of-sample boundary fade even during release tails.
- Sampled voice deactivation now waits for both low release gain and low activity level before dropping voice state.

Result: less risk of tiny end-tail discontinuities.

### 3) Denormal Hardening
- Added very-small-value zeroing in post-processor and resonance state paths.
- Enabled FTZ/DAZ CPU floating-point mode in engine initialization (x86 SSE2 path).

Result: reduced risk of denormal-related CPU spikes in low-level tails on realtime systems.

### 4) Realtime Streaming Default
- Changed default `piano_disk_streaming_enabled` from on to off for the default engine path.

Result: avoids OS page-fault jitter in typical realtime playback unless host explicitly opts into disk-backed behavior.

## Verification Commands
- `MIN_MEDIAN_RTF=1.00 BENCHMARK_BIN=./build/release/vb_engine_benchmark ./scripts/check-performance.sh 7`
- `bash scripts/run-full-verification.sh`

## Verification Results
- Debug tests: pass
- Soak tests: pass
- ASan/UBSan: pass
- TSan: pass
- WAV health gate: pass (`clip_samples=0` across all showcase files)

Performance:
- Iteration-6 7-run median: `x19.5198`

Full pipeline performance gate (3-run):
- median realtime factor: `x18.294`

Selected WAV-health metrics (iteration 6):
- Fur Elise: `max_jump=0.0238647`
- Moonlight Sonata: `max_jump=0.0378418`
- Turkish March: `max_jump=0.0286865`
- Clair de Lune: `max_jump=0.0358887`
- Chopin Ballade No.1: `max_jump=0.0254822`
- Creep (transposed): `max_jump=0.0239258`
- Bach Prelude C: `max_jump=0.0305481`

## Outcome
This pass improves realtime headroom and directly addresses two practical crackle vectors (tail discontinuity and denormal spikes) while preserving tonal direction and test stability.
