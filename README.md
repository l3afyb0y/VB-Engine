# VB-Engine

Vibe Based Engine (VBE) is a sound engine made from the ground up, written in C++(20). It is designed to take MIDI inputs and output high-quality audio as a variety of instruments. (Only Acoustic Grand Piano is currently implemented, but more instruments are planned).

This program was made with the intention of implementation within my other project found here: https://github.com/l3afyb0y/Piano-Midi-Player

I have decided to release this engine to the public for use and modifications from others so that, if they find the engine's outputs to be of a higher quality than those they have access to, or find online, they may use them indiscriminately. This means the program was both designed to be built into my "MIDI player", but also attempts to be a plugin of sorts for other applications.

If you'd like compatibility with another program, please let us know in the discussions page and we can try to work on it if compaibility is possible. If there is an instrument you'd like to see in this program, please reach out there as well.

This project is a WIP until v1 release, when it will offically be considered stable and, in some sense, complete; however this doesn't mean it won't be subject to changes in the future.

Please enjoy this engine and it's features/ability. The point of this being FOSS is to help allow anyone to get into music, as I am a firm believer music (and art), and software should always be free and accessible. To that end, in order to make that possible, we must all contribute to it's accesibility. This is my attempt at a contribution to such.

Primary target is Arch Linux, with generic Linux installation support and architecture boundaries prepared for macOS/Windows wrappers.


## Build
```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```


## Generate Instrument Samples
Prepare a redistributable sampled library (recommended):
```bash
bash scripts/fetch-piano-assets.sh salamander
```

```bash
bash scripts/generate-samples.sh
```

Generated public showcase audio is stored in `Samples/`.

Override the library path:
```bash
VB_PIANO_SFZ_PATH=/absolute/path/to/library.sfz bash scripts/generate-samples.sh
```

Notes:
- The core engine now requires an explicit `piano_sfz_path` if you want sampled playback from host code.
- The sample generator and live tester still fall back to `Samples/Piano-Library/default.sfz` when present.
- Current loader supports SFZ regions referencing WAV/AIFF/FLAC samples.
- Demo showcase generation now includes slight deterministic micro-timing/velocity humanization to reduce rigid quantized feel.


## Live Piano Tester
Build and run the standalone click-to-play tester:
```bash
cmake --preset release
cmake --build --preset release --target vb_engine_piano_tester
./build/release/vb_engine_piano_tester
```

Quick launcher:
```bash
scripts/run-piano-tester.sh
```

Features:
- key range `A2-C5`
- pedal indicator/control in top bar (`gray` off, `green` on)
- ALSA MIDI input selection via top-bar dropdown (`PORT n`)
- async startup (window opens immediately while engine/sample library initializes)

Desktop entry (for local launcher integration):
- `Samples/VB-Engine-Piano-Tester.desktop`

Realtime defaults for stability:
- sample rate: `48000`
- audio buffer: `512`
- max voices: `96`

Optional overrides:
```bash
VB_TESTER_SAMPLE_RATE=96000 VB_TESTER_BUFFER=1024 VB_TESTER_MAX_VOICES=80 scripts/run-piano-tester.sh
```

WAV health gate (peak/clipping/DC/jump checks):
```bash
bash scripts/check-wav-health.sh
```

Capture a new comparison baseline:
```bash
bash scripts/capture-audio-baseline.sh /tmp/vb_engine_baseline_samples
```

Audio-diff quality gate against baseline:
```bash
./scripts/check-audio-quality.sh /tmp/vb_engine_baseline_samples
```

Run full verification (tests + sanitizers + perf + sample health):
```bash
bash scripts/run-full-verification.sh
```

## Install
### Arch Linux (one-and-done)
```bash
bash scripts/install-arch.sh
```

### Generic Linux (one-and-done)
```bash
bash scripts/install-linux.sh
```

## Packaging
- `PKGBUILD`: repository-source install package

## Documentation
- Docs index: `docs/README.md`
- Architecture: `docs/architecture.md`
- App integration guide: `docs/developer-app-integration.md`
- Licensing/compliance guide: `docs/licensing-compliance.md`
- Current known limits and improvement targets: `docs/limitations.md`
- Compatibility matrix: `docs/compatibility-matrix.md`
- Implementation plan: `docs/phased-implementation-plan.md`
- Piano Player adapter contract: `docs/piano-player-adapter-spec.md`
- Piano acoustics + replication doc: `docs/instruments/piano-acoustics-and-implementation.md`
- Sampled piano implementation doc: `docs/instruments/piano-sampled-engine-implementation.md`
- Instrument implementation standard: `docs/instrument-implementation-standard.md`
- Packaging/install behavior: `docs/packaging-install.md`
- Risk/tradeoff analysis: `docs/risks-tradeoffs.md`
- Piano CPU optimization report: `docs/performance/2026-02-15-piano-cpu-optimization.md`
- Piano voicing/sample refactor report: `docs/performance/2026-02-15-piano-voicing-sample-refactor.md`
- Sampled realism pass report: `docs/performance/2026-02-15-piano-realism-pass-2.md`
- Sampled piano refactor verification: `docs/performance/2026-02-15-sampled-piano-refactor.md`
- Sampled refactor specification: `docs/plans/2026-02-15-sampled-piano-refactor-spec.md`
- Realism iteration 2 spec: `docs/plans/2026-02-16-piano-realism-iteration-2-spec.md`
- Realism iteration 2 verification: `docs/performance/2026-02-16-piano-realism-iteration-2.md`
- Realism iteration 3 high-impact pass: `docs/performance/2026-02-16-piano-realism-iteration-3.md`
- Realism iteration 4 bedroom-acoustic retune: `docs/performance/2026-02-16-piano-realism-iteration-4-bedroom.md`
- Realism iteration 5 clarity/decay pass: `docs/performance/2026-02-16-piano-realism-iteration-5-clarity-decay.md`
- Realism iteration 6 realtime stability pass: `docs/performance/2026-02-16-piano-realism-iteration-6-realtime-stability.md`
- Realism iteration 7 low-register polyphony pass: `docs/performance/2026-02-16-piano-realism-iteration-7-low-register-polyphony.md`
- Realism iteration 8 polyphony headroom pass: `docs/performance/2026-02-16-piano-realism-iteration-8-polyphony-headroom.md`
- Realism iteration 9 waveform forensics pass: `docs/performance/2026-02-16-piano-realism-iteration-9-waveform-forensics.md`
- Realism iteration 10 realtime de-crackle pass: `docs/performance/2026-02-16-piano-realism-iteration-10-realtime-declick.md`
- Realtime crackle follow-up pass: `docs/performance/2026-02-17-piano-realtime-crackle-pass.md`
- Realism iteration 11 physical-model pass: `docs/performance/2026-02-17-piano-realism-iteration-11-physical-model-pass.md`

## Status
This is an iterative foundation intended for rapid integration into Piano Player first, then expansion toward plugin wrappers (CLAP/VST3/LV2/AU/AAX by tier).
