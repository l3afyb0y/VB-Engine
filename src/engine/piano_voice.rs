use super::{
    piano_hammer::{HammerModel, StrikeProfile},
    piano_strings::StringBank,
};

#[derive(Debug, Clone, Copy, Default)]
pub(super) struct VoiceLayers {
    pub(super) strings_left: f32,
    pub(super) strings_right: f32,
    pub(super) mechanical_left: f32,
    pub(super) mechanical_right: f32,
    pub(super) bridge_drive: f32,
}

#[derive(Debug, Clone, Default)]
pub(super) struct PianoVoice {
    active: bool,
    note: u8,
    key_is_down: bool,
    released: bool,
    sustained: bool,
    strike_age_frames: u32,
    activity: f32,
    quiet_frames: u32,
    output_gain: f32,
    pan: f32,
    last_string_feedback: f32,
    hammer: HammerModel,
    strings: StringBank,
}

impl PianoVoice {
    pub(super) fn is_active(&self) -> bool {
        self.active
    }

    pub(super) fn note(&self) -> u8 {
        self.note
    }

    pub(super) fn activity(&self) -> f32 {
        self.activity
    }

    pub(super) fn start(
        &mut self,
        note: u8,
        velocity: u8,
        sample_rate_hz: u32,
        soft_pedal_amount: f32,
        hammer_hardness: f32,
    ) {
        let profile = StrikeProfile::from_note(note, velocity, soft_pedal_amount);

        self.active = true;
        self.note = note;
        self.key_is_down = true;
        self.released = false;
        self.sustained = false;
        self.strike_age_frames = 0;
        self.activity = profile.initial_activity;
        self.quiet_frames = 0;
        self.output_gain = profile.output_gain;
        self.pan = profile.pan;
        self.last_string_feedback = 0.0;

        self.hammer.start(
            note,
            velocity,
            sample_rate_hz,
            soft_pedal_amount,
            hammer_hardness,
        );
        self.strings
            .start(note, velocity, sample_rate_hz, soft_pedal_amount, self.pan);
    }

    pub(super) fn note_off(&mut self, sustain_pedal_lift: f32) {
        if !self.active {
            return;
        }

        self.key_is_down = false;
        self.set_sustain_pedal_lift(sustain_pedal_lift);
    }

    pub(super) fn set_sustain_pedal_lift(&mut self, sustain_pedal_lift: f32) {
        if !self.active {
            return;
        }

        let sustain_pedal_lift = sustain_pedal_lift.clamp(0.0, 1.0);
        self.strings
            .set_damper_control(self.key_is_down, sustain_pedal_lift);
        self.sustained = !self.key_is_down && sustain_pedal_lift > 0.02;
        self.released = !self.key_is_down && !self.sustained;
    }

    pub(super) fn render(
        &mut self,
        hammer_noise_gain: f32,
        external_bridge_feedback: f32,
    ) -> VoiceLayers {
        if !self.active {
            return VoiceLayers::default();
        }

        let hammer_frame = self
            .hammer
            .step(self.last_string_feedback, hammer_noise_gain, self.pan);
        let strike_progress = (self.strike_age_frames as f32 / 192.0).clamp(0.0, 1.0);
        let hammer_presence = (1.0 - (strike_progress * 0.96)).clamp(0.03, 1.0);
        let string_presence = 1.00 + (strike_progress * 0.03);
        let (mut left, mut right, string_feedback) = self.strings.render(
            hammer_frame.harmonic_brightness,
            hammer_frame.excitation_drive,
            hammer_frame.string_transfer,
            hammer_frame.structural_launch,
            external_bridge_feedback,
        );
        let free_string_gain = 1.0 - (hammer_frame.contact_damping * 0.08);
        left *= free_string_gain;
        right *= free_string_gain;
        left *= string_presence;
        right *= string_presence;
        let transient_shape =
            1.0 + (hammer_frame.impact_emphasis * (0.010 + (0.010 * hammer_presence)));
        left *= transient_shape;
        right *= transient_shape;
        self.last_string_feedback = string_feedback;
        let strings_left = left * self.output_gain;
        let strings_right = right * self.output_gain;
        let register_position = ((self.note as f32 - 21.0).max(0.0) / 87.0).clamp(0.0, 1.0);
        let bridge_drive = string_feedback * self.output_gain * (0.48 + (register_position * 0.52));
        let direct_impulse = hammer_frame.direct_impulse * (0.006 * hammer_presence);
        let mut mechanical_left = direct_impulse * (0.72 - (self.pan * 0.12));
        let mut mechanical_right = direct_impulse * (0.72 + (self.pan * 0.12));
        let attack_left = hammer_frame.attack_left * (0.13 * hammer_presence);
        let attack_right = hammer_frame.attack_right * (0.13 * hammer_presence);
        mechanical_left += attack_left;
        mechanical_right += attack_right;
        mechanical_left *= self.output_gain;
        mechanical_right *= self.output_gain;
        self.strike_age_frames = self.strike_age_frames.saturating_add(1);

        let output_left = strings_left + mechanical_left;
        let output_right = strings_right + mechanical_right;
        let frame_activity = output_left.abs().max(output_right.abs());
        let physical_activity =
            self.strings.activity().max(self.hammer.activity()) * self.output_gain;
        self.activity = (self.activity * 0.90).max(frame_activity.max(physical_activity));
        let quiet_enough = frame_activity < 0.00040;
        if self.released && quiet_enough {
            self.quiet_frames = self.quiet_frames.saturating_add(1);
        } else {
            self.quiet_frames = 0;
        }

        if self.released && (self.activity < 0.00030 || self.quiet_frames > 512) {
            self.reset();
            return VoiceLayers::default();
        }

        VoiceLayers {
            strings_left,
            strings_right,
            mechanical_left,
            mechanical_right,
            bridge_drive,
        }
    }

    fn reset(&mut self) {
        self.active = false;
        self.key_is_down = false;
        self.released = false;
        self.sustained = false;
        self.strike_age_frames = 0;
        self.activity = 0.0;
        self.quiet_frames = 0;
        self.output_gain = 0.0;
        self.pan = 0.0;
        self.last_string_feedback = 0.0;
        self.hammer.reset();
        self.strings.reset();
    }
}
