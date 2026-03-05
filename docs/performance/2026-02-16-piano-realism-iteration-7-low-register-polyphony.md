# Piano Realism Iteration 7 - Low-Register Polyphony Crackle Pass (2026-02-16)

## Scope
Targeted user-reported residual crackles concentrated in low-register note onsets and dense simultaneous-note playback.

Goal: maintain clear, glitch-free output in the 20-simultaneous-note + pedal-down scenario.

## Implemented Changes

### 1) Low-End Voice Stabilization
- Added a gentle per-voice high-pass/DC stabilization stage in sampled voice rendering.
- This reduces very-low drift and startup discontinuity risk in low-register sampled tails.

### 2) Onset Spike Softening (Non-Hard)
- Replaced hard delta clamping in post output with a soft delta guard formulation.
- This preserves anti-crackle protection while avoiding hard-edge artifacts from abrupt clamp transitions.

### 3) Convolution CPU/Noise Tradeoff Tuning
- Increased sparse-room tap threshold used for runtime tap-list generation.
- This trims very-low-energy taps that contribute CPU and can introduce low-level granular texture.

### 4) New Stress Test for Requirement
- Added explicit test for 20 simultaneous notes with sustain pedal depressed in low register.
- The test validates clipping bounds and strict jump bounds during onset and sustain windows.

## Verification Commands
- `cmake --preset debug && cmake --build --preset debug && ctest --test-dir build/debug --output-on-failure`
- `MIN_MEDIAN_RTF=1.00 BENCHMARK_BIN=./build/release/vb_engine_benchmark ./scripts/check-performance.sh 7`
- `bash scripts/run-full-verification.sh`

## Verification Results
- Debug tests: pass
- Soak tests: pass
- ASan/UBSan: pass
- TSan: pass
- New 20-note pedal glitch test: pass
- WAV health gate: pass (`clip_samples=0` across all showcase files)

Performance:
- 7-run benchmark median realtime factor: `x18.3939`
- Full pipeline (3-run) median realtime factor: `x18.3461`

Selected WAV-health metrics (iteration 7):
- Fur Elise: `max_jump=0.0241394`
- Moonlight Sonata: `max_jump=0.0320129`
- Turkish March: `max_jump=0.0287476`
- Clair de Lune: `max_jump=0.0354614`
- Chopin Ballade No.1: `max_jump=0.0248718`
- Creep (transposed): `max_jump=0.0237427`
- Bach Prelude C: `max_jump=0.0307312`

## Outcome
This pass directly addresses low-register, multi-note crackle vectors and adds a permanent regression test around the 20-note pedal-down quality target while preserving strong realtime headroom.
