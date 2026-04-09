# Task Plan: VB-Engine Piano Tone Refamiliarization And Tuning

## Goal
Refamiliarize with VB-Engine, identify the architectural sources of its synthetic piano tone, and land the highest-value changes toward a more acoustic sound while preserving stability and coherence.

## Current Phase
Phase 5

## Phases
### Phase 1: Requirements & Discovery
- [x] Understand user intent
- [x] Identify constraints and requirements
- [x] Document findings in findings.md
- **Status:** complete

### Phase 2: Planning & Structure
- [x] Define technical approach
- [x] Identify the most leverageful tone-shaping stage to change first
- [x] Document decisions with rationale
- **Status:** complete

### Phase 3: Implementation
- [x] Execute the plan step by step
- [x] Improve tonal realism without destabilizing the engine
- [x] Add or tighten regression coverage where the current tests are misleading
- **Status:** complete

### Phase 4: Testing & Verification
- [x] Verify audio/render behavior with real commands
- [x] Document test results in progress.md
- [x] Fix any issues found
- **Status:** complete

### Phase 5: Delivery
- [x] Review output and coherency
- [x] Summarize architectural findings and tradeoffs
- [x] Deliver next-step recommendations if more tuning stages remain
- **Status:** in_progress

## Key Questions
1. Which current subsystem contributes most to the synthetic or glassy tone: hammer excitation, string dispersion/decay, output voicing, or resonance/body filtering?
2. What is the smallest coherent tonal change that moves the engine toward the C4 reference without opening a wide stability/regression surface?
3. Where are the codebase consistency/coherency mismatches that will make further tone work harder unless we clean them up now?

## Decisions Made
| Decision | Rationale |
|----------|-----------|
| Start with refamiliarization and architecture audit before tonal edits | Prevents random DSP tweaks and helps isolate where the tone is actually being shaped |
| Treat the repository state and measurements as source of truth over memory | Local memory only contains a light checkpoint, not technical detail |
| Keep working docs in-project during this session | This is a multi-step tuning effort and we need durable notes while exploring |
| First implementation stage will target unison realism rather than whole-engine redesign | Current evidence points to detune/pan/string-count voicing as a high-leverage synthetic-sound source |
| Fold the weak low-sample-rate regression test fix into the same pass | This is a small, important coherency/stability correction and should not wait |
| Keep the existing 1/2/3-string note ranges for now | The current resonator-bank model lost center energy when middle C was moved into the 3-string region |
| Use a slightly less brittle held-note decay threshold | The tuned note still clearly decays, and the test should guard behavior rather than freeze one exact voicing constant |

## Errors Encountered
| Error | Attempt | Resolution |
|-------|---------|------------|
|       | 1       |            |

## Notes
- Re-read this plan before making the first code change
- Prefer one strong tonal milestone at a time, then verify
- Treat the reference waveform and audible character as complementary signals
