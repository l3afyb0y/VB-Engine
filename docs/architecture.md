# VB-Engine Rust Architecture

## Purpose
VB-Engine is a Rust-native realtime audio engine focused on modeled instrument synthesis, starting with acoustic piano and targeting plugin-host integration first.

## Design Priorities
- Lean codebase with minimal scaffolding
- Realtime-safe audio rendering
- Predictable performance under bounded polyphony
- Strong typed APIs and diagnostics
- Minimal `unsafe`, isolated when unavoidable

## Workspace Shape
- The root `vb-engine` crate is the shared DSP core.
- `plugins/vst3` is the first host wrapper crate and is intentionally thin.
- The SDL/ALSA `piano_tester` is now behind the optional `tester` feature so downstream plugin builds do not inherit tester-only dependencies.

## Current Runtime Shape
The initial Rust engine is intentionally small:
- `EngineConfig` defines sample rate, max block size, max voices, sustain threshold, and the current top-level tone controls:
  - `master_gain`
  - `hammer_noise_gain`
  - `resonance_gain`
  - `body_gain`
  - `ambience_gain`
- `Engine` owns a preallocated voice pool and diagnostics counters.
- `Engine::process_events` provides sample-offset block processing for plugin-host style MIDI, pedal, and parameter scheduling without reallocating the audio buffers.
- `PianoModel` owns the preallocated piano voice pool plus resonance and output stages.
- `PianoVoice` is now a coordinator over explicit piano submodels:
  - `HammerModel` for strike energy, contact-force-style excitation, attack transient, and hammer escape behavior,
  - `StringBank` for stateful damped string resonators, inharmonic partial response, bridge-memory coupling, and unison layout,
  - voice lifecycle state for held vs. released decay plus bridge-feedback handoff between hammer and strings.
- `render` mixes the modeled voice path with:
  - hammer-driven string excitation,
  - register-dependent 1/2/3-string unison behavior,
  - lightweight sympathetic resonance,
  - body coupling,
  - lightweight ambience bloom.
- `render_scale`, `render_showcase`, and `vb_engine` provide Rust-native offline render and verification entrypoints.
- `RenderEvent` and `ProcessEvent` cover offline sequencing and plugin-facing block events respectively.
- `EngineParameter` provides a typed automation surface for wrapper crates so plugin formats do not need their own private parameter-to-engine scheduling layer.
- `src/c_api.rs` exposes a thin C-compatible ABI shim over the Rust engine for embeddable hosts.
- `plugins/vst3` owns:
  - VST3 processor/controller classes,
  - host parameter metadata plus MIDI pedal mapping,
  - block-event translation into `ProcessEvent`,
  - the only intentionally retained raw-COM/FFI `unsafe` boundary in the current architecture.
- `piano_tester` is a temporary validation harness for direct interaction with the engine. It is not a core product surface and can eventually be replaced by host-side plugin validation, especially in Carla.

## Immediate Evolution Path
The current voice model is still a bootstrap, not the destination. Planned upgrades:
- package and validate the first Carla-loadable VST3 bundle
- deepen hammer/contact behavior
- richer string and damper interaction
- deeper sympathetic resonance
- stronger body or soundboard modeling
- add LV2 later without reopening the core event/parameter seam

## Legacy Reference
The prior C/C++ implementation is stored in `archive/` for local reference only and is no longer the active architecture.
