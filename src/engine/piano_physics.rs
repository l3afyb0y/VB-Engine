//! Shared note-wise physical parameter derivation for the piano engine.
//!
//! This module is intentionally lightweight and formula-driven for now. It
//! gives hammer, string, and bridge code a common physical vocabulary so core
//! mechanics do not stay buried in separate register-position curves forever.

use core::f32::consts::PI;

const A0_HZ: f32 = 27.5;
const C8_HZ: f32 = 4_186.01;
const STEEL_DENSITY_KG_M3: f32 = 7_850.0;
const COPPER_DENSITY_KG_M3: f32 = 8_940.0;
const STRING_YOUNGS_MODULUS_PA: f32 = 200.0e9;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(super) enum StringMaterialRegime {
    PlainSteel,
    CopperWoundBass,
}

#[derive(Debug, Clone, Copy)]
pub(super) struct HammerPhysics {
    pub(super) mass_kg: f32,
    pub(super) impact_velocity_m_s: f32,
    pub(super) felt_hardness: f32,
    pub(super) spring_stiffness: f32,
    pub(super) compression_power: f32,
    pub(super) hysteresis: f32,
    pub(super) impedance_inverse: f32,
    pub(super) bridge_coupling: f32,
}

#[derive(Debug, Clone, Copy)]
#[allow(dead_code)]
pub(super) struct NotePhysics {
    pub(super) note: u8,
    pub(super) frequency_hz: f32,
    pub(super) register_position: f32,
    pub(super) low_register_weight: f32,
    pub(super) string_count: usize,
    pub(super) material_regime: StringMaterialRegime,
    pub(super) speaking_length_m: f32,
    pub(super) core_radius_m: f32,
    pub(super) outer_radius_m: f32,
    pub(super) effective_linear_density_kg_per_m: f32,
    pub(super) tension_n: f32,
    pub(super) string_impedance: f32,
    pub(super) soundboard_impedance: f32,
    pub(super) inharmonicity_coefficient: f32,
    pub(super) strike_position_base: f32,
    pub(super) excitation_scale_base: f32,
    pub(super) t60_base_s: f32,
    pub(super) damping_coeff: f32,
    pub(super) register_output_scale: f32,
}

impl NotePhysics {
    pub(super) fn for_note(note: u8) -> Self {
        let frequency_hz = midi_note_hz(note);
        let register_position = ((note as f32 - 21.0).max(0.0) / 87.0).clamp(0.0, 1.0);
        let low_register_weight = 1.0 - register_position;
        let normalized_log = (frequency_hz / A0_HZ).ln() / (C8_HZ / A0_HZ).ln();
        let material_regime = if note < 52 {
            StringMaterialRegime::CopperWoundBass
        } else {
            StringMaterialRegime::PlainSteel
        };
        let speaking_length_m = 0.045 + 1.36 / (1.0 + f32::exp(-3.45 + (1.45 * normalized_log)));
        let outer_radius_m = 0.0020 * f32::powf(1.0 + (0.60 * (frequency_hz / A0_HZ).ln()), -1.38);
        let core_radius_m = outer_radius_m.min(0.0006);
        let effective_linear_density_kg_per_m =
            effective_linear_density(core_radius_m, outer_radius_m, material_regime);
        let tension_n =
            (2.0 * speaking_length_m * frequency_hz).powi(2) * effective_linear_density_kg_per_m;
        let string_impedance = (tension_n * effective_linear_density_kg_per_m).sqrt();
        let inharmonicity_coefficient =
            ((PI * PI * PI) * STRING_YOUNGS_MODULUS_PA * core_radius_m.powi(4))
                / (4.0 * speaking_length_m.powi(2) * tension_n.max(1.0));
        let strike_position_base = 0.115 + (register_position * 0.070);
        let excitation_scale_base = 0.42
            + (register_position * 0.90)
            + (low_register_weight * 0.16)
            + match material_regime {
                StringMaterialRegime::CopperWoundBass => 0.10,
                StringMaterialRegime::PlainSteel => 0.0,
            };
        let t60_base_s = 1.6
            + (low_register_weight * 4.9)
            + match material_regime {
                StringMaterialRegime::CopperWoundBass => 0.9,
                StringMaterialRegime::PlainSteel => 0.0,
            };
        let damping_coeff = 0.115 + (register_position * 0.235)
            - match material_regime {
                StringMaterialRegime::CopperWoundBass => 0.020,
                StringMaterialRegime::PlainSteel => 0.0,
            };
        let register_output_scale = 0.90
            + (register_position * 0.15)
            + match material_regime {
                StringMaterialRegime::CopperWoundBass => 0.03,
                StringMaterialRegime::PlainSteel => 0.0,
            };

        Self {
            note,
            frequency_hz,
            register_position,
            low_register_weight,
            string_count: string_count_for_note(note),
            material_regime,
            speaking_length_m,
            core_radius_m,
            outer_radius_m,
            effective_linear_density_kg_per_m,
            tension_n,
            string_impedance,
            soundboard_impedance: 4_000.0,
            inharmonicity_coefficient,
            strike_position_base,
            excitation_scale_base,
            t60_base_s,
            damping_coeff,
            register_output_scale,
        }
    }

    pub(super) fn hammer_physics(
        &self,
        velocity: u8,
        soft_pedal_amount: f32,
        hardness_offset: f32,
    ) -> HammerPhysics {
        let normalized_velocity = velocity as f32 / 127.0;
        let soft_pedal_amount = soft_pedal_amount.clamp(0.0, 1.0);
        let hardness_offset = hardness_offset.clamp(-1.0, 1.0);
        let hammer_mass_bias = match self.material_regime {
            StringMaterialRegime::CopperWoundBass => 0.010,
            StringMaterialRegime::PlainSteel => 0.0,
        };
        let mass_kg = (0.0035 + (self.low_register_weight * 0.010) + hammer_mass_bias)
            * (1.0 + (0.10 * soft_pedal_amount));
        let impact_velocity_m_s =
            (0.34 + (normalized_velocity * 2.45)) * (1.0 - (0.35 * soft_pedal_amount));
        let felt_hardness = (0.34 + (normalized_velocity * 0.48) + (self.register_position * 0.10)
            - (soft_pedal_amount * 0.22)
            + (hardness_offset * 0.24))
            .clamp(0.18, 0.96);
        let tension_scale = (self.tension_n / 9_000.0).sqrt().clamp(0.55, 1.45);
        let impedance_ratio =
            (self.string_impedance / self.soundboard_impedance).clamp(0.0005, 0.40);

        HammerPhysics {
            mass_kg,
            impact_velocity_m_s,
            felt_hardness,
            spring_stiffness: (2_700.0 + (felt_hardness * 4_400.0) + (tension_scale * 1_200.0))
                .clamp(1_500.0, 9_500.0),
            compression_power: 1.95 + (felt_hardness * 1.00),
            hysteresis: 0.13 + (felt_hardness * 0.13),
            impedance_inverse: (0.0018
                + (self.low_register_weight * 0.0010)
                + (impedance_ratio * 0.004))
                .clamp(0.0012, 0.0055),
            bridge_coupling: (0.0009
                + (impedance_ratio * 0.0065)
                + (self.low_register_weight * 0.0008))
                .clamp(0.0008, 0.0045),
        }
    }

    pub(super) fn unison_detune_cents(self) -> [f32; 3] {
        let detune_scale = match self.material_regime {
            StringMaterialRegime::CopperWoundBass => 0.55,
            StringMaterialRegime::PlainSteel => 0.82 + (self.register_position * 0.12),
        };
        match self.string_count {
            1 => [0.0, 0.0, 0.0],
            2 => [-0.70 * detune_scale, 0.70 * detune_scale, 0.0],
            _ => [-0.62 * detune_scale, 0.0, 0.56 * detune_scale],
        }
    }

    pub(super) fn unison_balance(self) -> [f32; 3] {
        match self.string_count {
            1 => [1.0, 0.0, 0.0],
            2 => [0.54, 0.54, 0.0],
            _ => [0.36, 0.42, 0.36],
        }
    }

    pub(super) fn unison_positions(self) -> [f32; 3] {
        match self.string_count {
            1 => [0.0, 0.0, 0.0],
            2 => [-0.82, 0.82, 0.0],
            _ => [-1.08, 0.0, 1.04],
        }
    }
}

pub(super) fn midi_note_hz(note: u8) -> f32 {
    440.0 * 2.0_f32.powf((note as f32 - 69.0) / 12.0)
}

pub(super) fn string_count_for_note(note: u8) -> usize {
    if note < 40 {
        1
    } else if note < 60 {
        2
    } else {
        3
    }
}

fn effective_linear_density(
    core_radius_m: f32,
    outer_radius_m: f32,
    material_regime: StringMaterialRegime,
) -> f32 {
    let core_area = PI * core_radius_m.powi(2);
    let core_density = core_area * STEEL_DENSITY_KG_M3;
    match material_regime {
        StringMaterialRegime::PlainSteel => core_density,
        StringMaterialRegime::CopperWoundBass => {
            let winding_area = (PI * outer_radius_m.powi(2) - core_area).max(0.0);
            core_density + (winding_area * COPPER_DENSITY_KG_M3 * 0.76)
        }
    }
}

#[cfg(test)]
mod tests {
    use super::{NotePhysics, StringMaterialRegime};

    #[test]
    fn bass_notes_use_copper_wound_regime() {
        let bass = NotePhysics::for_note(36);
        let mid = NotePhysics::for_note(60);

        assert_eq!(bass.material_regime, StringMaterialRegime::CopperWoundBass);
        assert_eq!(mid.material_regime, StringMaterialRegime::PlainSteel);
        assert!(bass.effective_linear_density_kg_per_m > mid.effective_linear_density_kg_per_m);
    }

    #[test]
    fn lower_notes_have_longer_strings_and_more_tension_mass() {
        let low = NotePhysics::for_note(36);
        let high = NotePhysics::for_note(84);

        assert!(low.speaking_length_m > high.speaking_length_m);
        assert!(low.effective_linear_density_kg_per_m > high.effective_linear_density_kg_per_m);
        assert!(low.outer_radius_m > high.outer_radius_m);
    }

    #[test]
    fn middle_register_promotes_to_trichords() {
        assert_eq!(NotePhysics::for_note(48).string_count, 2);
        assert_eq!(NotePhysics::for_note(60).string_count, 3);
        assert_eq!(NotePhysics::for_note(69).string_count, 3);
    }

    #[test]
    fn trichord_detune_stays_subtle() {
        let middle = NotePhysics::for_note(60).unison_detune_cents();
        assert!(middle[0].abs() < 0.7);
        assert!(middle[2].abs() < 0.7);
    }
}
