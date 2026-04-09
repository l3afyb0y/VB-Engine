# Progress Log

## Session: 2026-04-09

### Phase 1: Requirements & Discovery
- **Status:** complete
- **Started:** 2026-04-09
- Actions taken:
  - Read the repo shape to identify engine, examples, tests, and docs
  - Loaded the `brainstorming` and `planning-with-files` skill instructions relevant to this task
  - Queried local memory and confirmed only a lightweight checkpoint exists for this repo
  - Created persistent planning files for this tuning session
- Files created/modified:
  - task_plan.md (created)
  - findings.md (created)
  - progress.md (created)

### Phase 2: Planning & Structure
- **Status:** complete
- Actions taken:
  - Read the engine architecture and the hammer, string, resonance, and output submodels closely
  - Compared VB-Engine’s approach against the older `Piano` waveguide engine for concrete acoustic voicing cues
  - Extracted and inspected the reference audio timing enough to locate the main C4 onset region
  - Rendered the current component-isolation examples and measured rough C4 partial balance
  - Identified the first implementation stage: unison-realism tuning plus the misleading regression-test fix
- Files created/modified:
  - findings.md (updated)
  - task_plan.md (updated)

### Phase 3: Implementation
- **Status:** complete
- Actions taken:
  - Tuned string-bank unison voicing with smaller detune values, narrower stereo spread, and rebalanced multi-string gains
  - Refined per-partial decay modeling to use explicit T60-derived pole radii and shorter sustain targets
  - Shifted the hammer-to-string handoff so the string layer starts stronger and swells less
  - Tightened the low-sample-rate regression test so it checks diagnostics instead of only sanitized output
  - Updated architecture docs to match the actual engine control surface
  - Tried a broader 3-string middle-C experiment, measured the downside, and backed it out
- Files created/modified:
  - src/engine/piano_strings.rs
  - src/engine/piano_voice.rs
  - tests/engine_behaviors.rs
  - docs/architecture.md

### Phase 4: Testing & Verification
- **Status:** complete
- Actions taken:
  - Ran the full `cargo test` suite until it passed after the tuning changes
  - Re-rendered the component-isolation WAVs for listening comparison
  - Compared the reference C4 attack window against current full-mix and strings-only renders with a rough partial-bin analysis
- Files created/modified:
  - /home/rowen/Videos/component-isolation/01-full-mix.wav
  - /home/rowen/Videos/component-isolation/02-strings-only.wav
  - /home/rowen/Videos/component-isolation/03-resonance-only.wav
  - /home/rowen/Videos/component-isolation/04-body-only.wav
  - /home/rowen/Videos/component-isolation/05-mechanical-only.wav
  - /home/rowen/Videos/component-isolation/06-ambience-only.wav
  - /home/rowen/Videos/component-isolation/07-strings-plus-resonance.wav
  - /home/rowen/Videos/component-isolation/08-no-resonance.wav
  - /home/rowen/Videos/component-isolation/09-no-body.wav

### Phase 5: Delivery
- **Status:** in_progress
- Actions taken:
  - Audited the committed docs and corrected stale install and parameter-surface references
  - Analyzed Pianoteq screenshots as architecture clues for what the next VB-Engine tuning stages should target
  - Implemented a second tuning pass focused on dry-core fullness, harmonic readout weighting, and louder VST3 defaults
  - Rebuilt and installed the release VST3 bundle into `~/.vst3` for Carla testing
- Files created/modified:
  - README.md
  - docs/README.md
  - Samples/README.md
  - findings.md
  - progress.md
  - src/engine/piano_strings.rs
  - src/engine/piano_output.rs
  - plugins/vst3/src/parameters.rs
  - tests/engine_behaviors.rs

## Test Results
| Test | Input | Expected | Actual | Status |
|------|-------|----------|--------|--------|
| Memory checkpoint relevance | OpenMemory query for VB-Engine tuning context | Useful technical context or none | Only lightweight checkpoint, no technical detail | ✓ |
| Low-sample-rate regression status | `cargo test extremely_low_sample_rates_do_not_make_dc_blockers_explode -- --nocapture` | Current test passes or reveals instability | Passes, but still only proves sanitized output is finite | ✓ |
| Layer control spot-check | `cargo test --test engine_behaviors ...` for hammer/string/mechanical gain tests | Layer controls still affect behavior | All three targeted tests passed | ✓ |
| Full regression suite | `cargo test` | All tests pass after tuning changes | Pass | ✓ |
| C4 reference timing | `ffmpeg ... silencedetect` on `reference.wav` | Identify main note onset region | First strong onset starts around `1.40 s` | ✓ |
| C4 attack comparison | Python partial-bin comparison of reference and current renders | Rough direction on harmonic balance | Fundamental improved; 2nd harmonic still too strong | ✓ |
| Pianoteq screenshot inference | Manual architecture read from provided screenshots | Better clues about why Pianoteq sounds fuller | Points toward stronger dry core, hotter internal signal, richer hammer/impedance/direct-radiation model | ✓ |
| Carla-ready release bundle | `bash ./install-vst3.sh` | Fresh release VST3 installed under `~/.vst3` | Pass | ✓ |
| Second-pass C4 comparison | Python partial-bin comparison after pickup/direct changes | Lower 2nd harmonic, stronger dry-core match | Pass, though odd dominance may now be slightly over-corrected | ✓ |

## Error Log
| Timestamp | Error | Attempt | Resolution |
|-----------|-------|---------|------------|
|           |       | 1       |            |

## 5-Question Reboot Check
| Question | Answer |
|----------|--------|
| Where am I? | Phase 1, requirements and architecture discovery |
| Where am I going? | Planning, implementation, then verification of tonal/stability improvements |
| What's the goal? | Refamiliarize with VB-Engine and improve piano realism without losing stability |
| What have I learned? | See findings.md |
| What have I done? | Repo scan, skill load, memory check, planning-file creation |

---
*Update after completing each phase or encountering errors*
