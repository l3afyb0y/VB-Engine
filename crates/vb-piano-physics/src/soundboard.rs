//! Reduced modal soundboard radiation.
//!
//! The soundboard model receives bridge force and motion, stores energy in a
//! compact modal bank, and exposes stereo radiation as an observation of that
//! mechanical state. It is not a room reverb or downstream EQ substitute.

use crate::{TAU, radius_from_t60, soft_clip};

/// Number of reduced soundboard modes evaluated per audio frame.
pub const SOUNDBOARD_MODE_COUNT: usize = 32;

const MODE_FREQUENCIES_HZ: [f32; SOUNDBOARD_MODE_COUNT] = [
    64.0, 78.0, 96.0, 118.0, 146.0, 180.0, 222.0, 274.0, 338.0, 416.0, 512.0, 630.0, 774.0, 950.0,
    1_166.0, 1_430.0, 1_754.0, 2_150.0, 2_632.0, 3_220.0, 3_936.0, 4_810.0, 5_876.0, 7_170.0,
    8_720.0, 10_540.0, 12_540.0, 14_600.0, 16_200.0, 17_700.0, 19_100.0, 20_400.0,
];

const MODE_T60_SECONDS: [f32; SOUNDBOARD_MODE_COUNT] = [
    0.70, 0.84, 0.98, 1.12, 1.28, 1.42, 1.56, 1.70, 1.82, 1.92, 2.02, 2.10, 2.14, 2.12, 2.06, 1.96,
    1.82, 1.66, 1.48, 1.30, 1.12, 0.96, 0.82, 0.70, 0.58, 0.48, 0.40, 0.34, 0.29, 0.25, 0.22, 0.20,
];

const MODE_COUPLINGS: [f32; SOUNDBOARD_MODE_COUNT] = [
    0.0088, 0.0096, 0.0106, 0.0115, 0.0124, 0.0130, 0.0132, 0.0128, 0.0122, 0.0114, 0.0106, 0.0098,
    0.0090, 0.0084, 0.0078, 0.0072, 0.0066, 0.0060, 0.0054, 0.0048, 0.0042, 0.0037, 0.0032, 0.0028,
    0.0024, 0.0020, 0.0017, 0.00145, 0.00120, 0.00100, 0.00085, 0.00070,
];

const LEFT_RADIATION: [f32; SOUNDBOARD_MODE_COUNT] = [
    0.76, 0.42, 0.70, 0.36, 0.64, 0.48, 0.72, 0.40, 0.58, 0.50, 0.66, 0.34, 0.74, 0.44, 0.60, 0.52,
    0.68, 0.38, 0.62, 0.46, 0.56, 0.54, 0.70, 0.32, 0.50, 0.66, 0.42, 0.58, 0.36, 0.60, 0.48, 0.52,
];

const RIGHT_RADIATION: [f32; SOUNDBOARD_MODE_COUNT] = [
    0.38, 0.72, 0.46, 0.76, 0.40, 0.68, 0.44, 0.70, 0.52, 0.56, 0.36, 0.74, 0.42, 0.66, 0.50, 0.60,
    0.34, 0.72, 0.48, 0.64, 0.58, 0.52, 0.36, 0.76, 0.62, 0.44, 0.70, 0.50, 0.66, 0.46, 0.58, 0.54,
];

/// Runtime configuration for the reduced soundboard.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct SoundboardConfig {
    /// Audio sample rate used to derive modal coefficients.
    pub sample_rate_hz: u32,
    /// Overall modal coupling scale.
    pub modal_gain: f32,
    /// How much rim-like reflection keeps low and mid board motion alive.
    pub rim_reflection: f32,
    /// Stereo observation width for the board radiation path.
    pub radiation_width: f32,
    /// How strongly low-frequency board radiation collapses toward mono.
    pub low_frequency_mono_collapse: f32,
}

impl Default for SoundboardConfig {
    fn default() -> Self {
        Self {
            sample_rate_hz: 48_000,
            modal_gain: 1.0,
            rim_reflection: 0.72,
            radiation_width: 1.0,
            low_frequency_mono_collapse: 0.78,
        }
    }
}

/// Per-frame bridge drive sent into the soundboard.
#[derive(Debug, Clone, Copy, Default, PartialEq)]
pub struct SoundboardDrive {
    /// Reduced bridge force into the board.
    pub bridge_force: f32,
    /// Reduced bridge velocity, useful for attack and upper partial transfer.
    pub bridge_velocity: f32,
    /// Slowly tracked bridge energy.
    pub bridge_energy: f32,
    /// Stereo side component from the string groups reaching the bridge.
    pub string_side: f32,
}

/// Stereo soundboard readout for one audio frame.
#[derive(Debug, Clone, Copy, Default, PartialEq)]
pub struct SoundboardOutput {
    /// Left observation of the soundboard radiation.
    pub left: f32,
    /// Right observation of the soundboard radiation.
    pub right: f32,
    /// Absolute reduced modal energy after this frame.
    pub modal_energy: f32,
    /// Low-band board motion after the current frame.
    pub low_board_motion: f32,
    /// Mid-band board motion after the current frame.
    pub mid_board_motion: f32,
    /// Air-motion proxy after the current frame.
    pub air_board_motion: f32,
    /// Stereo side motion that survives low-frequency mono collapse.
    pub side_board_motion: f32,
}

/// Compact modal soundboard state.
#[derive(Debug, Clone)]
pub struct SoundboardModel {
    modes: [SoundboardMode; SOUNDBOARD_MODE_COUNT],
    low_board_motion: f32,
    mid_board_motion: f32,
    air_board_motion: f32,
    side_board_motion: f32,
    stored_energy: f32,
    rim_reflection: f32,
    modal_gain: f32,
    radiation_width: f32,
    low_frequency_mono_collapse: f32,
}

impl SoundboardModel {
    /// Create a reduced soundboard modal bank.
    #[must_use]
    pub fn new(config: SoundboardConfig) -> Self {
        let mut modes = [SoundboardMode::default(); SOUNDBOARD_MODE_COUNT];
        for (index, mode) in modes.iter_mut().enumerate() {
            mode.configure(
                MODE_FREQUENCIES_HZ[index],
                config.sample_rate_hz,
                MODE_T60_SECONDS[index],
                MODE_COUPLINGS[index] * config.modal_gain,
                LEFT_RADIATION[index],
                RIGHT_RADIATION[index],
            );
        }

        Self {
            modes,
            low_board_motion: 0.0,
            mid_board_motion: 0.0,
            air_board_motion: 0.0,
            side_board_motion: 0.0,
            stored_energy: 0.0,
            rim_reflection: config.rim_reflection.clamp(0.0, 1.0),
            modal_gain: config.modal_gain.clamp(0.0, 4.0),
            radiation_width: config.radiation_width.clamp(0.0, 2.0),
            low_frequency_mono_collapse: config.low_frequency_mono_collapse.clamp(0.0, 1.0),
        }
    }

    /// Advance the board by one audio frame and return its stereo radiation.
    #[must_use]
    pub fn render(&mut self, drive: SoundboardDrive) -> SoundboardOutput {
        let bridge_force = soft_clip((drive.bridge_force * 0.82) + (drive.bridge_velocity * 0.24));
        let bridge_velocity = soft_clip(drive.bridge_velocity);
        let bridge_energy = drive
            .bridge_energy
            .abs()
            .max(bridge_force.abs())
            .max(bridge_velocity.abs());

        let energy_response = if bridge_energy > self.stored_energy {
            0.050
        } else {
            0.0045
        };
        self.stored_energy += energy_response * (bridge_energy - self.stored_energy);

        let rim_support = 0.35 + (self.rim_reflection * 0.65);
        let low_target = (bridge_force * (0.72 + (self.stored_energy * 0.34)))
            + (self.low_board_motion * self.rim_reflection * 0.04);
        let mid_target =
            (bridge_velocity * 0.58) + (bridge_force * 0.24) + (self.mid_board_motion * 0.015);
        let side_source = drive.string_side * (0.24 - (self.low_frequency_mono_collapse * 0.10));
        let air_target = ((bridge_velocity - bridge_force) * 0.34)
            + (drive.string_side * (0.014 - (self.low_frequency_mono_collapse * 0.004)));
        let side_target = side_source + (bridge_velocity * 0.020) - (bridge_force * 0.008);

        self.low_board_motion += 0.010 * rim_support * (low_target - self.low_board_motion);
        self.mid_board_motion += 0.038 * (mid_target - self.mid_board_motion);
        self.air_board_motion += 0.092 * (air_target - self.air_board_motion);
        self.side_board_motion += 0.052 * (side_target - self.side_board_motion);

        let direct_side_width =
            self.radiation_width * (1.0 - (self.low_frequency_mono_collapse * 0.74));
        let mut left = (self.low_board_motion * 0.048)
            + (self.mid_board_motion * 0.036)
            + (self.air_board_motion * 0.016)
            - (self.side_board_motion * 0.050 * direct_side_width);
        let mut right = (self.low_board_motion * 0.048)
            + (self.mid_board_motion * 0.036)
            + (self.air_board_motion * 0.016)
            + (self.side_board_motion * 0.050 * direct_side_width);
        let mut modal_energy = 0.0;

        for mode in &mut self.modes {
            let modal_drive = (self.low_board_motion * mode.low_weight)
                + (self.mid_board_motion * mode.mid_weight)
                + (self.air_board_motion * mode.air_weight)
                + (self.side_board_motion * mode.side_weight);
            let sample = mode.step(soft_clip(
                modal_drive * mode.coupling * (0.80 + (self.stored_energy * 0.90)),
            ));
            let mode_width = self.radiation_width
                * ((1.0 - self.low_frequency_mono_collapse)
                    + (self.low_frequency_mono_collapse * mode.stereo_weight));
            let mono_radiation = (mode.left_radiation + mode.right_radiation) * 0.5;
            let left_radiation =
                mono_radiation + ((mode.left_radiation - mono_radiation) * mode_width);
            let right_radiation =
                mono_radiation + ((mode.right_radiation - mono_radiation) * mode_width);
            left += sample * left_radiation * 0.19;
            right += sample * right_radiation * 0.19;
            modal_energy += sample.abs();
        }

        SoundboardOutput {
            left: soft_clip(left * self.modal_gain),
            right: soft_clip(right * self.modal_gain),
            modal_energy: modal_energy + self.stored_energy,
            low_board_motion: self.low_board_motion,
            mid_board_motion: self.mid_board_motion,
            air_board_motion: self.air_board_motion,
            side_board_motion: self.side_board_motion * direct_side_width,
        }
    }

    /// Apply a gentle global damping factor without erasing the board.
    pub fn dampen(&mut self, amount: f32) {
        let amount = amount.clamp(0.0, 1.0);
        self.low_board_motion *= amount;
        self.mid_board_motion *= amount;
        self.air_board_motion *= amount;
        self.side_board_motion *= amount;
        self.stored_energy *= amount;
        for mode in &mut self.modes {
            mode.dampen(amount);
        }
    }

    /// Update realtime observation controls without resetting stored board energy.
    pub fn set_controls(
        &mut self,
        modal_gain: f32,
        rim_reflection: f32,
        radiation_width: f32,
        low_frequency_mono_collapse: f32,
    ) {
        self.modal_gain = modal_gain.clamp(0.0, 4.0);
        self.rim_reflection = rim_reflection.clamp(0.0, 1.0);
        self.radiation_width = radiation_width.clamp(0.0, 2.0);
        self.low_frequency_mono_collapse = low_frequency_mono_collapse.clamp(0.0, 1.0);
    }
}

#[derive(Debug, Clone, Copy)]
struct SoundboardMode {
    omega_cos: f32,
    radius: f32,
    coupling: f32,
    left_radiation: f32,
    right_radiation: f32,
    low_weight: f32,
    mid_weight: f32,
    air_weight: f32,
    side_weight: f32,
    stereo_weight: f32,
    previous_1: f32,
    previous_2: f32,
}

impl Default for SoundboardMode {
    fn default() -> Self {
        Self {
            omega_cos: 0.0,
            radius: 0.0,
            coupling: 0.0,
            left_radiation: 0.0,
            right_radiation: 0.0,
            low_weight: 0.0,
            mid_weight: 0.0,
            air_weight: 0.0,
            side_weight: 0.0,
            stereo_weight: 0.0,
            previous_1: 0.0,
            previous_2: 0.0,
        }
    }
}

impl SoundboardMode {
    fn configure(
        &mut self,
        frequency_hz: f32,
        sample_rate_hz: u32,
        t60_seconds: f32,
        coupling: f32,
        left_radiation: f32,
        right_radiation: f32,
    ) {
        let omega = TAU * frequency_hz / sample_rate_hz.max(1) as f32;
        self.omega_cos = omega.cos();
        self.radius = radius_from_t60(t60_seconds, sample_rate_hz);
        self.coupling = coupling;
        self.left_radiation = left_radiation;
        self.right_radiation = right_radiation;

        let low_weight = (1.0 - ((frequency_hz - 140.0).abs() / 260.0)).clamp(0.0, 1.0);
        let mid_weight = (1.0 - ((frequency_hz - 520.0).abs() / 620.0)).clamp(0.0, 1.0);
        let air_weight = (1.0 - ((frequency_hz - 1_800.0).abs() / 1_800.0)).clamp(0.0, 1.0);
        let side_weight = (0.22 + ((frequency_hz / 2_800.0).clamp(0.0, 1.0) * 0.78))
            * (left_radiation - right_radiation).abs().clamp(0.12, 0.72);
        let total = (low_weight + mid_weight + air_weight + side_weight).max(1.0e-4);
        self.low_weight = low_weight / total;
        self.mid_weight = mid_weight / total;
        self.air_weight = air_weight / total;
        self.side_weight = side_weight / total;
        self.stereo_weight = ((frequency_hz / 1_200.0).clamp(0.0, 1.0)).powf(0.72);
        self.previous_1 = 0.0;
        self.previous_2 = 0.0;
    }

    fn step(&mut self, drive: f32) -> f32 {
        let coeff_a = 2.0 * self.radius * self.omega_cos;
        let coeff_b = self.radius * self.radius;
        let current =
            ((coeff_a * self.previous_1) - (coeff_b * self.previous_2) + drive).clamp(-1.1, 1.1);
        self.previous_2 = self.previous_1;
        self.previous_1 = current;
        current
    }

    fn dampen(&mut self, amount: f32) {
        self.previous_1 *= amount;
        self.previous_2 *= amount;
    }
}

#[cfg(test)]
mod tests {
    use super::{SoundboardConfig, SoundboardDrive, SoundboardModel};

    #[test]
    fn board_keeps_a_tail_after_bridge_input_stops() {
        let mut board = SoundboardModel::new(SoundboardConfig::default());

        for _ in 0..256 {
            let _ = board.render(SoundboardDrive {
                bridge_force: 0.35,
                bridge_velocity: 0.20,
                bridge_energy: 0.35,
                string_side: 0.0,
            });
        }

        let mut tail_energy = 0.0;
        for _ in 0..1_024 {
            let output = board.render(SoundboardDrive::default());
            tail_energy += output.left.abs() + output.right.abs();
        }

        assert!(tail_energy > 0.02);
    }

    #[test]
    fn centered_bridge_input_still_radiates_stereo_width() {
        let mut board = SoundboardModel::new(SoundboardConfig::default());
        let mut side_energy = 0.0;

        for _ in 0..512 {
            let output = board.render(SoundboardDrive {
                bridge_force: 0.24,
                bridge_velocity: 0.18,
                bridge_energy: 0.24,
                string_side: 0.0,
            });
            side_energy += (output.left - output.right).abs();
        }

        assert!(side_energy > 0.01);
    }

    #[test]
    fn low_frequency_mono_collapse_reduces_side_radiation() {
        let mut wide = SoundboardModel::new(SoundboardConfig {
            low_frequency_mono_collapse: 0.0,
            ..SoundboardConfig::default()
        });
        let mut collapsed = SoundboardModel::new(SoundboardConfig {
            low_frequency_mono_collapse: 1.0,
            ..SoundboardConfig::default()
        });
        let mut wide_side_energy = 0.0;
        let mut collapsed_side_energy = 0.0;

        for _ in 0..512 {
            let drive = SoundboardDrive {
                bridge_force: 0.26,
                bridge_velocity: 0.14,
                bridge_energy: 0.26,
                string_side: 0.22,
            };
            let wide_output = wide.render(drive);
            let collapsed_output = collapsed.render(drive);
            wide_side_energy += (wide_output.left - wide_output.right).abs();
            collapsed_side_energy += (collapsed_output.left - collapsed_output.right).abs();
        }

        assert!(collapsed_side_energy < wide_side_energy * 0.70);
    }
}
