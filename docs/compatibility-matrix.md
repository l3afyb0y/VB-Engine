# Compatibility Matrix and Rollout

Legend:
- `Now`: in v0/v1 target scope
- `Next`: planned soon after stable Linux integration
- `Later`: deferred, architecture-compatible but not immediate

| Host/DAW | CLAP (Linux) | VST3 (Linux) | LV2 (Linux) | AU (macOS) | AAX (macOS/Win) | Status |
|---|---|---|---|---|---|---|
| Piano Player (embed via C ABI) | N/A | N/A | N/A | N/A | N/A | Now |
| REAPER | Next | Next | Next | Next | Later | Next |
| Bitwig Studio | Next | Next | N/A | N/A | N/A | Next |
| Ardour | N/A | N/A | Next | N/A | N/A | Next |
| Carla | Next | Next | Next | N/A | N/A | Next |
| LMMS | Next | Next | Next | N/A | N/A | Next |
| FL Studio | Later | Next (Win path) | N/A | N/A | N/A | Later |
| Ableton Live | Later | Next | N/A | Next | N/A | Later |
| Cubase | Later | Next | N/A | Next | N/A | Later |
| Studio One | Later | Next | N/A | Next | N/A | Later |
| Logic Pro | N/A | N/A | N/A | Later | N/A | Later |
| Pro Tools | N/A | N/A | N/A | N/A | Later | Later |

## Rollout Tiers
1. Tier 0 (Now): Embedded C ABI for Piano Player on Arch Linux.
2. Tier 1 (Next): CLAP/VST3/LV2 wrappers on Linux hosts.
3. Tier 2 (Later): macOS AU and expanded VST3 host coverage.
4. Tier 3 (Later): AAX feasibility and SDK/legal process.
