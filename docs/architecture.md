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
- `EngineConfig` defines sample rate, max block size, max voices, sustain threshold, sustain pedal mode, and the current top-level tone controls:
  - `master_gain`
  - `string_gain`
  - `mechanical_gain`
  - `hammer_noise_gain`
  - `resonance_gain`
  - `body_gain`
  - `ambience_gain`
- `Engine` owns a preallocated voice pool and diagnostics counters.
- `Engine::process_events` provides sample-offset block processing for plugin-host style MIDI, pedal, and parameter scheduling without reallocating the audio buffers.
- `PianoModel` owns the preallocated piano voice pool plus a bridge-centered coupling stage.
- `piano_physics` now provides a shared note-wise physical-parameter layer so hammer, string, and bridge setup do not each reinvent the note with separate register curves.
- `PianoVoice` is now a coordinator over explicit piano submodels:
  - `HammerModel` for strike energy, contact-force-style excitation, attack transient, and hammer escape behavior,
  - `StringBank` for stateful damped string resonators, inharmonic partial response, bridge-memory coupling, and unison layout,
  - `DamperModel` through the string bank for continuous openness, felt contact, and choke behavior,
  - voice lifecycle state for held vs. released decay plus bridge-drive handoff between hammer and strings.
- `BridgeNetwork` is the new coupling hub. It currently bundles:
  - bridge-motion tracking and projection,
  - a temporary bridge-fed receptor bank that replaces the old standalone resonance stage,
    now driven by harmonic-partial overlap instead of hardcoded triad shortcuts,
  - reduced soundboard/body modes,
    now driven by separate low, mid, air, and width states instead of one pooled body state,
  - lightweight ambience bloom.
- `render` now follows a more physically coherent flow:
  - hammer-driven string excitation,
  - key-local damper release when a note is held,
  - binary or continuous sustain-pedal interpretation above the physical damper model,
  - register-dependent 1/2/3-string unison behavior, with middle C and above now promoted into full trichord treatment,
  - per-voice bridge-drive export,
  - bridge-centered coupling into receptors and soundboard modes,
  - final observation-path mixing.
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
The current redesign stage is still a bootstrap, not the destination. Planned upgrades:
- package and validate the first Carla-loadable VST3 bundle
- deepen hammer/contact behavior
- richer string and damper interaction
- keep `SustainPedalMode::Binary` as the default for ordinary on/off pedals while preserving `SustainPedalMode::Continuous` for half-pedal-capable inputs
- replace the temporary bridge-fed receptor bank with a sparse sympathetic receptor network
  that is wider than the current low-mid reduced bank and more directly driven by bridge spectral structure
- deepen soundboard modeling further, especially note-wise string-group to bridge coupling and richer modal density
- add pedal interpretation and assistive clarity logic above the physical damper model
- add LV2 later without reopening the core event/parameter seam

## Legacy Reference
The prior C/C++ implementation is stored in `archive/` for local reference only and is no longer the active architecture.
