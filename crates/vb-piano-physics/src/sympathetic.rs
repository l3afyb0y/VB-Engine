//! Sparse sympathetic string receptors driven by bridge energy.
//!
//! This is a reduced bridge-mediated model, not chord logic. Each receptor is
//! a quiet string-family mode that wakes when the bridge contains energy near
//! one of its low partials and the dampers are sufficiently open.

use crate::{TAU, radius_from_t60, soft_clip};

/// Number of quiet string receptors used for bridge-mediated resonance.
pub const SYMPATHETIC_RECEPTOR_COUNT: usize = 60;

const RECEPTOR_NOTE_START: u8 = 28;

/// Runtime configuration for the sparse sympathetic receptor bank.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct SympatheticConfig {
    /// Audio sample rate used to derive modal coefficients.
    pub sample_rate_hz: u32,
    /// Overall bridge-to-receptor coupling amount.
    pub coupling_gain: f32,
}

impl Default for SympatheticConfig {
    fn default() -> Self {
        Self {
            sample_rate_hz: 48_000,
            coupling_gain: 1.0,
        }
    }
}

/// Per-frame bridge energy available to sympathetic string receptors.
#[derive(Debug, Clone, Copy, Default, PartialEq)]
pub struct SympatheticInput {
    /// Slowly tracked bridge motion, favoring bass and mid receptors.
    pub bridge_slow: f32,
    /// Band-limited bridge motion, favoring higher receptors.
    pub bridge_band: f32,
    /// Current bridge-junction output directed toward receptors.
    pub receptor_drive: f32,
    /// Reduced bridge energy after the bridge junction advances.
    pub bridge_energy: f32,
    /// Damper lift from `0.0` closed to `1.0` fully open.
    pub damper_lift: f32,
}

/// Stereo output from sympathetic receptors for one audio frame.
#[derive(Debug, Clone, Copy, Default, PartialEq)]
pub struct SympatheticOutput {
    /// Left sympathetic receptor readout.
    pub left: f32,
    /// Right sympathetic receptor readout.
    pub right: f32,
    /// Absolute receptor energy after this frame.
    pub energy: f32,
}

/// Sparse bank of bridge-mediated sympathetic string receptors.
#[derive(Debug, Clone)]
pub struct SympatheticReceptors {
    receptors: [ReceptorMode; SYMPATHETIC_RECEPTOR_COUNT],
    coupling_gain: f32,
}

impl SympatheticReceptors {
    /// Create a sparse sympathetic receptor bank.
    #[must_use]
    pub fn new(config: SympatheticConfig) -> Self {
        let mut receptors = [ReceptorMode::default(); SYMPATHETIC_RECEPTOR_COUNT];
        for (index, mode) in receptors.iter_mut().enumerate() {
            let note = receptor_note(index);
            let frequency_hz = midi_note_hz(note);
            let register_position = register_position_for_note(note);
            let pan = (((note as f32 - 60.0) / 27.0).clamp(-1.0, 1.0)) * 0.12;
            let low_weight = 1.0 - register_position;
            let t60 = 0.70 + (low_weight * 2.6) + (register_position * 0.34);
            let coupling = (0.00010 + (low_weight * 0.00018) + (register_position * 0.00010))
                .clamp(0.00008, 0.00055);

            mode.configure(
                frequency_hz,
                config.sample_rate_hz,
                t60,
                register_position,
                pan,
                coupling,
            );
        }

        Self {
            receptors,
            coupling_gain: config.coupling_gain.clamp(0.0, 2.0),
        }
    }

    /// Update the global coupling amount without reallocating or resetting modes.
    pub fn set_coupling_gain(&mut self, coupling_gain: f32) {
        self.coupling_gain = coupling_gain.clamp(0.0, 2.0);
    }

    /// Seed receptor modes from a struck note before the continuous bridge drive arrives.
    pub fn note_on(&mut self, note: u8, velocity: u8, damper_lift: f32) {
        let velocity_norm = velocity as f32 / 127.0;
        let source_hz = midi_note_hz(note);
        let register_scale = 1.00 - (register_position_for_note(note) * 0.18);
        let admittance = damper_admittance(damper_lift);
        let base_seed = (0.00018 + (velocity_norm * 0.00130)) * register_scale * admittance;

        for mode in &mut self.receptors {
            let overlap = harmonic_overlap_score(source_hz, mode.frequency_hz);
            if overlap < 0.022 {
                continue;
            }

            mode.drive =
                (mode.drive + (overlap * base_seed * self.coupling_gain)).clamp(-0.035, 0.035);
        }
    }

    /// Dampen receptor families related to a retriggered note.
    pub fn dampen_for_retrigger(&mut self, note: u8) {
        let source_hz = midi_note_hz(note);
        for mode in &mut self.receptors {
            let overlap = harmonic_overlap_score(source_hz, mode.frequency_hz);
            if overlap < 0.040 {
                continue;
            }

            let retain = (1.0 - (overlap * 2.5)).clamp(0.0, 0.32);
            mode.dampen(retain);
        }
    }

    /// Advance and render the receptor bank.
    #[must_use]
    pub fn process(&mut self, input: SympatheticInput) -> SympatheticOutput {
        let admittance = damper_admittance(input.damper_lift);
        let damped_leak = 0.035 + (input.bridge_energy * 0.020);
        let low_bridge_drive = input.bridge_slow * admittance;
        let band_bridge_drive = input.bridge_band * admittance;
        let receptor_drive = input.receptor_drive * admittance;
        let overload = ((self
            .receptors
            .iter()
            .map(|mode| mode.drive.abs())
            .sum::<f32>()
            - 0.36)
            .max(0.0)
            * 0.08)
            .clamp(0.0, 0.12);

        let mut left = 0.0;
        let mut right = 0.0;
        let mut energy = 0.0;

        for mode in &mut self.receptors {
            if overload > 0.0 {
                mode.drive *= 1.0 - overload;
            }

            let low_weight = (1.0 - mode.register_position).powf(0.75);
            let band_weight = 0.38 + (mode.register_position * 0.95);
            let mode_drive = (low_bridge_drive * low_weight * 0.07)
                + (band_bridge_drive * band_weight * 0.42)
                + (receptor_drive * (0.08 + mode.register_position * 0.12));
            mode.drive = (mode.drive
                + (mode_drive
                    * mode.bridge_coupling
                    * self.coupling_gain
                    * (0.16 + (input.bridge_energy * 0.72))))
                .clamp(-0.045, 0.045);

            if admittance < 0.08 {
                mode.dampen(1.0 - damped_leak);
            }

            if mode.is_idle() {
                mode.drive = 0.0;
                continue;
            }

            let sample = mode.step(input.damper_lift);
            left += sample * (1.0 - mode.pan) * 0.066;
            right += sample * (1.0 + mode.pan) * 0.066;
            energy += sample.abs();
        }

        SympatheticOutput {
            left: soft_clip(left),
            right: soft_clip(right),
            energy,
        }
    }

    /// Gently damp all receptor states.
    pub fn dampen(&mut self, amount: f32) {
        let amount = amount.clamp(0.0, 1.0);
        for mode in &mut self.receptors {
            mode.dampen(amount);
        }
    }
}

#[derive(Debug, Clone, Copy)]
struct ReceptorMode {
    frequency_hz: f32,
    omega_cos: f32,
    base_radius: f32,
    register_position: f32,
    pan: f32,
    bridge_coupling: f32,
    previous_1: f32,
    previous_2: f32,
    drive: f32,
}

impl Default for ReceptorMode {
    fn default() -> Self {
        Self {
            frequency_hz: 0.0,
            omega_cos: 0.0,
            base_radius: 0.0,
            register_position: 0.0,
            pan: 0.0,
            bridge_coupling: 0.0,
            previous_1: 0.0,
            previous_2: 0.0,
            drive: 0.0,
        }
    }
}

impl ReceptorMode {
    fn configure(
        &mut self,
        frequency_hz: f32,
        sample_rate_hz: u32,
        t60_seconds: f32,
        register_position: f32,
        pan: f32,
        bridge_coupling: f32,
    ) {
        let omega = TAU * frequency_hz / sample_rate_hz.max(1) as f32;
        self.frequency_hz = frequency_hz;
        self.omega_cos = omega.cos();
        self.base_radius = radius_from_t60(t60_seconds, sample_rate_hz).clamp(0.9985, 0.999_95);
        self.register_position = register_position.clamp(0.0, 1.0);
        self.pan = pan.clamp(-0.95, 0.95);
        self.bridge_coupling = bridge_coupling.clamp(0.0, 0.001);
        self.previous_1 = 0.0;
        self.previous_2 = 0.0;
        self.drive = 0.0;
    }

    fn step(&mut self, damper_lift: f32) -> f32 {
        let radius = if damper_lift > 0.5 {
            (self.base_radius + 0.000_12).min(0.999_985)
        } else {
            self.base_radius
        };
        let coeff_a = 2.0 * radius * self.omega_cos;
        let coeff_b = radius * radius;
        let current = ((coeff_a * self.previous_1) - (coeff_b * self.previous_2) + self.drive)
            .clamp(-0.92, 0.92);
        self.previous_2 = self.previous_1;
        self.previous_1 = current;

        let drive_decay = if damper_lift > 0.5 { 0.992 } else { 0.970 };
        self.drive *= drive_decay;
        current
    }

    fn dampen(&mut self, amount: f32) {
        self.previous_1 *= amount;
        self.previous_2 *= amount;
        self.drive *= amount;
    }

    fn is_idle(&self) -> bool {
        self.drive.abs() < 1.0e-7
            && self.previous_1.abs() < 1.0e-7
            && self.previous_2.abs() < 1.0e-7
    }
}

fn damper_admittance(damper_lift: f32) -> f32 {
    let lift = damper_lift.clamp(0.0, 1.0);
    0.045 + (lift * lift * (3.0 - (2.0 * lift)) * 0.955)
}

fn receptor_note(index: usize) -> u8 {
    RECEPTOR_NOTE_START + index as u8
}

fn register_position_for_note(note: u8) -> f32 {
    ((note as f32 - 21.0).max(0.0) / 87.0).clamp(0.0, 1.0)
}

fn midi_note_hz(note: u8) -> f32 {
    440.0 * 2.0_f32.powf((note as f32 - 69.0) / 12.0)
}

fn harmonic_overlap_score(source_hz: f32, receptor_hz: f32) -> f32 {
    let mut best = 0.0_f32;

    for source_partial in 1..=8 {
        let source_partial_hz = source_hz * source_partial as f32;
        for receptor_partial in 1..=6 {
            let receptor_partial_hz = receptor_hz * receptor_partial as f32;
            let cents = 1200.0 * (source_partial_hz / receptor_partial_hz).log2().abs();
            let width = 16.0 + ((source_partial.max(receptor_partial) as f32) * 5.5);
            let alignment = (-0.5 * (cents / width).powi(2)).exp();
            let partial_weight =
                1.0 / ((source_partial as f32).powf(0.82) * (receptor_partial as f32).powf(0.92));
            best = best.max(alignment * partial_weight);
        }
    }

    best
}

#[cfg(test)]
mod tests {
    use super::{
        SympatheticConfig, SympatheticInput, SympatheticReceptors, harmonic_overlap_score,
        midi_note_hz,
    };

    #[test]
    fn exact_and_octave_families_score_above_symbolic_thirds() {
        let c3 = midi_note_hz(48);
        let c3_overlap = harmonic_overlap_score(c3, midi_note_hz(48));
        let g3_overlap = harmonic_overlap_score(c3, midi_note_hz(55));
        let e3_overlap = harmonic_overlap_score(c3, midi_note_hz(52));

        assert!(c3_overlap > g3_overlap);
        assert!(g3_overlap > e3_overlap);
    }

    #[test]
    fn pedal_lift_increases_sympathetic_receptor_energy() {
        let mut closed = SympatheticReceptors::new(SympatheticConfig::default());
        let mut open = SympatheticReceptors::new(SympatheticConfig::default());

        closed.note_on(60, 112, 0.0);
        open.note_on(60, 112, 1.0);

        let mut closed_energy = 0.0;
        let mut open_energy = 0.0;
        for _ in 0..512 {
            closed_energy += closed
                .process(SympatheticInput {
                    bridge_slow: 0.10,
                    bridge_band: 0.22,
                    receptor_drive: 0.16,
                    bridge_energy: 0.18,
                    damper_lift: 0.0,
                })
                .energy;
            open_energy += open
                .process(SympatheticInput {
                    bridge_slow: 0.10,
                    bridge_band: 0.22,
                    receptor_drive: 0.16,
                    bridge_energy: 0.18,
                    damper_lift: 1.0,
                })
                .energy;
        }

        assert!(open_energy > closed_energy * 4.0);
    }
}
