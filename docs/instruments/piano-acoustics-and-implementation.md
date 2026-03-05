# Piano Acoustics, Mechanical Origins, and VB-Engine Replication Plan

## Scope
This document captures:
- how acoustic pianos create their characteristic sound,
- why piano timbre changes with touch, register, and pedal state,
- which mechanisms are audibly important,
- how VB-Engine replicates each property now,
- what remains for later realism phases.

This is the baseline reference before and during piano implementation work.

## Update (2026-02-15)
- The engine now includes a primary sampled piano path (SFZ/WAV) with modular sample player, resonance matrix, and DSP post-processor.
- The former additive model remains available as fallback when no sample library is configured.
- Per current user requirement, explicit hammer-thud and pedal-squeak layers are intentionally omitted in this pass.

## Tuning Reference
VB-Engine uses standard concert pitch tuning:
- `A4 = 440 Hz` (equal-tempered semitone spacing as the base map).

Reference: ISO 16 defines standard tuning pitch with A above middle C at 440 Hz [R1].

## 1) Mechanical Chain: Key -> Action -> Hammer -> String -> Bridge -> Soundboard -> Air

### Physical origin
In a grand piano, pressing a key drives a high-leverage action with escapement. The hammer is accelerated, released in free flight, strikes string(s), rebounds, and a damper mechanism controls decay unless sustain pedal is engaged. The escapement mechanism lets the hammer strike and return without blocking string vibration and supports fast repetition.

### Audible consequence
- Distinct attack transient from hammer-string interaction.
- Touch-dependent onset timing and brightness.
- Mechanical release character (damper return and key-off artifacts).

### VB-Engine replication (current)
- Velocity-dependent attack stage (faster rise for higher velocity).
- Dedicated attack/release noise bursts for hammer/key-off texture.
- Sustain pedal behavior via damper-lift simulation in the voice pool (CC64 gating release).

### VB-Engine replication (next)
- Separate hammer noise component tied to strike velocity and note range.
- Explicit keybed and pedal mechanical noise layers (controllable mix).
- Repetition-specific behavior for fast retrigger intervals.

## 2) Hammer Felt Nonlinearity and Touch-Dependent Spectral Brightness

### Physical origin
Hammer felt behaves nonlinearly. Harder strikes increase effective contact stiffness and shorten contact time, which generally excites stronger high-frequency partial energy.

### Audible consequence
- Soft touch (`pp`) sounds rounder/darker.
- Hard touch (`ff`) sounds brighter and more percussive.
- Timbre varies continuously with velocity, not just loudness.

### VB-Engine replication (current)
- Velocity controls spectral rolloff across partial amplitudes.
- Higher velocities flatten harmonic rolloff (brighter spectrum).
- Velocity also changes attack time and output gain.

### VB-Engine replication (next)
- Replace static rolloff law with calibrated per-key, per-velocity tables.
- Add hammer-contact-time-informed brightness curve fit from sample analysis.

## 3) String Layout by Register: 1/2/3 Strings Per Note + Intentional Mistuning

### Physical origin
Pianos use different string counts by register: low notes often one string, middle typically two, upper-mid/high commonly three. Unison strings are intentionally detuned slightly to enrich tone and increase apparent sustain through beating/coupled behavior.

### Audible consequence
- Bass has fundamental-heavy clarity.
- Middle/high registers have chorus-like width and shimmer.
- Slow beats and "living" sustain in unisons.

### VB-Engine replication (current)
- Register-dependent string count in the piano voice:
  - lower register: 1 string,
  - transition register: 2 strings,
  - upper register: 3 strings.
- Per-string detune offsets (in cents) for unison spread.

### VB-Engine replication (next)
- Detune tables by key and dynamic level.
- Coupled-string beating model with bridge-mediated energy exchange.

## 4) String Stiffness and Inharmonicity

### Physical origin
Real piano strings are stiff, so overtone frequencies deviate from perfect integer multiples. Higher partials are stretched upward by an inharmonicity term that depends on string construction and speaking length.

### Audible consequence
- Piano partials are not perfectly harmonic.
- Perceived brightness and pitch fusion vary by register.
- Contributes to stretched tuning practice on real instruments.

### VB-Engine replication (current)
- Per-note inharmonicity coefficient applied to each partial:
  - modal frequency uses stiff-string scaling `f_n = n*f0*sqrt(1 + B*n^2)`.
- `B` increases with note number to approximate stronger treble inharmonicity.

### VB-Engine replication (next)
- Per-key measured inharmonicity map.
- Compatibility mode for stretched tuning curves beyond strict 12-TET mapping.

## 5) Partial-Dependent Decay and Double-Slope Envelope Behavior

### Physical origin
Piano note decay is not a single exponential. Different modes decay at different rates; high partials often decay faster. Coupled strings and structural energy paths can produce two-stage decay behavior.

### Audible consequence
- Attack brightness fades into warmer sustain.
- Some notes exhibit complex "bloom" and two-stage loudness slope.

### VB-Engine replication (current)
- Independent per-partial decay coefficients.
- Decay time shortens for higher partial index.
- Register-dependent base decay time.

### VB-Engine replication (next)
- Two-stage per-mode envelopes calibrated from recordings.
- Pedal-state-specific decay law sets.

## 6) Dampers and Sustain Pedal (CC64)

### Physical origin
With pedal up, dampers mute strings once keys are released. With sustain pedal down, dampers are lifted and strings continue ringing. This also allows passive excitation of other strings by energy transfer through bridge/soundboard.

### Audible consequence
- Long sustain and harmonic cloud under pedal.
- Richer ambience and sympathetic ringing.
- Clear contrast between dry (pedal-up) and open (pedal-down) articulation.

### VB-Engine replication (current)
- Note-off while sustain is down defers damper release until pedal-up.
- Pedal-down state drives a sympathetic resonance bus (12 pitch classes) that is excited by played notes and lightly by pedal engagement.
- Pedal-up transition triggers deferred releases.

### VB-Engine replication (next)
- Resonance driven by active note spectrum, not just pitch-class bank.
- Half-pedal and repedaling detail with continuous damping depth.
- Damper noise component for pedal transitions.

## 7) Sympathetic and Duplex/Longitudinal Resonances

### Physical origin
Beyond struck-string modes, pianos radiate additional components: sympathetic resonances, duplex scale effects, and inharmonic/longitudinal interactions that can create non-harmonic or "phantom" partials.

### Audible consequence
- Increased complexity and realism in sustained passages.
- Subtle "halo" above the direct tone.
- Context-sensitive color shifts under pedal and dense harmony.

### VB-Engine replication (current)
- Sympathetic resonance approximation through pedal-dependent pitch-class resonator bank.

### VB-Engine replication (next)
- Add duplex/aliquot layer and non-harmonic sidebands.
- Couple resonance amplitude to actual spectral energy per active key.

## 8) Release/Key-Off Behavior

### Physical origin
When a key is released, damper-string interaction creates a brief transient and modified decay path. This is especially audible in staccato passages and repeated notes.

### Audible consequence
- Small but important realism cue in dry close-mic or headphone listening.
- Articulation identity between detached and connected phrasing.

### VB-Engine replication (current)
- Key-off transient noise envelope triggered on release.
- Faster decay multiplier applied during release phase.

### VB-Engine replication (next)
- Frequency-shaped key-off noise by register.
- Velocity-dependent release transient behavior.

## 9) Why Piano Sound Is Hard to Fake
Key realism is not one feature; it is interaction among many:
- velocity -> hammer contact -> spectral tilt,
- string count/detune -> beating,
- stiffness -> inharmonicity,
- pedal state -> damping and sympathetic fields,
- release mechanics -> articulation transients,
- register-dependent behavior across 88 keys.

A credible engine must make these interactions coherent over time.

## 10) How Other Engines Reach Acoustic Realism Without Raw Samples

This task explicitly asked whether near-acoustic realism is possible without a sample library. The answer is yes, and public references show it is a mature approach:
- Some commercial instruments explicitly advertise full physical modeling rather than recorded sample playback [R10].
- Long-running research in digital waveguides, commuted synthesis, and stiff-string modeling describes how to synthesize convincing struck-string instruments in realtime [R11].
- Production engines often combine multiple mechanisms (string model + hammer interaction + resonance/body layers) instead of relying on one oscillator bank.

Important implementation policy for VB-Engine:
- We do **not** copy proprietary code or private parameter tables.
- We use public acoustic literature and public product-level descriptions as design inspiration.
- We implement a clean-room model that follows known physical principles and is tuned by our own tests/listening.

## 11) VB-Engine Clean-Room Replication Strategy (Current Pass)

This realism pass maps the above research to concrete synthesis behavior:
- **Strike-position spectral imprint**:
  - each partial is weighted by a hammer-strike-position comb term (`sin(pi * n * x_strike)` style weighting), which suppresses/boosts different overtones like a real strike location.
- **Touch-dependent hammer hardness transient**:
  - velocity drives a short-lived attack-brightness bloom that decays into warmer sustain, rather than static spectral tilt only.
- **Faster high-partial decay law**:
  - upper partials now decay more aggressively so sustain loses edge and avoids static synth-like brightness.
- **Filtered mechanical noise**:
  - hammer and key-off noises are band-shaped (not raw white bursts) to reduce crackle/zipper character and keep articulation cues natural.
- **Body/soundboard coloration stage**:
  - voice output is blended with lightweight body-shaped components before final DC/rumble control.
- **Retrigger and voice management cleanup**:
  - same-note retriggers restrike existing note slots to avoid uncontrolled stacking and reduce electronic smear under dense passages.
- **Master transient safety + headroom**:
  - output uses smooth saturation and DC blocking to keep transients clean while avoiding clipping.

## VB-Engine Current Piano v1 Summary (Implemented in this phase)
- Standard tuning anchor: `A4 = 440 Hz`.
- Register-aware string multiplicity (1/2/3 strings).
- Unison detune offsets for width and beating.
- Inharmonic partial frequency model per note.
- Velocity-dependent spectral excitation and attack profile.
- Per-partial decay shaping for attack-to-sustain evolution.
- Sustain pedal damping logic and deferred release.
- Pedal-coupled sympathetic resonance approximation.
- Attack/key-off transient components.
- Low-end control via per-note gain shaping and per-voice high-pass filtering.
- Strike-position comb weighting for realistic overtone excitation.
- Time-varying brightness bloom and decay tied to velocity (hammer hardness proxy).
- Band-shaped mechanical transient noise and body coloration stage.
- Slot-aware retrigger behavior and quieter voice-steal policy for smoother dense playback.

## Validation Strategy for This Model
- Objective checks:
  - spectral centroid increase with velocity,
  - longer RT-like tail with sustain pedal down,
  - measurable inharmonic partial offsets vs ideal harmonic grid,
  - faster release slope after key-off than held sustain.
- Subjective checks:
  - A/B listening against close-mic grand references,
  - headphone checks for key-off cues and pedal cloud,
  - phrase-level realism using legato/staccato/pedal showcases.

## Sample Suite Requirement (Public Repo)
All piano implementation changes must regenerate and update `Samples/`.
Current showcase set:
- `Piano-Fur-Elise-Excerpt.wav`
- `Piano-Moonlight-Sonata-Excerpt.wav`
- `Piano-Turkish-March-Excerpt.wav`
- `Piano-Clair-de-Lune-Excerpt.wav`
- `Piano-Chopin-Ballade-No1-Excerpt.wav`
- `Piano-Creep-Transposed-Excerpt.wav`
- `Piano-Bach-Prelude-C-Excerpt.wav`

Generation command:
```bash
bash scripts/generate-samples.sh
```

## References
- [R1] ISO 16:1975 (standard tuning frequency, A = 440 Hz): https://www.iso.org/standard/3601.html
- [R2] Goebl et al., key touch and action effects (JASA abstract): https://pubmed.ncbi.nlm.nih.gov/16583901/
- [R3] Simulated piano hammer felt force law (Acta Acustica, 2024): https://acta-acustica.edpsciences.org/articles/aacus/full_html/2024/01/aacus230091/aacus230091.html
- [R4] Conklin, inharmonicity and coupling in piano strings (JASA abstract): https://pubmed.ncbi.nlm.nih.gov/20136287/
- [R5] Coupled-string/double-decay modeling (InterNoise 2022): https://doi.org/10.3397/IN_2022_0561
- [R6] Piano spectral inharmonicity formula context (Acoustical Science and Technology): https://www.jstage.jst.go.jp/article/ast1966/23/2/23_2_137/_article
- [R7] Longitudinal/phantom partials and non-harmonic components (JASA): https://pmc.ncbi.nlm.nih.gov/articles/PMC3155581/
- [R8] Sympathetic vibration study in piano (TU Eindhoven conference contribution): https://research.tue.nl/en/publications/sympathetic-vibration-in-a-piano
- [R9] Hammer-shank and string dynamics model (Acoustical Science and Technology, 2023): https://doi.org/10.1250/ast.44.230
- [R10] Modartt Pianoteq (product page; physical-modeling instrument position): https://www.modartt.com/pianoteq
- [R11] Smith, *Physical Audio Signal Processing* (commuted piano / digital waveguide context): https://www.dsprelated.com/freebooks/pasp/Commuted_Piano_Synthesis.html
- [R12] Casio AiR Grand Sound Source (example of production resonance/nuance feature stack): https://support.casio.com/en/support/answer.php?cid=008001004001&num=5&qid=232782
