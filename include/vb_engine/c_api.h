#ifndef VB_ENGINE_C_API_H
#define VB_ENGINE_C_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VB_ENGINE_ABI_VERSION 1u

typedef struct vb_engine_handle vb_engine_handle;

typedef enum vb_engine_result {
    VB_ENGINE_OK = 0,
    VB_ENGINE_ERROR_INVALID_ARGUMENT = 1,
    VB_ENGINE_ERROR_ABI_MISMATCH = 2,
    VB_ENGINE_ERROR_QUEUE_FULL = 3,
    VB_ENGINE_ERROR_INTERNAL = 4
} vb_engine_result;

typedef enum vb_instrument {
    VB_INSTRUMENT_ACOUSTIC_GRAND_PIANO = 0,
    VB_INSTRUMENT_CLEAN_ELECTRIC_GUITAR = 1
} vb_instrument;

typedef enum vb_pedal_mode {
    VB_PEDAL_MODE_AUTO = 0,
    VB_PEDAL_MODE_BINARY = 1,
    VB_PEDAL_MODE_CONTINUOUS = 2
} vb_pedal_mode;

typedef struct vb_engine_config {
    uint32_t struct_size;
    uint32_t abi_version;
    double sample_rate;
    uint32_t max_block_size;
    uint32_t max_voices;
    uint32_t default_instrument;
    const char* piano_sfz_path;
    float piano_humanize_timing_ms;
    float piano_humanize_velocity;
    float piano_reverb_wet;
    float piano_mic_mix;
    float piano_presence;
    float piano_stretch_strength;
    uint8_t piano_disk_streaming_enabled;
    uint32_t piano_disk_stream_threshold_frames;
    uint32_t piano_pedal_mode;
    uint8_t piano_pedal_binary_threshold;
    uint8_t piano_pedal_noise_enabled;
} vb_engine_config;

typedef struct vb_engine_diagnostics {
    uint32_t struct_size;
    uint64_t voice_steals;
    float worst_stolen_activity;
    float max_output_delta;
    uint64_t hard_jump_events;
    uint64_t non_finite_output_samples;
} vb_engine_diagnostics;

uint32_t vb_engine_get_abi_version(void);
void vb_engine_default_config(vb_engine_config* config_out);

vb_engine_result vb_engine_create(const vb_engine_config* config, vb_engine_handle** out_handle);
void vb_engine_destroy(vb_engine_handle* handle);

vb_engine_result vb_engine_note_on(vb_engine_handle* handle, uint8_t channel, uint8_t note, uint8_t velocity);
vb_engine_result vb_engine_note_off(vb_engine_handle* handle, uint8_t channel, uint8_t note, uint8_t velocity);
vb_engine_result vb_engine_control_change(vb_engine_handle* handle, uint8_t channel, uint8_t control, uint8_t value);

vb_engine_result vb_engine_process(
    vb_engine_handle* handle,
    float* const* outputs,
    uint32_t num_channels,
    uint32_t num_frames
);
vb_engine_result vb_engine_get_diagnostics(vb_engine_handle* handle, vb_engine_diagnostics* out_diagnostics);
vb_engine_result vb_engine_reset_diagnostics(vb_engine_handle* handle);

#ifdef __cplusplus
}
#endif

#endif
