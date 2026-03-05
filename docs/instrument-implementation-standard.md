# Instrument Implementation Standard (VB-Engine)

This standard applies to every new instrument added to VB-Engine.

## Required Workflow
1. Complete research first:
   - physics/acoustics of sound production,
   - mechanical origin of audible behavior,
   - what makes the instrument perceptually distinctive.
2. Write public documentation in `docs/instruments/`:
   - mechanism overview,
   - unique properties,
   - section-by-section replication plan (`physical origin -> audible effect -> VB replication`).
3. Implement minimal credible model in engine core.
4. Generate and update public showcase audio in `Samples/`.
5. Run tests/benchmarks/sanitizer checks before completion claims.

## Sample Policy (Public)
- Store instrument showcase outputs in root `Samples/`.
- Keep files up to date whenever instrument implementation changes.
- Cover broad behavior, not just one dry note.

### Required sample dimensions
- dynamic range (soft to loud),
- articulation (staccato, legato, repeated notes),
- register coverage,
- pedal/control interactions where applicable,
- release/transient behavior,
- musically realistic phrase/chord context.

## Clean Project Rules
- Keep temporary notes/scratch in `dev/` only.
- Keep maintained public docs in `docs/` only.
- Avoid stray generated files outside prescribed directories.
- Regenerate samples through scripts to keep process reproducible.

## Minimum implementation checklist
- Stable tuning reference documented.
- Realtime-safe behavior preserved (no callback allocations/locks/I/O).
- Regression tests for core instrument behavior.
- Benchmark impact measured.
- Documentation updated in same iteration.
