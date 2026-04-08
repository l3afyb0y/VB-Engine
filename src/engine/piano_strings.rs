const PARTIAL_COUNT: usize = 12;
const MAX_STRING_COUNT: usize = 3;
const TAU: f32 = core::f32::consts::PI * 2.0;

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
        let current = (coeff_a * self.previous_1) - (coeff_b * self.previous_2) + drive;
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
    damper_position: f32,
    damper_target: f32,
    last_activity: f32,
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
            damper_position: 0.0,
            damper_target: 0.0,
            last_activity: 0.0,
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
        let register_position = ((note as f32 - 21.0).max(0.0) / 87.0).clamp(0.0, 1.0);
        let low_register_weight = 1.0 - register_position;

        self.bridge_memory = 0.0;
        self.string_count = string_count_for_note(note);
        self.register_position = register_position;
        self.base_pan = pan;
        self.excitation_scale = (0.38 + (register_position * 0.96) + (low_register_weight * 0.08))
            * (0.78 + (normalized_velocity * 0.38))
            * (1.0 - (0.52 * soft_pedal_amount));
        self.damper_position = 0.0;
        self.damper_target = 0.0;
        self.last_activity = 0.0;
        let spread = unison_spread_for_note(note);
        let detune_cents = match self.string_count {
            1 => [0.0, 0.0, 0.0],
            2 => [-3.4, 3.8, 0.0],
            _ => [-6.0, 0.0, 4.8],
        };
        let string_balance = match self.string_count {
            1 => [1.0, 0.0, 0.0],
            2 => [0.62, 0.62, 0.0],
            _ => [0.48, 0.06, 0.46],
        };
        let string_positions = match self.string_count {
            1 => [0.0, 0.0, 0.0],
            2 => [-1.1, 1.1, 0.0],
            _ => [-1.35, 0.0, 1.35],
        };

        for string_index in 0..self.string_count {
            self.string_gain[string_index] = string_balance[string_index];
            self.string_pan[string_index] = pan + (string_positions[string_index] * spread);
        }
        for string_index in self.string_count..MAX_STRING_COUNT {
            self.string_gain[string_index] = 0.0;
            self.string_pan[string_index] = pan;
        }

        let fundamental = midi_note_hz(note);
        let inharmonicity = 0.00010 + ((1.0 - register_position) * 0.0018);
        let strike_position = 0.12 + (register_position * 0.08) + (0.010 * soft_pedal_amount);
        let rolloff = 1.46 - (normalized_velocity * 0.34) - (register_position * 0.18);
        let resonator_decay = 0.999997 - (register_position * 0.000060);

        for partial_index in 0..PARTIAL_COUNT {
            let harmonic = partial_index as f32 + 1.0;
            let strike_comb = (core::f32::consts::PI * harmonic * strike_position)
                .sin()
                .abs()
                .max(0.06);
            let softness = 1.0 / (1.0 + (soft_pedal_amount * harmonic * 0.30));
            let gain = ((strike_comb / harmonic.powf(rolloff)) * softness)
                * (1.0 + (low_register_weight * 0.06));
            let mode_decay = (resonator_decay - (harmonic * 0.0000026)).clamp(0.99993, 0.999998);
            let damper_sensitivity =
                (0.00062 + (harmonic * 0.00022) + (register_position * 0.00024))
                    .clamp(0.00070, 0.0050);

            for (string_index, cents) in detune_cents
                .iter()
                .copied()
                .enumerate()
                .take(self.string_count)
            {
                let detune_ratio = 2.0_f32.powf(cents / 1200.0);
                let partial_hz = fundamental
                    * detune_ratio
                    * harmonic
                    * (1.0 + (inharmonicity * harmonic * harmonic));
                self.resonators[string_index][partial_index].configure(
                    partial_hz,
                    sample_rate_hz,
                    gain,
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
    ) -> (f32, f32, f32) {
        self.update_damper();

        let mut left = 0.0;
        let mut right = 0.0;
        let mut bridge_feedback = 0.0;
        let mut activity = 0.0;
        let bridge_drive = self.bridge_memory * (0.007 * (1.0 - (self.damper_position * 0.92)));

        for string_index in 0..self.string_count {
            let mut string_sample = 0.0;
            let string_excitation =
                excitation_drive * self.excitation_scale * (0.92 + (string_index as f32 * 0.04));
            let impact_transfer =
                string_transfer * self.excitation_scale * (1.00 + (string_index as f32 * 0.06));

            for partial_index in 0..PARTIAL_COUNT {
                let harmonic = partial_index as f32 + 1.0;
                let bloom_weight = (harmonic / PARTIAL_COUNT as f32).powf(1.28);
                let spectral_tilt = (bloom_weight * 2.0) - 0.55;
                let brightness_boost =
                    (0.90 + (harmonic_brightness * spectral_tilt * 1.35)).max(0.45);
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
                    + (impact_transfer * transfer_focus * transfer_drive * resonator.gain * 0.058)
                    + (bridge_drive * resonator.bridge_send * 0.004);
                let mode_sample = resonator.step(mode_drive, self.damper_position);
                string_sample += mode_sample * brightness_boost;
                bridge_feedback += mode_sample * resonator.bridge_send;
            }

            let damper_gain = (1.0
                - (self.damper_position * (0.96 + (self.register_position * 0.24))))
                .clamp(0.0, 1.0);
            let register_output_scale = 0.62 + (self.register_position * 1.08);
            let string_sample = string_sample
                * self.string_gain[string_index]
                * damper_gain
                * register_output_scale;
            activity += string_sample.abs();
            let keyboard_pan = 0.0;
            let unison_pan = (self.string_pan[string_index] - self.base_pan)
                * (1.55 + (self.register_position * 4.80));
            let string_pan = (keyboard_pan + unison_pan).clamp(-0.96, 0.96);
            left += string_sample * (1.0 - string_pan);
            right += string_sample * (1.0 + string_pan);
        }

        let soundboard_bloom = self.bridge_memory
            * (0.022 + ((1.0 - self.register_position) * 0.022))
            * (1.0 - (self.damper_position * 0.75));
        left += soundboard_bloom * 0.76;
        right += soundboard_bloom * 0.88;
        activity += soundboard_bloom.abs() * 0.75;

        let bridge_feedback = if self.string_count == 0 {
            0.0
        } else {
            bridge_feedback / self.string_count as f32
        };
        let bridge_retention = (0.64 + ((1.0 - self.register_position) * 0.16)
            - (self.damper_position * 0.52))
            .clamp(0.10, 0.86);
        self.bridge_memory = (self.bridge_memory * bridge_retention) + (bridge_feedback * 0.026);
        self.last_activity = (self.last_activity * 0.90).max(activity + self.bridge_memory.abs());

        (left, right, bridge_feedback)
    }

    pub(super) fn note_off(&mut self) {
        self.damper_position = self.damper_position.max(0.12);
        self.damper_target = 1.0;
        self.bridge_memory *= 0.90;
        self.last_activity *= 0.97;
    }

    pub(super) fn activity(&self) -> f32 {
        self.last_activity
    }

    pub(super) fn reset(&mut self) {
        *self = Self::default();
    }

    fn update_damper(&mut self) {
        let response = if self.damper_target > self.damper_position {
            0.08
        } else {
            0.03
        };
        self.damper_position += (self.damper_target - self.damper_position) * response;
    }
}

pub(super) fn midi_note_hz(note: u8) -> f32 {
    440.0 * 2.0_f32.powf((note as f32 - 69.0) / 12.0)
}

fn string_count_for_note(note: u8) -> usize {
    if note < 52 {
        1
    } else if note < 68 {
        2
    } else {
        3
    }
}

fn unison_spread_for_note(note: u8) -> f32 {
    let register_position = ((note as f32 - 21.0).max(0.0) / 87.0).clamp(0.0, 1.0);
    0.012 + (register_position * 0.280)
}

#[cfg(test)]
mod tests {
    use super::StringBank;

    fn stereo_width(bank: &mut StringBank, brightness: f32, drive: f32) -> f32 {
        let (left, right, _) = bank.render(brightness, drive, 0.0);
        (left - right).abs()
    }

    #[test]
    fn upper_register_uses_more_unison_width() {
        let mut low = StringBank::default();
        let mut high = StringBank::default();

        low.start(45, 100, 48_000, 0.0, 0.0);
        high.start(76, 100, 48_000, 0.0, 0.0);

        assert!(stereo_width(&mut high, 0.4, 0.3) > stereo_width(&mut low, 0.4, 0.3));
    }

    #[test]
    fn strings_keep_ringing_after_single_excitation_frame() {
        let mut bank = StringBank::default();
        bank.start(60, 110, 48_000, 0.0, 0.0);

        let first = bank.render(0.6, 0.8, 0.05).0.abs();
        let second = bank.render(0.2, 0.0, 0.0).0.abs();
        let third = bank.render(0.1, 0.0, 0.0).0.abs();

        assert!(first > 0.0);
        assert!(second > 0.0);
        assert!(third > 0.0);
    }
}
