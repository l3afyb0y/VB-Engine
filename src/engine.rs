use core::fmt;

mod piano_hammer;
mod piano_model;
mod piano_output;
mod piano_resonance;
mod piano_strings;
mod piano_voice;

use piano_model::PianoModel;
use piano_output::soft_clip;
use piano_strings::DcBlocker;

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct EngineConfig {
    pub sample_rate_hz: u32,
    pub max_block_size: usize,
    pub max_voices: usize,
    pub master_gain: f32,
    pub string_gain: f32,
    pub mechanical_gain: f32,
    pub sustain_pedal_threshold: u8,
    pub hammer_noise_gain: f32,
    pub resonance_gain: f32,
    pub body_gain: f32,
    pub ambience_gain: f32,
}

impl Default for EngineConfig {
    fn default() -> Self {
        Self {
            sample_rate_hz: 48_000,
            max_block_size: 512,
            max_voices: 32,
            master_gain: 1.40,
            string_gain: 1.25,
            mechanical_gain: 0.24,
            sustain_pedal_threshold: 64,
            hammer_noise_gain: 0.08,
            resonance_gain: 0.26,
            body_gain: 0.22,
            ambience_gain: 0.14,
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SustainPedalState {
    Up,
    Down,
}

/// A host-automatable engine parameter exposed by wrapper layers such as VST3.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum EngineParameter {
    MasterGain,
    StringGain,
    MechanicalGain,
    HammerNoiseGain,
    ResonanceGain,
    BodyGain,
    AmbienceGain,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum EngineError {
    InvalidConfig(&'static str),
    BlockTooLarge {
        requested: usize,
        max: usize,
    },
    EventOutOfRange {
        frame_offset: usize,
        block_len: usize,
    },
    EventsNotSorted,
}

impl fmt::Display for EngineError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::InvalidConfig(field) => write!(f, "invalid engine config field: {field}"),
            Self::BlockTooLarge { requested, max } => {
                write!(f, "render block {requested} exceeds configured limit {max}")
            }
            Self::EventOutOfRange {
                frame_offset,
                block_len,
            } => {
                write!(
                    f,
                    "event frame offset {frame_offset} exceeds current block length {block_len}"
                )
            }
            Self::EventsNotSorted => {
                write!(f, "process events must be sorted by frame_offset")
            }
        }
    }
}

impl std::error::Error for EngineError {}

#[derive(Debug, Clone, Copy, Default, PartialEq)]
pub struct Diagnostics {
    pub active_voices: usize,
    pub voice_steals: u64,
    pub worst_stolen_activity: f32,
    pub max_output_delta: f32,
    pub hard_jump_events: u64,
    pub non_finite_output_samples: u64,
}

#[derive(Debug, Clone, Copy, Default, PartialEq)]
pub struct RenderStats {
    pub peak_abs_sample: f32,
    pub rendered_frames: usize,
}

pub struct Engine {
    config: EngineConfig,
    piano: PianoModel,
    voice_steals: u64,
    worst_stolen_activity: f32,
    max_output_delta: f32,
    hard_jump_events: u64,
    non_finite_output_samples: u64,
    previous_output_left: f32,
    previous_output_right: f32,
    has_previous_output: bool,
    /// Final-stage DC blocker: catches DC accumulated by body resonators,
    /// ambience feedback loops, and any residual string-level bias.
    dc_block_left: DcBlocker,
    dc_block_right: DcBlocker,
}

impl Engine {
    pub fn new(config: EngineConfig) -> Result<Self, EngineError> {
        validate_config(config)?;
        let piano = PianoModel::new(config);

        Ok(Self {
            config,
            piano,
            voice_steals: 0,
            worst_stolen_activity: 0.0,
            max_output_delta: 0.0,
            hard_jump_events: 0,
            non_finite_output_samples: 0,
            previous_output_left: 0.0,
            previous_output_right: 0.0,
            has_previous_output: false,
            dc_block_left: DcBlocker::new(config.sample_rate_hz, 10.0),
            dc_block_right: DcBlocker::new(config.sample_rate_hz, 10.0),
        })
    }

    pub fn note_on(&mut self, note: u8, velocity: u8) {
        if let Some(stolen_energy) =
            self.piano
                .note_on(note, velocity.max(1), self.config.sample_rate_hz)
        {
            self.voice_steals += 1;
            self.worst_stolen_activity = self.worst_stolen_activity.max(stolen_energy);
        }
    }

    pub fn note_off(&mut self, note: u8) {
        self.piano.note_off(note);
    }

    pub fn set_sustain_pedal(&mut self, value: u8) -> SustainPedalState {
        let next_state = if value >= self.config.sustain_pedal_threshold {
            SustainPedalState::Down
        } else {
            SustainPedalState::Up
        };

        self.piano
            .set_sustain_pedal(matches!(next_state, SustainPedalState::Down));
        next_state
    }

    pub fn set_soft_pedal(&mut self, value: u8) -> f32 {
        let amount = (value as f32 / 127.0).clamp(0.0, 1.0);
        self.piano.set_soft_pedal(amount);
        amount
    }

    pub fn set_master_gain(&mut self, value: f32) -> f32 {
        self.config.master_gain = value.clamp(0.05, 2.0);
        self.config.master_gain
    }

    pub fn set_string_gain(&mut self, value: f32) -> f32 {
        self.config.string_gain = value.clamp(0.0, 2.5);
        self.config.string_gain
    }

    pub fn set_mechanical_gain(&mut self, value: f32) -> f32 {
        self.config.mechanical_gain = value.clamp(0.0, 2.0);
        self.config.mechanical_gain
    }

    pub fn set_hammer_noise_gain(&mut self, value: f32) -> f32 {
        self.config.hammer_noise_gain = value.clamp(0.0, 1.0);
        self.config.hammer_noise_gain
    }

    pub fn set_resonance_gain(&mut self, value: f32) -> f32 {
        self.config.resonance_gain = value.clamp(0.0, 2.0);
        self.config.resonance_gain
    }

    pub fn set_body_gain(&mut self, value: f32) -> f32 {
        self.config.body_gain = value.clamp(0.0, 2.0);
        self.config.body_gain
    }

    pub fn set_ambience_gain(&mut self, value: f32) -> f32 {
        self.config.ambience_gain = value.clamp(0.0, 1.6);
        self.config.ambience_gain
    }

    pub fn set_parameter(&mut self, parameter: EngineParameter, value: f32) -> f32 {
        match parameter {
            EngineParameter::MasterGain => self.set_master_gain(value),
            EngineParameter::StringGain => self.set_string_gain(value),
            EngineParameter::MechanicalGain => self.set_mechanical_gain(value),
            EngineParameter::HammerNoiseGain => self.set_hammer_noise_gain(value),
            EngineParameter::ResonanceGain => self.set_resonance_gain(value),
            EngineParameter::BodyGain => self.set_body_gain(value),
            EngineParameter::AmbienceGain => self.set_ambience_gain(value),
        }
    }

    pub fn control_change(&mut self, control: u8, value: u8) {
        match control {
            64 => {
                let _ = self.set_sustain_pedal(value);
            }
            67 => {
                let _ = self.set_soft_pedal(value);
            }
            _ => {}
        }
    }

    pub fn render(
        &mut self,
        left: &mut [f32],
        right: &mut [f32],
    ) -> Result<RenderStats, EngineError> {
        validate_block_sizes(left, right, self.config.max_block_size)?;

        left.fill(0.0);
        right.fill(0.0);

        let mut peak_abs_sample: f32 = 0.0;
        for frame in 0..left.len() {
            let (raw_left, raw_right) = self.piano.render_frame(&self.config);
            let blocked_left = self.dc_block_left.step(raw_left);
            let blocked_right = self.dc_block_right.step(raw_right);
            let mut frame_left = soft_clip(blocked_left * self.config.master_gain);
            let mut frame_right = soft_clip(blocked_right * self.config.master_gain);

            if !frame_left.is_finite() || !frame_right.is_finite() {
                self.non_finite_output_samples += 1;
                frame_left = 0.0;
                frame_right = 0.0;
            }

            if self.has_previous_output {
                let delta = (frame_left - self.previous_output_left)
                    .abs()
                    .max((frame_right - self.previous_output_right).abs());
                self.max_output_delta = self.max_output_delta.max(delta);
                if delta > 0.65 {
                    self.hard_jump_events += 1;
                }
            } else {
                self.has_previous_output = true;
            }

            self.previous_output_left = frame_left;
            self.previous_output_right = frame_right;
            peak_abs_sample = peak_abs_sample.max(frame_left.abs()).max(frame_right.abs());
            left[frame] = frame_left;
            right[frame] = frame_right;
        }

        Ok(RenderStats {
            peak_abs_sample,
            rendered_frames: left.len(),
        })
    }

    pub fn diagnostics(&self) -> Diagnostics {
        Diagnostics {
            active_voices: self.piano.active_voice_count(),
            voice_steals: self.voice_steals,
            worst_stolen_activity: self.worst_stolen_activity,
            max_output_delta: self.max_output_delta,
            hard_jump_events: self.hard_jump_events,
            non_finite_output_samples: self.non_finite_output_samples,
        }
    }

    pub fn reset_diagnostics(&mut self) {
        self.voice_steals = 0;
        self.worst_stolen_activity = 0.0;
        self.max_output_delta = 0.0;
        self.hard_jump_events = 0;
        self.non_finite_output_samples = 0;
    }
}

fn validate_config(config: EngineConfig) -> Result<(), EngineError> {
    if config.sample_rate_hz == 0 {
        return Err(EngineError::InvalidConfig("sample_rate_hz"));
    }
    if config.max_block_size == 0 {
        return Err(EngineError::InvalidConfig("max_block_size"));
    }
    if config.max_voices == 0 {
        return Err(EngineError::InvalidConfig("max_voices"));
    }
    if !config.master_gain.is_finite() || config.master_gain <= 0.0 {
        return Err(EngineError::InvalidConfig("master_gain"));
    }
    if !config.string_gain.is_finite() || config.string_gain < 0.0 {
        return Err(EngineError::InvalidConfig("string_gain"));
    }
    if !config.mechanical_gain.is_finite() || config.mechanical_gain < 0.0 {
        return Err(EngineError::InvalidConfig("mechanical_gain"));
    }
    if !config.hammer_noise_gain.is_finite() || config.hammer_noise_gain < 0.0 {
        return Err(EngineError::InvalidConfig("hammer_noise_gain"));
    }
    if !config.resonance_gain.is_finite() || config.resonance_gain < 0.0 {
        return Err(EngineError::InvalidConfig("resonance_gain"));
    }
    if !config.body_gain.is_finite() || config.body_gain < 0.0 {
        return Err(EngineError::InvalidConfig("body_gain"));
    }
    if !config.ambience_gain.is_finite() || config.ambience_gain < 0.0 {
        return Err(EngineError::InvalidConfig("ambience_gain"));
    }

    Ok(())
}

fn validate_block_sizes(
    left: &[f32],
    right: &[f32],
    max_block_size: usize,
) -> Result<(), EngineError> {
    if left.len() != right.len() {
        return Err(EngineError::BlockTooLarge {
            requested: left.len().max(right.len()),
            max: max_block_size,
        });
    }

    if left.len() > max_block_size {
        return Err(EngineError::BlockTooLarge {
            requested: left.len(),
            max: max_block_size,
        });
    }

    Ok(())
}
