//! Bridge-to-soundboard junction model.
//!
//! The bridge junction is the reduced mechanical hub between the active string
//! groups, sympathetic receptors, and soundboard radiation.

use crate::{
    soft_clip,
    soundboard::{SoundboardConfig, SoundboardDrive, SoundboardModel},
};

/// Runtime configuration for the reduced bridge junction.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct BridgeJunctionConfig {
    /// Audio sample rate used by the soundboard modal bank.
    pub sample_rate_hz: u32,
    /// Vertical preload from string downbearing into the bridge and board.
    pub downbearing_preload: f32,
    /// Coupling from bridge motion into the soundboard.
    pub bridge_to_soundboard_coupling: f32,
    /// Small loss branch into the cast-iron plate, treated as damped color.
    pub plate_leak: f32,
    /// Soundboard modal gain.
    pub soundboard_modal_gain: f32,
    /// Stereo observation width for soundboard radiation.
    pub soundboard_radiation_width: f32,
    /// How aggressively low-frequency soundboard radiation collapses toward mono.
    pub low_frequency_mono_collapse: f32,
}

impl Default for BridgeJunctionConfig {
    fn default() -> Self {
        Self {
            sample_rate_hz: 48_000,
            downbearing_preload: 0.62,
            bridge_to_soundboard_coupling: 0.86,
            plate_leak: 0.035,
            soundboard_modal_gain: 1.0,
            soundboard_radiation_width: 1.0,
            low_frequency_mono_collapse: 0.78,
        }
    }
}

/// Reduced string-group drive reaching the bridge this frame.
#[derive(Debug, Clone, Copy, Default, PartialEq)]
pub struct BridgeInput {
    /// Summed string force into the bridge.
    pub string_force: f32,
    /// Reduced string velocity or bridge-band motion proxy.
    pub string_velocity: f32,
    /// Mono direct-string observation reaching the bridge area.
    pub string_mono: f32,
    /// Side component from the string group before board radiation.
    pub string_side: f32,
    /// Damper lift amount from `0.0` closed to `1.0` fully open.
    pub damper_lift: f32,
}

/// Per-frame output from the bridge junction.
#[derive(Debug, Clone, Copy, Default, PartialEq)]
pub struct BridgeJunctionFrame {
    /// Left soundboard radiation readout.
    pub soundboard_left: f32,
    /// Right soundboard radiation readout.
    pub soundboard_right: f32,
    /// Bridge motion readout before room or final output shaping.
    pub bridge_motion: f32,
    /// Drive value suitable for sympathetic receptor excitation.
    pub receptor_drive: f32,
    /// Reduced reflected force back toward strings for future closed-loop use.
    pub reflected_string_force: f32,
    /// Stored soundboard/bridge energy after this frame.
    pub stored_energy: f32,
    /// Low-band board motion after this frame.
    pub low_board_motion: f32,
    /// Mid-band board motion after this frame.
    pub mid_board_motion: f32,
    /// Air-motion proxy after this frame.
    pub air_board_motion: f32,
    /// Surviving side-board motion after low-frequency mono collapse.
    pub side_board_motion: f32,
}

/// Reduced bridge junction plus soundboard modal bank.
#[derive(Debug, Clone)]
pub struct BridgeJunction {
    soundboard: SoundboardModel,
    bridge_position: f32,
    bridge_velocity: f32,
    bridge_energy: f32,
    plate_loss_state: f32,
    downbearing_preload: f32,
    bridge_to_soundboard_coupling: f32,
    plate_leak: f32,
}

impl BridgeJunction {
    /// Create the reduced bridge junction.
    #[must_use]
    pub fn new(config: BridgeJunctionConfig) -> Self {
        let soundboard = SoundboardModel::new(SoundboardConfig {
            sample_rate_hz: config.sample_rate_hz,
            modal_gain: config.soundboard_modal_gain,
            rim_reflection: 0.70 + (config.downbearing_preload.clamp(0.0, 1.0) * 0.18),
            radiation_width: config.soundboard_radiation_width,
            low_frequency_mono_collapse: config.low_frequency_mono_collapse,
        });

        Self {
            soundboard,
            bridge_position: 0.0,
            bridge_velocity: 0.0,
            bridge_energy: 0.0,
            plate_loss_state: 0.0,
            downbearing_preload: config.downbearing_preload.clamp(0.0, 1.0),
            bridge_to_soundboard_coupling: config.bridge_to_soundboard_coupling.clamp(0.0, 2.0),
            plate_leak: config.plate_leak.clamp(0.0, 0.30),
        }
    }

    /// Update realtime coupling controls without resetting bridge or board state.
    pub fn set_controls(
        &mut self,
        downbearing_preload: f32,
        bridge_to_soundboard_coupling: f32,
        plate_leak: f32,
        soundboard_modal_gain: f32,
        soundboard_radiation_width: f32,
        low_frequency_mono_collapse: f32,
    ) {
        self.downbearing_preload = downbearing_preload.clamp(0.0, 1.0);
        self.bridge_to_soundboard_coupling = bridge_to_soundboard_coupling.clamp(0.0, 2.0);
        self.plate_leak = plate_leak.clamp(0.0, 0.30);
        self.soundboard.set_controls(
            soundboard_modal_gain,
            0.70 + (self.downbearing_preload * 0.18),
            soundboard_radiation_width,
            low_frequency_mono_collapse,
        );
    }

    /// Advance the bridge junction by one audio frame.
    #[must_use]
    pub fn process(&mut self, input: BridgeInput) -> BridgeJunctionFrame {
        let preload = self.downbearing_preload;
        let damper_lift = input.damper_lift.clamp(0.0, 1.0);
        let downbearing_transfer = 0.44 + (preload * 0.72);
        let overload_loss = (self.bridge_energy * (0.004 + (preload * 0.010))).clamp(0.0, 0.035);
        let plate_loss = self.plate_leak * (0.35 + (1.0 - damper_lift) * 0.24);
        let damping = (0.006 + overload_loss + plate_loss).clamp(0.002, 0.080);
        let spring = 0.020 + (preload * 0.018);
        let force = soft_clip(
            (input.string_force * downbearing_transfer)
                + (input.string_velocity * 0.26)
                + (input.string_mono * 0.035)
                - (self.bridge_position * spring)
                - (self.bridge_velocity * damping),
        );

        self.bridge_velocity = soft_clip((self.bridge_velocity * (0.986 - damping)) + force);
        self.bridge_position = soft_clip((self.bridge_position * 0.996) + self.bridge_velocity);
        self.plate_loss_state +=
            0.18 * ((self.bridge_velocity * self.plate_leak) - self.plate_loss_state);

        let energy_target = self
            .bridge_velocity
            .abs()
            .max(self.bridge_position.abs() * 0.35)
            .max(input.string_force.abs() * 0.16);
        let energy_response = if energy_target > self.bridge_energy {
            0.060
        } else {
            0.006
        };
        self.bridge_energy += energy_response * (energy_target - self.bridge_energy);

        let soundboard_drive = SoundboardDrive {
            bridge_force: soft_clip(
                (self.bridge_position * 0.22)
                    + (self.bridge_velocity * self.bridge_to_soundboard_coupling),
            ),
            bridge_velocity: self.bridge_velocity,
            bridge_energy: self.bridge_energy,
            string_side: input.string_side,
        };
        let soundboard = self.soundboard.render(soundboard_drive);
        let bridge_motion = soft_clip(
            (self.bridge_velocity * 0.16)
                + (self.bridge_position * 0.028)
                + (self.plate_loss_state * 0.08),
        );

        BridgeJunctionFrame {
            soundboard_left: soundboard.left,
            soundboard_right: soundboard.right,
            bridge_motion,
            receptor_drive: soft_clip(
                (self.bridge_velocity * (0.70 + (damper_lift * 0.22)))
                    + (soundboard.modal_energy * 0.014),
            ),
            reflected_string_force: soft_clip(
                -self.bridge_velocity * (0.020 + (preload * 0.020)) - self.bridge_position * 0.004,
            ),
            stored_energy: self.bridge_energy + soundboard.modal_energy,
            low_board_motion: soundboard.low_board_motion,
            mid_board_motion: soundboard.mid_board_motion,
            air_board_motion: soundboard.air_board_motion,
            side_board_motion: soundboard.side_board_motion,
        }
    }

    /// Gently damp the global junction state without muting the soundboard tail.
    pub fn dampen(&mut self, amount: f32) {
        let amount = amount.clamp(0.0, 1.0);
        self.bridge_position *= amount;
        self.bridge_velocity *= amount;
        self.bridge_energy *= amount;
        self.plate_loss_state *= amount;
        self.soundboard.dampen(amount);
    }
}

#[cfg(test)]
mod tests {
    use super::{BridgeInput, BridgeJunction, BridgeJunctionConfig};

    #[test]
    fn junction_output_remains_finite_under_strong_drive() {
        let mut junction = BridgeJunction::new(BridgeJunctionConfig::default());

        for _ in 0..8_192 {
            let frame = junction.process(BridgeInput {
                string_force: 4.0,
                string_velocity: 3.0,
                string_mono: 2.0,
                string_side: 0.8,
                damper_lift: 1.0,
            });

            assert!(frame.soundboard_left.is_finite());
            assert!(frame.soundboard_right.is_finite());
            assert!(frame.receptor_drive.is_finite());
            assert!(frame.reflected_string_force.is_finite());
        }
    }

    #[test]
    fn downbearing_changes_bridge_transfer() {
        let mut loose = BridgeJunction::new(BridgeJunctionConfig {
            downbearing_preload: 0.05,
            ..BridgeJunctionConfig::default()
        });
        let mut seated = BridgeJunction::new(BridgeJunctionConfig {
            downbearing_preload: 0.78,
            ..BridgeJunctionConfig::default()
        });
        let input = BridgeInput {
            string_force: 0.32,
            string_velocity: 0.18,
            string_mono: 0.10,
            string_side: 0.0,
            damper_lift: 1.0,
        };
        let mut loose_energy = 0.0;
        let mut seated_energy = 0.0;

        for _ in 0..512 {
            loose_energy += loose.process(input).stored_energy;
            seated_energy += seated.process(input).stored_energy;
        }

        assert!(seated_energy > loose_energy);
    }
}
