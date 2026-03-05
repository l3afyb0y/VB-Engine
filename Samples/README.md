# VB-Engine Instrument Samples

This directory is public and versioned. Keep it current whenever instrument implementations change.

## Piano Song Showcase Set
These are rendered arrangements/excerpts for tonal and articulation evaluation of the engine (not full canonical performances).

- `Piano-Fur-Elise-Excerpt.wav`
- `Piano-Moonlight-Sonata-Excerpt.wav` (1st movement opening-focused)
- `Piano-Turkish-March-Excerpt.wav`
- `Piano-Clair-de-Lune-Excerpt.wav`
- `Piano-Chopin-Ballade-No1-Excerpt.wav`
- `Piano-Creep-Transposed-Excerpt.wav`
- `Piano-Bach-Prelude-C-Excerpt.wav`

## Why this set
- Dynamic control and touch response: `Fur Elise`, `Chopin Ballade` excerpt
- Sustained pedaled resonance and softness: `Moonlight Sonata`, `Clair de Lune`
- Fast articulation and repeated-note clarity: `Turkish March`
- Modern harmonic texture and chord sustain: `Creep` transposed excerpt
- Transparent arpeggio continuity: `Bach Prelude` excerpt

Regeneration command:
```bash
bash scripts/generate-samples.sh
```

## Live Tester
- `VB-Engine-Piano-Tester.desktop`

This desktop launcher opens a lightweight click-to-play tester for quick live auditioning:
- Key range: `A2` through `C5`
- Pedal toggle: top-bar indicator circle (`gray` off, `green` on)
- MIDI input dropdown in top bar (`PORT n`) to select ALSA input
- Backed by the same engine + SFZ library path used by sample generation
- Fast-launch behavior: GUI appears immediately; engine loading continues in background

Launcher target script:
```bash
scripts/run-piano-tester.sh
```

Tester stability defaults:
- `VB_TESTER_SAMPLE_RATE=48000`
- `VB_TESTER_BUFFER=256`
- `VB_TESTER_MAX_VOICES=192`
- `VB_TESTER_PEDAL_NOISE=0`
- `VB_TESTER_MIC_MIX=0.42`
- `VB_TESTER_REVERB_WET=0.10`
