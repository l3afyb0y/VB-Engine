# Piano Player Adapter Contract (VB-Engine)

## Objective
Allow Piano Player (`~/github-branches/Piano-MIDI-Player`) to adopt VB-Engine with minimal disruption and reversible migration.

## Adapter Surface
- Engine lifecycle
  - `init(sample_rate, block_size, channels)`
  - `shutdown()`
- Event API
  - `note_on(channel, note, velocity)`
  - `note_off(channel, note, velocity)`
  - `control_change(channel, cc, value)`
  - `pitch_bend(channel, value14)` (next)
- Render API
  - `render(float** outputs, frames)`

## Mapping Rules
- MIDI note/velocity are passed as raw values.
- Velocity normalization occurs in engine (`0-127 -> [0, 1]`).
- Sustain pedal is CC64 with threshold handling in core.
- Channel routing retained for future multitimbral support.

## Migration Plan
1. Add adapter behind existing Piano Player engine interface.
2. Add runtime toggle: `legacy` vs `vb_engine`.
3. Compare output parity and latency in dev builds.
4. Promote VB-Engine as default after acceptance tests.
5. Keep legacy fallback for one release cycle.

## ABI Stability Policy
- `VB_ENGINE_ABI_VERSION` increments only on breaking changes.
- New fields in config structs are appended with size/version checks.
- Deprecated C functions remain for >= 1 minor release.
