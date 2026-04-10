//! Reduced-physics piano modeling primitives for VB-Engine.
//!
//! This crate is internal for now. It exists so the realtime engine can grow
//! around named piano mechanics instead of burying bridge, soundboard, and
//! radiation behavior inside broad output-mix stages.

pub mod bridge_junction;
pub mod soundboard;
pub mod sympathetic;

pub(crate) const TAU: f32 = core::f32::consts::PI * 2.0;

/// Soft saturation used at mechanical coupling boundaries.
///
/// This keeps reduced modal and bridge states bounded without turning the
/// limiter into a visible musical effect.
#[must_use]
pub fn soft_clip(sample: f32) -> f32 {
    sample / (1.0 + sample.abs())
}

pub(crate) fn radius_from_t60(t60_seconds: f32, sample_rate_hz: u32) -> f32 {
    10.0_f32
        .powf(-3.0 / (t60_seconds.max(0.02) * sample_rate_hz.max(1) as f32))
        .clamp(0.0, 0.999_995)
}
