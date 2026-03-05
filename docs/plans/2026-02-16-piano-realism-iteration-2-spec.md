# Piano Realism Iteration 2 Spec (2026-02-16)

## Objective
Push VB-Engine from "good sampled instrument" toward "high-fidelity digital piano" by reducing remaining synthetic cues:
- pitch-shift artifacts,
- static envelope behavior,
- simplified resonance,
- thin body/room response,
- and sample-format friction (notably FLAC-backed SFZ libraries).

## Research: What strong engines are doing

### 1) Sampler architecture patterns
- LinuxSampler describes a modular backend with disk-streamed playback designed for large libraries [R1].
- FluidSynth exposes explicit polyphony and overflow scoring controls instead of naive voice replacement [R2].
- HISE's StreamingSampler is built around preload + stream buffers + explicit voice limits [R3].

Takeaway for VB-Engine:
- keep deterministic voice policy,
- keep sample I/O outside realtime rendering,
- use explicit quality/latency controls rather than hidden behavior.

### 2) Interpolation and anti-artifact tradeoffs
- FluidSynth documents multiple interpolation levels and notes that aggressive interpolation can introduce ringing in problematic regions [R4].

Takeaway:
- default to a robust interpolation choice (Hermite here) and avoid "max-order by default" tuning.

### 3) Convolution behavior in production engines
- JUCE's convolution API explicitly distinguishes zero-latency and fixed-latency modes and references non-uniform partitioning [R5].
- HISE documentation similarly calls out CPU/latency tradeoffs and background preparation for convolution [R6].

Takeaway:
- realism must be constrained by stable realtime behavior. "Best quality" settings are only useful if they do not destabilize playback.

### 4) Resonance and inharmonicity grounding
- Tunelab's inharmonicity notes provide stiff-string relation context and practical B-value ranges seen in pianos [R7].
- Aalto work on physical piano modeling describes sympathetic resonance and waveguide-based realism factors [R8].
- Concert-grand scale reference point: Steinway Model D length (8' 11 3/4") [R9].

Takeaway:
- realistic piano behavior requires inharmonic partial placement and pedal-dependent coupled resonance, not just a single sinusoidal ring.

### 5) SFZ realism semantics
- SFZ references cover trigger/mapping and region behavior [R10][R11].
- SFZ sympathetic-resonance examples show practical pedal-conditioned region logic [R12].

Takeaway:
- realism comes from articulation metadata as much as raw sample quality.

### 6) Source-library licensing and format pragmatics
- Salamander is CC-BY licensed and commonly distributed as SFZ+FLAC [R13].
- Practical FLAC decode integration can be done via a permissive decoder such as dr_flac [R14].

Takeaway:
- without FLAC support, many high-quality free libraries remain awkward to use.

## What high-end engines intentionally do not do
- They avoid forcing maximum oversampling/interpolation in all contexts because stability and latency regressions can outweigh sonic gains (also seen in sfizz release notes where unstable oversampling behavior was disabled) [R15].
- They avoid realtime allocations and blocking I/O in the render path even when this complicates architecture [R2][R3].

## Potentially under-explored opportunities
The following are candidate directions for future VB-Engine research beyond this iteration:
1. Dual-domain resonance state:
Model bridge-drive and air-cavity energy separately, then couple them dynamically.
2. Pedal continuum model:
Treat CC64 as continuous damper aperture (not binary), affecting damping and coupling smoothly.
3. Phase-coherent voice stealing:
When stealing/retriggering, preserve partial-phase continuity in resonance bus to hide transitions.
4. Context-aware body/room morph:
Adapt IR blend and tonal contour from instantaneous register density and sustain state.

## Iteration 2 implementation strategy
1. Add FLAC decode path in sample loading.
2. Expand SFZ articulation support for realism-critical opcodes:
   - `offset`, `end`, `tune`, `ampeg_release`, `loop_mode`, `loop_start`, `loop_end`.
3. Improve sampled voice behavior:
   - loop crossfade,
   - release-time aware envelope,
   - click-safe boundary fades.
4. Improve resonance:
   - multi-partial inharmonic rendering,
   - pedal/frequency-shaped damping,
   - simple soundboard coloration.
5. Improve post-DSP:
   - body IR + room IR chain,
   - tonal contour smoothing,
   - keep zero added algorithmic latency.

## Success criteria for this iteration
- FLAC-backed SFZ loads and renders.
- Existing tests + sanitizers + soak pass.
- No new clipping/DC/pop regressions in wav-health gate.
- Measurable reduction in jump discontinuities relative to prior pass.

## References
- [R1] LinuxSampler about: http://www.linuxsampler.org/about.html
- [R2] FluidSynth synth settings (polyphony/overflow): https://www.fluidsynth.org/api/settings_synth.html
- [R3] HISE StreamingSampler docs: https://docs.hise.audio/hise-modules/sound-generators/list/streamingsampler.html
- [R4] FluidSynth audio/interpolation settings: https://www.fluidsynth.org/api/settings_audio.html
- [R5] JUCE convolution docs: https://docs.juce.com/master/classjuce_1_1dsp_1_1Convolution.html
- [R6] HISE convolution docs: https://docs.hise.audio/hise-modules/effects/list/convolution.html
- [R7] Tunelab inharmonicity notes: https://www.tunelab-world.com/inharmonicity.html
- [R8] Valimaki dissertation summary (physics-based synthesis of inharmonic piano tones): https://aaltodoc.aalto.fi/items/87984f1d-1fe2-41e0-a900-0b429af11f63
- [R9] Steinway Model D spec: https://www.steinway.com/pianos/steinway/grand/model-d
- [R10] SFZ region header: https://sfzformat.com/headers/region/
- [R11] SFZ `trigger` opcode: https://sfzformat.com/opcodes/trigger/
- [R12] SFZ sympathetic resonance tutorial: https://sfzformat.com/tutorials/sympathetic/
- [R13] Salamander Grand Piano repository/license context: https://github.com/sfzinstruments/SalamanderGrandPiano
- [R14] dr_flac (dr_libs): https://github.com/mackron/dr_libs/blob/master/dr_flac.h
- [R15] sfizz v1.2.0 release notes (oversampling stability notes): https://github.com/sfztools/sfizz/releases/tag/v1.2.0
