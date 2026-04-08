use std::ptr;

use vb_engine::{
    VB_ENGINE_ABI_VERSION, vb_engine_config, vb_engine_create, vb_engine_default_config,
    vb_engine_destroy, vb_engine_diagnostics, vb_engine_get_abi_version, vb_engine_get_diagnostics,
    vb_engine_handle, vb_engine_note_off, vb_engine_note_on, vb_engine_process,
    vb_engine_reset_diagnostics, vb_engine_result,
};

#[test]
fn c_api_can_create_render_and_destroy_engine() {
    let mut config = vb_engine_config {
        struct_size: 0,
        abi_version: 0,
        sample_rate: 0.0,
        max_block_size: 0,
        max_voices: 0,
        default_instrument: 0,
        sustain_pedal_threshold: 0,
        master_gain: 0.0,
        string_gain: 0.0,
        mechanical_gain: 0.0,
        hammer_noise_gain: 0.0,
        resonance_gain: 0.0,
        body_gain: 0.0,
        ambience_gain: 0.0,
        reserved_0: ptr::null(),
        reserved_1: 0,
    };
    unsafe { vb_engine_default_config(&mut config) };
    assert_eq!(vb_engine_get_abi_version(), VB_ENGINE_ABI_VERSION);

    let mut handle: *mut vb_engine_handle = ptr::null_mut();
    assert_eq!(
        unsafe { vb_engine_create(&config, &mut handle) },
        vb_engine_result::VB_ENGINE_OK
    );
    assert!(!handle.is_null());

    assert_eq!(
        unsafe { vb_engine_note_on(handle, 0, 60, 112) },
        vb_engine_result::VB_ENGINE_OK
    );
    let mut left = vec![0.0_f32; 128];
    let mut right = vec![0.0_f32; 128];
    let mut outputs = [left.as_mut_ptr(), right.as_mut_ptr()];
    assert_eq!(
        unsafe { vb_engine_process(handle, outputs.as_mut_ptr(), 2, 128) },
        vb_engine_result::VB_ENGINE_OK
    );
    assert!(
        left.iter()
            .zip(&right)
            .any(|(l, r)| l.abs() + r.abs() > 0.0)
    );

    let mut diagnostics = vb_engine_diagnostics {
        struct_size: std::mem::size_of::<vb_engine_diagnostics>() as u32,
        ..vb_engine_diagnostics::default()
    };
    assert_eq!(
        unsafe { vb_engine_get_diagnostics(handle, &mut diagnostics) },
        vb_engine_result::VB_ENGINE_OK
    );
    assert_eq!(diagnostics.active_voices, 1);
    assert_eq!(diagnostics.voice_steals, 0);

    assert_eq!(
        unsafe { vb_engine_note_off(handle, 0, 60, 0) },
        vb_engine_result::VB_ENGINE_OK
    );
    assert_eq!(
        unsafe { vb_engine_reset_diagnostics(handle) },
        vb_engine_result::VB_ENGINE_OK
    );
    unsafe { vb_engine_destroy(handle) };
}
