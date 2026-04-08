use super::piano_strings::midi_note_hz;

const RESONANCE_MODE_COUNT: usize = 12;
const TAU: f32 = core::f32::consts::PI * 2.0;

#[derive(Debug, Clone, Copy)]
struct ResonanceMode {
    omega_cos: f32,
    base_radius: f32,
    pan: f32,
    previous_1: f32,
    previous_2: f32,
    drive: f32,
}

impl Default for ResonanceMode {
    fn default() -> Self {
        Self {
            omega_cos: 0.0,
            base_radius: 0.0,
            pan: 0.0,
            previous_1: 0.0,
            previous_2: 0.0,
            drive: 0.0,
        }
    }
}

impl ResonanceMode {
    fn configure(&mut self, hz: f32, sample_rate_hz: u32, decay: f32, pan: f32) {
        let omega = TAU * hz / sample_rate_hz.max(1) as f32;
        self.omega_cos = omega.cos();
        self.base_radius = decay.clamp(0.0, 0.999995);
        self.pan = pan;
        self.previous_1 = 0.0;
        self.previous_2 = 0.0;
        self.drive = 0.0;
    }

    fn step(&mut self, sustain_pedal_down: bool) -> f32 {
        let radius = if sustain_pedal_down {
            (self.base_radius + 0.00012).min(0.999995)
        } else {
            self.base_radius
        };
        let coeff_a = 2.0 * radius * self.omega_cos;
        let coeff_b = radius * radius;
        let current = ((coeff_a * self.previous_1) - (coeff_b * self.previous_2) + self.drive)
            .clamp(-0.85, 0.85);
        self.previous_2 = self.previous_1;
        self.previous_1 = current;
        let drive_decay = if sustain_pedal_down { 0.990 } else { 0.970 };
        self.drive *= drive_decay;
        current
    }

    fn dampen(&mut self, amount: f32) {
        self.previous_1 *= amount;
        self.previous_2 *= amount;
        self.drive *= amount;
    }
}

#[derive(Debug, Clone)]
pub(super) struct ResonanceBank {
    modes: [ResonanceMode; RESONANCE_MODE_COUNT],
}

impl ResonanceBank {
    pub(super) fn new(sample_rate_hz: u32) -> Self {
        let mut modes = [ResonanceMode::default(); RESONANCE_MODE_COUNT];
        for (index, mode) in modes.iter_mut().enumerate() {
            let note = 48 + index as u8;
            let hz = midi_note_hz(note);
            let pan = ((index as f32 - 5.5) / 5.5) * 0.08;
            let decay = 0.99918 + ((index as f32 / (RESONANCE_MODE_COUNT - 1) as f32) * 0.00020);
            mode.configure(hz, sample_rate_hz, decay, pan);
        }

        Self { modes }
    }

    pub(super) fn excite(&mut self, note: u8, velocity: u8, sustain_pedal_down: bool) {
        let velocity_norm = velocity as f32 / 127.0;
        let register_position = ((note as f32 - 21.0).max(0.0) / 87.0).clamp(0.0, 1.0);
        let register_scale = 1.00 - (register_position * 0.20);
        let pitch_class = (note as usize) % RESONANCE_MODE_COUNT;
        let pedal_scale = if sustain_pedal_down { 0.58 } else { 1.0 };
        let related_modes = [
            (
                pitch_class,
                (0.0045 + (velocity_norm * 0.0080)) * register_scale * pedal_scale,
            ),
            (
                (pitch_class + 7) % RESONANCE_MODE_COUNT,
                (0.0016 + (velocity_norm * 0.0030)) * register_scale * pedal_scale,
            ),
            (
                (pitch_class + 12) % RESONANCE_MODE_COUNT,
                (0.0012 + (velocity_norm * 0.0022)) * register_scale * pedal_scale,
            ),
            (
                (pitch_class + 4) % RESONANCE_MODE_COUNT,
                (0.0009 + (velocity_norm * 0.0018)) * register_scale * pedal_scale,
            ),
        ];

        for (mode_index, amount) in related_modes {
            self.modes[mode_index].drive = (self.modes[mode_index].drive + amount).min(0.08);
        }

        if sustain_pedal_down {
            self.modes[pitch_class].drive = (self.modes[pitch_class].drive
                + (0.0024 * velocity_norm * register_scale))
                .min(0.10);
        }
    }

    pub(super) fn dampen_for_retrigger(&mut self, note: u8) {
        let pitch_class = (note as usize) % RESONANCE_MODE_COUNT;
        let related_modes = [
            pitch_class,
            (pitch_class + 7) % RESONANCE_MODE_COUNT,
            (pitch_class + 12) % RESONANCE_MODE_COUNT,
            (pitch_class + 4) % RESONANCE_MODE_COUNT,
        ];

        for mode_index in related_modes {
            self.modes[mode_index].dampen(0.0);
        }
    }

    pub(super) fn render(&mut self, sustain_pedal_down: bool) -> (f32, f32) {
        let mut left = 0.0;
        let mut right = 0.0;
        let total_drive: f32 = self.modes.iter().map(|mode| mode.drive.abs()).sum();
        let overload = ((total_drive - 0.24).max(0.0) * 0.10).clamp(0.0, 0.10);

        for mode in &mut self.modes {
            if mode.drive.abs() < 1.0e-6
                && mode.previous_1.abs() < 1.0e-6
                && mode.previous_2.abs() < 1.0e-6
            {
                mode.drive = 0.0;
                continue;
            }

            if overload > 0.0 {
                mode.drive *= 1.0 - overload;
            }
            let sample = mode.step(sustain_pedal_down);
            left += sample * (1.0 - mode.pan) * 0.28;
            right += sample * (1.0 + mode.pan) * 0.28;
        }

        (left, right)
    }
}
