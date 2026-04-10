use vb_engine::{EngineConfig, EngineParameter, ProcessEvent, SustainPedalMode};
use vst3::Steinberg::Vst::{ParamID, ParameterInfo_::ParameterFlags_};

pub const PARAM_COUNT: usize = 14;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ParameterId {
    MasterGain = 0,
    StringLevel = 1,
    MechanicalLevel = 2,
    HammerHardness = 3,
    HammerNoise = 4,
    Resonance = 5,
    Body = 6,
    Ambience = 7,
    BridgeFeedback = 8,
    Downbearing = 9,
    PlateLeak = 10,
    SoundboardWidth = 11,
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
    master_gain: 1.65,
    string_gain: 1.50,
    mechanical_gain: 0.22,
    sustain_pedal_threshold: 64,
    sustain_pedal_mode: SustainPedalMode::Binary,
    hammer_hardness: 0.0,
    hammer_noise_gain: 0.07,
    resonance_gain: 0.36,
    body_gain: 0.42,
    ambience_gain: 0.12,
    bridge_feedback_gain: 0.36,
    downbearing_preload: 0.62,
    plate_leak: 0.032,
    soundboard_width: 1.0,
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
        id: ParameterId::HammerHardness,
        title: "Hammer Hardness",
        short_title: "Hardness",
        units: "",
        default_normalized: normalize_linear(ENGINE_DEFAULTS.hammer_hardness as f64, -1.0, 1.0),
        flags: ParameterFlags_::kCanAutomate,
        precision: 2,
        kind: ParameterKind::Engine {
            parameter: EngineParameter::HammerHardness,
            min: -1.0,
            max: 1.0,
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
        id: ParameterId::BridgeFeedback,
        title: "Bridge Feedback",
        short_title: "BrdgFb",
        units: "",
        default_normalized: normalize_linear(ENGINE_DEFAULTS.bridge_feedback_gain as f64, 0.0, 1.5),
        flags: ParameterFlags_::kCanAutomate,
        precision: 2,
        kind: ParameterKind::Engine {
            parameter: EngineParameter::BridgeFeedbackGain,
            min: 0.0,
            max: 1.5,
        },
    },
    ParameterSpec {
        id: ParameterId::Downbearing,
        title: "Downbearing",
        short_title: "Downbr",
        units: "",
        default_normalized: normalize_linear(ENGINE_DEFAULTS.downbearing_preload as f64, 0.0, 1.0),
        flags: ParameterFlags_::kCanAutomate,
        precision: 2,
        kind: ParameterKind::Engine {
            parameter: EngineParameter::DownbearingPreload,
            min: 0.0,
            max: 1.0,
        },
    },
    ParameterSpec {
        id: ParameterId::PlateLeak,
        title: "Plate Leak",
        short_title: "Plate",
        units: "",
        default_normalized: normalize_linear(ENGINE_DEFAULTS.plate_leak as f64, 0.0, 0.30),
        flags: ParameterFlags_::kCanAutomate,
        precision: 3,
        kind: ParameterKind::Engine {
            parameter: EngineParameter::PlateLeak,
            min: 0.0,
            max: 0.30,
        },
    },
    ParameterSpec {
        id: ParameterId::SoundboardWidth,
        title: "Soundboard Width",
        short_title: "BoardW",
        units: "",
        default_normalized: normalize_linear(ENGINE_DEFAULTS.soundboard_width as f64, 0.0, 2.0),
        flags: ParameterFlags_::kCanAutomate,
        precision: 2,
        kind: ParameterKind::Engine {
            parameter: EngineParameter::SoundboardWidth,
            min: 0.0,
            max: 2.0,
        },
    },
    ParameterSpec {
        id: ParameterId::SustainPedal,
        title: "Sustain Pedal",
        short_title: "Sustain",
        units: "",
        default_normalized: 0.0,
        flags: ParameterFlags_::kCanAutomate,
        precision: 0,
        kind: ParameterKind::MidiCc { control: 64 },
    },
    ParameterSpec {
        id: ParameterId::SoftPedal,
        title: "Soft Pedal",
        short_title: "Soft",
        units: "",
        default_normalized: 0.0,
        flags: ParameterFlags_::kCanAutomate,
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
    use super::{ParameterId, default_parameter_values, midi_mapping, spec_by_id};
    use vst3::Steinberg::Vst::ParameterInfo_::ParameterFlags_::kIsHidden;

    #[test]
    fn engine_parameters_round_trip_cleanly() {
        let spec = spec_by_id(ParameterId::Body as u32).expect("body spec exists");
        let normalized = 0.42;
        let plain = spec.normalized_to_plain(normalized);

        assert!((spec.plain_to_normalized(plain) - normalized).abs() < 1.0e-9);
    }

    #[test]
    fn default_engine_parameters_are_not_minimum_values() {
        let defaults = default_parameter_values();
        let expected = [
            (ParameterId::MasterGain, 1.65),
            (ParameterId::StringLevel, 1.50),
            (ParameterId::MechanicalLevel, 0.22),
            (ParameterId::HammerHardness, 0.0),
            (ParameterId::HammerNoise, 0.07),
            (ParameterId::Resonance, 0.36),
            (ParameterId::Body, 0.42),
            (ParameterId::Ambience, 0.12),
            (ParameterId::BridgeFeedback, 0.36),
            (ParameterId::Downbearing, 0.62),
            (ParameterId::PlateLeak, 0.032),
            (ParameterId::SoundboardWidth, 1.0),
        ];

        for (index, (id, expected_plain)) in expected.into_iter().enumerate() {
            let spec = spec_by_id(id as u32).expect("parameter spec exists");
            let plain = spec.normalized_to_plain(defaults[index]);
            assert!(
                (plain - expected_plain).abs() < 1.0e-6,
                "default for {id:?} drifted to {plain}, expected {expected_plain}"
            );
            assert!(
                defaults[index] > 0.0,
                "default for {id:?} should not display as its minimum value"
            );
        }
    }

    #[test]
    fn midi_mapping_covers_supported_pedals() {
        assert_eq!(midi_mapping(64), Some(ParameterId::SustainPedal));
        assert_eq!(midi_mapping(67), Some(ParameterId::SoftPedal));
        assert_eq!(midi_mapping(1), None);
    }

    #[test]
    fn pedal_parameters_are_visible_for_host_control() {
        for id in [ParameterId::SustainPedal, ParameterId::SoftPedal] {
            let spec = spec_by_id(id as u32).expect("pedal spec exists");
            assert_eq!(
                spec.flags & kIsHidden,
                0,
                "{id:?} should be visible in host parameter menus"
            );
        }
    }
}
