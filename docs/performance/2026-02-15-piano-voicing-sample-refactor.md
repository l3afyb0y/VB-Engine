# Piano Voicing and Sample Suite Refactor (2026-02-15)

## User-facing issues addressed
- Prior samples sounded overly synth-like.
- Output level was too hot.
- Low-end energy felt excessive.
- Sample set did not reflect musical showcase expectations.

## Engine changes
- Added deterministic per-string phase offsets to reduce static phase-lock tone while keeping performance stable.
- Added attack noise component (hammer-like onset texture).
- Kept key-off transient but reduced harshness.
- Added per-note gain shaping to reduce low-register boom and improve balance.
- Added per-voice high-pass stage (~42 Hz) to reduce rumble/DC-like buildup.
- Reduced sympathetic resonance bus gain and excitation amounts.
- Added output headroom shaping in voice mix path.
- Added anti-alias partial culling and reduced active partial/string counts to keep rendering above realtime.

## Sample generation changes
- Replaced technical-only demos with song-based showcases:
  - Fur Elise excerpt
  - Moonlight Sonata 1st movement opening-focused excerpt
  - Turkish March excerpt
  - Clair de Lune excerpt
  - Chopin Ballade No.1 excerpt
  - Creep transposed excerpt
  - Bach Prelude in C excerpt
- Added deterministic file cleanup (old `Piano-*.wav` removal before regen).
- Added linear peak normalization target (`~0.45` FS) and edge fades to avoid click artifacts.

## Added validation coverage
- New soak/stability test binary integrated into CTest:
  - randomized MIDI stress
  - repeated create/destroy cycles
  - Linux RSS growth bound check
- New WAV health analyzer:
  - peak limit
  - clipped-sample detection
  - DC offset bound
  - sample-to-sample jump bound
- Full verification script now includes:
  - debug tests
  - release perf gate
  - ASan/UBSan (with leak detection)
  - TSan
  - sample regeneration + WAV health gate

## Current verification summary
- Debug CTest: pass (`vb_engine_tests`, `vb_engine_soak_tests`).
- ASan/UBSan CTest with leak detection: pass.
- TSan CTest: pass.
- Release perf gate: pass.
- WAV health gate on generated samples: pass.
