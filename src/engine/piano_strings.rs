use super::{
    piano_damper::DamperModel,
    piano_physics::{NotePhysics, StringMaterialRegime},
};

const PARTIAL_COUNT: usize = 12;
const MAX_STRING_COUNT: usize = 3;
const TAU: f32 = core::f32::consts::PI * 2.0;

/// First-order DC blocking filter: y[n] = x[n] - x[n-1] + R·y[n-1].
///
/// Removes DC and near-DC content while preserving all musical frequencies.
/// Cutoff frequency is specified at construction time.
#[derive(Debug, Clone, Copy)]
pub(super) struct DcBlocker {
    pub(super) coefficient: f32,
    previous_input: f32,
    previous_output: f32,
}

impl DcBlocker {
    pub(super) fn new(sample_rate_hz: u32, cutoff_hz: f32) -> Self {
        let coefficient = (1.0 - (TAU * cutoff_hz / sample_rate_hz as f32)).clamp(0.0, 0.999_995);
        Self {
            coefficient,
            previous_input: 0.0,
            previous_output: 0.0,
        }
    }

    pub(super) fn step(&mut self, input: f32) -> f32 {
        let output = input - self.previous_input + self.coefficient * self.previous_output;
        self.previous_input = input;
        self.previous_output = output;
        output
    }
}

#[derive(Debug, Clone, Copy)]
struct PartialResonator {
    omega_cos: f32,
    base_radius: f32,
    gain: f32,
    bridge_send: f32,
    damper_sensitivity: f32,
    previous_1: f32,
    previous_2: f32,
}

impl Default for PartialResonator {
    fn default() -> Self {
        Self {
            omega_cos: 0.0,
            base_radius: 0.0,
            gain: 0.0,
            bridge_send: 0.0,
            damper_sensitivity: 0.0,
            previous_1: 0.0,
            previous_2: 0.0,
        }
    }
}

impl PartialResonator {
    fn configure(
        &mut self,
        partial_hz: f32,
        sample_rate_hz: u32,
        gain: f32,
        damping: f32,
        damper_sensitivity: f32,
    ) {
        let omega = TAU * partial_hz / sample_rate_hz as f32;
        self.omega_cos = omega.cos();
        self.base_radius = damping.clamp(0.0, 0.999995);
        self.gain = gain;
        self.bridge_send = gain.sqrt().clamp(0.0, 1.0);
        self.damper_sensitivity = damper_sensitivity;
        self.previous_1 = 0.0;
        self.previous_2 = 0.0;
    }

    fn step(&mut self, drive: f32, damper: f32) -> f32 {
        let radius = (self.base_radius - (damper * self.damper_sensitivity)).clamp(0.0, 0.999995);
        let coeff_a = 2.0 * radius * self.omega_cos;
        let coeff_b = radius * radius;
        let current =
            ((coeff_a * self.previous_1) - (coeff_b * self.previous_2) + drive).clamp(-0.85, 0.85);
        self.previous_2 = self.previous_1;
        self.previous_1 = current;
        current
    }

    fn reset(&mut self) {
        *self = Self::default();
    }
}

#[derive(Debug, Clone, Copy)]
pub(super) struct StringBank {
    string_count: usize,
    register_position: f32,
    base_pan: f32,
    string_gain: [f32; MAX_STRING_COUNT],
    string_pan: [f32; MAX_STRING_COUNT],
    resonators: [[PartialResonator; PARTIAL_COUNT]; MAX_STRING_COUNT],
    bridge_memory: f32,
    excitation_scale: f32,
    register_output_scale: f32,
    damper: DamperModel,
    last_activity: f32,
    excitation_dc_blocker: DcBlocker,
}

impl Default for StringBank {
    fn default() -> Self {
        Self {
            string_count: 0,
            register_position: 0.0,
            base_pan: 0.0,
            string_gain: [0.0; MAX_STRING_COUNT],
            string_pan: [0.0; MAX_STRING_COUNT],
            resonators: [[PartialResonator::default(); PARTIAL_COUNT]; MAX_STRING_COUNT],
            bridge_memory: 0.0,
            excitation_scale: 1.0,
            register_output_scale: 1.0,
            damper: DamperModel::default(),
            last_activity: 0.0,
            // Placeholder; replaced in start() with sample-rate-aware instance.
            excitation_dc_blocker: DcBlocker {
                coefficient: 0.0,
                previous_input: 0.0,
                previous_output: 0.0,
            },
        }
    }
}

impl StringBank {
    pub(super) fn start(
        &mut self,
        note: u8,
        velocity: u8,
        sample_rate_hz: u32,
        soft_pedal_amount: f32,
        pan: f32,
    ) {
        let normalized_velocity = velocity as f32 / 127.0;
        let note_physics = NotePhysics::for_note(note);
        let register_position = note_physics.register_position;
        let low_register_weight = note_physics.low_register_weight;

        self.bridge_memory = 0.0;
        self.string_count = note_physics.string_count;
        self.register_position = register_position;
        self.base_pan = pan;
        self.excitation_scale = note_physics.excitation_scale_base
            * (0.78 + (normalized_velocity * 0.38))
            * (1.0 - (0.52 * soft_pedal_amount));
        self.register_output_scale = note_physics.register_output_scale;
        self.damper.reset_open();
        self.last_activity = 0.0;
        // Keep only a very light blocker on the resonator-drive path. The
        // heavier 12 Hz cutoff stabilized earlier tuning passes, but it also
        // shaved too much weight off the struck-note launch.
        self.excitation_dc_blocker = DcBlocker::new(sample_rate_hz, 3.0);
        let spread = unison_spread_for_note(note);
        let detune_cents = note_physics.unison_detune_cents();
        let string_balance = note_physics.unison_balance();
        let string_positions = note_physics.unison_positions();

        for string_index in 0..self.string_count {
            self.string_gain[string_index] = string_balance[string_index];
            self.string_pan[string_index] = pan + (string_positions[string_index] * spread);
        }
        for string_index in self.string_count..MAX_STRING_COUNT {
            self.string_gain[string_index] = 0.0;
            self.string_pan[string_index] = pan;
        }

        let fundamental = note_physics.frequency_hz;
        // Inharmonicity coefficient B: increases with register (thin treble strings
        // have more bending stiffness relative to tension than thick bass strings).
        let inharmonicity = note_physics.inharmonicity_coefficient;
        let strike_position = note_physics.strike_position_base + (0.010 * soft_pedal_amount);
        let rolloff = 1.46 - (normalized_velocity * 0.34) - (register_position * 0.18);

        // Frequency-dependent damping via per-partial T60 targets.
        // T60 = time for a partial to decay 60 dB. Real pianos: upper partials
        // decay 10-50x faster than the fundamental, creating the signature
        // "bright attack → warm sustain" timbral evolution.
        let sr = sample_rate_hz as f32;
        let t60_base = note_physics.t60_base_s;
        let damping_coeff = note_physics.damping_coeff;

        let nyquist = sr / 2.0;
        let fade_start = nyquist * 0.80;

        for partial_index in 0..PARTIAL_COUNT {
            let harmonic = partial_index as f32 + 1.0;
            let strike_comb = (core::f32::consts::PI * harmonic * strike_position)
                .sin()
                .abs()
                .max(0.06);
            let softness = 1.0 / (1.0 + (soft_pedal_amount * harmonic * 0.30));
            let wound_weight = match note_physics.material_regime {
                StringMaterialRegime::CopperWoundBass => 0.12,
                StringMaterialRegime::PlainSteel => 0.0,
            };
            let gain = ((strike_comb / harmonic.powf(rolloff)) * softness)
                * (1.0 + (low_register_weight * 0.06) + wound_weight);

            // Per-partial decay: exponential T60 falloff with harmonic number.
            let t60 = t60_base * (-damping_coeff * harmonic).exp();
            // Convert T60 to per-sample pole radius: r = 10^(-3 / (T60 * sr))
            let mode_decay = 10.0_f32.powf(-3.0 / (t60 * sr)).clamp(0.99900, 0.999998);

            let damper_sensitivity = (0.00062
                + (harmonic * 0.00022)
                + (register_position * 0.00024)
                + match note_physics.material_regime {
                    StringMaterialRegime::CopperWoundBass => 0.00018,
                    StringMaterialRegime::PlainSteel => 0.0,
                })
            .clamp(0.00070, 0.0050);

            for (string_index, cents) in detune_cents
                .iter()
                .copied()
                .enumerate()
                .take(self.string_count)
            {
                let detune_ratio = 2.0_f32.powf(cents / 1200.0);
                // Correct inharmonicity: f_n = n * f0 * sqrt(1 + B*n²)
                let partial_hz = fundamental
                    * detune_ratio
                    * harmonic
                    * (1.0 + inharmonicity * harmonic * harmonic).sqrt();
                let anti_alias_gain = if partial_hz >= nyquist {
                    0.0
                } else if partial_hz > fade_start {
                    (nyquist - partial_hz) / (nyquist - fade_start)
                } else {
                    1.0
                };
                self.resonators[string_index][partial_index].configure(
                    partial_hz,
                    sample_rate_hz,
                    gain * anti_alias_gain,
                    mode_decay,
                    damper_sensitivity,
                );
            }
        }

        for string_index in self.string_count..MAX_STRING_COUNT {
            for resonator in &mut self.resonators[string_index] {
                resonator.reset();
            }
        }
    }

    pub(super) fn render(
        &mut self,
        harmonic_brightness: f32,
        excitation_drive: f32,
        string_transfer: f32,
        structural_launch: f32,
        external_bridge_feedback: f32,
    ) -> (f32, f32, f32) {
        let damper = self.damper.step(self.register_position);

        // Split the excitation path:
        // - resonator_drive is lightly DC-blocked to prevent runaway bias
        // - structural_launch keeps more of the low asymmetric strike energy
        //   so the note can still feel "thrown into" the instrument.
        let resonator_drive = self.excitation_dc_blocker.step(excitation_drive);
        let launch_drive = structural_launch + (excitation_drive * 0.16);

        let mut left = 0.0;
        let mut right = 0.0;
        let mut bridge_feedback = 0.0;
        let mut activity = 0.0;
        let free_motion = 0.22 + (damper.openness * 0.78);
        let reflected_bridge_drive = external_bridge_feedback
            * (0.18 + ((1.0 - self.register_position) * 0.12))
            * free_motion
            * (1.0 - (damper.choke * 0.72));
        let bridge_drive = (self.bridge_memory * 0.010 + reflected_bridge_drive * 0.020)
            * free_motion
            * (1.0 - (damper.choke * 0.58));

        for string_index in 0..self.string_count {
            let mut string_sample = 0.0;
            let string_excitation =
                resonator_drive * self.excitation_scale * (0.92 + (string_index as f32 * 0.04));
            let impact_transfer =
                string_transfer * self.excitation_scale * (1.00 + (string_index as f32 * 0.06));
            let pickup_position = 0.478 - (self.register_position * 0.038);

            for partial_index in 0..PARTIAL_COUNT {
                let harmonic = partial_index as f32 + 1.0;
                let bloom_weight = (harmonic / PARTIAL_COUNT as f32).powf(1.28);
                let spectral_tilt = (bloom_weight * 2.0) - 0.55;
                let brightness_boost =
                    (0.90 + (harmonic_brightness * spectral_tilt * 1.35)).max(0.45);
                let pickup_weight = (core::f32::consts::PI * harmonic * pickup_position)
                    .sin()
                    .abs()
                    .max(0.08)
                    .powf(0.82);
                let base_drive = 1.0 / (1.0 + (harmonic * 0.18));
                let bright_drive = 0.72 + (harmonic_brightness * (0.30 + (bloom_weight * 1.80)));
                let transfer_focus = 0.42 + (bloom_weight * 0.98);
                let transfer_drive = 0.78 + (harmonic_brightness * (0.28 + (bloom_weight * 1.25)));
                let resonator = &mut self.resonators[string_index][partial_index];
                let mode_drive = (string_excitation
                    * base_drive
                    * bright_drive
                    * resonator.gain
                    * 0.030)
                    + (impact_transfer * transfer_focus * transfer_drive * resonator.gain * 0.070)
                    + (bridge_drive * resonator.bridge_send * 0.0048);
                let mode_sample = resonator.step(mode_drive, damper.felt_contact);
                string_sample += mode_sample * brightness_boost * pickup_weight;
                bridge_feedback += mode_sample * resonator.bridge_send;
            }

            let damper_gain =
                (1.0 - (damper.choke * (0.76 + (self.register_position * 0.18)))).clamp(0.0, 1.0);
            let string_sample = string_sample
                * self.string_gain[string_index]
                * damper_gain
                * self.register_output_scale;
            activity += string_sample.abs();
            // Keyboard-position pan is applied in the mechanical layer (PianoVoice);
            // strings use only the unison-spread offset for stereo width.
            let keyboard_pan = 0.0;
            let unison_pan = (self.string_pan[string_index] - self.base_pan)
                * (1.20 + (self.register_position * 2.40));
            let string_pan = (keyboard_pan + unison_pan).clamp(-0.96, 0.96);
            left += string_sample * (1.0 - string_pan);
            right += string_sample * (1.0 + string_pan);
        }

        let soundboard_bloom = (self.bridge_memory
            * (0.028 + ((1.0 - self.register_position) * 0.026))
            * (0.62 + (damper.openness * 0.38))
            * (1.0 - (damper.choke * 0.36)))
            + (launch_drive * (0.040 + ((1.0 - self.register_position) * 0.018)));
        left += soundboard_bloom * 0.76;
        right += soundboard_bloom * 0.88;
        activity += soundboard_bloom.abs() * 0.75;

        let bridge_feedback = if self.string_count == 0 {
            0.0
        } else {
            bridge_feedback / self.string_count as f32
        };
        let bridge_retention = (0.66 + ((1.0 - self.register_position) * 0.12)
            - (damper.choke * 0.28))
            .clamp(0.10, 0.82);
        self.bridge_memory = (self.bridge_memory * bridge_retention)
            + (bridge_feedback * 0.030)
            + (launch_drive * (0.045 + ((1.0 - self.register_position) * 0.018)))
            + (reflected_bridge_drive * 0.028);
        self.last_activity = (self.last_activity * 0.90).max(activity + self.bridge_memory.abs());

        (left, right, bridge_feedback)
    }

    pub(super) fn set_damper_control(&mut self, key_is_down: bool, sustain_pedal_lift: f32) {
        self.damper.set_control(key_is_down, sustain_pedal_lift);
        self.bridge_memory *= 0.96;
        self.last_activity *= 0.985;
    }

    pub(super) fn activity(&self) -> f32 {
        self.last_activity
    }

    pub(super) fn reset(&mut self) {
        *self = Self::default();
    }
}

fn unison_spread_for_note(note: u8) -> f32 {
    let register_position = ((note as f32 - 21.0).max(0.0) / 87.0).clamp(0.0, 1.0);
    0.010 + (register_position * 0.090)
}

#[cfg(test)]
mod tests {
    use super::StringBank;

    #[test]
    fn middle_register_initializes_full_trichord_state() {
        let mut bass = StringBank::default();
        let mut middle = StringBank::default();

        bass.start(33, 100, 48_000, 0.0, 0.0);
        middle.start(60, 100, 48_000, 0.0, 0.0);

        assert_eq!(bass.string_count, 1);
        assert_eq!(middle.string_count, 3);
        assert!(middle.string_gain[1] > middle.string_gain[0]);
        assert!(middle.string_pan[0] < middle.base_pan);
        assert!(middle.string_pan[2] > middle.base_pan);
    }

    #[test]
    fn strings_keep_ringing_after_single_excitation_frame() {
        let mut bank = StringBank::default();
        bank.start(60, 110, 48_000, 0.0, 0.0);

        let first = bank.render(0.6, 0.8, 0.05, 0.04, 0.0).0.abs();
        let second = bank.render(0.2, 0.0, 0.0, 0.0, 0.0).0.abs();
        let third = bank.render(0.1, 0.0, 0.0, 0.0, 0.0).0.abs();

        assert!(first > 0.0);
        assert!(second > 0.0);
        assert!(third > 0.0);
    }
}
