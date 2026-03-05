# Piano Realism Iteration 4 - Bedroom Acoustic Retune (2026-02-16)

## Scope
This pass targeted the remaining "compressed/synthetic" character by retuning post-DSP toward a small-room (bedroom-scale) acoustic profile while preserving realtime safety and artifact-free rendering.

Implemented changes:
- Bedroom-profile room IR retune in `post_processor`:
  - first-order reflection taps derived from a representative small-room geometry,
  - shorter late decay profile with high-frequency damping,
  - reduced diffuse tail density compared with iteration 3.
- Non-uniform partition cadence smoothing:
  - replaced held mid/late partition outputs with interpolated targets to reduce cadence-step roughness.
- Dynamic response refinement:
  - widened modeled fallback voice velocity response in `acoustic_grand_voice.cpp`.
- Test stability improvements:
  - pedal and dynamics behavior tests now set `piano_reverb_wet=0` to validate pedal/dynamic logic directly, independent of room-tail coloration.

## Acoustic Research Notes (small room target)
- EBU Tech 3276 recommends very short early reflection-free windows in control-room listening geometry and gives practical short-room RT guidance; we used this as a constraint for tight early reflection timing and short decay behavior.
- A large-scale dwellings study reports reverberation behavior in occupied rooms and indicates bedroom acoustics are strongly volume/furnishing dependent; this supports using a shorter, damped tail for a furnished bedroom profile.
- Image-source modeling (Allen & Berkley) remains a practical baseline for deterministic early reflection timing in rectangular rooms.

## Verification Command
- `bash scripts/run-full-verification.sh`

## Verification Results
- Debug tests: pass
- Soak tests: pass
- ASan/UBSan: pass
- TSan: pass
- Performance gate: pass
  - median realtime factor: `x17.9341`
- WAV health gate: pass
  - all showcase files: `clip_samples=0`

Selected WAV-health metrics:
- Fur Elise: `max_jump=0.0346985`
- Moonlight Sonata: `max_jump=0.0404053`
- Turkish March: `max_jump=0.0299377`
- Clair de Lune: `max_jump=0.0377197`
- Chopin Ballade No.1: `max_jump=0.0308838`
- Creep (transposed): `max_jump=0.0368958`
- Bach Prelude C: `max_jump=0.0343018`

## Outcome
This pass improves small-room realism and reduces residual synthetic grain without sacrificing stability, CPU margin, or click/pop safety.

## References
- EBU Tech 3276 (Listening conditions for control rooms): https://tech.ebu.ch/files/live/sites/tech/files/shared/tech/tech3276.pdf
- Bedroom reverberation in occupied dwellings (Applied Acoustics 2024): https://www.sciencedirect.com/science/article/pii/S0003682X24000457
- Image method for room impulse response simulation (Allen & Berkley, 1979): https://doi.org/10.1121/1.382599
- JUCE convolution docs (zero latency / non-uniform): https://docs.juce.com/master/classjuce_1_1dsp_1_1Convolution.html
