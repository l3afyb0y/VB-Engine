use vb_engine::{EngineConfig, EngineParameter, ProcessEvent};
use vst3::Steinberg::Vst::{ParamID, ParameterInfo_::ParameterFlags_};

pub const PARAM_COUNT: usize = 9;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ParameterId {
    MasterGain = 0,
    StringLevel = 1,
    MechanicalLevel = 2,
    HammerNoise = 3,
    Resonance = 4,
    Body = 5,
    Ambience = 6,
    SustainPedal = 64,
    SoftPedal = 67,
}

#[derive(Debug, Clone, Copy)]
enum ParameterKind {
    Engine {
        parameter: EngineParameter,
        min: f64,
        max: f64,
    },
    MidiCc {
        control: u8,
    },
}

#[derive(Debug, Clone, Copy)]
pub struct ParameterSpec {
    pub id: ParameterId,
    pub title: &'static str,
    pub short_title: &'static str,
    pub units: &'static str,
    pub default_normalized: f64,
    pub flags: i32,
    pub precision: usize,
    kind: ParameterKind,
}

const ENGINE_DEFAULTS: EngineConfig = EngineConfig {
    sample_rate_hz: 48_000,
    max_block_size: 512,
    max_voices: 32,
    master_gain: 1.45,
    string_gain: 1.35,
    mechanical_gain: 0.22,
    sustain_pedal_threshold: 64,
    hammer_noise_gain: 0.07,
    resonance_gain: 0.32,
    body_gain: 0.28,
    ambience_gain: 0.14,
};

const PARAMS: [ParameterSpec; PARAM_COUNT] = [
    ParameterSpec {
        id: ParameterId::MasterGain,
        title: "Master Gain",
        short_title: "Gain",
        units: "x",
        default_normalized: normalize_linear(ENGINE_DEFAULTS.master_gain as f64, 0.05, 2.0),
        flags: ParameterFlags_::kCanAutomate,
        precision: 2,
        kind: ParameterKind::Engine {
            parameter: EngineParameter::MasterGain,
            min: 0.05,
            max: 2.0,
        },
    },
    ParameterSpec {
        id: ParameterId::StringLevel,
        title: "String Level",
        short_title: "Strings",
        units: "",
        default_normalized: normalize_linear(ENGINE_DEFAULTS.string_gain as f64, 0.0, 2.5),
        flags: ParameterFlags_::kCanAutomate,
        precision: 2,
        kind: ParameterKind::Engine {
            parameter: EngineParameter::StringGain,
            min: 0.0,
            max: 2.5,
        },
    },
    ParameterSpec {
        id: ParameterId::MechanicalLevel,
        title: "Mechanical Level",
        short_title: "Mech",
        units: "",
        default_normalized: normalize_linear(ENGINE_DEFAULTS.mechanical_gain as f64, 0.0, 2.0),
        flags: ParameterFlags_::kCanAutomate,
        precision: 2,
        kind: ParameterKind::Engine {
            parameter: EngineParameter::MechanicalGain,
            min: 0.0,
            max: 2.0,
        },
    },
    ParameterSpec {
        id: ParameterId::HammerNoise,
        title: "Hammer Noise",
        short_title: "Hammer",
        units: "",
        default_normalized: normalize_linear(ENGINE_DEFAULTS.hammer_noise_gain as f64, 0.0, 1.0),
        flags: ParameterFlags_::kCanAutomate,
        precision: 2,
        kind: ParameterKind::Engine {
            parameter: EngineParameter::HammerNoiseGain,
            min: 0.0,
            max: 1.0,
        },
    },
    ParameterSpec {
        id: ParameterId::Resonance,
        title: "Resonance",
        short_title: "Res",
        units: "",
        default_normalized: normalize_linear(ENGINE_DEFAULTS.resonance_gain as f64, 0.0, 2.0),
        flags: ParameterFlags_::kCanAutomate,
        precision: 2,
        kind: ParameterKind::Engine {
            parameter: EngineParameter::ResonanceGain,
            min: 0.0,
            max: 2.0,
        },
    },
    ParameterSpec {
        id: ParameterId::Body,
        title: "Body",
        short_title: "Body",
        units: "",
        default_normalized: normalize_linear(ENGINE_DEFAULTS.body_gain as f64, 0.0, 2.0),
        flags: ParameterFlags_::kCanAutomate,
        precision: 2,
        kind: ParameterKind::Engine {
            parameter: EngineParameter::BodyGain,
            min: 0.0,
            max: 2.0,
        },
    },
    ParameterSpec {
        id: ParameterId::Ambience,
        title: "Ambience",
        short_title: "Amb",
        units: "",
        default_normalized: normalize_linear(ENGINE_DEFAULTS.ambience_gain as f64, 0.0, 1.6),
        flags: ParameterFlags_::kCanAutomate,
        precision: 2,
        kind: ParameterKind::Engine {
            parameter: EngineParameter::AmbienceGain,
            min: 0.0,
            max: 1.6,
        },
    },
    ParameterSpec {
        id: ParameterId::SustainPedal,
        title: "Sustain Pedal",
        short_title: "Sustain",
        units: "",
        default_normalized: 0.0,
        flags: ParameterFlags_::kCanAutomate | ParameterFlags_::kIsHidden,
        precision: 0,
        kind: ParameterKind::MidiCc { control: 64 },
    },
    ParameterSpec {
        id: ParameterId::SoftPedal,
        title: "Soft Pedal",
        short_title: "Soft",
        units: "",
        default_normalized: 0.0,
        flags: ParameterFlags_::kCanAutomate | ParameterFlags_::kIsHidden,
        precision: 0,
        kind: ParameterKind::MidiCc { control: 67 },
    },
];

const fn normalize_linear(value: f64, min: f64, max: f64) -> f64 {
    if max <= min {
        0.0
    } else {
        (value - min) / (max - min)
    }
}

pub fn specs() -> &'static [ParameterSpec] {
    &PARAMS
}

pub fn default_parameter_values() -> [f64; PARAM_COUNT] {
    PARAMS.map(|spec| spec.default_normalized)
}

pub fn spec_by_index(index: i32) -> Option<&'static ParameterSpec> {
    usize::try_from(index)
        .ok()
        .and_then(|index| PARAMS.get(index))
}

pub fn spec_by_id(id: ParamID) -> Option<&'static ParameterSpec> {
    PARAMS.iter().find(|spec| spec.id as ParamID == id)
}

pub fn midi_mapping(controller_number: i16) -> Option<ParameterId> {
    match controller_number {
        64 => Some(ParameterId::SustainPedal),
        67 => Some(ParameterId::SoftPedal),
        _ => None,
    }
}

impl ParameterSpec {
    pub fn normalized_to_plain(self, normalized: f64) -> f64 {
        let normalized = normalized.clamp(0.0, 1.0);
        match self.kind {
            ParameterKind::Engine { min, max, .. } => min + (max - min) * normalized,
            ParameterKind::MidiCc { .. } => normalized,
        }
    }

    pub fn plain_to_normalized(self, plain: f64) -> f64 {
        match self.kind {
            ParameterKind::Engine { min, max, .. } => {
                if max <= min {
                    0.0
                } else {
                    ((plain - min) / (max - min)).clamp(0.0, 1.0)
                }
            }
            ParameterKind::MidiCc { .. } => plain.clamp(0.0, 1.0),
        }
    }

    pub fn format_plain(self, plain: f64) -> String {
        if self.units.is_empty() {
            format!("{plain:.precision$}", precision = self.precision)
        } else {
            format!(
                "{plain:.precision$} {}",
                self.units,
                precision = self.precision
            )
        }
    }

    pub fn to_process_event(self, frame_offset: usize, normalized: f64) -> ProcessEvent {
        match self.kind {
            ParameterKind::Engine { parameter, .. } => ProcessEvent::ParameterChange {
                frame_offset,
                parameter,
                value: self.normalized_to_plain(normalized) as f32,
            },
            ParameterKind::MidiCc { control } => ProcessEvent::ControlChange {
                frame_offset,
                control,
                value: (normalized.clamp(0.0, 1.0) * 127.0).round() as u8,
            },
        }
    }
}

#[cfg(test)]
mod tests {
    use super::{ParameterId, midi_mapping, spec_by_id};

    #[test]
    fn engine_parameters_round_trip_cleanly() {
        let spec = spec_by_id(ParameterId::Body as u32).expect("body spec exists");
        let normalized = 0.42;
        let plain = spec.normalized_to_plain(normalized);

        assert!((spec.plain_to_normalized(plain) - normalized).abs() < 1.0e-9);
    }

    #[test]
    fn midi_mapping_covers_supported_pedals() {
        assert_eq!(midi_mapping(64), Some(ParameterId::SustainPedal));
        assert_eq!(midi_mapping(67), Some(ParameterId::SoftPedal));
        assert_eq!(midi_mapping(1), None);
    }
}
