#![allow(non_camel_case_types)]

use std::ffi::c_char;

use crate::{Engine, EngineConfig};

pub const VB_ENGINE_ABI_VERSION: u32 = 3;

#[repr(C)]
pub struct vb_engine_handle {
    engine: Engine,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum vb_engine_result {
    VB_ENGINE_OK = 0,
    VB_ENGINE_ERROR_INVALID_ARGUMENT = 1,
    VB_ENGINE_ERROR_ABI_MISMATCH = 2,
    VB_ENGINE_ERROR_QUEUE_FULL = 3,
    VB_ENGINE_ERROR_INTERNAL = 4,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum vb_instrument {
    VB_INSTRUMENT_ACOUSTIC_GRAND_PIANO = 0,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct vb_engine_config {
    pub struct_size: u32,
    pub abi_version: u32,
    pub sample_rate: f64,
    pub max_block_size: u32,
    pub max_voices: u32,
    pub default_instrument: u32,
    pub sustain_pedal_threshold: u8,
    pub master_gain: f32,
    pub string_gain: f32,
    pub mechanical_gain: f32,
    pub hammer_noise_gain: f32,
    pub resonance_gain: f32,
    pub body_gain: f32,
    pub ambience_gain: f32,
    pub reserved_0: *const c_char,
    pub reserved_1: u32,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct vb_engine_diagnostics {
    pub struct_size: u32,
    pub active_voices: u32,
    pub voice_steals: u64,
    pub worst_stolen_activity: f32,
    pub max_output_delta: f32,
    pub hard_jump_events: u64,
    pub non_finite_output_samples: u64,
}

#[unsafe(no_mangle)]
pub extern "C" fn vb_engine_get_abi_version() -> u32 {
    VB_ENGINE_ABI_VERSION
}

#[unsafe(no_mangle)]
/// Fills `config_out` with the default engine configuration.
///
/// # Safety
/// `config_out` must be null or point to writable memory for a
/// `vb_engine_config` value.
pub unsafe extern "C" fn vb_engine_default_config(config_out: *mut vb_engine_config) {
    if config_out.is_null() {
        return;
    }
    // SAFETY: caller provided writable pointer or null handled above.
    unsafe {
        *config_out = vb_engine_config {
            struct_size: std::mem::size_of::<vb_engine_config>() as u32,
            abi_version: VB_ENGINE_ABI_VERSION,
            sample_rate: 48_000.0,
            max_block_size: 512,
            max_voices: 64,
            default_instrument: vb_instrument::VB_INSTRUMENT_ACOUSTIC_GRAND_PIANO as u32,
            sustain_pedal_threshold: 64,
            master_gain: 1.40,
            string_gain: 1.25,
            mechanical_gain: 0.24,
            hammer_noise_gain: 0.08,
            resonance_gain: 0.26,
            body_gain: 0.22,
            ambience_gain: 0.14,
            reserved_0: std::ptr::null(),
            reserved_1: 0,
        };
    }
}

#[unsafe(no_mangle)]
/// Creates a new engine handle from the provided configuration.
///
/// # Safety
/// `config` must point to a readable `vb_engine_config`, and `out_handle` must
/// point to writable memory for a single `vb_engine_handle*`.
pub unsafe extern "C" fn vb_engine_create(
    config: *const vb_engine_config,
    out_handle: *mut *mut vb_engine_handle,
) -> vb_engine_result {
    if config.is_null() || out_handle.is_null() {
        return vb_engine_result::VB_ENGINE_ERROR_INVALID_ARGUMENT;
    }

    // SAFETY: pointers validated non-null above.
    let config = unsafe { &*config };
    let legacy_size = std::mem::offset_of!(vb_engine_config, sustain_pedal_threshold);
    if config.struct_size < legacy_size as u32 || config.abi_version != VB_ENGINE_ABI_VERSION {
        return vb_engine_result::VB_ENGINE_ERROR_ABI_MISMATCH;
    }
    if config.sample_rate <= 0.0 || config.max_block_size == 0 || config.max_voices == 0 {
        return vb_engine_result::VB_ENGINE_ERROR_INVALID_ARGUMENT;
    }
    if config.default_instrument != vb_instrument::VB_INSTRUMENT_ACOUSTIC_GRAND_PIANO as u32 {
        return vb_engine_result::VB_ENGINE_ERROR_INVALID_ARGUMENT;
    }

    let engine_config = EngineConfig {
        sample_rate_hz: config.sample_rate.round() as u32,
        max_block_size: config.max_block_size as usize,
        max_voices: config.max_voices as usize,
        master_gain: config.master_gain,
        string_gain: config.string_gain,
        mechanical_gain: config.mechanical_gain,
        sustain_pedal_threshold: config.sustain_pedal_threshold.max(1),
        hammer_noise_gain: config.hammer_noise_gain,
        resonance_gain: config.resonance_gain,
        body_gain: config.body_gain,
        ambience_gain: config.ambience_gain,
    };

    match Engine::new(engine_config) {
        Ok(engine) => {
            let boxed = Box::new(vb_engine_handle { engine });
            // SAFETY: out_handle validated non-null above.
            unsafe {
                *out_handle = Box::into_raw(boxed);
            }
            vb_engine_result::VB_ENGINE_OK
        }
        Err(_) => vb_engine_result::VB_ENGINE_ERROR_INTERNAL,
    }
}

#[unsafe(no_mangle)]
/// Destroys a handle previously returned by `vb_engine_create`.
///
/// # Safety
/// `handle` must be null or a valid engine handle returned by
/// `vb_engine_create` that has not already been destroyed.
pub unsafe extern "C" fn vb_engine_destroy(handle: *mut vb_engine_handle) {
    if handle.is_null() {
        return;
    }
    // SAFETY: ownership transferred from create; reclaim once.
    unsafe {
        drop(Box::from_raw(handle));
    }
}

#[unsafe(no_mangle)]
/// Starts or retriggers a note on the engine.
///
/// # Safety
/// `handle` must be a valid engine handle returned by `vb_engine_create`.
pub unsafe extern "C" fn vb_engine_note_on(
    handle: *mut vb_engine_handle,
    _channel: u8,
    note: u8,
    velocity: u8,
) -> vb_engine_result {
    if handle.is_null() {
        return vb_engine_result::VB_ENGINE_ERROR_INVALID_ARGUMENT;
    }
    // SAFETY: handle checked non-null.
    unsafe { (&mut *handle).engine.note_on(note, velocity) };
    vb_engine_result::VB_ENGINE_OK
}

#[unsafe(no_mangle)]
/// Releases a note on the engine.
///
/// # Safety
/// `handle` must be a valid engine handle returned by `vb_engine_create`.
pub unsafe extern "C" fn vb_engine_note_off(
    handle: *mut vb_engine_handle,
    _channel: u8,
    note: u8,
    _velocity: u8,
) -> vb_engine_result {
    if handle.is_null() {
        return vb_engine_result::VB_ENGINE_ERROR_INVALID_ARGUMENT;
    }
    // SAFETY: handle checked non-null.
    unsafe { (&mut *handle).engine.note_off(note) };
    vb_engine_result::VB_ENGINE_OK
}

#[unsafe(no_mangle)]
/// Sends a control-change event to the engine.
///
/// # Safety
/// `handle` must be a valid engine handle returned by `vb_engine_create`.
pub unsafe extern "C" fn vb_engine_control_change(
    handle: *mut vb_engine_handle,
    _channel: u8,
    control: u8,
    value: u8,
) -> vb_engine_result {
    if handle.is_null() {
        return vb_engine_result::VB_ENGINE_ERROR_INVALID_ARGUMENT;
    }
    // SAFETY: handle checked non-null.
    unsafe { (&mut *handle).engine.control_change(control, value) };
    vb_engine_result::VB_ENGINE_OK
}

#[unsafe(no_mangle)]
/// Renders `num_frames` of stereo audio into the provided output buffers.
///
/// # Safety
/// `handle` must be a valid engine handle returned by `vb_engine_create`.
/// `outputs` must point to at least two valid writable buffers, and each buffer
/// must contain space for `num_frames` samples.
pub unsafe extern "C" fn vb_engine_process(
    handle: *mut vb_engine_handle,
    outputs: *mut *mut f32,
    num_channels: u32,
    num_frames: u32,
) -> vb_engine_result {
    if handle.is_null() || outputs.is_null() || num_channels < 2 {
        return vb_engine_result::VB_ENGINE_ERROR_INVALID_ARGUMENT;
    }
    // SAFETY: pointers validated above; caller provides at least two output pointers.
    let (left_ptr, right_ptr) = unsafe { (*outputs, *outputs.add(1)) };
    if left_ptr.is_null() || right_ptr.is_null() {
        return vb_engine_result::VB_ENGINE_ERROR_INVALID_ARGUMENT;
    }
    // SAFETY: output buffers expected to have num_frames capacity by caller contract.
    let left = unsafe { std::slice::from_raw_parts_mut(left_ptr, num_frames as usize) };
    let right = unsafe { std::slice::from_raw_parts_mut(right_ptr, num_frames as usize) };
    // SAFETY: handle checked non-null.
    match unsafe { (&mut *handle).engine.render(left, right) } {
        Ok(_) => vb_engine_result::VB_ENGINE_OK,
        Err(_) => vb_engine_result::VB_ENGINE_ERROR_INVALID_ARGUMENT,
    }
}

#[unsafe(no_mangle)]
/// Writes the current diagnostics into `out_diagnostics`.
///
/// # Safety
/// `handle` must be a valid engine handle returned by `vb_engine_create`, and
/// `out_diagnostics` must point to writable memory for a
/// `vb_engine_diagnostics` value.
pub unsafe extern "C" fn vb_engine_get_diagnostics(
    handle: *mut vb_engine_handle,
    out_diagnostics: *mut vb_engine_diagnostics,
) -> vb_engine_result {
    if handle.is_null() || out_diagnostics.is_null() {
        return vb_engine_result::VB_ENGINE_ERROR_INVALID_ARGUMENT;
    }
    let legacy_size = std::mem::offset_of!(vb_engine_diagnostics, active_voices);
    // SAFETY: pointer checked non-null.
    let out = unsafe { &mut *out_diagnostics };
    if out.struct_size < legacy_size as u32 {
        return vb_engine_result::VB_ENGINE_ERROR_INVALID_ARGUMENT;
    }

    // SAFETY: handle checked non-null.
    let stats = unsafe { (&*handle).engine.diagnostics() };
    out.active_voices = stats.active_voices as u32;
    out.voice_steals = stats.voice_steals;
    out.worst_stolen_activity = stats.worst_stolen_activity;
    out.max_output_delta = stats.max_output_delta;
    out.hard_jump_events = stats.hard_jump_events;
    out.non_finite_output_samples = stats.non_finite_output_samples;
    vb_engine_result::VB_ENGINE_OK
}

#[unsafe(no_mangle)]
/// Clears the engine's accumulated diagnostics counters.
///
/// # Safety
/// `handle` must be a valid engine handle returned by `vb_engine_create`.
pub unsafe extern "C" fn vb_engine_reset_diagnostics(
    handle: *mut vb_engine_handle,
) -> vb_engine_result {
    if handle.is_null() {
        return vb_engine_result::VB_ENGINE_ERROR_INVALID_ARGUMENT;
    }
    // SAFETY: handle checked non-null.
    unsafe { (&mut *handle).engine.reset_diagnostics() };
    vb_engine_result::VB_ENGINE_OK
}
