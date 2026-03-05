# Piano Realism Iteration 2 - Verification Report (2026-02-16)

## Scope
Verification after adding:
- FLAC decoding for SFZ sample regions
- extended SFZ articulation support (`offset`, `end`, `tune`, `ampeg_release`, loop controls)
- sampled-voice click suppression and loop handling
- inharmonic multi-partial resonance update
- body+room post-DSP refinement

## Commands
- `bash scripts/run-full-verification.sh`

## Results

### Functional tests
- Debug tests: pass
- Soak tests: pass
- Added tests:
  - FLAC SFZ region load test: pass
  - SFZ offset behavior energy-reduction test: pass

### Sanitizer suites
- ASan/UBSan: pass
- TSan: pass

### Performance gate
- Benchmark median realtime factor: `x21.6471`
- Threshold: `x1.00`
- Result: pass

### Audio output health gate
All generated showcase WAV files passed:
- `clip_samples = 0` for all
- peaks normalized to `0.449982`
- no DC drift regressions

Observed max jump (`max_jump`) values:
- Fur Elise: `0.055542`
- Moonlight Sonata: `0.0402222`
- Turkish March: `0.0504456`
- Clair de Lune: `0.0527954`
- Chopin Ballade No.1: `0.0491943`
- Creep (transposed): `0.0572815`
- Bach Prelude C: `0.0442505`

## Notes
- CPU cost increased vs previous simpler post-DSP/resonance path, but remains comfortably realtime.
- This iteration targets realism quality first while preserving clean, stable rendering.
