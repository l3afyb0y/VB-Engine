# Sampled Piano Engine Implementation (2026-02-15)

## Summary
VB-Engine now supports a modular sampled piano path built from:
- `Sample Player` (`sample_library.*`, `sampled_piano_voice.*`)
- `Resonance Calculator` (`resonance_matrix.*`)
- `DSP Post-Processor` (`post_processor.*`)

The oscillator-style piano remains as a fallback only when no SFZ samples are available.

## Module Design

### 1) Sample Player
Files:
- `src/instruments/piano/sample_library.hpp`
- `src/instruments/piano/sample_library.cpp`
- `src/instruments/piano/sampled_piano_voice.hpp`
- `src/instruments/piano/sampled_piano_voice.cpp`

Implemented behavior:
- Loads SFZ region subsets:
  - Mapping: `sample`, `key`/`lokey`/`hikey`, `pitch_keycenter`, `lovel`/`hivel`
  - Trigger/gain: `trigger`, `volume`
  - Realism articulation: `offset`, `end`, `tune`, `ampeg_release`, `loop_mode`, `loop_start`, `loop_end`
- Loads WAV PCM/float, AIFF/AIFC PCM, and FLAC audio as mono playback data.
- Selects velocity layers and applies boundary crossfades to reduce stair-stepping.
- Uses precomputed `(trigger, note, velocity)` layer-selection cache to avoid runtime allocation/sort in note-on path.
- Uses 4-point Hermite interpolation for pitch-shift playback.
- Applies velocity/note-aware cosine attack shaping and exponential-style release using region release time when present.
- Supports looped sustain regions with loop crossfade to minimize wrap clicks.
- Adds micro fade-in/out around boundaries to suppress edit-point pops.
- Adds gentle per-voice low-cut/DC stabilization to reduce low-register drift and onset crackle risk.
- Promotes large sample regions to disk-backed mapped storage for lower resident memory and OS-paged access.

### 2) Resonance Calculator
Files:
- `src/instruments/piano/resonance_matrix.hpp`
- `src/instruments/piano/resonance_matrix.cpp`

Implemented behavior:
- 88-key shared resonance state and coupling matrix.
- Multi-partial inharmonic resonance rendering per key.
- `note_on` injects energy into the struck key plus coupled intervals.
- Resonance excitation uses a short pending-energy slew so note-on coupling does not hard-step the shared resonance bus.
- Sustain pedal modifies damping and resonance audibility.
- Frequency-shaped damping and soundboard coloration are applied before bus mix.

### 3) DSP Post-Processor
Files:
- `src/instruments/piano/post_processor.hpp`
- `src/instruments/piano/post_processor.cpp`

Implemented behavior:
- Zero-added-latency FIR convolution stages:
  - short piano-body modal IR
  - room diffusion IR
- Bedroom-sized room profile tuned for small-room acoustics:
  - image-source-inspired first-order reflections,
  - shorter RT-style late decay with HF damping.
- Non-uniform partitioned room rendering cadence (early/mid/late bands) to keep long tails practical.
- Smoothed partition target interpolation to avoid zipper/crackle artifacts at cadence boundaries.
- Wet/dry mixing with tonal contour smoothing.
- Default piano room mix is now slightly drier (`piano_reverb_wet` default `0.10`) for closer in-room presence.
- Transparent peak guard and DC blocker for click/static control without audible compression character.
- Narrow output step guard to suppress sparse crackle-like spikes without broad transient flattening.
- Sparse tap evaluation for convolution IR partitions to reduce realtime CPU cost.
- Denormal hardening on low-level post-DSP/resonance state variables.

## Voice Management
File:
- `src/core/voice_pool.cpp`

Implemented behavior:
- Piano voices now prefer sampled playback and fallback to modeled synthesis if no sample map is loaded.
- Same-note retriggers restart the mapped slot.
- Stealing policy targets the quietest active voice.
- Release-trigger regions (`trigger=release`) are spawned on note release when present.
- Release-trigger starts are intentionally softened (velocity cap + reduced gain scaling) to reduce impulsive declick risk.

## Tuning and Humanization
- A stretch-tuning table is generated from A4=440 base plus smooth high/low cent offsets.
- Humanization is configurable by timing jitter and velocity jitter.

## Pedal Handling
- Supports three pedal modes:
  - `Auto`: infers binary vs continuous based on incoming CC64 values.
  - `Binary`: threshold-based sustain behavior (configurable threshold).
  - `Continuous`: half-pedal behavior where pedal amount shapes release damping.
- Binary pedals remain fully supported while continuous controllers get graded damping.

## C API additions
File:
- `include/vb_engine/c_api.h`

New config fields:
- `piano_sfz_path`
- `piano_humanize_timing_ms`
- `piano_humanize_velocity`
- `piano_reverb_wet`
- `piano_stretch_strength`
- `piano_presence`
- `piano_disk_streaming_enabled`
- `piano_disk_stream_threshold_frames`
- `piano_pedal_mode`
- `piano_pedal_binary_threshold`

Backward-compatibility:
- `vb_engine_create` now accepts legacy config struct sizes and only reads new fields when present.

## Asset Workflow
- `scripts/fetch-piano-assets.sh` downloads supported redistributable SFZ libraries.
- Core engine embedding requires explicit `piano_sfz_path` from host code.
- `Samples/Piano-Library/default.sfz` is still auto-used by the sample generator/test tools when present.
- `VB_PIANO_SFZ_PATH` overrides library path for rendering.
- Showcase sample generator dispatches events at sample-accurate boundaries (not only block boundaries) to keep transients and timing more natural in rendered references.

## Realtime Stress Benchmark
- `vb_engine_onset_stress_benchmark` measures callback-time stability under repeated 20-note low-register burst scenarios at 64-frame block size.
- Reports max/p95/p99/p99.9 block processing times and explicit block-budget overruns.

## Mechanical Noise Policy
Per user requirement, this implementation intentionally omits added hammer-thud and pedal-squeak layers.
