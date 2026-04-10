use vb_piano_physics::{
    bridge_junction::{BridgeInput, BridgeJunction, BridgeJunctionConfig},
    soft_clip,
    sympathetic::{SympatheticConfig, SympatheticInput, SympatheticReceptors},
};

#[derive(Debug, Clone, Copy, Default)]
pub(super) struct BridgeFrame {
    pub(super) left: f32,
    pub(super) right: f32,
    pub(super) reflected_string_force: f32,
    pub(super) stored_energy: f32,
}

#[derive(Debug, Clone)]
pub(super) struct BridgeNetwork {
    bridge_fast: f32,
    bridge_slow: f32,
    bridge_energy: f32,
    projection_fast: f32,
    projection_slow: f32,
    projection_energy: f32,
    junction: BridgeJunction,
    sympathetic: SympatheticReceptors,
    bridge_feedback_gain: f32,
    ambience_a_left: [f32; 1597],
    ambience_a_right: [f32; 1597],
    ambience_b_left: [f32; 2251],
    ambience_b_right: [f32; 2251],
    ambience_a_index: usize,
    ambience_b_index: usize,
    ambience_filter_left: f32,
    ambience_filter_right: f32,
}

impl BridgeNetwork {
    pub(super) fn new(sample_rate_hz: u32) -> Self {
        Self {
            bridge_fast: 0.0,
            bridge_slow: 0.0,
            bridge_energy: 0.0,
            projection_fast: 0.0,
            projection_slow: 0.0,
            projection_energy: 0.0,
            junction: BridgeJunction::new(BridgeJunctionConfig {
                sample_rate_hz,
                downbearing_preload: 0.62,
                bridge_to_soundboard_coupling: 0.88,
                plate_leak: 0.032,
                soundboard_modal_gain: 1.0,
                soundboard_radiation_width: 1.0,
            }),
            sympathetic: SympatheticReceptors::new(SympatheticConfig {
                sample_rate_hz,
                coupling_gain: 1.0,
            }),
            bridge_feedback_gain: 0.36,
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

    pub(super) fn set_physical_controls(
        &mut self,
        bridge_feedback_gain: f32,
        downbearing_preload: f32,
        plate_leak: f32,
        soundboard_width: f32,
        sympathetic_gain: f32,
    ) {
        self.bridge_feedback_gain = bridge_feedback_gain.clamp(0.0, 1.5);
        self.junction
            .set_controls(downbearing_preload, 0.88, plate_leak, 1.0, soundboard_width);
        self.sympathetic.set_coupling_gain(sympathetic_gain);
    }

    pub(super) fn note_on(&mut self, note: u8, velocity: u8, sustain_pedal_down: bool) {
        self.sympathetic
            .note_on(note, velocity, if sustain_pedal_down { 1.0 } else { 0.0 });
    }

    pub(super) fn dampen_for_retrigger(&mut self, note: u8) {
        self.sympathetic.dampen_for_retrigger(note);
        self.bridge_fast *= 0.20;
        self.bridge_slow *= 0.20;
        self.bridge_energy *= 0.18;
        self.projection_fast *= 0.24;
        self.projection_slow *= 0.24;
        self.projection_energy *= 0.18;
        self.junction.dampen(0.82);
    }

    #[allow(clippy::too_many_arguments)]
    pub(super) fn process(
        &mut self,
        strings_left: f32,
        strings_right: f32,
        mechanical_left: f32,
        mechanical_right: f32,
        bridge_drive: f32,
        sustain_pedal_lift: f32,
        resonance_gain: f32,
        body_gain: f32,
        ambience_gain: f32,
    ) -> BridgeFrame {
        let mut left = strings_left + mechanical_left;
        let mut right = strings_right + mechanical_right;
        let sustain_pedal_lift = sustain_pedal_lift.clamp(0.0, 1.0);

        let string_mono = (strings_left + strings_right) * 0.5;
        let string_stereo = strings_right - strings_left;
        let bridge_input = soft_clip(((bridge_drive * 0.72) + (string_mono * 0.05)) * 1.8) * 0.55;

        self.bridge_fast += 0.22 * (bridge_input - self.bridge_fast);
        self.bridge_slow += 0.018 * (bridge_input - self.bridge_slow);
        let bridge_band = self.bridge_fast - self.bridge_slow;
        let bridge_energy_target = bridge_band.abs();
        let bridge_response = if bridge_energy_target > self.bridge_energy {
            0.060
        } else {
            0.008
        };
        self.bridge_energy += bridge_response * (bridge_energy_target - self.bridge_energy);

        let junction_frame = self.junction.process(BridgeInput {
            string_force: bridge_input,
            string_velocity: bridge_band,
            string_mono,
            string_side: string_stereo,
            damper_lift: sustain_pedal_lift,
        });

        let sympathetic = self.sympathetic.process(SympatheticInput {
            bridge_slow: self.bridge_slow,
            bridge_band,
            receptor_drive: junction_frame.receptor_drive,
            bridge_energy: junction_frame.stored_energy,
            damper_lift: sustain_pedal_lift,
        });
        left += sympathetic.left * resonance_gain;
        right += sympathetic.right * resonance_gain;

        self.projection_fast += 0.24 * (bridge_input - self.projection_fast);
        self.projection_slow += 0.040 * (bridge_input - self.projection_slow);
        let projection_band = self.projection_fast - self.projection_slow;
        let projection_energy_target = projection_band.abs();
        let projection_response = if projection_energy_target > self.projection_energy {
            0.08
        } else {
            0.018
        };
        self.projection_energy +=
            projection_response * (projection_energy_target - self.projection_energy);

        let bridge_projection = soft_clip(
            (bridge_input * 0.10)
                + (projection_band * (0.20 + (self.projection_energy * 0.80)))
                + (bridge_band * 0.04)
                + (junction_frame.bridge_motion * 0.72),
        );
        left += (bridge_projection - (string_stereo * 0.010)) * 0.14;
        right += (bridge_projection + (string_stereo * 0.010)) * 0.14;

        let body_left = junction_frame.soundboard_left;
        let body_right = junction_frame.soundboard_right;
        left += body_left * body_gain * 0.62;
        right += body_right * body_gain * 0.62;

        let ambience_input_left = (body_left * 0.15) + (body_right * 0.03) + (left * 0.04);
        let ambience_input_right = (body_right * 0.15) + (body_left * 0.03) + (right * 0.04);
        let (ambience_left, ambience_right) =
            self.render_ambience(ambience_input_left, ambience_input_right);
        left += ambience_left * ambience_gain * 1.10;
        right += ambience_right * ambience_gain * 1.10;

        BridgeFrame {
            left,
            right,
            reflected_string_force: junction_frame.reflected_string_force
                * self.bridge_feedback_gain,
            stored_energy: junction_frame.stored_energy + sympathetic.energy,
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

#[cfg(test)]
mod tests {
    #[test]
    fn body_path_preserves_stereo_difference() {
        let mut bridge = super::BridgeNetwork::new(48_000);
        let mut side_energy = 0.0;

        for _ in 0..128 {
            let frame = bridge.process(0.18, 0.34, 0.0, 0.0, 0.12, 0.0, 0.0, 1.0, 0.0);
            side_energy += (frame.left - frame.right).abs();
        }

        assert!(side_energy > 0.5);
    }
}
