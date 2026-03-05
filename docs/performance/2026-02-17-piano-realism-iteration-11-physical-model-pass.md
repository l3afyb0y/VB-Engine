# Piano Realism Iteration 11 (2026-02-17)

## Goal
Execute the next realism pass from the research brief:
- add dedicated mechanical noise behavior,
- smooth velocity timbre transitions continuously,
- replace sine-style resonance behavior with a physically informed prototype,
- preserve realtime safety and remove crackle/static regressions.

## Implementation

### 1) Mechanical layer integration
- Added dedicated mechanics module:
  - `src/instruments/piano/mechanics.hpp`
  - `src/instruments/piano/mechanics.cpp`
- Triggered independently from tonal voices:
  - key-down thump from `note_on`,
  - damper-fall thud from `note_off`,
  - pedal whoosh on CC64 edges.
- Added pedal sample selection path in sample library:
  - `select_pedal_sample(bool pedal_down, uint32_t seed)`.

### 2) Continuous timbre smoothing
- Updated `SampledPianoVoice`:
  - equal-power layer blend (replacing simple linear blend),
  - velocity-tracked low-pass coefficient,
  - spectral tilt (body + high-band mix),
  - gentle sustain darkening over time.

### 3) Resonance prototype replacement
- Replaced prior oscillator-style resonance core with a delay-line/waveguide-style network in:
  - `src/instruments/piano/resonance_matrix.cpp`
  - `src/instruments/piano/resonance_matrix.hpp`
- Implemented:
  - per-key delay-line state (88 keys),
  - inharmonicity-informed delay warp,
  - coupling excitation spread,
  - small soundboard diffusion network (4-line scattering).
- Added finite-state sanitization guards in resonance path to prevent NaN propagation under dense pedaled passages.

### 4) Test coverage additions
- Added regression test:
  - `test_pedal_edge_mechanics_emit_energy_without_spikes`
- Existing suite remained green, including:
  - low-register 20-note pedal glitch bound,
  - soak stability and memory-growth checks,
  - SFZ/FLAC/sample streaming tests.

## Verification

### Full verification pipeline
- Command: `bash scripts/run-full-verification.sh`
- Result: pass
  - debug tests: pass
  - release perf gate: pass (median realtime factor `x6.50722`)
  - ASAN/UBSAN tests: pass
  - TSAN tests: pass
  - sample generation: pass
  - wav health gate: pass

### Onset stress benchmark (release)
- Command: `build/release/vb_engine_onset_stress_benchmark`
- Result:
  - `block_budget_ms=1.3333`
  - `max_block_ms=1.1108`
  - `over_budget_blocks=0/6000`
  - `hard_jump_events=0`
  - `non_finite_output_samples=0`

### Showcase render diagnostics
- Command: `build/release/vb_engine_sample_generator`
- Result: all generated songs reported:
  - `hard_jump_events=0`
  - `non_finite_output_samples=0`

## Outcome
- This pass removes the biggest remaining synthetic resonance signature from the previous core.
- Runtime stability remains within realtime constraints for the provided stress workload.
- Generated sample artifacts now pass quality gates without non-finite output regressions.
