#[derive(Debug, Clone, Copy)]
pub(super) struct DamperFrame {
    pub(super) openness: f32,
    pub(super) felt_contact: f32,
    pub(super) choke: f32,
}

impl Default for DamperFrame {
    fn default() -> Self {
        Self {
            openness: 1.0,
            felt_contact: 0.0,
            choke: 0.0,
        }
    }
}

#[derive(Debug, Clone, Copy)]
pub(super) struct DamperModel {
    openness: f32,
    target_openness: f32,
}

impl Default for DamperModel {
    fn default() -> Self {
        Self {
            openness: 1.0,
            target_openness: 1.0,
        }
    }
}

impl DamperModel {
    pub(super) fn reset_open(&mut self) {
        self.openness = 1.0;
        self.target_openness = 1.0;
    }

    pub(super) fn set_control(&mut self, key_is_down: bool, pedal_lift: f32) {
        self.target_openness = if key_is_down {
            1.0
        } else {
            pedal_lift.clamp(0.0, 1.0)
        };
    }

    pub(super) fn step(&mut self, register_position: f32) -> DamperFrame {
        let target_delta = self.target_openness - self.openness;
        let response = if target_delta >= 0.0 {
            // Lifting the damper is comparatively quick: the key/action or pedal
            // shaft pulls the felt away before the string can speak freely.
            0.070
        } else {
            // Closing is gentler and more pitch dependent. Bass dampers are heavier
            // and strong low modes should not vanish the instant felt touches.
            0.014 + (register_position * 0.010)
        };
        self.openness += target_delta * response;

        let closedness = 1.0 - self.openness.clamp(0.0, 1.0);
        DamperFrame {
            openness: self.openness,
            felt_contact: smoothstep(((closedness - 0.08) / 0.70).clamp(0.0, 1.0)),
            choke: smoothstep(((closedness - 0.38) / 0.52).clamp(0.0, 1.0)),
        }
    }
}

fn smoothstep(x: f32) -> f32 {
    let x = x.clamp(0.0, 1.0);
    x * x * (3.0 - (2.0 * x))
}

#[cfg(test)]
mod tests {
    use super::DamperModel;

    #[test]
    fn key_press_overrides_closed_pedal_state() {
        let mut damper = DamperModel::default();
        damper.set_control(false, 0.0);
        for _ in 0..128 {
            let _ = damper.step(0.5);
        }

        damper.set_control(true, 0.0);
        let lifted = damper.step(0.5);

        assert!(lifted.openness > 0.0);
        assert!(lifted.choke < 1.0);
    }

    #[test]
    fn half_pedal_settles_between_closed_and_open() {
        let mut damper = DamperModel::default();
        damper.set_control(false, 0.45);

        let mut frame = damper.step(0.5);
        for _ in 0..512 {
            frame = damper.step(0.5);
        }

        assert!((0.40..0.50).contains(&frame.openness));
        assert!(frame.felt_contact > 0.0);
        assert!(frame.choke < 1.0);
    }
}
