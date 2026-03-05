# Piano CPU Optimization Loop Report (2026-02-15)

## Goal
Reduce piano render CPU cost iteratively while preserving perceived quality and avoiding artifacts (pops, static, compression-like behavior, glitching).

## Gates Used
- CPU gate (release benchmark): `scripts/check-performance.sh`
- Audio gate (baseline comparison): `scripts/check-audio-quality.sh /tmp/vb_engine_baseline_samples`
- Functional checks: unit tests + ASan/UBSan test run

Quality gate thresholds:
- worst-case SNR >= 65 dB across showcase samples
- worst-case peak difference <= 0.03

## Baseline (before optimization loop)
Release benchmark (`vb_engine_benchmark`):
- Median realtime factor: `3.93522`
- Mean realtime factor: `3.96467`

## Iteration Summary
1. Attempt: oscillator recurrence replacing `std::sin`
- Result: rejected
- Reason: slower CPU and larger audio divergence on sustained samples.

2. Attempt: active-voice-only render loop in `VoicePool`
- Result: kept
- Reason: behavior-preserving structural optimization.

3. Attempt: high-resolution interpolated sine lookup table (`FastSinTable`), normalized fast path
- Result: kept
- Reason: major CPU reduction with very high signal similarity.

4. Attempt: reduced sine table size (32768 -> 16384)
- Result: rejected
- Reason: median performance regressed versus best pass.

## Best Stable Result
Best kept pass median realtime factor (5 runs): `11.4452`
- Improvement vs baseline median: `+190.8%`
- Quality gate: PASS
  - worst SNR: `110.668 dB`
  - worst peak diff: `3.05176e-05`

## Final Notes
- Performance variance exists run-to-run; median is used as decision metric.
- Audio comparison indicates no meaningful perceptual degradation across current showcase set.
- Optimization loop stopped after a non-improving iteration and revert.
