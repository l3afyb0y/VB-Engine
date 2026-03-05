# 2026-03-05: Piano Realism Pass (Physical/Body/SFZ)

## Scope
This pass implements the coding work from the realism refactor specification with emphasis on realism and realtime stability.

## Implemented

### 1) Physical-model refinement and stability
- `AcousticGrandVoice`:
  - Coupled-string bloom and hammer-contact-time dynamics retained and tuned.
  - Release/tail behavior tightened to preserve natural decay while meeting realtime crackle/decay constraints.
- `ResonanceMatrix`:
  - Spectral sympathetic excitation and duplex shimmer retained.
  - Injection/feedback paths re-gained and bounded to eliminate runaway energy under dense pedal/polyphony.

### 2) Dynamic body coupling
- `PianoPostProcessor::process()` now accepts a realtime `body_drive` signal.
- `VoicePool` computes smoothed resonance activity and feeds it into the post stage.
- Body/room balance and tone contour now respond to resonance energy (soundboard "load" effect) without added latency.

### 3) Advanced SFZ opcode support
- Added support for:
  - `amp_veltrack` (`ampeg_veltrack` alias)
  - `amp_velcurve`
  - `off_mode=fast`
- New fields are carried through parsing and used in `SampledPianoVoice` gain/release behavior.

### 4) Predictive release prefetch
- `VoicePool` now prefetches release-layer selection on note-on and warms the release sample page.
- Note-off uses the prefetched release layer when available for lower-latency release triggering.

### 5) New user controls
- Added `piano_mic_mix` to the public C API (`vb_engine_config`) and engine config path.
- Added true SFZ round-robin/repetition controls:
  - `lorand` / `hirand`
  - `seq_length` / `seq_position`
- Selection logic keeps cache fast-path for non-RR regions and dynamically resolves RR regions at runtime.

### 6) Tester app quality profile
- Updated `tools/piano_tester/main.cpp` to use a higher-quality default profile:
  - `max_voices=192`
  - `piano_disk_streaming_enabled=1`
  - `piano_disk_stream_threshold_frames=16384`
  - `piano_mic_mix` configurable via env (`VB_TESTER_MIC_MIX`, default `0.42`)
  - humanize disabled by default for cleaner A/B auditioning
- Updated launcher defaults in `scripts/run-piano-tester.sh`.
- Refreshed desktop launcher metadata in `Samples/VB-Engine-Piano-Tester.desktop`.

## Stability/Quality Validation

### Unit + soak tests
- `ctest --test-dir build/release --output-on-failure`
- Result: **PASS** (`vb_engine_tests`, `vb_engine_soak_tests`)

### WAV health gate
- `bash scripts/check-wav-health.sh`
- Result: **PASS**
- All generated showcase WAVs: no clipping samples, bounded max-jump, low DC offset.

### Realtime stress
- `build/release/vb_engine_onset_stress_benchmark`
- 64-frame @ 48k:
  - `block_budget_ms=1.3333`
  - `max_block_ms=1.2413`
  - `p99_block_ms=1.2027`
  - `over_budget_blocks=0/6000`
  - `hard_jump_events=0`

### Sample regeneration
- `build/release/vb_engine_sample_generator`
- Regenerated showcase WAVs under `Samples/` with zero hard-jump diagnostics.

## Follow-up Pass: "Do All Three" (same day)

### Implemented now
- Release/damper realism refinement (already in `SampledPianoVoice`) was kept active and retuned for stronger attack-to-sustain tonal evolution:
  - sustained high-band darkening increased over note lifetime to better preserve acoustic onset brightness.
- True close/room mic architecture was fully wired end-to-end:
  - `SampledPianoVoice::render_sample()` bus outputs are now collected in `VoicePool`,
  - fallback voice, resonance, mechanics, and steal tails are distributed into close/room sums,
  - `PianoPostProcessor` receives staged `close_bus`, `room_bus`, and `mic_mix` every frame.
- Higher-order resonance/body model added in `ResonanceMatrix`:
  - new register-aware bridge coupling table (`bridge_coupling_`),
  - 3-band soundboard modal state (`board_modes_`, velocity, damping, energy),
  - per-register mode drive accumulation and nonlinear modal saturation,
  - inharmonicity-compensated sympathetic injection and hammer-to-board mode excitation on note-on.

### Validation
- Build:
  - `cmake --build --preset release --target vb_engine_tests vb_engine_soak_tests vb_engine_sample_generator vb_engine_piano_tester -j`
  - Result: **PASS**
- Tests:
  - `ctest --test-dir build/release --output-on-failure`
  - Result: **PASS** (`vb_engine_tests`, `vb_engine_soak_tests`)
- Regenerated samples + health gate:
  - `build/release/vb_engine_sample_generator`
  - `bash scripts/check-wav-health.sh`
  - Result: **PASS** (no clip samples, no non-finite output, no hard-jump events)

## Follow-up Upgrade Pass: "All Possible Realism Upgrades" (same day)

### Implemented
- Higher-quality interpolation for transposed sampled playback:
  - Added 8-tap windowed-sinc interpolation path in `SampledPianoVoice` with adaptive selection.
  - Hermite remains for near-unity ratios; sinc path is used where pitch-shift artifacts are most audible.
- Output realism/de-compression tuning:
  - Rebalanced body/room wet mix and tone contour in `PianoPostProcessor` to reduce processed/muffled character.
  - Replaced heavy full-scale saturation with a gentler soft limiter to preserve transient shape.
- Continuous pedal behavior:
  - Continuous mode sustain-hold threshold set to realistic full-pedal range (`>= 0.90`) so half-pedal decays naturally while full pedal sustains.
- Headroom stabilization:
  - Reduced master staging trim in `VoicePool` to keep dense passages below clipping bounds under test stress.

### Verification
- `cmake --build --preset release --target vb_engine_tests vb_engine_soak_tests vb_engine_sample_generator vb_engine_piano_tester -j` -> **PASS**
- `ctest --test-dir build/release --output-on-failure` -> **PASS**
- `build/release/vb_engine_sample_generator` -> regenerated all showcase samples
- `bash scripts/check-wav-health.sh` -> **PASS**

## Targeted Voicing Pass: Reduce "Synthy" Mid/High Character (same day)

### Implemented
- `SampledPianoVoice` voicing adjustments:
  - reduced close-bus high-band emphasis and room-delay comb contribution,
  - slightly slower/cleaner room low-pass movement for less artificial edge.
- `ResonanceMatrix` upper-register voicing:
  - reduced duplex/aliquot shimmer contribution in string, board, and treble mode drive,
  - reduced high-band modal body weighting to avoid metallic synthetic ring.
- `PianoPostProcessor` ambience voicing:
  - reduced room dominance in wet blend and mic-room feed intensity to improve direct clarity.

### Verification
- `cmake --build --preset release --target vb_engine_tests vb_engine_soak_tests vb_engine_sample_generator vb_engine_piano_tester -j` -> **PASS**
- `ctest --test-dir build/release --output-on-failure` -> **PASS**
- `build/release/vb_engine_sample_generator` -> regenerated all showcase samples
- `bash scripts/check-wav-health.sh` -> **PASS**

## Iterative Loop Pass: "Do All Remaining Upgrades" (same day)

### Implemented in this loop
- Multi-mic architecture groundwork in SFZ layer resolver:
  - Added sample perspective classification (`Close`/`Player`/`Room`) from source names.
  - Added perspective-aware secondary-layer pairing when suitable alternate mic regions exist.
- Pedal-down layer support from SFZ:
  - Added parsing/support for `locc64` / `hicc64` (and `on_locc64` / `on_hicc64`) in sample regions.
  - `VoicePool` now passes current CC64 value into sample selection so sustain-state-conditioned regions are selectable.
- Una corda (soft pedal, CC67):
  - Added CC67 handling in `VoicePool`.
  - Soft-pedal amount now affects sampled voice gain and timbre (reduced attack brightness/body gain at higher CC67).
- Velocity-dependent hammer contact filtering:
  - Added hammer-contact envelope/decay shaping in `SampledPianoVoice` that modulates early high-band presence by velocity/register.
- 2x-style nonlinear cleanup:
  - Added an oversampled-style two-point limiting stage in post DSP to smooth nonlinear transient edges.
- Perceptual quality automation:
  - Extended `vb_engine_compare_wavs` with brightness proxy (`hf_ratio_delta`) and stereo-width delta metrics in addition to SNR/peak/jump checks.

### New tests added
- `test_sfz_cc64_pedal_conditioned_region_selection`
- `test_soft_pedal_reduces_attack_energy`

### Verification
- `cmake --build --preset release --target vb_engine_tests vb_engine_soak_tests vb_engine_sample_generator vb_engine_piano_tester vb_engine_compare_wavs -j` -> **PASS**
- `ctest --test-dir build/release --output-on-failure` -> **PASS**
- `build/release/vb_engine_sample_generator` -> regenerated all showcase samples
- `bash scripts/check-wav-health.sh` -> **PASS**
- `build/release/vb_engine_compare_wavs Samples Samples` -> **PASS** (including perceptual metric paths)

## Targeted Realism Pass: True Stereo Imaging (same day)

### Implemented
- Engine audio path now emits true stereo (not duplicated mono):
  - `Engine::process` now consumes `VoicePool::render_stereo()` and writes independent L/R channels.
  - Mono output is still preserved when `num_channels == 1` (L/R average).
- Voice rendering upgraded to keyboard-position stereo imaging:
  - Added note-position pan model (`A0`->left, `C8`->right) with equal-power panning.
  - Close bus is intentionally narrower; room bus is wider for natural spatial perspective.
- Independent post-DSP state per channel:
  - `VoicePool` now uses separate `PianoPostProcessor` instances for left and right to avoid cross-channel state contamination.
- Added regression coverage:
  - `test_stereo_output_has_keyboard_spatial_width` verifies non-trivial stereo width under dual-register playback.

### Why this matters
- Prior mono duplication flattened spatial cues and could read as "synthy/flat" even with high-quality sampled timbre.
- Realistic piano perception strongly depends on left-right keyboard image and room perspective; this pass restores those cues in the core engine.

### Verification
- `cmake --build --preset release --target vb_engine_tests vb_engine_soak_tests vb_engine_sample_generator vb_engine_piano_tester -j` -> **PASS**
- `ctest --test-dir build/release --output-on-failure` -> **PASS**
- `bash scripts/generate-samples.sh` -> regenerated all showcase samples
- `bash scripts/check-wav-health.sh` -> **PASS**

## Targeted Realism Pass: True Stereo Sample Playback (same day)

### Implemented
- Stereo sample decode preserved in `PianoSampleLibrary` for WAV/AIFF/FLAC:
  - regions now retain left/right channels when source assets are stereo.
  - mono-compatible behavior is kept for single-channel assets.
- Stereo-aware sampled voice rendering:
  - `SampledPianoVoice` now interpolates left/right channels independently (Hermite and windowed-sinc paths).
  - internal timbre/body/room shaping is applied per-channel to preserve channel differences instead of collapsing early to mono.
  - voice render API now exposes direct stereo output for the mixer path.
- Voice mixer integration:
  - `VoicePool` now consumes direct sampled stereo from the voice and keeps fallback/model contribution centered.
  - channel asymmetry from stereo assets is preserved through the engine output path.

### New test coverage
- Added `test_stereo_sample_region_preserves_channel_asymmetry`:
  - constructs a synthetic stereo SFZ sample with strong L/R imbalance,
  - verifies output preserves this asymmetry (i.e., not downmixed to mono before playback).

### Verification
- `cmake --build --preset release --target vb_engine_tests vb_engine_sample_generator vb_engine_piano_tester -j` -> **PASS**
- `ctest --test-dir build/release --output-on-failure` -> **PASS**
- `bash scripts/generate-samples.sh` -> regenerated all showcase samples
- `bash scripts/check-wav-health.sh` -> **PASS**

## Realtime Stability Pass: Post-Stereo CPU/Crackle Regression Fix (same day)

### Problem
- Live tester could accumulate crackle/static after sustained play, with high CPU usage.
- Root causes were:
  - heavier stereo-sample render cost under high active-voice counts,
  - denormal-risk on the realtime callback thread,
  - overly aggressive tester defaults for sustained live play.

### Implemented
- Realtime denormal guard on the processing thread:
  - `Engine::process` now enables FTZ/DAZ once per calling thread (thread-local init), ensuring the actual audio callback thread is protected.
- Adaptive stereo-sample path:
  - true stereo sample playback remains enabled under normal load,
  - when active voices exceed threshold, renderer falls back to mono-sampled path for CPU safety.
- Interpolation CPU trim:
  - raised expensive sinc threshold in sampled playback to reduce unnecessary high-cost interpolation.
- Post DSP cadence optimization:
  - room mid/late convolution partitions moved to short-stride interpolated cadence (`mid=2`, `late=8`) for realtime safety.
- Tester defaults tuned for live reliability:
  - reduced default `VB_TESTER_MAX_VOICES` to `96`,
  - added streaming controls:
    - `VB_TESTER_STREAMING` (default `1`)
    - `VB_TESTER_STREAM_THRESHOLD` (default `65536`).

### Validation
- `ctest --test-dir build/release --output-on-failure` -> **PASS**
- Onset stress benchmark:
  - `VB_MAX_VOICES=96`:
    - `over_budget_blocks=0/6000` (budget 1.333 ms) -> realtime safe
  - `VB_MAX_VOICES=192`:
    - significantly improved but still above strict 64-frame budget in stress conditions.
- `bash scripts/generate-samples.sh` -> regenerated samples
- `bash scripts/check-wav-health.sh` -> **PASS**
