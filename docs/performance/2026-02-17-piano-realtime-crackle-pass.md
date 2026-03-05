# Piano Realtime Crackle Pass (2026-02-17)

## Goal
- Investigate persistent user-reported crackle/static and regenerate showcase samples after a focused declick pass.

## Investigation
- Reproduced sample generation with Salamander Grand and collected diagnostics from `vb_engine_sample_generator`.
- Ran waveform forensics on `Samples/Piano-Fur-Elise-Excerpt.wav`:
  - largest frame-to-frame deltas cluster around strong chord attacks,
  - no clipping (`clip_samples=0`), no NaN/Inf output, no hard-jump events.
- Correlated reported timestamps with event timings and post-DSP behavior.

## Changes Applied
- Refined early-room reflection cluster in `src/instruments/piano/post_processor.cpp`:
  - replaced alternating-sign single-tap injection with a smoother 3-tap micro-spread cloud,
  - reduced per-tap excitation gain to avoid impulse-like spikes.
- Added deterministic micro-humanization in showcase event authoring (`tools/generate_piano_samples.cpp`):
  - small per-note timing offsets,
  - small per-note duration and velocity variation,
  - keeps structure recognizable while reducing quantized/robotic attack alignment.
- Added standalone SDL tester app (`tools/piano_tester/main.cpp`) with:
  - click-to-play keyboard range `A2-C5`,
  - top-bar pedal toggle control,
  - realtime engine playback via C API.

## Validation
- `ctest --test-dir build/release --output-on-failure` -> pass.
- `build/release/vb_engine_sample_generator` -> regenerated all showcase WAV files.
- `bash scripts/check-wav-health.sh` -> pass (`clip_samples=0` across all generated files).
- `build/release/vb_engine_onset_stress_benchmark`:
  - typical run shows `over_budget_blocks=0/6000` at 64f/48k under 224-voice stress.

## Live Tester Root Cause (follow-up)
- Measured realtime headroom under pedal/polyphony load showed tester-default `96 kHz` was over budget:
  - `sr=96000`, `block=256`: `over=405/8000`
  - `sr=96000`, `block=512`: `over=540/8000`
- Equivalent `48 kHz` runs stayed within budget (`over=0/8000`).
- Tester defaults were changed to realtime-safe values:
  - sample rate `48000`,
  - buffer `512`,
  - max voices `96`.

## Output
- Updated showcase files in `Samples/`:
  - `Piano-Fur-Elise-Excerpt.wav`
  - `Piano-Moonlight-Sonata-Excerpt.wav`
  - `Piano-Turkish-March-Excerpt.wav`
  - `Piano-Clair-de-Lune-Excerpt.wav`
  - `Piano-Chopin-Ballade-No1-Excerpt.wav`
  - `Piano-Creep-Transposed-Excerpt.wav`
  - `Piano-Bach-Prelude-C-Excerpt.wav`
