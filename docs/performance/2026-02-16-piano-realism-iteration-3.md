# Piano Realism Iteration 3 - High-Impact Pass (2026-02-16)

## Scope
This pass implemented the next high-impact roadmap items:
- disk-backed large-sample storage (OS-paged mapped regions),
- pedal handling modes with binary and continuous support,
- non-uniform partitioned room convolution cadence,
- and a post-DSP retune to reduce perceived compression.

## User-Driven Audio Target
Addressed explicit feedback: output sounded "realistic but compressed."

Applied corrective changes:
- removed strong waveshaping behavior,
- replaced it with a transparent peak guard,
- retained body/room realism with more linear dynamics.

## Key Implementation Notes
- Large loaded regions can be promoted to mapped disk-backed storage in `PianoSampleLibrary` when above threshold.
- Pedal modes:
  - auto mode infers binary/continuous from CC64 data,
  - binary mode honors configurable threshold,
  - continuous mode supports graded damping (half-pedal behavior).
- Room convolution path uses non-uniform partition evaluation cadence:
  - early partition every sample,
  - mid partition every 2 samples,
  - late partition every 8 samples.

## Verification Commands
- `bash scripts/run-full-verification.sh`

## Verification Results
- Debug tests: pass
- Soak tests: pass
- ASan/UBSan: pass
- TSan: pass
- Performance gate: pass
  - median realtime factor: `x12.0953`
- WAV health gate: pass
  - all showcase files: `clip_samples=0`

Selected WAV-health metrics:
- Fur Elise: `max_jump=0.0365295`
- Moonlight Sonata: `max_jump=0.0278931`
- Turkish March: `max_jump=0.0283508`
- Clair de Lune: `max_jump=0.039856`
- Chopin Ballade No.1: `max_jump=0.030304`
- Creep (transposed): `max_jump=0.0400696`
- Bach Prelude C: `max_jump=0.0335693`

## Outcome
This pass prioritizes realism without compression-like flattening while preserving stability, zero clipping, and strong realtime margins.
