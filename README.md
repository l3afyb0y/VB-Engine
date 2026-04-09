# VB-Engine

VB-Engine is being rewritten as a lean, Rust-native realtime audio engine with a modeled acoustic piano as the first serious target.

The old C/C++ implementation has been moved into `archive/` as a local reference and safety net. The live repo root now represents the future codebase.

## Current State
- Rust project at repo root
- Workspace now includes a thin `plugins/vst3` wrapper crate
- Legacy implementation parked in `archive/`
- Thin C ABI restored under `include/vb_engine/c_api.h`
- Rebuilt Rust-native piano tester (`cargo run --features tester --bin piano_tester`)
- Rust-backed verification and sample-generation scripts restored
- Modeled piano currently includes:
  - bounded block-size validation
  - fixed-capacity voice pool
  - inharmonic partial-bank voice with 1/2/3-string unison behavior
  - sustain and soft pedal handling
  - filtered hammer transient
  - lightweight sympathetic resonance, body coupling, and ambience bloom
  - voice-steal and output diagnostics

## Goals
- Pure idiomatic Rust
- Lean architecture with low dependency overhead
- Realtime-safe render path
- Minimal `unsafe`
- Modeled sound generation instead of sample-library-driven playback

## Build
```bash
cargo test
```

Build the VST3 wrapper:

```bash
cargo build -p vb-engine-vst3 --release
```

Install the staged Linux VST3 bundle to `~/.vst3`:

```bash
bash ./install-vst3.sh
```

Install to a different VST3 directory:

```bash
bash ./install-vst3.sh /some/other/vst3-dir
```

The install script builds `vb-engine-vst3`, stages a Linux VST3 bundle under `target/vst3-bundles/`, and then copies it into the destination VST3 directory.

Render a quick Rust-generated demo WAV:

```bash
cargo run --bin render_scale -- target/render-scale.wav
```

Render a small showcase set with plain, pedal, and soft-pedal phrases:

```bash
cargo run --bin render_showcase -- target/showcase
```

Run the rebuilt piano tester:

```bash
cargo run --features tester --bin piano_tester
```

The tester includes a separate settings window for live voicing changes. Use the top-bar settings button to open it, then drag the five sliders to tune:
- master gain
- hammer noise
- resonance
- body
- ambience

The VST3 wrapper currently exposes a wider advanced voicing surface than the tester, including:
- master gain
- string level
- mechanical level
- hammer noise
- resonance
- body
- ambience

That gives host-side tuning and quick layer isolation in Carla by simply zeroing the layers you do not want to hear.

The same controls can also be seeded through environment variables:

```bash
VB_TESTER_MASTER_GAIN=0.18 \
VB_TESTER_HAMMER_NOISE=0.18 \
VB_TESTER_RESONANCE=1.40 \
VB_TESTER_BODY=1.00 \
VB_TESTER_AMBIENCE=0.30 \
cargo run --features tester --bin piano_tester
```

Run the full verification flow:

```bash
bash scripts/run-full-verification.sh
```

The same verification entrypoint is also available through:

```bash
cargo run --bin vb_engine -- verify
```

## Repo Layout
- `src/`: live Rust engine code
- `plugins/vst3/`: thin VST3 wrapper crate
- `src/bin/`: Rust-native command-line utilities
- `include/`: thin embeddable C ABI header
- `install-vst3.sh`: Linux VST3 staging and install helper
- `scripts/`: Rust-backed verification and generation entrypoints
- `Samples/`: generated showcase renders
- `tests/`: integration tests for engine behavior
- `archive/`: local legacy C/C++ implementation, ignored by git
- `dev/`: local scratch notes and prior planning artifacts

## Fish PATH Note
If Cargo-installed tools are not on `PATH`:

```fish
set -e fish_user_paths
set -U fish_user_paths $HOME/.cargo/bin $fish_user_paths
exec fish
```

Then verify:

```fish
which cargo
which c2rust
which autocxx
```

## Status
The Rust rewrite now covers the core engine, embeddable ABI surface, showcase generation, verification scripts, a rebuilt piano tester, and the first thin VST3 wrapper. The remaining work is about host validation, bundle polish, and deeper piano realism rather than standing up the basic replacement surfaces.
