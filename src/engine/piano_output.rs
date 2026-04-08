const BODY_MODE_COUNT: usize = 8;
const TAU: f32 = core::f32::consts::PI * 2.0;

#[derive(Debug, Clone, Copy)]
struct BodyMode {
    omega_cos: f32,
    radius: f32,
    coupling: f32,
    pan: f32,
    previous_1: f32,
    previous_2: f32,
}

impl Default for BodyMode {
    fn default() -> Self {
        Self {
            omega_cos: 0.0,
            radius: 0.0,
            coupling: 0.0,
            pan: 0.0,
            previous_1: 0.0,
            previous_2: 0.0,
        }
    }
}

impl BodyMode {
    fn configure(
        &mut self,
        frequency_hz: f32,
        sample_rate_hz: u32,
        decay: f32,
        coupling: f32,
        pan: f32,
    ) {
        let omega = TAU * frequency_hz / sample_rate_hz.max(1) as f32;
        self.omega_cos = omega.cos();
        self.radius = decay.clamp(0.0, 0.999995);
        self.coupling = coupling;
        self.pan = pan;
        self.previous_1 = 0.0;
        self.previous_2 = 0.0;
    }

    fn step(&mut self, drive: f32) -> f32 {
        let coeff_a = 2.0 * self.radius * self.omega_cos;
        let coeff_b = self.radius * self.radius;
        let current =
            ((coeff_a * self.previous_1) - (coeff_b * self.previous_2) + drive).clamp(-1.2, 1.2);
        self.previous_2 = self.previous_1;
        self.previous_1 = current;
        current
    }

    fn dampen(&mut self, amount: f32) {
        self.previous_1 *= amount;
        self.previous_2 *= amount;
    }
}

#[derive(Debug, Clone)]
pub(super) struct PianoOutputStage {
    body_state_left: f32,
    body_state_right: f32,
    body_modes: [BodyMode; BODY_MODE_COUNT],
    ambience_a_left: [f32; 1597],
    ambience_a_right: [f32; 1597],
    ambience_b_left: [f32; 2251],
    ambience_b_right: [f32; 2251],
    ambience_a_index: usize,
    ambience_b_index: usize,
    ambience_filter_left: f32,
    ambience_filter_right: f32,
}

impl PianoOutputStage {
    pub(super) fn new(sample_rate_hz: u32) -> Self {
        let body_frequencies = [92.0, 148.0, 224.0, 318.0, 452.0, 640.0, 904.0, 1280.0];
        let body_decays = [
            0.9983, 0.9986, 0.9988, 0.9990, 0.99915, 0.99925, 0.99935, 0.99945,
        ];
        let body_couplings = [0.012, 0.011, 0.0095, 0.0080, 0.0068, 0.0058, 0.0048, 0.0040];
        let body_pans = [-0.18, 0.14, -0.10, 0.09, -0.05, 0.06, -0.03, 0.02];
        let mut body_modes = [BodyMode::default(); BODY_MODE_COUNT];

        for (index, mode) in body_modes.iter_mut().enumerate() {
            mode.configure(
                body_frequencies[index],
                sample_rate_hz,
                body_decays[index],
                body_couplings[index],
                body_pans[index],
            );
        }

        Self {
            body_state_left: 0.0,
            body_state_right: 0.0,
            body_modes,
            ambience_a_left: [0.0; 1597],
            ambience_a_right: [0.0; 1597],
            ambience_b_left: [0.0; 2251],
            ambience_b_right: [0.0; 2251],
            ambience_a_index: 0,
            ambience_b_index: 0,
            ambience_filter_left: 0.0,
            ambience_filter_right: 0.0,
        }
    }

    pub(super) fn process(
        &mut self,
        mut frame_left: f32,
        mut frame_right: f32,
        body_gain: f32,
        ambience_gain: f32,
    ) -> (f32, f32) {
        let mono_drive = (frame_left + frame_right) * 0.5;
        let stereo_drive = frame_right - frame_left;

        self.body_state_left += 0.020 * (frame_left - self.body_state_left);
        self.body_state_right += 0.020 * (frame_right - self.body_state_right);

        let mut body_left =
            (self.body_state_left * 0.18) + (self.body_state_right * 0.06) + (mono_drive * 0.05);
        let mut body_right =
            (self.body_state_right * 0.18) + (self.body_state_left * 0.06) + (mono_drive * 0.05);

        for mode in &mut self.body_modes {
            let excite = soft_clip(
                (mono_drive * mode.coupling)
                    + (stereo_drive * mode.pan * 0.010)
                    + (((self.body_state_left + self.body_state_right) * 0.5)
                        * mode.coupling
                        * 0.35),
            ) * 0.65;
            let sample = mode.step(excite);
            body_left += sample * (1.0 - mode.pan) * 0.28;
            body_right += sample * (1.0 + mode.pan) * 0.28;
        }

        frame_left += body_left * body_gain * 0.16;
        frame_right += body_right * body_gain * 0.16;

        let ambience_input_left = (frame_left * 0.22) + (frame_right * 0.05);
        let ambience_input_right = (frame_right * 0.22) + (frame_left * 0.05);
        let (ambience_left, ambience_right) =
            self.render_ambience(ambience_input_left, ambience_input_right);
        frame_left += ambience_left * ambience_gain * 1.10;
        frame_right += ambience_right * ambience_gain * 1.10;

        (frame_left, frame_right)
    }

    pub(super) fn dampen_for_retrigger(&mut self) {
        self.body_state_left *= 0.28;
        self.body_state_right *= 0.28;
        for mode in &mut self.body_modes {
            mode.dampen(0.20);
        }
    }

    fn render_ambience(&mut self, input_left: f32, input_right: f32) -> (f32, f32) {
        let a_left = self.ambience_a_left[self.ambience_a_index];
        let a_right = self.ambience_a_right[self.ambience_a_index];
        let b_left = self.ambience_b_left[self.ambience_b_index];
        let b_right = self.ambience_b_right[self.ambience_b_index];
        let in_left = soft_clip(input_left * 0.40);
        let in_right = soft_clip(input_right * 0.40);
        let diffuse_left = (a_left * 0.46) + (b_left * 0.30) + (a_right * 0.14) + (b_right * 0.10);
        let diffuse_right = (a_right * 0.46) + (b_right * 0.30) + (a_left * 0.14) + (b_left * 0.10);
        self.ambience_filter_left += 0.14 * (diffuse_left - self.ambience_filter_left);
        self.ambience_filter_right += 0.14 * (diffuse_right - self.ambience_filter_right);

        self.ambience_a_left[self.ambience_a_index] = soft_clip(
            in_left + (self.ambience_filter_left * 0.74) + (self.ambience_filter_right * 0.06),
        );
        self.ambience_a_right[self.ambience_a_index] = soft_clip(
            in_right + (self.ambience_filter_right * 0.74) + (self.ambience_filter_left * 0.06),
        );
        self.ambience_b_left[self.ambience_b_index] = soft_clip(
            in_left + (self.ambience_filter_left * 0.68) + (self.ambience_filter_right * 0.08),
        );
        self.ambience_b_right[self.ambience_b_index] = soft_clip(
            in_right + (self.ambience_filter_right * 0.68) + (self.ambience_filter_left * 0.08),
        );

        self.ambience_a_index = (self.ambience_a_index + 1) % self.ambience_a_left.len();
        self.ambience_b_index = (self.ambience_b_index + 1) % self.ambience_b_left.len();

        (
            (self.ambience_filter_left * 0.78) + (diffuse_left * 0.22),
            (self.ambience_filter_right * 0.78) + (diffuse_right * 0.22),
        )
    }
}

pub(super) fn soft_clip(sample: f32) -> f32 {
    sample / (1.0 + sample.abs())
}
