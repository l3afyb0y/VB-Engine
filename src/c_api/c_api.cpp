#include "vb_engine/c_api.h"

#include <algorithm>
#include <cstddef>
#include <new>

#include "core/engine.hpp"

struct vb_engine_handle {
    vb::Engine engine;
    vb::InstrumentType default_instrument;

    explicit vb_engine_handle(const vb::EngineConfig& config)
        : engine(config), default_instrument(config.default_instrument) {}
};

namespace {

vb::InstrumentType to_instrument(const std::uint32_t instrument) {
    if (instrument == static_cast<std::uint32_t>(VB_INSTRUMENT_CLEAN_ELECTRIC_GUITAR)) {
        return vb::InstrumentType::CleanElectricGuitar;
    }
    return vb::InstrumentType::AcousticGrandPiano;
}

vb::PedalMode to_pedal_mode(const std::uint32_t mode) {
    if (mode == static_cast<std::uint32_t>(VB_PEDAL_MODE_BINARY)) {
        return vb::PedalMode::Binary;
    }
    if (mode == static_cast<std::uint32_t>(VB_PEDAL_MODE_CONTINUOUS)) {
        return vb::PedalMode::Continuous;
    }
    return vb::PedalMode::Auto;
}

vb_engine_result push_event(vb_engine_handle* handle, const vb::MidiEvent& event) {
    if (!handle->engine.enqueue_event(event)) {
        return VB_ENGINE_ERROR_QUEUE_FULL;
    }
    return VB_ENGINE_OK;
}

vb::MidiEvent make_event(
    const vb::MidiEventType type,
    const uint8_t channel,
    const uint8_t data1,
    const uint8_t data2,
    const vb::InstrumentType instrument
) {
    return vb::MidiEvent{
        .type = type,
        .channel = channel,
        .data1 = data1,
        .data2 = data2,
        .instrument = instrument,
    };
}

}  // namespace

extern "C" {

uint32_t vb_engine_get_abi_version(void) {
    return VB_ENGINE_ABI_VERSION;
}

void vb_engine_default_config(vb_engine_config* config_out) {
    if (config_out == nullptr) {
        return;
    }

    config_out->struct_size = sizeof(vb_engine_config);
    config_out->abi_version = VB_ENGINE_ABI_VERSION;
    config_out->sample_rate = 48000.0;
    config_out->max_block_size = 512;
    config_out->max_voices = 64;
    config_out->default_instrument = VB_INSTRUMENT_ACOUSTIC_GRAND_PIANO;
    config_out->piano_sfz_path = nullptr;
    config_out->piano_humanize_timing_ms = 1.2F;
    config_out->piano_humanize_velocity = 2.0F;
    config_out->piano_reverb_wet = 0.08F;
    config_out->piano_mic_mix = 0.28F;
    config_out->piano_presence = 0.56F;
    config_out->piano_stretch_strength = 0.96F;
    config_out->piano_disk_streaming_enabled = 0;
    config_out->piano_disk_stream_threshold_frames = 192000;
    config_out->piano_pedal_mode = VB_PEDAL_MODE_AUTO;
    config_out->piano_pedal_binary_threshold = 64;
    config_out->piano_pedal_noise_enabled = 0;
}

vb_engine_result vb_engine_create(const vb_engine_config* config, vb_engine_handle** out_handle) {
    if (config == nullptr || out_handle == nullptr) {
        return VB_ENGINE_ERROR_INVALID_ARGUMENT;
    }

    constexpr std::size_t kLegacyConfigSize = offsetof(vb_engine_config, piano_sfz_path);
    if (config->struct_size < kLegacyConfigSize || config->abi_version != VB_ENGINE_ABI_VERSION) {
        return VB_ENGINE_ERROR_ABI_MISMATCH;
    }

    if (config->sample_rate <= 0.0 || config->max_block_size == 0 || config->max_voices == 0) {
        return VB_ENGINE_ERROR_INVALID_ARGUMENT;
    }

    vb::EngineConfig internal{};
    internal.sample_rate = static_cast<float>(config->sample_rate);
    internal.max_block_size = config->max_block_size;
    internal.max_voices = config->max_voices;
    internal.default_instrument = to_instrument(config->default_instrument);

    const auto has_field = [&](const std::size_t offset, const std::size_t size) {
        return config->struct_size >= (offset + size);
    };

    if (has_field(offsetof(vb_engine_config, piano_sfz_path), sizeof(config->piano_sfz_path))) {
        internal.piano_sfz_path = config->piano_sfz_path != nullptr ? config->piano_sfz_path : "";
    }
    if (has_field(offsetof(vb_engine_config, piano_humanize_timing_ms), sizeof(config->piano_humanize_timing_ms))) {
        internal.piano_humanize_timing_ms = config->piano_humanize_timing_ms;
    }
    if (has_field(offsetof(vb_engine_config, piano_humanize_velocity), sizeof(config->piano_humanize_velocity))) {
        internal.piano_humanize_velocity = config->piano_humanize_velocity;
    }
    if (has_field(offsetof(vb_engine_config, piano_reverb_wet), sizeof(config->piano_reverb_wet))) {
        internal.piano_reverb_wet = config->piano_reverb_wet;
    }
    if (has_field(offsetof(vb_engine_config, piano_mic_mix), sizeof(config->piano_mic_mix))) {
        internal.piano_mic_mix = std::clamp(config->piano_mic_mix, 0.0F, 1.0F);
    }
    if (has_field(offsetof(vb_engine_config, piano_presence), sizeof(config->piano_presence))) {
        internal.piano_presence = std::clamp(config->piano_presence, 0.0F, 1.0F);
    }
    if (has_field(offsetof(vb_engine_config, piano_stretch_strength), sizeof(config->piano_stretch_strength))) {
        internal.piano_stretch_strength = config->piano_stretch_strength;
    }
    if (has_field(offsetof(vb_engine_config, piano_disk_streaming_enabled), sizeof(config->piano_disk_streaming_enabled))) {
        internal.piano_disk_streaming_enabled = config->piano_disk_streaming_enabled != 0;
    }
    if (has_field(offsetof(vb_engine_config, piano_disk_stream_threshold_frames), sizeof(config->piano_disk_stream_threshold_frames))) {
        internal.piano_disk_stream_threshold_frames = std::max(4096u, config->piano_disk_stream_threshold_frames);
    }
    if (has_field(offsetof(vb_engine_config, piano_pedal_mode), sizeof(config->piano_pedal_mode))) {
        internal.piano_pedal_mode = to_pedal_mode(config->piano_pedal_mode);
    }
    if (has_field(offsetof(vb_engine_config, piano_pedal_binary_threshold), sizeof(config->piano_pedal_binary_threshold))) {
        internal.piano_pedal_binary_threshold = static_cast<std::uint8_t>(std::clamp(config->piano_pedal_binary_threshold, static_cast<std::uint8_t>(1), static_cast<std::uint8_t>(127)));
    }
    if (has_field(offsetof(vb_engine_config, piano_pedal_noise_enabled), sizeof(config->piano_pedal_noise_enabled))) {
        internal.piano_pedal_noise_enabled = (config->piano_pedal_noise_enabled != 0);
    }

    try {
        *out_handle = new vb_engine_handle(internal);
    } catch (const std::bad_alloc&) {
        return VB_ENGINE_ERROR_INTERNAL;
    }

    return VB_ENGINE_OK;
}

void vb_engine_destroy(vb_engine_handle* handle) {
    delete handle;
}

vb_engine_result vb_engine_note_on(
    vb_engine_handle* handle,
    const uint8_t channel,
    const uint8_t note,
    const uint8_t velocity
) {
    if (handle == nullptr) {
        return VB_ENGINE_ERROR_INVALID_ARGUMENT;
    }

    const vb::MidiEvent event =
        make_event(vb::MidiEventType::NoteOn, channel, note, velocity, handle->default_instrument);

    return push_event(handle, event);
}

vb_engine_result vb_engine_note_off(
    vb_engine_handle* handle,
    const uint8_t channel,
    const uint8_t note,
    const uint8_t velocity
) {
    if (handle == nullptr) {
        return VB_ENGINE_ERROR_INVALID_ARGUMENT;
    }

    const vb::MidiEvent event =
        make_event(vb::MidiEventType::NoteOff, channel, note, velocity, handle->default_instrument);

    return push_event(handle, event);
}

vb_engine_result vb_engine_control_change(
    vb_engine_handle* handle,
    const uint8_t channel,
    const uint8_t control,
    const uint8_t value
) {
    if (handle == nullptr) {
        return VB_ENGINE_ERROR_INVALID_ARGUMENT;
    }

    const vb::MidiEvent event =
        make_event(vb::MidiEventType::ControlChange, channel, control, value, handle->default_instrument);

    return push_event(handle, event);
}

vb_engine_result vb_engine_process(
    vb_engine_handle* handle,
    float* const* outputs,
    const uint32_t num_channels,
    const uint32_t num_frames
) {
    if (handle == nullptr || outputs == nullptr) {
        return VB_ENGINE_ERROR_INVALID_ARGUMENT;
    }

    if (!handle->engine.process(outputs, num_channels, num_frames)) {
        return VB_ENGINE_ERROR_INVALID_ARGUMENT;
    }

    return VB_ENGINE_OK;
}

vb_engine_result vb_engine_get_diagnostics(vb_engine_handle* handle, vb_engine_diagnostics* out_diagnostics) {
    if (handle == nullptr || out_diagnostics == nullptr || out_diagnostics->struct_size < sizeof(vb_engine_diagnostics)) {
        return VB_ENGINE_ERROR_INVALID_ARGUMENT;
    }

    const vb::EngineDiagnostics stats = handle->engine.diagnostics();
    out_diagnostics->voice_steals = stats.voice_steals;
    out_diagnostics->worst_stolen_activity = stats.worst_stolen_activity;
    out_diagnostics->max_output_delta = stats.max_output_delta;
    out_diagnostics->hard_jump_events = stats.hard_jump_events;
    out_diagnostics->non_finite_output_samples = stats.non_finite_output_samples;
    return VB_ENGINE_OK;
}

vb_engine_result vb_engine_reset_diagnostics(vb_engine_handle* handle) {
    if (handle == nullptr) {
        return VB_ENGINE_ERROR_INVALID_ARGUMENT;
    }
    handle->engine.reset_diagnostics();
    return VB_ENGINE_OK;
}

}  // extern "C"
