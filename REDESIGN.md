# VB-Engine Redesign Guide

## Purpose
This document is the loose architectural guide for the next major refactor of VB-Engine's piano engine.

It exists to preserve the design direction across long sessions, context compaction, tuning dead ends, and future rewrites. It is not a rigid spec. It is the durable statement of what we think the engine should become, what we are optimizing for, and which simplifications are acceptable versus misleading.

## Core Thesis
VB-Engine should become a waveguide-centered, impedance-coupled, measurement-calibrated piano model.

That means:

- the string system remains the realtime core
- the instrument is modeled as a coupled mechanical network, not a serial audio effects chain
- body, bridge, soundboard, and sympathetic behavior are part of the instrument topology
- calibration and offline analysis are treated as first-class tooling, not an afterthought
- assistive performer features are allowed, but they must sit above the physical model rather than replacing it

## What We Are Moving Away From
The current engine shape is playable, but it still behaves too much like:

- voices render dry string sound
- a separate resonance layer adds extra tone
- a downstream body stage colors the output
- a downstream ambience stage adds bloom

That structure is convenient, but it tells the wrong physical story. A real piano is not "string sound plus later body effect plus later room effect." Energy flows through the instrument continuously through coupling paths.

The redesign should therefore move away from:

- serial tone-stage thinking
- coarse pitch-class resonance
- body and ambience as primarily downstream sweetening layers
- pedal logic that is too binary to represent damper behavior
- global realism knobs that hide the absence of subsystem structure

## Recent Lessons We Do Not Want To Forget
As the refactor progresses, we should preserve a few specific discoveries so we do not accidentally tune against the wrong target.

### 1. Correct Pitch Math Is Necessary But Not Sufficient
The equal-tempered note-frequency relationship anchored from A4 is already straightforward and should remain correct.

What can still go wrong is:

- low-register voicing
- inharmonicity choices
- stretch behavior
- sympathetic coupling
- soundboard and bridge coloration

If a low chord sounds harmonically wrong while higher chords sound correct, the first suspicion should be voicing and coupling, not immediately "our note-frequency formula is wrong."

### 2. Symbolic Harmony Must Not Masquerade As Physical Resonance
The redesign explicitly rejects chord-aware resonance as the truth model.

Symbolic relationships such as:

- same note
- octave
- fifth
- third

may be useful as candidate hints, debugging aids, or analysis shortcuts, but they must not directly define what resonates in the final engine.

This matters because we already learned that a physically suspicious low-register harmony problem can hide inside a seemingly helpful symbolic resonance shortcut.

### 3. Tests Must Not Defend Stale Acoustics
Behavioral tests are valuable, but they should protect the intended instrument behavior, not preserve artifacts from earlier tuning passes.

In particular, tests should avoid forcing:

- overly clicky hammer noise
- unrealistically abrupt attack shapes
- unrealistically binary note release
- coarse pitch-class resonance behavior

Tests should prefer measuring:

- control influence
- stability
- decay plausibility
- relative energy trends
- absence of runaway or broken states

### 4. The Low Register Needs Special Respect
Bass and lower-mid notes are not just "lower versions of treble notes."

They need more careful treatment of:

- fundamental support
- partial balance
- bridge loading
- body radiation
- chord blending

If the low register sounds dark, tinny, or harmonically confused, that is likely a sign that the reduced model is still missing important mechanical structure rather than merely needing more EQ-like tuning.

### 5. Explicit Physical Parameters Matter
One of the clearest lessons from the reference repos is that we have still been collapsing too many real physical variables into broad voicing curves.

The redesign should move toward explicit note-wise or regime-wise treatment of:

- speaking length
- effective linear density
- tension
- core radius
- winding radius where applicable
- Young's modulus or equivalent stiffness proxy
- string impedance
- bridge and soundboard impedance proxies
- hammer mass and felt hardness regime

If these stay hidden inside a few generic "register_position" curves forever, the engine will hit a realism ceiling long before Pianoteq-like quality.

### 6. Bass Strings Are A Different Regime
Bass strings are not just lower steel strings.

They are effectively a different physical regime, typically with:

- a steel core
- copper winding
- different effective mass per length
- different stiffness behavior
- different partial balance
- different bridge loading

That does not mean we must model every winding detail immediately, but it does mean the redesign should explicitly leave room for bass-string material and geometry differences instead of pretending one generic string model covers the whole keyboard equally well.

### 7. ML Is More Promising As Calibration Than As Core Synthesis
Machine learning may become useful in this project, but the most promising use right now is offline calibration and fitting, not replacing the realtime physical engine.

Promising ML roles:

- fitting per-note inharmonicity or stretch targets
- fitting decay curves
- fitting hammer or bridge parameters from reference renders
- tuning soundboard modal weights

Less promising right now:

- neural black-box audio generation in the main realtime path
- using ML as a substitute for finishing the physical architecture

The current redesign should keep ML firmly in the offline analysis and calibration toolbox.

If we explore ML later, the likely training data shape is audio plus metadata, not an LLM-style chat dataset. A useful dataset would contain aligned note or chord renders with rows describing:

- audio file path
- MIDI note or notes
- velocity
- pedal state or continuous pedal lift
- duration and release timing
- sample rate
- source identity such as VB-Engine, our own recordings, or an FDTD lab render

The strongest near-term ML target is a small DDSP-style controller or optimizer that predicts physical parameter corrections, excitation curves, decay curves, or residual noise. It should not be a black-box waveform generator in the realtime VST path.

### 8. FDTD Is A Lab Tool Before It Is A Runtime Rewrite
The older FDTD warnings should not be treated as permanent law. Modern hardware is much stronger, and Rust can support cache-local, SIMD-friendly, safe numerical code without turning the audio path into unsafe C or C++ soup.

The important distinction is scope:

- one-dimensional string or trichord FDTD is plausible as an offline or optional focused experiment
- damper contact, hammer contact, and bridge coupling are good FDTD research targets
- a full audio-rate 2D or 3D piano-body simulation inside the VST callback is a much bigger and riskier project

The current direction is therefore:

- keep the realtime VST as a controllable hybrid physical model
- add FDTD as an offline physics lab and calibration teacher when needed
- port only the useful reduced behavior back into the realtime engine
- revisit a runtime FDTD backend only after a focused prototype proves it solves a problem the hybrid model cannot solve

The principle is not "FDTD is too expensive forever." The principle is "do not simulate the wrong piano at higher cost." FDTD only helps if the hammer, string, damper, bridge, and soundboard assumptions are already disciplined enough to be worth numerically evolving.

### 9. Fullness Is Also A Spatial Problem
The low A-minor comparison against Pianoteq made one thing very obvious: VB-Engine is still far too mid-heavy and narrow in stereo practice.

In that comparison:

- VB-Engine left and right were extremely correlated
- Pianoteq left and right were much less correlated
- VB-Engine side energy was tiny compared with its mid energy
- Pianoteq preserved substantial side energy through the body of the note

That means fullness is not just about "more body" or "more resonance." It is also about where the energy lives spatially.

The redesign should preserve this rule:

- direct strings may be relatively narrow
- bridge and soundboard radiation should create meaningful width
- shared body energy must not collapse everything back into near-mono

If the instrument reads as almost mono in practice, it will struggle to sound physically large no matter how much spectral tuning we do.

### 10. The Body Layer Must Not Become A Low-Mid Energy Pool
The same low A-minor comparison also showed that VB-Engine was over-concentrating energy in the low-mid region.

The measured early-note spectrum was dominated overwhelmingly by roughly `80-160 Hz`, while Pianoteq distributed much more energy into higher partial bands and retained a denser overtone cloud through the sustain.

That makes the following failure mode explicit:

- too little structured upper-partial support
- too much shared low-mid board energy
- too few body modes, or body modes with overly pooled excitation
- a "growl" or "boardy" quality when body and resonance are raised

This means the body system should evolve toward:

- richer modal density
- less pooled low-mid excitation
- better separation between bridge projection and soundboard radiation
- less reliance on a shared mono-ish body state

Ambience can exaggerate this problem, but it should be treated as an amplifier of an existing body issue, not the primary cause.

### 11. String Group Structure Still Needs To Get More Literal
We have started modeling per-note string counts, detune, and position, but the current regime is still only a coarse first pass.

Important reminder:

- most notes are not single strings
- upper and middle notes often need full trichord treatment
- neighboring strings in a unison group are not perfectly identical
- the bridge location and coupling of those strings matters to decay, bloom, and width

The current string-count thresholds are still too blunt for a realistic middle register. If a note that should feel like a rich trichord is still modeled as a simpler group, fullness and decay complexity will be capped before later stages even begin.

The redesign should therefore leave room for:

- more realistic note-by-note string-group mapping
- unison-specific bridge coupling
- note-wise detune and balance calibration
- stronger interaction between string-group geometry and soundboard readout

### 12. C4 Release Comparison Exposed A Body-Radiation Gap
The C4 Pianoteq versus VB-Engine comparison gave a cleaner single-note version of the same issue we heard in chords.

Observed from `~/Videos/pianoteq-example.mp4` versus `~/Videos/vb-example.mp4`:

- VB-Engine was still much quieter at peak than Pianoteq
- VB-Engine released into silence faster, especially at very low envelope thresholds
- VB-Engine's stereo channels were almost fully correlated
- Pianoteq kept substantially more side energy in the body of the note and into the tail

This is important because it means the release problem should not be treated as only a damper problem. The dampers can and should control string motion, but the bridge, soundboard, cabinet, and air radiation should keep speaking briefly after the string is muted.

The current bridge/body path is still too dependent on a nearly mono string seed. A real grand piano's soundboard does not wait for wide stereo string input before it radiates width; the board geometry, bridge placement, and observation point create spatial structure from the instrument itself.

The next body/soundboard pass should therefore prioritize:

- persistent post-release body modes that are not reset just because a voice deactivates
- spatial radiation modes with independent left/right observation weights
- bridge-to-board energy storage that survives damper contact briefly
- less mono pooling of low-mid body energy
- a cleaner distinction between sympathetic string receptors and soundboard/body radiation

### 13. The Host Parameter Menu Should Become A Real Control Surface
The Pianoteq parameter list is a useful architectural clue, not just a UI reference. It exposes controls that correspond to real subsystems:

- condition and dynamics
- hammer hardness, hammer noise, hammer tone, and hammer strike point
- impedance, impedance cutoff, and impedance slope
- string length and unison/string-group behavior
- sympathetic resonance and duplex-scale resonance
- blooming energy and blooming inertia
- pedal noise, damper position, and damping duration
- microphone/head/listener placement and stereo width

VB-Engine should move toward a similarly honest parameter surface over time. The rule is that a visible knob must either control a real subsystem now or be deferred until that subsystem exists. Placeholder controls are dangerous because they make tuning feel more powerful while hiding the fact that the model is not actually changing.

Near-term parameter exposure should prioritize controls that are already backed by real behavior:

- master gain
- string level
- mechanical/action level
- hammer noise
- resonance
- body
- ambience
- sustain pedal amount
- soft pedal amount

Future parameters should be added alongside the physics they control:

- hammer hardness and strike point when the hammer model supports them as runtime controls
- damper position, release speed, and damping duration when the damper model exposes those separately
- impedance, impedance cutoff, and impedance slope when the bridge/soundboard model has meaningful impedance parameters
- string length/stretch, unison detune, and unison balance when note-wise string groups become calibration targets
- stereo width, head/listener position, and microphone placement when soundboard radiation has independent observation geometry

### 14. String Terminations, Downbearing, And The Plate Are Missing Boundary Physics
The string should not be modeled as if it lives in empty space between an abstract hammer and an abstract bridge.

Important physical boundaries:

- agraffes define the speaking length in the bass, tenor, and lower-treble region
- the capo d'astro bar or V-bar defines the speaking length in the upper treble
- the bridge pins and bridge define the soundboard-side speaking-length boundary
- the cast-iron plate holds tension and should mostly behave like a rigid, lossy boundary, not a primary acoustic conductor
- the bridge sits slightly higher than the termination points, creating downbearing force into the soundboard

This matters because terminations determine how much energy is reflected back into the speaking length, how much leaks into non-speaking string segments and plate structure, and how strongly the string remains coupled to the bridge.

The first realtime model does not need full geometry, but it should include the right reduced variables:

- `front_termination_rigidity`: high for agraffe/capo boundaries, with small losses for metallic color
- `bridge_termination_rigidity`: how cleanly the bridge reflects string waves while still extracting energy
- `downbearing_preload`: vertical string force on the bridge/soundboard junction
- `plate_leak`: a deliberately small, damped loss/color path, never the main soundboard feed
- `duplex_segment_coupling`: optional high-frequency leakage into non-speaking string segments later

Downbearing should be treated as a bridge-to-soundboard impedance/preload parameter. Too little downbearing should make the bridge boundary fuzzy, weakly coupled, and thin. Too much downbearing should over-load the board, increasing harsh transfer and shortening sustain. The useful middle region should improve power, sustain, and clarity by keeping string energy coupled to the bridge without choking the board.

This suggests a future bridge model shaped like:

- string group sends force into a bridge junction
- front termination reflects most energy back into the speaking length
- bridge termination reflects some energy and transfers some into the bridge
- downbearing preload controls the transfer coefficient and damping at the bridge/soundboard contact
- soundboard modes receive energy from the bridge, not from the plate
- plate leakage remains a small, highly damped metallic loss branch

This is likely a missing piece behind the current weak sustain/body behavior: our bridge is still a signal mixer, not yet a preloaded mechanical junction.

## What We Are Keeping
We are not throwing away everything.

- The engine should stay Rust-native and safe in the realtime path.
- The piano should remain waveguide-centered in the realtime path rather than pivoting immediately into a full research-grade FDTD simulator.
- FDTD is welcome as an offline or optional focused physics lab for subsystems where the reduced model fails.
- Minimal dependencies remain a priority.
- Python is allowed and encouraged for offline analysis, parameter fitting, and calibration tooling.
- External repos are primarily references, not runtime dependencies.

## Design Priorities
- believable dry piano tone before polish
- coherent physical topology before parameter count
- realtime stability before heroic simulation scope
- subsystem separation that matches the instrument's mechanics
- strong offline calibration and measurement support
- assistive playability features for real musicians, including players with binary sustain pedals

## Guiding Principles

### 1. Reduced Physics, Not Fake Polish
Physically motivated filters are acceptable when they stand in for real behaviors such as dispersion, impedance, damping, or radiation. What we want to avoid is using reverb, EQ, or body wash to compensate for a weak dry model.

### 2. Energy Paths Matter More Than Fancy Math In Isolation
A simpler equation inside the right topology is more valuable than a more advanced equation inside the wrong topology.

### 3. Observation Is Separate From Mechanics
The instrument should have one internal mechanical truth and multiple readout points. "Direct sound", "body", and "space" should emerge from different observation paths through the same instrument state, not from unrelated downstream processors.

### 4. Adaptive Fidelity Beats Uniform Fidelity
The engine should spend detail where it matters most:

- hammer contact gets oversampled only during contact
- bass notes get more elaborate coupling than treble notes
- soundboard richness should respond to bridge energy, not run at full intensity all the time
- sympathetic activation should be sparse and targeted rather than globally brute-forced

### 5. Playability Is Part Of The Product
If the engine is physically respectable but musically unusable for a player with ordinary hardware, then the design is incomplete.

## Proposed Top-Level Architecture
The new piano should be organized around a bridge-centered energy network.

### Internal Crate Boundary
The physics core is now beginning to move into an internal crate:

- `crates/vb-piano-physics`

This does not mean the project is ready to publish a public physics library. It means we are drawing the right boundary early, while the refactor is still flexible.

The crate should hold reusable piano-mechanics primitives:

- note and material profiles
- string-group physics when it becomes independent enough to move
- bridge-junction coupling
- soundboard modal radiation
- termination and downbearing parameters
- offline calibration-ready data structures

The VST and engine wrapper should remain responsible for:

- host parameters
- MIDI/control interpretation
- voice allocation
- final audio callback integration
- plugin installation and packaging

The current first step is intentionally bounded but no longer tiny: the reduced `BridgeJunction`, `SoundboardModel`, and `SympatheticReceptors` live in the crate, while the existing engine still adapts them through `BridgeNetwork`. That gives us a real seam without pretending the whole instrument API is final.

### Paper-Derived Implementation Notes
The 2023 Sato and Samejima hammer-shank paper, Stulov's piano string scale paper, Haifan Xie's 2024 whole-piano model, and the Yamaha CMS preprint all point toward the same practical reduced architecture:

- Use nonlinear hammer/felt behavior as the source of velocity-dependent brightness rather than a static impulse.
- Treat bridge, agraffe, capo, bearing, and downbearing as termination and mobility parameters, not as output EQ.
- Make the string-to-soundboard seam force/mobility based: strings push support-point force into the bridge, the bridge stores motion, and a controlled reflection goes back toward active strings.
- Keep the full FEM/BEM/CMS literature as calibration guidance, but use a reduced modal soundboard bank in the realtime VST path.
- Model sympathetic resonance as sparse bridge-mediated receptor motion gated by damper admittance, not as symbolic chord recognition.
- Keep ML out of the audio callback. Use it later for offline parameter fitting, residual analysis, or generated calibration data if we build that tooling.

Current implementation status:

- `BridgeJunction` now returns a reduced reflected force used by the active string banks on the next frame.
- `SympatheticReceptors` now lives in `vb-piano-physics` and is driven by bridge slow/band energy plus continuous damper lift.
- `SoundboardModel` now uses a denser reduced modal bank with separate modal gain, rim reflection, and stereo radiation width controls.
- Host parameters now expose hammer hardness, bridge feedback, downbearing, plate leak, and soundboard width so Carla can reach the physical seams directly.

### Mechanical Signal Flow
1. Hammer injects force into one struck string group at a strike point.
2. The struck string group evolves as a waveguide-centered system with dispersion, damping, and unison structure.
3. The string group pushes energy into a bridge junction.
4. The bridge junction distributes energy into:
   - the struck group again
   - idle string receptor groups
   - a soundboard modal bank
   - a small airborne or cavity path later, if added
5. Audio is read from several observation paths:
   - string/direct path
   - bridge path
   - soundboard/body path
   - mechanical/noise path
   - optional room path

This is the central redesign move. The bridge is no longer a downstream flavor stage. It is the central coupling object.

## Core Subsystems

### Hammer System
The hammer remains a nonlinear exciter and should be upgraded rather than discarded.

Responsibilities:

- compute felt compression force
- model velocity-dependent hardness
- model contact duration
- generate the direct mechanical transient
- return contact state needed by the string system

Planned direction:

- keep the current nonlinear strike path as the starting point
- move toward a hysteretic felt model if justified by measurements and complexity budget
- treat contact oversampling as local to hammer contact, not global engine oversampling

### String Group
A "string group" represents the one, two, or three strings associated with a piano key.

Responsibilities:

- fundamental tuning and stretch tuning
- dispersion or inharmonicity
- unison detune and beating
- string damping and damper interaction
- bridge interaction
- readout taps for direct sound

Important note:
The string group is not just "the audible string output." It is the source of bridge force and the home of most note identity.

Important expansion:
The string group should eventually be constructed from an explicit note-physics profile rather than ad hoc formulas scattered across the hammer, string, and bridge code.

That profile should at minimum describe:

- note frequency
- string-count regime
- material regime
- speaking length
- effective radius and density
- tension and impedance proxies
- stiffness or inharmonicity proxy
- strike-point defaults
- expected detune structure

### Bridge Junction
This is the most important missing abstraction in the current engine.

Responsibilities:

- accept force or motion from active string groups
- apply frequency-dependent impedance
- return reflected energy back into strings
- feed soundboard modes
- feed idle-string sympathetic receptors
- expose bridge motion as a readout point

Design intent:
The bridge should behave like a coupling hub, not a glorified EQ stage.

### Sympathetic Receptor Network
This replaces the coarse resonance bank model.

Responsibilities:

- decide which idle strings are physically eligible to respond
- inject small amounts of energy into those string groups based on overlap and damping
- respect pedal and damper state
- remain sparse enough for realtime use

This system should be bridge-mediated first, not chord-logic-first.

### Soundboard Modal Bank
The soundboard should be represented as a reduced modal system rather than a generic body color stage.

Responsibilities:

- receive bridge motion or force
- accumulate resonant energy across a compact set of modes
- re-radiate energy to output observation paths
- optionally feed a small amount of energy back into the bridge later if needed

This is where much of the fullness should come from.

Important reminder:
The current bridge-centered refactor introduced a better body path, but it is still only an early reduced soundboard model. We should not mentally mark the soundboard problem as "done." The soundboard is still one of the main places where fullness, projection, and low-register chord coherence can improve.

### Observation And Radiation Layer
The final audio should be built from several readout points over the same instrument state.

Readout candidates:

- direct string readout
- bridge readout
- soundboard readout
- mechanical transient readout
- optional cavity or room readout

This layer is where we decide "what the listener hears," not "how the piano works."

## Resonance Model
Sympathetic resonance should not be treated as merely "enough bridge energy."

It should be treated as the product of four gates:

`resonance ~= spectral_overlap * coupling_path * admittance * available_energy`

Where:

- `spectral_overlap` asks whether the source partials line up with receptor modes
- `coupling_path` asks whether energy can actually travel through bridge, soundboard, and minor airborne paths
- `admittance` asks whether the receptor string is free enough to move, given damper state and note-specific damping
- `available_energy` asks whether enough energy remains after losses to wake the receptor

This is a better frame than symbolic note-relationship logic alone.

## Sympathetic Resonance Design

### Physical Rule
Idle strings respond because they share modal or partial alignment with energy traveling through the instrument, not because the engine recognizes a musical key or chord as a concept.

### Symbolic Logic Still Has Value
`rust-music-theory` can still be useful for:

- generating candidate note relationships quickly
- ranking likely overlap families such as octaves and fifths
- supporting alternate tuning systems or future temperament features
- debugging and analysis tooling

But symbolic logic is only a pruning hint. Physics makes the final decision.

### Practical Expectations
- Not every same-letter note should ring equally.
- Same-note and octave-family strings should respond the strongest when free to move.
- Fifth-related and other partial-aligned notes should respond more weakly.
- Notes with dampers down should show tiny residual response at most, and very short-lived.
- There should not be a hand-coded "2 octave" or "3 octave" cutoff. Distance and falloff should emerge from overlap and damping.

### Proposed Implementation Shape
- Precompute a compact receptor profile for each note.
- Each profile contains the strongest modal receptivity regions for that string group.
- The bridge produces a reduced spectral or band-energy proxy each frame or each short window.
- Only the strongest candidate idle groups are evaluated for wake-up.
- Wake-up energy is injected sparsely and with note-specific damping.

This should give us a physically grounded resonance model without brute-forcing full string-to-string coupling.

### Rough Wake-Up Scoring Model
When the time comes to implement candidate selection, a useful rough score is:

`wake_score = overlap_score * path_gain * damper_admittance * source_energy * distance_weight`

Where:

- `overlap_score` measures how closely active bridge bands match the receptor profile
- `path_gain` captures the reduced coupling coefficient from bridge to receptor family
- `damper_admittance` is near `1.0` for free strings and very small for closed dampers
- `source_energy` is the current reduced bridge drive for the matched region
- `distance_weight` is optional and should be weak, mainly to break ties rather than impose a hard cutoff

The practical rule is to rank candidates by score, then wake only a small top set per short window instead of evaluating every possible idle string every sample.

## Damper Model
The physical engine should expose a continuous damper openness variable for each relevant string group or note state.

Why:

- real pianos support partial damper lift
- this matters for half-pedaling and realistic decay control
- even with binary hardware, the engine should preserve the richer internal model

The mistake would be to collapse the engine itself to an on or off damper model just because some controllers only send on or off sustain.

Important reminder:
The current engine now has a first-class `DamperModel` instead of only a string-bank scalar choke. That model tracks continuous openness, felt contact, and choke separately. It is still reduced physics, but it gives us a real place to evolve damper behavior instead of hiding it inside generic release tuning.

The key rule is:

- if the sustain pedal is pressed, dampers are off every string according to the pedal lift amount
- if the sustain pedal is not pressed, dampers rest on every string unless that note's key is currently down
- if a key is pressed, the dampers are off that key's string group regardless of global pedal state
- if the key is released and the pedal is not lifting the dampers, felt contact returns progressively and damping increases
- if the key is released under half-pedal, the string group should land in a partially damped state rather than a hard binary sustain state

The redesign still wants a better representation of:

- initial damper contact
- progressive choke
- persistence of strong string motion after contact
- continued body and soundboard radiation after damper closure
- pedal-dependent residual openness
- note-wise damper felt shape and contact pressure
- realistic bass-vs-treble damper travel and settling behavior

## Pedal Input Interpretation
Pedal interpretation should be a separate layer above the physical damper model.

This layer translates incoming pedal hardware behavior into the engine's continuous damper control.

Examples:

- true continuous pedal input can map directly with smoothing
- binary pedal input can be mapped into a virtual lift curve with attack and release timing
- future host automation can drive exact openness

Current product default:

- `SustainPedalMode::Binary` is the default because Parker's current MIDI pedal is on or off
- `SustainPedalMode::Continuous` exists for hardware or host automation that sends meaningful intermediate CC values
- binary input should not force the damper model itself to become binary internally

This separation lets the instrument remain physically richer than the controller.

## Auto-Pedal Clarity Layer
This is one of the most promising practical additions in the redesign.

### Motivation
Many players use binary sustain pedals. When too many notes build up, the result can become muddy and unpleasant. Commercial digital pianos often hide this with simplistic note stealing or sustain cleanup. We want something better.

### Core Principle
Auto-pedal is not the piano model itself. It is an assistive performance layer above the piano model.

### Desired Behavior
- enabled by default
- disable-able in a future config menu or plugin UI
- conservative enough to preserve realism
- musical enough to prevent unusable blur

### Proposed Modes
- `Off`: no clarity intervention
- `Assist`: default mode, gently reduces clutter
- `Strict`: stronger cleanup for players who prefer high separation

If the final UI wants only one switch at first, `Assist` should be the default and `Off` should be available.

### How It Should Work
The engine monitors:

- total sustaining energy
- low-mid spectral congestion
- number of currently undamped string groups
- age and loudness of sustaining notes
- harmonic relevance of older notes to newer ones

When clutter exceeds a threshold, the assist layer should:

- preferentially re-damp the oldest and quietest sustaining notes
- preserve recent notes
- preserve notes that strongly reinforce current harmonic content
- preserve octave and fifth support more readily than weak clutter tones
- avoid abrupt hard cuts whenever possible

This should feel like intelligent pedal cleanup, not fake gating.

### Rough Note-Retention Scoring Model
A useful first-pass priority score for deciding which sustained notes deserve to remain open is:

`retain_score = recency + loudness + melodic_bias + harmonic_relevance - clutter_penalty`

Where:

- `recency` favors newly played notes
- `loudness` favors notes still carrying meaningful energy
- `melodic_bias` can favor the highest recent note or notes with attack prominence
- `harmonic_relevance` favors octave and fifth support and notes strongly aligned with current bridge energy
- `clutter_penalty` increases for old, quiet, spectrally redundant sustain tails

The assist layer should begin by reducing damper openness or shortening the residual tail of the lowest-ranked notes first, rather than hard-killing whichever note happens to be oldest.

### Default Product Direction
The expected end-user behavior should be:

- `Assist` on by default
- a clear enable or disable control in the future config menu or plugin GUI
- an intensity control later if needed, but not required for the first usable version

This keeps the engine friendly for players with ordinary binary pedals while still respecting players who want fully manual physical behavior.

### Important Separation
The assist layer should operate by nudging damper openness or note release policy, not by simply muting random audio downstream.

That distinction matters. We want the cleanup to remain mechanically plausible.

## Cabinet And Air Paths
Cabinet geometry and internal air reflections are real, but they should not become the first explanatory model for sympathetic behavior.

Priority order:

1. string-to-bridge coupling
2. bridge-to-soundboard coupling
3. soundboard-to-string feedback and minor airborne influence
4. cabinet and cavity detail

Grand piano size and cavity behavior may matter later for realism and energy-loss behavior, but they should be introduced only after the mechanical core is believable.

If and when added, cabinet or cavity effects should remain subtle and should sit in the coupling graph, not as a rescue reverb.

## Tuning And Calibration
Calibration should be treated as a first-class subsystem of the project, even if it lives in scripts rather than the realtime engine.

### Python's Role
Python is encouraged for:

- parameter fitting
- spectral analysis
- coefficient generation
- reference comparison
- extracting note-wise calibration tables

This is not a retreat from Rust. It is the right tool split.

### What Should Be Calibrated
- inharmonicity per note
- optional stretch-tuning offsets or equivalent per-note pitch corrections
- note-wise decay targets
- unison detune structure
- note-wise physical parameter tables when formulas are too crude
- bridge coupling weights
- sympathetic receptor strength
- soundboard modal density and decay
- hammer contact behavior

Historical reminder:
Human piano tuners did not carry 88 forks. They anchored from a small trusted reference and tuned the rest by interval and listening. That is useful as a design principle even if the engine remains mathematically exact: anchor a few reliable references, then make sure the rest of the instrument behaves coherently by interval and ear, not just by formula.

Reference-repo reminder:
The Rust references are a useful check on missing structure:

- `study_physical_sound_model` is valuable for hammer-junction, impedance, loss-filter, and dispersion ideas
- `riano` is valuable as a reminder to keep string physical parameters explicit instead of burying them in broad voicing curves

We do not need to copy either project wholesale, but we should notice what kinds of physical parameters they refuse to hide.

### Output Form
Offline tools should ideally emit compact machine-readable tables or Rust-ready data files so that the realtime engine stays simple and deterministic.

## Observation Model
The engine should eventually expose distinct internal contributions for debugging and tuning:

- struck strings
- sympathetic strings
- bridge motion
- soundboard/body modes
- mechanical noises
- room or space contribution
- assistive pedal intervention state

This will help us avoid tuning blind and prevent us from confusing a polished mix with a good physical model.

## UI And Config Implications
The redesign implies a richer control surface later, even if the first build remains sparse.

Future UI directions should likely include:

- direct or body balance
- hammer hardness or tone
- strike point
- sympathetic resonance amount
- pedal assist enable or disable
- pedal assist intensity
- perhaps a small "player help" section separate from core piano physics

We do not need to design the plugin GUI now, but we should keep these categories in mind so the engine is not boxed into a tiny control vocabulary.

## Suggested Refactor Direction

### First Major Refactor
Replace the current "voices plus resonance bank plus output stage" topology with a bridge-centered coupling graph.

### Second Major Refactor
Replace the coarse resonance system with sparse sympathetic receptors driven by bridge energy and damper state.

Explicit requirement for that stage:

- remove any remaining hardcoded chord-family or pitch-class-major-logic resonance shortcuts
- make candidate selection harmonic-overlap-driven instead
- make low-register behavior a first-class validation target

### Third Major Refactor
Replace generic body coloration with a true reduced soundboard modal bank.

Explicit requirement for that stage:

- improve low-register projection and chord blending
- let body radiation contribute to lingering note life after damper contact
- separate soundboard radiation from generic ambience

### Fourth Major Refactor
Add the pedal interpretation and clarity-assist layer as an explicit control subsystem above the physical engine.

These stages are intentionally ordered so we improve topology before adding smart performer assistance.

### Foundation Layer Under All Of The Above
The codebase now has an explicit note-physics layer shared by hammer, strings, bridge, and future calibration tooling. This should keep expanding rather than letting each subsystem invent its own private register curves again.

That layer should centralize:

- note-wise derived physical parameters
- material regime selection
- bass-vs-treble string behavior differences
- impedance and stiffness proxies
- durable places to later inject measured or fitted tables

This remains one of the most important foundation areas, but it is no longer purely missing. The next step is to make the tables and formulas more physically calibrated.

## What To Avoid
- full FDTD as the main engine right now
- giant unsafe C or C++ integrations in the audio path
- treating symbolic music theory as physical truth
- bolting more ambience or body wash onto the current topology and calling it realism
- using auto-pedal as a crude note killer
- hiding all important mechanics behind a handful of generic register curves forever

## Open Questions
- How much bridge-to-string feedback should be explicit versus folded into reduced coefficients?
- Should the soundboard feed a small amount of energy back into the bridge in phase-aware form, or only in reduced band-energy form?
- How sparse can the sympathetic receptor network be before it sounds artificial?
- How much damper residual motion should be preserved when "closed"?
- Should pedal assist operate per note, per string group, or per spectral band?

## Working Rule Of Thumb
Whenever we are unsure whether a new subsystem belongs:

Ask whether it represents:

- mechanical state
- coupling between mechanical states
- observation of mechanical state
- assistive performer control above the instrument

If it fits none of those, it is probably either a convenience hack or a downstream effect that should not drive the architecture.

## Final Direction
The redesign should aim for a piano that is:

- waveguide-centered
- bridge-centered
- modal in the body where useful
- sparse and physical in sympathetic activation
- continuous in damper behavior
- optionally assistive for binary-pedal players
- calibrated through offline analysis

If we stay faithful to that, the engine should become both more realistic and more playable, which is the whole point.
