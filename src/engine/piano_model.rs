use super::{
    EngineConfig,
    piano_output::PianoOutputStage,
    piano_resonance::ResonanceBank,
    piano_voice::{PianoVoice, VoiceLayers},
};

#[derive(Debug, Clone)]
pub(super) struct PianoModel {
    voices: Vec<PianoVoice>,
    resonance: ResonanceBank,
    output_stage: PianoOutputStage,
    sustain_pedal_down: bool,
    soft_pedal_amount: f32,
}

impl PianoModel {
    pub(super) fn new(config: EngineConfig) -> Self {
        let mut voices = Vec::with_capacity(config.max_voices);
        voices.resize_with(config.max_voices, PianoVoice::default);

        Self {
            voices,
            resonance: ResonanceBank::new(config.sample_rate_hz),
            output_stage: PianoOutputStage::new(config.sample_rate_hz),
            sustain_pedal_down: false,
            soft_pedal_amount: 0.0,
        }
    }

    pub(super) fn note_on(&mut self, note: u8, velocity: u8, sample_rate_hz: u32) -> Option<f32> {
        let retriggering_same_note = self
            .voices
            .iter()
            .any(|voice| voice.is_active() && voice.note() == note);
        if retriggering_same_note {
            self.resonance.dampen_for_retrigger(note);
        }
        let (slot_index, stolen_energy) = self.pick_voice_slot(note);
        let voice = &mut self.voices[slot_index];
        voice.start(note, velocity, sample_rate_hz, self.soft_pedal_amount);
        let soft_resonance_scale = 1.0 - (self.soft_pedal_amount * 0.60);
        let resonance_velocity = if retriggering_same_note {
            ((velocity as f32 * 0.25 * soft_resonance_scale) as u8).max(1)
        } else {
            ((velocity as f32 * soft_resonance_scale) as u8).max(1)
        };
        self.resonance
            .excite(note, resonance_velocity, self.sustain_pedal_down);
        if retriggering_same_note {
            self.output_stage.dampen_for_retrigger();
        }
        stolen_energy
    }

    pub(super) fn note_off(&mut self, note: u8) {
        for voice in self
            .voices
            .iter_mut()
            .filter(|voice| voice.is_active() && voice.note() == note)
        {
            voice.note_off(self.sustain_pedal_down);
        }
    }

    pub(super) fn set_sustain_pedal(&mut self, is_down: bool) {
        if self.sustain_pedal_down && !is_down {
            for voice in &mut self.voices {
                voice.pedal_release();
            }
        }

        self.sustain_pedal_down = is_down;
    }

    pub(super) fn set_soft_pedal(&mut self, amount: f32) {
        self.soft_pedal_amount = amount.clamp(0.0, 1.0);
    }

    pub(super) fn render_frame(&mut self, config: &EngineConfig) -> (f32, f32) {
        let mut frame_left = 0.0;
        let mut frame_right = 0.0;

        for voice in &mut self.voices {
            let VoiceLayers {
                strings_left,
                strings_right,
                mechanical_left,
                mechanical_right,
            } = voice.render(config.hammer_noise_gain);
            frame_left += strings_left * config.string_gain;
            frame_right += strings_right * config.string_gain;
            frame_left += mechanical_left * config.mechanical_gain;
            frame_right += mechanical_right * config.mechanical_gain;
        }

        let (res_left, res_right) = self.resonance.render(self.sustain_pedal_down);
        frame_left += res_left * config.resonance_gain;
        frame_right += res_right * config.resonance_gain;

        self.output_stage.process(
            frame_left,
            frame_right,
            config.body_gain,
            config.ambience_gain,
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
}
