# Current Limitations and Future Work

## 1. Piano Realism Boundaries
- The engine is a hybrid sampled + modeled design, not a full finite-element piano simulation.
- Real-string nonlinear interactions are approximated (not exact hammer/string PDE solving).
- Stereo image realism depends heavily on the quality and perspective of the loaded SFZ library.

## 2. Sample Dependency Quality Ceiling
- Final tone quality is constrained by source samples:
  - velocity layer count,
  - release/mic captures,
  - noise floor and edit quality.
- If samples are sparse or heavily processed, realism ceiling drops regardless of DSP quality.

## 3. Pedal / Controller Nuance
- Binary and continuous pedal modes are supported, but controller hardware variance remains large.
- Edge hysteresis reduces chatter, but unusual noisy CC streams may still require host-side filtering.

## 4. Performance Scope
- Realtime path is optimized, but very high sample rates + high polyphony + dense convolution can still raise CPU.
- Large sample libraries can increase startup/load time depending on disk speed.

## 5. Platform Coverage
- Primary tested target is Linux.
- Cross-platform wrappers and full QA on macOS/Windows are not yet complete.

## 6. API Scope
- C ABI is intentionally lean.
- Advanced internals are not all exposed as host-facing controls.

## 7. Guitar Status
- Guitar engine remains a stub compared to piano quality level.
- Significant work remains for realistic guitar articulation, picking, body behavior, and effects chain.

## 8. Paths to Improve Further
1. Add deeper physical-model coupling (string/bridge/soundboard) with SIMD-friendly implementations.
2. Expand host-exposed parameter API for fine voicing/perspective control.
3. Add automated perceptual regression testing from reference recordings.
4. Add platform CI matrix and packaged wrapper targets.
5. Add richer sample-library validation tooling (noise, phase alignment, loop quality reports).
