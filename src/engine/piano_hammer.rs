const CONTACT_OVERSAMPLE: usize = 3;

#[derive(Debug, Clone, Copy)]
pub(super) struct StrikeProfile {
    pub(super) initial_activity: f32,
    pub(super) output_gain: f32,
    pub(super) pan: f32,
}

impl StrikeProfile {
    pub(super) fn from_note(note: u8, velocity: u8, soft_pedal_amount: f32) -> Self {
        let normalized_velocity = velocity as f32 / 127.0;
        let register_position = ((note as f32 - 21.0).max(0.0) / 87.0).clamp(0.0, 1.0);
        let soft_pedal_amount = soft_pedal_amount.clamp(0.0, 1.0);
        let register_output_scale = 0.82 - (register_position * 0.10);

        Self {
            initial_activity: (0.015 + (normalized_velocity.powf(1.20) * 0.30))
                * (1.0 - (0.28 * soft_pedal_amount)),
            output_gain: ((0.040 + (normalized_velocity.powf(1.75) * 0.52))
                * register_output_scale)
                * (1.0 - (0.52 * soft_pedal_amount)),
            pan: (((note as f32 - 60.0) / 24.0).clamp(-1.0, 1.0)) * 0.08,
        }
    }
}

#[derive(Debug, Clone, Copy, Default)]
pub(super) struct HammerFrame {
    pub(super) harmonic_brightness: f32,
    pub(super) excitation_drive: f32,
    pub(super) string_transfer: f32,
    pub(super) impact_emphasis: f32,
    pub(super) contact_damping: f32,
    pub(super) direct_impulse: f32,
    pub(super) attack_left: f32,
    pub(super) attack_right: f32,
}

#[derive(Debug, Clone, Copy)]
pub(super) struct HammerModel {
    dt: f32,
    mass_inverse: f32,
    spring_stiffness: f32,
    compression_power: f32,
    hysteresis: f32,
    impedance_inverse: f32,
    bridge_coupling: f32,
    contact_position: f32,
    contact_velocity: f32,
    contact_acceleration: f32,
    contact_force: f32,
    previous_compression_shape: f32,
    strike_brightness_bias: f32,
    impact_pulse: f32,
    impact_pulse_decay: f32,
    chatter_amount: f32,
    chatter_decay: f32,
    chatter_phase: f32,
    chatter_phase_step: f32,
    residual_bloom: f32,
    bloom_decay: f32,
    attack_noise: f32,
    attack_noise_decay: f32,
    noise_state: u32,
    noise_color: f32,
    noise_lowpass: f32,
    escape_counter: u8,
    escape_delay: u8,
    escaped: bool,
}

impl Default for HammerModel {
    fn default() -> Self {
        Self {
            dt: 0.0,
            mass_inverse: 0.0,
            spring_stiffness: 0.0,
            compression_power: 0.0,
            hysteresis: 0.0,
            impedance_inverse: 0.0,
            bridge_coupling: 0.0,
            contact_position: 0.0,
            contact_velocity: 0.0,
            contact_acceleration: 0.0,
            contact_force: 0.0,
            previous_compression_shape: 0.0,
            strike_brightness_bias: 0.0,
            impact_pulse: 0.0,
            impact_pulse_decay: 0.0,
            chatter_amount: 0.0,
            chatter_decay: 0.0,
            chatter_phase: 0.0,
            chatter_phase_step: 0.0,
            residual_bloom: 0.0,
            bloom_decay: 0.0,
            attack_noise: 0.0,
            attack_noise_decay: 0.0,
            noise_state: 0,
            noise_color: 0.0,
            noise_lowpass: 0.0,
            escape_counter: 0,
            escape_delay: 0,
            escaped: false,
        }
    }
}

impl HammerModel {
    pub(super) fn start(
        &mut self,
        note: u8,
        velocity: u8,
        sample_rate_hz: u32,
        soft_pedal_amount: f32,
    ) {
        let normalized_velocity = velocity as f32 / 127.0;
        let register_position = ((note as f32 - 21.0).max(0.0) / 87.0).clamp(0.0, 1.0);
        let soft_pedal_amount = soft_pedal_amount.clamp(0.0, 1.0);

        let hammer_mass =
            (0.009 + ((1.0 - register_position) * 0.006)) * (1.0 + (0.12 * soft_pedal_amount));
        let impact_velocity =
            (0.34 + (normalized_velocity * 2.45)) * (1.0 - (0.35 * soft_pedal_amount));

        self.dt = 1.0 / (sample_rate_hz as f32 * CONTACT_OVERSAMPLE as f32);
        self.mass_inverse = 1.0 / hammer_mass.max(1.0e-5);
        self.spring_stiffness =
            3_000.0 + (normalized_velocity * 5_800.0) + ((1.0 - register_position) * 2_400.0);
        self.compression_power = 2.15 + (register_position * 0.75);
        self.hysteresis = 0.18 + (normalized_velocity * 0.10);
        self.impedance_inverse = 0.0028 + ((1.0 - register_position) * 0.0020);
        self.bridge_coupling = 0.0015 + ((1.0 - register_position) * 0.0022);
        self.contact_position = 0.0;
        self.contact_velocity = impact_velocity;
        self.contact_acceleration = 0.0;
        self.contact_force = 0.0;
        self.previous_compression_shape = 0.0;
        self.strike_brightness_bias =
            normalized_velocity.powf(1.35) * (1.0 - (0.70 * soft_pedal_amount));
        self.impact_pulse =
            (0.04 + (normalized_velocity.powf(1.90) * 0.46)) * (1.0 - (0.66 * soft_pedal_amount));
        self.impact_pulse_decay =
            (0.78 - (normalized_velocity * 0.10) + (register_position * 0.03)).clamp(0.52, 0.86);
        self.chatter_amount = normalized_velocity.powf(1.4)
            * (0.08 + (register_position * 0.03))
            * (1.0 - (0.55 * soft_pedal_amount));
        self.chatter_decay = (0.81 - (normalized_velocity * 0.08)).clamp(0.62, 0.88);
        self.chatter_phase = 0.0;
        self.chatter_phase_step = 1.35 + (register_position * 0.35);
        self.residual_bloom =
            (0.05 + (normalized_velocity.powf(1.25) * 0.34)) * (1.0 - (0.58 * soft_pedal_amount));
        self.bloom_decay = 0.969 - (normalized_velocity * 0.016);
        self.attack_noise = (0.005 + (normalized_velocity.powf(1.7) * 0.075))
            * (0.22 + (register_position * 0.10))
            * (1.0 - (0.72 * soft_pedal_amount));
        self.attack_noise_decay = 0.44 + (register_position * 0.05);
        self.noise_state =
            0x9E37_79B9 ^ ((note as u32) << 11) ^ ((velocity as u32) << 3) ^ sample_rate_hz;
        self.noise_color = 0.0;
        self.noise_lowpass = 0.08 + (normalized_velocity * 0.18) + (register_position * 0.04);
        self.escape_counter = 0;
        self.escape_delay = 2 + ((1.0 - register_position) * 4.0) as u8;
        self.escaped = false;
    }

    pub(super) fn step(
        &mut self,
        string_feedback: f32,
        hammer_noise_gain: f32,
        pan: f32,
    ) -> HammerFrame {
        let bridge_motion = string_feedback.abs() * self.bridge_coupling;

        for _ in 0..CONTACT_OVERSAMPLE {
            self.advance_contact(bridge_motion);
        }

        let dynamic_bloom = (self.contact_force * 0.014).sqrt().clamp(0.0, 1.45);
        self.residual_bloom = (self.residual_bloom * self.bloom_decay).max(dynamic_bloom);

        let raw_noise = self.next_noise();
        self.noise_color += (raw_noise - self.noise_color) * self.noise_lowpass;
        let noise_edge = raw_noise - self.noise_color;
        let noise_scale = 0.02
            + (self.residual_bloom * 0.02)
            + (dynamic_bloom * 0.06)
            + (self.impact_pulse * 0.04);
        let noise_mix = (self.noise_color * 0.30)
            + (noise_edge * (0.70 + (self.strike_brightness_bias * 0.55)));
        let attack_noise = noise_mix
            * self.attack_noise
            * hammer_noise_gain
            * noise_scale
            * (1.0 + (self.impact_pulse * 0.18));
        let chatter = self.chatter_phase.sin() * self.chatter_amount;
        self.attack_noise *= self.attack_noise_decay;

        let in_contact = self.contact_force > 1.0e-5;
        // Allow the sign of contact_velocity through: positive during
        // the compression phase, negative during rebound.  This produces
        // a naturally bipolar excitation pulse that avoids injecting DC
        // into the string resonators.
        let contact_velocity_drive = if in_contact {
            self.contact_velocity * 0.010
        } else {
            0.0
        };

        let frame = HammerFrame {
            harmonic_brightness: (self.strike_brightness_bias * 0.26)
                + (self.residual_bloom * 0.82)
                + (dynamic_bloom * 0.70)
                + (self.impact_pulse * 0.08),
            // Resonator drive uses only the short-lived contact force and
            // impact pulse.  Bloom terms modulate brightness (above), not
            // the resonator drive signal, to avoid sustained DC injection
            // into the high-DC-gain string resonators.
            excitation_drive: (self.contact_force * 0.00012) + contact_velocity_drive,
            string_transfer: self.impact_pulse
                * (0.14 + (self.strike_brightness_bias * 0.36)),
            impact_emphasis: self.impact_pulse * (0.06 + (self.strike_brightness_bias * 0.40)),
            contact_damping: (dynamic_bloom * 0.05 + self.impact_pulse * 0.08).clamp(0.0, 0.30),
            direct_impulse: (self.impact_pulse * (self.strike_brightness_bias * 0.02))
                + (chatter * 0.004),
            attack_left: attack_noise * (0.72 - (pan * 0.14)),
            attack_right: attack_noise * (0.72 + (pan * 0.14)),
        };
        self.impact_pulse *= self.impact_pulse_decay;
        self.chatter_amount *= self.chatter_decay;
        self.chatter_phase += self.chatter_phase_step;
        frame
    }

    pub(super) fn reset(&mut self) {
        *self = Self::default();
    }

    pub(super) fn activity(&self) -> f32 {
        let contact_motion = self.contact_force * 0.00018;
        let transient_energy = self.impact_pulse + self.attack_noise + self.residual_bloom;
        contact_motion + (transient_energy * 0.24)
    }

    fn advance_contact(&mut self, bridge_motion: f32) {
        if self.escaped {
            self.contact_force = 0.0;
            self.contact_velocity *= 0.35;
            self.contact_position *= 0.35;
            self.previous_compression_shape = 0.0;
            return;
        }

        let compression = (self.contact_position - bridge_motion).max(0.0);
        let compression_shape = compression.powf(self.compression_power);
        let compression_delta = (compression_shape - self.previous_compression_shape).max(0.0);
        let contact_force = if self.escaped {
            0.0
        } else {
            self.spring_stiffness * (compression_shape + (self.hysteresis * compression_delta))
        };

        self.contact_force = contact_force.max(0.0);
        self.contact_acceleration = -self.contact_force * self.mass_inverse;
        self.contact_velocity += self.contact_acceleration * self.dt;
        self.contact_position +=
            (self.contact_velocity - bridge_motion - (self.contact_force * self.impedance_inverse))
                * self.dt;
        self.contact_position = self.contact_position.max(0.0);
        self.previous_compression_shape = compression_shape;

        if self.contact_force <= 1.0e-6 {
            self.contact_velocity *= 0.82;
        }

        if self.contact_force <= 1.0e-6 {
            self.escape_counter = self.escape_counter.saturating_add(1);
            if self.escape_counter > self.escape_delay {
                self.escaped = true;
            }
        } else {
            self.escape_counter = 0;
        }
    }

    fn next_noise(&mut self) -> f32 {
        self.noise_state = self
            .noise_state
            .wrapping_mul(1_664_525)
            .wrapping_add(1_013_904_223);
        let unit = (self.noise_state >> 8) as f32 / ((u32::MAX >> 8) as f32);
        (unit * 2.0) - 1.0
    }
}

#[cfg(test)]
mod tests {
    use super::{HammerModel, StrikeProfile};

    #[test]
    fn soft_pedal_reduces_strike_energy() {
        let natural = StrikeProfile::from_note(60, 100, 0.0);
        let soft = StrikeProfile::from_note(60, 100, 1.0);
        assert!(soft.initial_activity < natural.initial_activity);
    }

    #[test]
    fn louder_strike_generates_more_initial_brightness() {
        let mut soft = HammerModel::default();
        let mut loud = HammerModel::default();
        soft.start(60, 32, 48_000, 0.0);
        loud.start(60, 120, 48_000, 0.0);

        let soft_frame = soft.step(0.0, 1.0, 0.0);
        let loud_frame = loud.step(0.0, 1.0, 0.0);

        assert!(loud_frame.harmonic_brightness > soft_frame.harmonic_brightness);
        assert!(loud_frame.excitation_drive > soft_frame.excitation_drive);
    }

    #[test]
    fn hammer_contact_eventually_escapes() {
        let mut hammer = HammerModel::default();
        hammer.start(60, 100, 48_000, 0.0);

        let mut last_frame = super::HammerFrame::default();
        for _ in 0..4096 {
            last_frame = hammer.step(0.0, 1.0, 0.0);
        }

        assert!(last_frame.impact_emphasis < 0.001);
        assert!(last_frame.direct_impulse.abs() < 0.001);
    }
}
