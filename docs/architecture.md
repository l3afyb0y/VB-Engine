# VB-Engine Architecture

## 1. Purpose
VB-Engine is a realtime-safe audio engine intended to be embedded in host apps (DAWs, MIDI trainers, game audio tools) through a stable C ABI (`include/vb_engine/c_api.h`).

Core design goals:
- No blocking/locking/allocation in audio callback.
- Deterministic event handling.
- High-quality piano rendering with robust anti-glitch guards.
- Clean integration surface for external applications.

## 2. High-Level Data Flow
1. Host thread(s) enqueue MIDI/control events through C API.
2. Audio callback calls `vb_engine_process(...)`.
3. Engine drains SPSC queue and applies events to `VoicePool`.
4. `VoicePool` renders active voices + mechanics + resonance.
5. `PianoPostProcessor` performs body/room/tone/output shaping.
6. Engine writes interleaved frame output per requested channel layout.

## 3. Realtime Model
Audio-thread invariants:
- No `new/delete`, no `malloc/free`, no filesystem access, no mutex.
- FTZ/DAZ enabled to reduce denormal CPU spikes on long decays.
- Event transfer uses lock-free SPSC queue (`src/core/spsc_queue.hpp`).

Control-thread responsibilities:
- Push `note_on`, `note_off`, `control_change`.
- Configure `vb_engine_config` before create.

## 4. Core Modules

### `src/core/engine.*`
- Owns queue and `VoicePool`.
- Drains MIDI events each process block.
- Applies output diagnostics (max jump, non-finite counters).
- Includes final safety slew guard against pathological discontinuities.

### `src/core/voice_pool.*`
- Preallocated voice slots (sampled piano + modeled fallback + guitar stub).
- Quietest-voice steal policy with short tail smoothing.
- Pedal logic supports:
  - `auto` (binary/continuous inference),
  - `binary` (threshold),
  - `continuous` (half-damper).
- Uses per-note cached pan/register tables to reduce hot-path trig overhead.

### `src/instruments/piano/sample_library.*`
- Parses SFZ subset.
- Loads WAV/AIFF/FLAC sample data.
- Supports stereo regions, velocity layers, offsets/loops/tuning/release.
- Optional disk-backed mapped storage for large regions.

### `src/instruments/piano/sampled_piano_voice.*`
- Per-voice sample playback with Hermite/sinc interpolation.
- Velocity/register-aware attack/timbre shaping.
- Continuous spectral smoothing to reduce synthetic timbre steps.
- Double-slope release behavior and boundary de-click shaping.

### `src/instruments/piano/mechanics.*`
- Specialized non-polyphony mechanics voices:
  - key-down,
  - damper-fall,
  - pedal motion.
- Independent envelope/filter path with gentle safety limiting.

### `src/instruments/piano/resonance_matrix.*`
- String-coupled sympathetic resonance model.
- Inharmonicity-aware feedback/damping behavior.
- Soundboard/bridge/body coupling with pedal-dependent behavior.

### `src/instruments/piano/post_processor.*`
- Body + room convolution-style processing.
- Presence/focus contour and phase-safe bus alignment.
- Transparent limiting and DC management tuned for low artifact risk.

### `src/c_api/c_api.cpp`
- ABI boundary wrapper over internal C++ engine.
- Supports forward-compatible config struct extension via `struct_size`.

## 5. C ABI Contracts
Defined in `include/vb_engine/c_api.h`:
- `vb_engine_default_config`
- `vb_engine_create` / `vb_engine_destroy`
- `vb_engine_note_on` / `vb_engine_note_off` / `vb_engine_control_change`
- `vb_engine_process`
- diagnostics getters/reset

Compatibility rule:
- New fields append to `vb_engine_config`.
- Runtime checks `struct_size` before reading optional fields.

## 6. Configuration Surface (Current)
Primary quality/performance knobs:
- `sample_rate`, `max_block_size`, `max_voices`
- `piano_sfz_path`
- `piano_humanize_timing_ms`, `piano_humanize_velocity`
- `piano_reverb_wet`, `piano_mic_mix`, `piano_presence`
- `piano_stretch_strength`
- disk streaming toggle/threshold
- pedal mode + binary threshold + pedal mechanics enable

## 7. Testing and Verification
- Unit/behavior tests: `tests/test_engine.cpp`
- Soak tests: `tests/test_soak.cpp`
- Sanitizer preset: `asan-ubsan`
- WAV artifact checks: `tools/analyze_wav_quality.cpp`, `scripts/check-wav-health.sh`
- Showcase sample generation: `tools/generate_piano_samples.cpp`, `scripts/generate-samples.sh`

## 8. Extending the Engine

### Add a new instrument
1. Add `voice` type under `src/instruments/<name>/`.
2. Wire it into `VoicePool::VoiceSlot`, `note_on`, `note_off`, `render`.
3. Add config and C API fields only if host-facing controls are required.
4. Add dedicated tests and soak scenarios.

### Modify piano behavior safely
1. Keep callback path allocation-free.
2. Validate with:
   - `vb_engine_tests`,
   - `vb_engine_soak_tests`,
   - `asan-ubsan`,
   - sample generation + wav health.
3. Update docs under `docs/instruments/` and `docs/performance/`.

## 9. Source Layout
- `include/`: public API headers.
- `src/`: engine/runtime.
- `tests/`: correctness/regression.
- `tools/`: offline generators/analyzers.
- `scripts/`: install/build/test helper workflows.
- `docs/`: architecture, integration, performance, limits.
- `Samples/`: public example outputs and optional local sample-library metadata.
