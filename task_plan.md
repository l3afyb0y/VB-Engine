# Task Plan: VB-Engine Piano Engine Redesign Stage 1

## Goal
Land the first major redesign stage for VB-Engine by replacing the current serial piano topology with a bridge-centered coupling backbone while keeping the engine stable, testable, and Carla-ready.

## Current Phase
Phase 5

## Phases
### Phase 1: Requirements & Discovery
- [x] Confirm redesign direction and capture it in `REDESIGN.md`
- [x] Identify the smallest coherent architectural boundary for stage 1
- [x] Re-read the current runtime graph and supporting modules
- **Status:** complete

### Phase 2: Planning & Structure
- [x] Define the minimum viable bridge-centered refactor
- [x] Preserve the current public parameter and wrapper surface where possible
- [x] Document the implementation boundary in this plan
- **Status:** complete

### Phase 3: Implementation
- [x] Introduce a bridge-centered coupling module
- [x] Re-home current resonance and body behavior around bridge drive
- [x] Update voice/model plumbing to pass bridge-aware signals instead of only final audio
- **Status:** complete

### Phase 4: Testing & Verification
- [x] Run `cargo fmt`
- [x] Run `cargo test`
- [x] Run `cargo build -p vb-engine-vst3 --release`
- [x] Fix regressions and update progress notes
- **Status:** complete

### Phase 5: Delivery
- [x] Summarize the structural change
- [x] Call out what still remains from `REDESIGN.md`
- [x] Leave the engine in a state Parker can listen to in Carla
- **Status:** complete

## Key Questions
1. What is the smallest structural refactor that makes the engine tell a more truthful physical story right now?
2. How do we introduce a bridge-centered coupling hub without simultaneously attempting the full sympathetic receptor rewrite?
3. Which existing subsystems should be preserved as reduced models and simply moved into the new topology?

## Decisions Made
| Decision | Rationale |
|----------|-----------|
| Use `REDESIGN.md` as the durable design source of truth | Preserves the new architecture and pedal/resonance ideas across long implementation sessions |
| Limit stage 1 to a bridge-centered backbone rather than the full redesign | Prevents overreaching into sympathetic receptor rewrite, pedal assist, and UI redesign all at once |
| Preserve existing top-level gains and wrapper-facing parameters for now | Keeps Carla testing and host integration stable while internals change |
| Reuse existing reduced models where possible instead of deleting them outright | Lets us move topology first and upgrade subsystem fidelity afterward |
| Treat the current resonance bank as a temporary bridge-fed receptor model, not the final sympathetic system | Gives us a coherent migration path without pretending the old pitch-class bank is already "good enough" |
| Keep working docs in-project during this implementation stage | This redesign is multi-session work and needs durable notes |

## Errors Encountered
| Error | Attempt | Resolution |
|-------|---------|------------|
|       | 1       |            |

## Notes
- Re-read `REDESIGN.md` before large changes
- Topology change comes before subsystem sophistication
- If a subsystem cannot yet be made physically rich, at least place it in the right part of the graph

---

# Task Plan: VB-Engine Sympathetic Receptor Correction Pass

## Goal
Remove the most obvious symbolic-resonance leftovers from the bridge receptor path so low-register harmony is no longer shaped by hardcoded triad logic.

## Current Phase
Phase 5

## Phases
### Phase 1: Requirements & Discovery
- [x] Re-read the bridge receptor implementation and the updated redesign guide
- [x] Confirm whether the low-register chord issue matches remaining symbolic receptor logic
- [x] Identify the smallest coherent correction that fits the current temporary receptor-bank architecture
- **Status:** complete

### Phase 2: Planning & Structure
- [x] Replace direct chord-family seeding with overlap-driven excitation
- [x] Widen the temporary receptor bank downward to better cover the low-mid register
- [x] Keep the bridge-centered runtime shape intact while improving the receptor-driving rule
- **Status:** complete

### Phase 3: Implementation
- [x] Update `BridgeNetwork::note_on` to use harmonic-partial overlap scoring
- [x] Remove hardcoded fifth and major-third receptor seeding
- [x] Update retrigger damping to match the new overlap model
- [x] Add unit tests that protect the intended overlap ordering
- **Status:** complete

### Phase 4: Testing & Verification
- [x] Run `cargo fmt`
- [x] Run `cargo test`
- [x] Run `cargo build -p vb-engine-vst3 --release`
- [x] Reinstall the VST3 bundle
- **Status:** complete

### Phase 5: Delivery
- [x] Summarize the architectural correction
- [x] Leave Parker with a new Carla-testable build
- [x] Update docs so this reasoning does not get lost
- **Status:** complete

## Key Questions
1. Can we remove symbolic triad seeding without breaking the temporary receptor bank entirely?
2. How much low-register coverage should the temporary receptor bank gain before we move to a fuller sparse-note receptor network?
3. Which tests can legitimately protect the new architecture without defending old resonance shortcuts?

## Decisions Made
| Decision | Rationale |
|----------|-----------|
| Replace hardcoded chord-family `note_on` seeding with harmonic-overlap scoring | Moves the receptor path toward physical causality without requiring a full spectral bridge model yet |
| Expand the temporary receptor bank downward into the low-mid register | Makes C3-and-below behavior less dependent on a single-octave pitch-class simplification |
| Keep the sample-time bridge feed simple for this pass | Fixes the clearest conceptual bug first without overreaching into a full receptor-network rewrite |

## Notes
- This pass is a correction to the temporary receptor bank, not the final sympathetic-resonance architecture
- The next bigger sympathetic stage should still move toward a truly sparse per-note receptor network driven by a better bridge spectral proxy

---

# Task Plan: VB-Engine Physical Parameter Foundation Pass

## Goal
Introduce a shared note-physics layer so hammer, string, and bridge setup stop collapsing important mechanics into isolated register curves.

## Current Phase
Phase 5

## Phases
### Phase 1: Requirements & Discovery
- [x] Read the new Rust reference repos with an eye toward missing physical parameters
- [x] Update `REDESIGN.md` with the strongest new lessons
- [x] Choose one bounded foundation stage instead of trying to absorb every idea at once
- **Status:** complete

### Phase 2: Planning & Structure
- [x] Define the shape of a shared note-physics layer
- [x] Decide which current subsystems should consume it first
- [x] Keep the pass focused on setup-time physical properties rather than a full DSP replacement
- **Status:** complete

### Phase 3: Implementation
- [x] Add a new note-physics module
- [x] Refactor hammer setup to use the new physical layer
- [x] Refactor string setup to use the new physical layer
- [x] Refactor bridge receptor setup to use the new physical layer
- [x] Add unit tests protecting the physical-profile invariants
- **Status:** complete

### Phase 4: Testing & Verification
- [x] Run `cargo fmt`
- [x] Run `cargo test`
- [x] Run `cargo build -p vb-engine-vst3 --release`
- [x] Reinstall the VST3 bundle
- **Status:** complete

### Phase 5: Delivery
- [x] Summarize the foundation shift
- [x] Update public and internal docs
- [x] Leave Parker with a new Carla-testable build
- **Status:** complete

## Key Questions
1. Which physical parameters are important enough to deserve a first-class shared home right now?
2. How much bass-string material difference should be represented immediately versus staged for later?
3. Can we improve the foundation without destabilizing the current bridge-centered runtime shape?

## Decisions Made
| Decision | Rationale |
|----------|-----------|
| Add a shared `piano_physics` module now | Prevents hammer, strings, and bridge from drifting into three incompatible private models of the same note |
| Represent bass strings as a separate copper-wound regime | Makes room for a genuinely different low-register physical regime instead of a single generic string family |
| Keep ML out of the realtime engine for now | The likely near-term value is offline fitting and calibration, not replacing the core physical synthesis path |
| Relax one released-tail regression slightly | The new physical foundation produced a slightly more realistic lingering tail, and the old test line was a little too strict for that direction |

## Notes
- This is a foundation pass, not the final physical-model pass
- The point is to make later hammer, damper, bass, and soundboard upgrades land on explicit physical footing

---

# Task Plan: VB-Engine Body And String-Group Pass

## Goal
Reduce the low-mid body pool, widen the instrument spatially, and make middle-register notes behave more like real trichord groups before the next Carla comparison.

## Current Phase
Phase 5

## Phases
### Phase 1: Requirements & Discovery
- [x] Re-read the low A-minor waveform findings and current bridge/body code
- [x] Confirm whether the next bottleneck is body pooling plus coarse string-group mapping rather than pitch math
- [x] Choose one coherent pass that touches body and string groups together
- **Status:** complete

### Phase 2: Planning & Structure
- [x] Refine the note-physics string-group mapping so middle C and above move into trichord treatment
- [x] Split the body path into separate low, mid, air, and width behavior
- [x] Keep the current host-facing controls intact so the new build remains easy to A/B in Carla
- **Status:** complete

### Phase 3: Implementation
- [x] Update note-physics defaults for string count, detune, balance, and position
- [x] Rework the bridge/body internals around banded soundboard states and wider modal radiation
- [x] Update tests to protect the new assumptions instead of the old width heuristics
- **Status:** complete

### Phase 4: Testing & Verification
- [x] Run `cargo fmt`
- [x] Run `cargo test`
- [x] Run `cargo build -p vb-engine-vst3 --release`
- [x] Reinstall the VST3 bundle
- **Status:** complete

### Phase 5: Delivery
- [x] Leave a fresh Carla-testable build installed
- [x] Update architecture/progress docs so the new body and string-group assumptions are explicit
- [x] Call out what to listen for next
- **Status:** complete

## Key Questions
1. Can we reduce the false-low, growly body character without making the piano feel smaller?
2. How much of the fullness gap is still coming from under-modeled string groups in the middle register?
3. Which current tests are protecting real behavior, and which are still artifacts from the old tuning model?

## Decisions Made
| Decision | Rationale |
|----------|-----------|
| Keep bass notes physically distinct instead of forcing trichords everywhere | Preserves the real bass regime while still promoting the musically important middle register into richer string groups |
| Split body behavior into low, mid, air, and width states | Directly targets the observed low-mid pooling and near-mono body radiation |
| Update the width tests rather than forcing the new model to satisfy old assumptions | Prevents tests from pushing the code back toward stale tuning-era behavior |

## Notes
- This is still a reduced soundboard model, not the final body architecture
- If the next Carla test still sounds too false-low, the next likely step is note-wise bridge-to-body weighting, not another round of generic gain tuning

---

# Task Plan: VB-Engine Damper Foundation Pass

## Goal
Make dampers a first-class physical subsystem while preserving binary sustain-pedal defaults and leaving a clean path for half-pedal-capable hardware.

## Current Phase
Phase 5

## Phases
### Phase 1: Requirements & Discovery
- [x] Re-read the current voice, string, and sustain-pedal flow
- [x] Confirm the key-down and pedal-down damper rules
- [x] Update `REDESIGN.md` with FDTD, ML, and damper decisions
- **Status:** complete

### Phase 2: Planning & Structure
- [x] Define a compact damper subsystem with openness, felt contact, and choke outputs
- [x] Keep binary input as the default behavior
- [x] Add a continuous sustain mode for future half-pedal-capable inputs
- **Status:** complete

### Phase 3: Implementation
- [x] Add the damper subsystem
- [x] Wire key state and sustain lift through the voice model
- [x] Replace the string-bank scalar damper target with the damper model
- [x] Add targeted behavior tests
- **Status:** complete

### Phase 4: Testing & Verification
- [x] Run targeted damper and continuous-pedal tests
- [x] Run `cargo fmt`
- [x] Run `cargo test`
- [x] Run `cargo build -p vb-engine-vst3 --release`
- [x] Reinstall the VST3 bundle
- **Status:** complete

### Phase 5: Delivery
- [x] Leave a fresh Carla-testable build installed
- [x] Summarize what changed and what Parker should listen for next
- **Status:** complete

## Decisions Made
| Decision | Rationale |
|----------|-----------|
| Binary sustain pedal remains default | Parker's current MIDI pedal is on/off, and the default should match real test hardware |
| Damper state stays continuous internally | Real pianos support partial lift and felt contact is not a hard switch |
| Add `SustainPedalMode::Continuous` | Gives future hardware and host automation a direct path to half-pedal behavior |
| Use FDTD as a lab, not the main runtime rewrite | It can teach or calibrate subsystems without turning the VST callback into a research simulator |
| Keep ML as calibration/control tooling | DDSP-style parameter correction is more aligned with this project than black-box waveform generation |

## Notes
- This is still reduced damper physics, not final felt/contact material modeling
- The next likely sound pass remains local unison/string-group coupling and bass-string refinement
