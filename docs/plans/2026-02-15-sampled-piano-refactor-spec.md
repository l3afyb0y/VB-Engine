# Sampled Piano Refactor Spec (2026-02-15)

## Scope
This document is the required research/specification phase before implementation.
It defines how VB-Engine moves from oscillator-style piano to a high-fidelity sampled instrument with physical resonance behavior and clean realtime performance.

User constraints applied:
- Target realistic acoustic piano behavior.
- Omit modeled/triggered hammer and pedal mechanical noises.
- No static/pops/glitches.
- Keep architecture modular: `Sample Player`, `Resonance Calculator`, `DSP Post-Processor`.
- Keep A4 = 440 Hz base while supporting stretch tuning.

## 1) Architecture Analysis (SFZ / FluidSynth / HISE / LinuxSampler)

### SFZ ecosystem (format + playback engines)
- SFZ defines key/velocity/CC-region mapping through text opcodes (`lokey`, `hikey`, `lovel`, `hivel`, `sample`, etc.) and can encode pedal-conditioned regions (`locc64` / `hicc64`) [A1][A2].
- Voice-lifecycle opcodes such as `group`, `off_by`, `off_mode`, `trigger`, and envelope opcodes are standard mechanisms for release control and note replacement [A3].

Practical implication for VB-Engine:
- Adopt SFZ-compatible region semantics (at least subset) for note, velocity, and release mapping.
- Use region groups/off-by semantics to avoid duplicate tails and reduce CPU spikes.

### FluidSynth voice overflow and realtime behavior
- FluidSynth exposes explicit overflow scoring (`synth.overflow.age`, `.released`, `.sustained`, `.volume`, `.important`) and polyphony limits (`synth.polyphony`) for deterministic voice stealing [A4].
- Dynamic sample loading exists but is explicitly marked non-realtime-safe due to allocation [A4].

Practical implication for VB-Engine:
- Voice stealing should be score-based, not round-robin.
- Runtime audio thread must avoid allocation and I/O; sample loading/prep must happen off audio thread.

### HISE streaming sampler architecture
- HISE `StreamingSampler` is disk-streaming, uses explicit preload size, per-voice streaming buffer sizing, voice count limits, and round-robin dimensions [A5].
- HISE docs call out realtime latency-vs-CPU tradeoffs and background-thread precompute behavior for heavy operations [A5].

Practical implication for VB-Engine:
- Add preload cache + per-voice read buffers.
- Separate realtime render from background streaming/decode.
- Keep explicit, tunable parameters (`preload`, `voice amount`, `buffer size`).

### LinuxSampler architecture
- LinuxSampler is explicitly described as modular and streaming-capable, with backend/server separation and support for SFZ [A6].

Practical implication for VB-Engine:
- Preserve clean backend architecture (engine core independent from UI/host).
- Implement sampler core with clear module boundaries and command/event control.

## 2) Acoustic Profiling: Concert Grand Behavior

### Inharmonicity model
The standard stiff-string relation is:

`f_n = n * f_0 * sqrt(1 + B * n^2)`

where `B` is the per-string inharmonicity coefficient [A7][A8].

Implementation consequence:
- For each key, estimate/stored `B[key]`.
- Use `B[key]` to drive stretch tuning and resonance partial alignment.

### Stretch tuning
- Real pianos are tuned to stretched octaves (Railsback behavior), not strict equal temperament, due to inharmonic overtones [A9][A10].

Implementation consequence:
- Keep equal-temperament base at A4=440, then apply per-key cent offsets:
  - `f_eq(key) = 440 * 2^((key-69)/12)`
  - `f_tuned(key) = f_eq(key) * 2^(stretch_cents[key]/1200)`
- `stretch_cents[key]` derived from calibrated curve (initially smooth low/high expansion; later data-driven).

### Tonal decay and spectral evolution
- Piano soundboard response is largely linear in normal playing range, with modal/radiation behavior changing by frequency range [A11][A12].

Implementation consequence:
- Use sample-native decay with exponential release shaping.
- Keep resonance/body filters linear and stable; avoid aggressive nonlinear coloration that causes synthetic artifacts.

## 3) Sample Asset Sourcing (free + redistributable)

### Verified license candidates
- Salamander Grand Piano v3: CC BY 3.0, 16 velocity layers, 48k/24-bit [A13].
- Accurate-Salamander Project: CC-BY listing [A14].
- Splendid Grand Piano: Public Domain listing [A14].
- VCSL Keys: CC0-1.0 listing [A14].
- University of Iowa MIS (includes piano content): explicitly "may be downloaded and used for any projects, without restrictions" [A15].

### License-risk candidate (not yet verified for redistribution terms)
- Ivy Audio Piano in 162 appears widely mirrored, but authoritative current license text was not recoverable from the primary host during this pass; treat as "license-unverified" until explicit upstream terms are captured [A14].

Policy for this refactor:
- Integrate loaders/mappers for redistributable libraries with confirmed terms first.
- Keep Ivy support as optional user-supplied path until authoritative license text is verified.

## 4) Technical Implementation Strategy

### Interpolation strategy
- Baseline realtime interpolation: 4-point Hermite.
  - Lower CPU than high-order sinc, smoother than linear.
- Optional high-quality mode: bandlimited/sinc resampler (offline render or high-latency mode).
  - Bandlimited methods yield best pitch-shift quality but increase latency/CPU [A16][A17].

Decision:
- Realtime default = Hermite.
- Future quality mode = sinc (non-realtime or larger buffer profiles).

### Sympathetic Resonance Matrix
Design a shared virtual soundboard state `E` over 88 keys:

- `E_i[t+1] = d_i * E_i[t] + sum_j (C_ij * A_j[t])`
- `A_j[t]` = instantaneous excitation from active voiced notes (and pedal-up/down rules).
- `C_ij` = coupling term based on harmonic proximity and interval affinity.

Initial coupling heuristic:
- Strongest for unison/octave/fifth and harmonic-near pairs.
- Pedal-up: heavy damping; pedal-down: lower damping and broader coupling.

Output:
- Resonance bus rendered from `E` through band-limited resonator bank and mixed post-voice.

### Zero-latency convolution strategy
- Use partitioned convolution architecture with direct head partition in audio thread and deferred tail partitions in background workers.
- "Zero-latency" here means no added algorithmic startup delay beyond host buffer scheduling.
- For CPU safety, allow fixed-latency mode as optional fallback (same pattern used in production frameworks) [A18][A19].

## 5) Refactor Blueprint (foundational, modular)

### Module decomposition
1. `piano/sample_player/*`
- sample cache, per-voice playback cursor, interpolation, loop/release handling.

2. `piano/resonance/*`
- resonance matrix state update, pedal-conditioned damping/coupling, audio render.

3. `piano/dsp/*`
- post-chain: anti-click envelope smoothing, optional convolution reverb, output safety.

4. `piano/library/*`
- SFZ subset parser + region index by note/velocity/trigger.

5. `core/voice_pool.*` integration
- score-based voice allocator with deterministic steal policy and sample voice lifecycle.

### Feature-level requirements mapping
- Velocity layering: strict `lovel/hivel` region selection + optional crossfade blend.
- Note mapping: `lokey/hikey/pitch_keycenter` with Hermite pitch-ratio playback.
- Mechanical noise: intentionally omitted per user request.
- Envelope: exponential attack/release shaping over sample amplitude.
- Humanization: bounded per-note velocity and start-time jitter, deterministic seed path for tests.
- Convolution reverb: partitioned engine skeleton + short IR default profile.

### Non-glitch policy
- No allocations in audio thread.
- No disk reads in audio thread.
- Smoothed gain transitions on voice start/steal/release.
- Denormal-safe processing and bounded limiter/post headroom.

## 6) Validation Plan
- Unit tests:
  - sample-region selection correctness,
  - interpolation continuity bounds,
  - voice-steal click bounds,
  - pedal resonance behavior.
- Soak tests:
  - long random MIDI streams,
  - memory stability and queue backpressure.
- Audio gates:
  - no clipping,
  - jump-threshold checks,
  - DC bounds,
  - baseline comparison.

## References
- [A1] SFZ basics (regions, key/velocity/CC conditions): https://sfzformat.com/tutorials/basics/
- [A2] SFZ region header reference: https://sfzformat.com/headers/region/
- [A3] LinuxSampler SFZ opcode support (`off_by`, `off_mode`, etc.): https://linuxsampler.org/sfz/
- [A4] FluidSynth synthesizer settings (polyphony, overflow scoring, dynamic sample loading): https://www.fluidsynth.org/api/settings_synth.html
- [A5] HISE StreamingSampler docs (disk streaming, preload/buffer/voice controls): https://docs.hise.audio/hise-modules/sound-generators/list/streamingsampler.html
- [A6] LinuxSampler man page (modular, streaming-capable sampler backend): https://man.archlinux.org/man/linuxsampler.1.en
- [A7] Fletcher/Conklin-style stiff-string inharmonic relation context: https://pubmed.ncbi.nlm.nih.gov/20136287/
- [A8] Stiff-string formula discussion (B coefficient context): https://physicstoday.aip.org/features/stiff-string-theory-richard-feynman-on-piano-tuning
- [A9] Piano tuning and Railsback stretch context: https://en.wikipedia.org/wiki/Piano_tuning
- [A10] Stretched tuning overview: https://en.wikipedia.org/wiki/Stretched_tuning
- [A11] Piano soundboard modal/vibroacoustic behavior (low-mid frequency): https://arxiv.org/abs/1212.2323
- [A12] Piano soundboard modal density and damping study: https://arxiv.org/abs/1212.3068
- [A13] Salamander Grand Piano v3 (CC BY 3.0, velocity-layer details): https://github.com/sfzinstruments/SalamanderGrandPiano
- [A14] SFZ instruments piano index (license/cost overview, includes Piano in 162 listing): https://sfzinstruments.github.io/pianos/
- [A15] University of Iowa MIS usage terms ("without restrictions"): https://theremin.music.uiowa.edu/MIS.html
- [A16] libsoxr design notes (bandlimited interpolation + latency tradeoff): https://github.com/chirlu/soxr
- [A17] libsamplerate converter mode taxonomy (sinc vs linear/zoh via API bindings): https://github.com/aolsenjazz/libsamplerate-js
- [A18] JUCE convolution class reference (zero-latency/fixed-latency and non-uniform partitioning APIs): https://docs.juce.com/master/classjuce_1_1dsp_1_1Convolution.html
- [A19] Gardner, "Efficient convolution without input-output delay" citation index: https://www.bibsonomy.org/bibtex/2e335d09ff0c8d2c638a588f245fd17dd/bovansnow
