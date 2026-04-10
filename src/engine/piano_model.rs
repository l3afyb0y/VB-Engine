use super::{
    EngineConfig, RenderDebugFrame, RenderMode,
    piano_bridge::BridgeNetwork,
    piano_voice::{PianoVoice, VoiceLayers},
};

#[derive(Debug, Clone)]
pub(super) struct PianoModel {
    voices: Vec<PianoVoice>,
    bridge: BridgeNetwork,
    sustain_pedal_lift: f32,
    soft_pedal_amount: f32,
    bridge_reflection: f32,
    bridge_energy: f32,
}

impl PianoModel {
    pub(super) fn new(config: EngineConfig) -> Self {
        let mut voices = Vec::with_capacity(config.max_voices);
        voices.resize_with(config.max_voices, PianoVoice::default);

        Self {
            voices,
            bridge: BridgeNetwork::new(config.sample_rate_hz),
            sustain_pedal_lift: 0.0,
            soft_pedal_amount: 0.0,
            bridge_reflection: 0.0,
            bridge_energy: 0.0,
        }
    }

    pub(super) fn note_on(
        &mut self,
        note: u8,
        velocity: u8,
        sample_rate_hz: u32,
        hammer_hardness: f32,
    ) -> Option<f32> {
        let retriggering_same_note = self
            .voices
            .iter()
            .any(|voice| voice.is_active() && voice.note() == note);
        if retriggering_same_note {
            self.bridge.dampen_for_retrigger(note);
        }
        let (slot_index, stolen_energy) = self.pick_voice_slot(note);
        let voice = &mut self.voices[slot_index];
        voice.start(
            note,
            velocity,
            sample_rate_hz,
            self.soft_pedal_amount,
            hammer_hardness,
        );
        let soft_bridge_scale = 1.0 - (self.soft_pedal_amount * 0.60);
        let bridge_velocity = if retriggering_same_note {
            ((velocity as f32 * 0.25 * soft_bridge_scale) as u8).max(1)
        } else {
            ((velocity as f32 * soft_bridge_scale) as u8).max(1)
        };
        self.bridge
            .note_on(note, bridge_velocity, self.sustain_pedal_is_down());
        stolen_energy
    }

    pub(super) fn note_off(&mut self, note: u8) {
        for voice in self
            .voices
            .iter_mut()
            .filter(|voice| voice.is_active() && voice.note() == note)
        {
            voice.note_off(self.sustain_pedal_lift);
        }
    }

    pub(super) fn set_sustain_pedal(&mut self, is_down: bool) {
        self.set_sustain_pedal_lift(if is_down { 1.0 } else { 0.0 });
    }

    pub(super) fn set_sustain_pedal_lift(&mut self, amount: f32) {
        self.sustain_pedal_lift = amount.clamp(0.0, 1.0);
        for voice in &mut self.voices {
            voice.set_sustain_pedal_lift(self.sustain_pedal_lift);
        }
    }

    pub(super) fn set_soft_pedal(&mut self, amount: f32) {
        self.soft_pedal_amount = amount.clamp(0.0, 1.0);
    }

    pub(super) fn render_frame(
        &mut self,
        config: &EngineConfig,
        render_mode: RenderMode,
        low_frequency_mono_collapse_override: Option<f32>,
    ) -> (f32, f32, RenderDebugFrame) {
        self.bridge.set_physical_controls(
            config.bridge_feedback_gain,
            config.downbearing_preload,
            config.plate_leak,
            config.soundboard_width,
            config.resonance_gain,
            low_frequency_mono_collapse_override.unwrap_or(0.78),
        );

        let mut strings_left = 0.0;
        let mut strings_right = 0.0;
        let mut mechanical_left = 0.0;
        let mut mechanical_right = 0.0;
        let mut bridge_drive = 0.0;
        let active_voice_count = self.active_voice_count().max(1) as f32;
        let per_voice_reflection = (self.bridge_reflection
            * (0.85 + self.bridge_energy.clamp(0.0, 1.0) * 0.15))
            / active_voice_count;

        for voice in &mut self.voices {
            let VoiceLayers {
                strings_left: voice_strings_left,
                strings_right: voice_strings_right,
                mechanical_left: voice_mechanical_left,
                mechanical_right: voice_mechanical_right,
                bridge_drive: voice_bridge_drive,
            } = voice.render(config.hammer_noise_gain, per_voice_reflection);
            let voice_strings_left = voice_strings_left * config.string_gain;
            let voice_strings_right = voice_strings_right * config.string_gain;
            let voice_mechanical_left = voice_mechanical_left * config.mechanical_gain;
            let voice_mechanical_right = voice_mechanical_right * config.mechanical_gain;
            bridge_drive += voice_bridge_drive * config.string_gain;
            strings_left += voice_strings_left;
            strings_right += voice_strings_right;
            mechanical_left += voice_mechanical_left;
            mechanical_right += voice_mechanical_right;
        }

        let bridge_frame = self.bridge.process(
            strings_left,
            strings_right,
            mechanical_left,
            mechanical_right,
            bridge_drive,
            self.sustain_pedal_lift,
            config.resonance_gain,
            config.body_gain,
            config.ambience_gain,
            render_mode,
        );
        self.bridge_reflection = bridge_frame.reflected_string_force;
        self.bridge_energy = bridge_frame.stored_energy;

        (
            bridge_frame.left,
            bridge_frame.right,
            RenderDebugFrame {
                strings_left,
                strings_right,
                mechanical_left,
                mechanical_right,
                bridge_left: bridge_frame.bridge_left,
                bridge_right: bridge_frame.bridge_right,
                body_left: bridge_frame.body_left,
                body_right: bridge_frame.body_right,
                ambience_left: bridge_frame.ambience_left,
                ambience_right: bridge_frame.ambience_right,
                bridge_drive,
                bridge_reflection: bridge_frame.reflected_string_force,
                bridge_energy: bridge_frame.stored_energy,
                projection_energy: bridge_frame.projection_energy,
                low_board_motion: bridge_frame.low_board_motion,
                mid_board_motion: bridge_frame.mid_board_motion,
                air_board_motion: bridge_frame.air_board_motion,
                side_board_motion: bridge_frame.side_board_motion,
                ..RenderDebugFrame::default()
            },
        )
    }

    pub(super) fn active_voice_count(&self) -> usize {
        self.voices.iter().filter(|voice| voice.is_active()).count()
    }

    fn pick_voice_slot(&self, note: u8) -> (usize, Option<f32>) {
        if let Some(index) = self
            .voices
            .iter()
            .position(|voice| voice.is_active() && voice.note() == note)
        {
            return (index, None);
        }

        if let Some(index) = self.voices.iter().position(|voice| !voice.is_active()) {
            return (index, None);
        }

        let (index, voice) = self
            .voices
            .iter()
            .enumerate()
            .min_by(|(_, left), (_, right)| {
                left.activity()
                    .partial_cmp(&right.activity())
                    .unwrap_or(core::cmp::Ordering::Equal)
            })
            .expect("engine always has at least one voice");
        (index, Some(voice.activity()))
    }

    fn sustain_pedal_is_down(&self) -> bool {
        self.sustain_pedal_lift >= 0.5
    }
}
