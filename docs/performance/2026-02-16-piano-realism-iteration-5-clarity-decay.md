# Piano Realism Iteration 5 - Clarity + Damper Decay Pass (2026-02-16)

## Scope
Targeted user-reported residual issues:
- occasional light crackles,
- slightly dark/muffled room impression,
- and insufficiently realistic post-strike damper decay without full pedal.

## Implementation Summary
- `SampledPianoVoice` release model updated to note-dependent physical decay times:
  - slower default post-key-up decay (non-pedal) so notes linger naturally,
  - continuous pedal now uses a curved blend (`sqrt`) for stronger half-pedal effect.
- `AcousticGrandVoice` fallback now supports graded pedal-dependent release (previously pedal amount was ignored in fallback release).
- `PianoPostProcessor` retuned for more direct/clear presentation:
  - lower wet cap and reduced wet contribution,
  - brighter contour (less HF suppression),
  - smoother partition interpolation endpoint handling,
  - narrow per-sample step guard to suppress sparse crackle spikes.
- Default reverb wet lowered from `0.12` to `0.10` at engine/C-API defaults.

## Verification Command
- `bash scripts/run-full-verification.sh`

## Verification Results
- Debug tests: pass
- Soak tests: pass
- ASan/UBSan: pass
- TSan: pass
- Performance gate: pass
  - median realtime factor: `x21.0797`
- WAV health gate: pass
  - all showcase files: `clip_samples=0`

Selected WAV-health metrics:
- Fur Elise: `max_jump=0.0238647`
- Moonlight Sonata: `max_jump=0.0378418`
- Turkish March: `max_jump=0.0286865`
- Clair de Lune: `max_jump=0.0358887`
- Chopin Ballade No.1: `max_jump=0.0254822`
- Creep (transposed): `max_jump=0.0239258`
- Bach Prelude C: `max_jump=0.0305481`

## Outcome
This pass improves note release realism and clarity while reducing crackle risk, with preserved stability and strong realtime headroom.
