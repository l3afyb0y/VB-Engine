# Findings & Decisions

## Requirements
- Refamiliarize with VB-Engine in its current tuning-focused branch state.
- Aim for the best implementation possible rather than a quick patch.
- Use `~/Videos/pianoteq-reference.mp4` as an ideal C4 reference anchor.
- Reduce the current synthetic or electronic character and move toward a more acoustic piano sound.
- It is acceptable to make substantial changes on this fresh branch if they are reasoned and verified.
- The `~/github-branches/Piano` C++ engine may be useful for algorithmic inspiration, but the Rust implementation should remain safe.
- Audit the repo for consistency and coherency, not just isolated DSP tweaks.
- Consider the external review note about the low-sample-rate regression test being too weak because sanitized output can hide internal non-finite behavior.

## Research Findings
- The user-provided external summary points toward four likely realism gaps: non-linear hammer interaction, string inharmonicity/dispersion, sympathetic resonance/body coupling, and mechanical/noise/body radiation components.
- The likely immediate value is not "add everything", but identify which of those categories already exist in VB-Engine and which are either missing or overly simplified.
- VB-Engine already implements more of the acoustic-model stack than the current sound suggests:
  - hammer contact uses a non-linear compression law with oversampled contact integration
  - strings include per-partial inharmonicity, detuned unisons, strike-position weighting, and per-partial decay
  - output includes a lightweight body-mode bank and ambience network
  - there is a separate resonance bank, but it is very coarse and pitch-class-based rather than behaving like broad sympathetic string coupling
- The likely glassy or synthetic character is therefore more likely due to voicing and interaction balance than a total lack of physical ingredients.
- Specific tonal risk areas identified in code:
  - hammer brightness and attack terms stack several additive "bloom" signals and may be over-feeding upper partial emphasis
  - string rendering applies explicit high-partial brightness boosts that may be too exposed, especially early in the note
  - resonance and body stages are intentionally lightweight and may read as generic resonator/reverb support rather than believable soundboard coupling
  - the final soft clip plus current default gains may be flattering harsh transients rather than warming them
- A stronger and more concrete realism problem emerged in the string-bank voicing:
  - VB-Engine currently gives C4 only 2 strings, while the older Piano engine switches to 3 strings well below middle C
  - current unison detune values are very large for an acoustic target (`~3.4` to `6` cents), which is much closer to audible chorus than natural piano bloom
  - current unison stereo spread is also very wide, which can make the note image feel synthetic rather than like a soundboard-radiated instrument
  - current 3-string balance heavily suppresses the center string, which may exaggerate the left/right split instead of creating a coherent struck note
- Rough C4 attack analysis:
  - reference clip onset begins at about `1.40 s`
  - reference attack is strongly fundamental-dominant with a relatively weak 2nd harmonic
  - VB-Engine full-mix and strings-only attacks show a noticeably stronger 2nd harmonic and more orderly partial structure
- Existing isolation tooling revealed another coherency issue:
  - `mechanical-only`, `body-only`, and `ambience-only` renders effectively collapse to silence in the current example output
  - that does not necessarily mean the layers are unused, but it does mean the current examples are not giving a truthful picture of perceived layer contribution
- Implemented tuning outcomes:
  - kept the original 1/2/3-string note ranges after a measured experiment showed that forcing C4 into the 3-string region weakened the center of the note in this model
  - reduced unison detune from chorus-like values into a much tighter acoustic range
  - narrowed unison stereo spread substantially while preserving greater width in the upper register than the bass
  - shortened and clarified per-partial decay targets using explicit T60-derived pole radii
  - reduced bridge-memory persistence and made the string layer less prone to late attack swelling
  - tightened the low-sample-rate regression test so it now checks `non_finite_output_samples` directly
- Post-change C4 snapshot:
  - full-mix attack odd/even ratio ended around `3.43`, better than the failed intermediate tuning attempt but still well short of the reference at `5.64`
  - full-mix fundamental magnitude moved closer to the reference while peak level remained slightly below it
  - conclusion: this pass reduced chorused/electronic behavior, but the next realism bottleneck is still low-order harmonic voicing in the excitation/string path
- Second tuning pass aimed at Carla readiness:
  - added a simple pickup/radiation weighting in the string-bank summation, which sharply reduced the over-strong 2nd harmonic in C4
  - added a restrained direct-versus-radiated lift in the output stage instead of relying only on body and ambience
  - raised the VST3 wrapper defaults so the plugin should come up audibly stronger in Carla without needing immediate gain compensation
  - rebuilt and reinstalled the release VST3 bundle into `~/.vst3`
- Latest rough C4 comparison after that pass:
  - full-mix odd/even ratio moved to about `6.99`, which is much closer in spirit to the reference’s odd-dominant character, though now arguably a bit too far in that direction
  - full-mix 2nd harmonic dropped from roughly `0.0051` to about `0.0017`, much closer to the reference at `0.00136`
  - overall peak is still below the reference in the offline render, but the plugin-side defaults are now hotter for listening in Carla
  - subjective expectation: less glassy even-harmonic glare, more centered dry note, somewhat fuller immediate body, and a louder starting point in the VST3
- Pianoteq screenshot analysis suggests several important architectural lessons:
  - the public parameter surface is dramatically wider than VB-Engine’s current 7 engine controls, especially around hammer voicing, impedance, strike point, direct sound, mic/radiation placement, pedal noise, and post-effects
  - many advanced parameters are shown as existing even when "locked", which strongly implies Pianoteq keeps a richer internal model than the preset UI exposes
  - several optional enhancement features are effectively off in the shown preset (`sympathetic resonance 0`, `duplex scale resonance 0`, note effects off, EQ modest, many mic/effect values near neutral), yet the sound is still much fuller than VB-Engine
  - that implies their quality is coming first from the dry core instrument and radiation model, not from rescue-by-effects
  - `Post Effect Gain = -12 dB` is a particularly strong clue: Pianoteq’s core signal appears hot enough that the preset trims it down after the fact, while VB-Engine is quiet before any such post trim exists
  - `Unison Width = 0` in the preset is another strong clue that their fullness is not coming from exaggerated stereo widening
  - exposed controls like `Hammer Hardness`, `Hammer Tone`, `Strike Point`, `Pickup Distance`, `Impedance Cutoff`, `Impedance Slope`, and `Direct Sound Duration` map much more closely to the low-order harmonic and radiation character we are still missing

## Technical Decisions
| Decision | Rationale |
|----------|-----------|
| Separate refamiliarization from implementation | Prevents mixing architecture discovery with speculative DSP edits |
| Include a consistency/coherency audit in the same pass | Tone work is harder if the model responsibilities are blurry or duplicated |
| Treat the weak regression test as part of the task scope | Stability checks should detect internal failures, not just sanitized output |
| Focus first on tonal voicing and subsystem coherency, not adding brand-new large subsystems | The current engine already contains simplified versions of the expected acoustic components |
| First code pass should narrow unison detune and stereo spread, and move C4 into the 3-string region | This is evidence-backed, acoustically plausible, and lower-risk than redesigning hammer or resonance math first |
| Revert the temporary 3-string C4 experiment while keeping the tighter unison voicing | The measurement and regression feedback showed that experiment hurt this model more than it helped |
| Next realism pass should prioritize dry-core fullness and low-order harmonic shaping over more reverb/resonance features | The Pianoteq screenshots show a fuller sound even with many optional enhancement features effectively neutralized |
| Use a simple pickup-style output weighting as a stand-in for richer direct/radiation sensing | It is a compact way to attack the low-order harmonic mismatch that the Pianoteq screenshots strongly suggest matters |

## Issues Encountered
| Issue | Resolution |
|-------|------------|
| No meaningful detailed project memory for this repo | Proceed from current code and repository docs |

## Resources
- `~/Videos/pianoteq-reference.mp4`
- `~/github-branches/Piano`
- User-provided acoustic realism notes about hammer nonlinearity, inharmonicity, resonance coupling, and mechanical noise
- Core tone-shaping files in this repo:
  - `src/engine/piano_hammer.rs`
  - `src/engine/piano_strings.rs`
  - `src/engine/piano_resonance.rs`
  - `src/engine/piano_output.rs`

## Visual/Browser Findings
- Not yet populated. The reference video still needs direct inspection.

---
*Update this file after every 2 view/browser/search operations*
*This prevents visual information from being lost*
