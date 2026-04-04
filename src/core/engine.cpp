#include "core/engine.hpp"

#include <algorithm>
#include <cmath>
#if defined(__SSE2__)
#include <xmmintrin.h>
#endif

namespace vb {

namespace {
void enable_realtime_fp_mode() noexcept {
#if defined(__SSE2__)
    unsigned int csr = _mm_getcsr();
    // FTZ (bit 15) and DAZ (bit 6) reduce denormal stalls in realtime DSP tails.
    csr |= (1u << 15);
    csr |= (1u << 6);
    _mm_setcsr(csr);
#endif
}

constexpr float kHardJumpThreshold = 0.25F;
constexpr float kOutputSlewMaxDelta = 0.82F;

float clamp_step(const float value, const float previous, const float max_delta) noexcept {
    const float delta = value - previous;
    if (std::abs(delta) <= max_delta) {
        return value;
    }
    return previous + std::copysign(max_delta, delta);
}
}  // namespace

Engine::Engine(const EngineConfig& config)
    : sample_rate_(config.sample_rate),
      max_block_size_(config.max_block_size),
      default_instrument_(config.default_instrument),
      voice_pool_(
          config.max_voices,
          config.sample_rate,
          PianoRenderOptions{
              .sfz_path = config.piano_sfz_path,
              .humanize_timing_ms = config.piano_humanize_timing_ms,
              .humanize_velocity = config.piano_humanize_velocity,
              .reverb_wet = config.piano_reverb_wet,
              .mic_mix = config.piano_mic_mix,
              .presence = config.piano_presence,
              .stretch_strength = config.piano_stretch_strength,
              .disk_streaming_enabled = config.piano_disk_streaming_enabled,
              .disk_stream_threshold_frames = config.piano_disk_stream_threshold_frames,
              .pedal_mode = config.piano_pedal_mode,
              .pedal_binary_threshold = config.piano_pedal_binary_threshold,
              .pedal_noise_enabled = config.piano_pedal_noise_enabled,
              .render_backend = config.piano_render_backend,
              .fem_mix = config.piano_fem_mix,
              .fem_brightness = config.piano_fem_brightness,
          }
      ) {
    enable_realtime_fp_mode();
}

bool Engine::enqueue_event(const MidiEvent& event) noexcept {
    return event_queue_.push(event);
}

bool Engine::process(float* const* outputs, const std::uint32_t num_channels, const std::uint32_t num_frames) noexcept {
    // Ensure the thread calling process() (typically the realtime audio callback)
    // has FTZ/DAZ enabled to avoid denormal-related CPU spikes on long decays.
    static thread_local bool s_fp_mode_initialized = false;
    if (!s_fp_mode_initialized) {
        enable_realtime_fp_mode();
        s_fp_mode_initialized = true;
    }

    if (outputs == nullptr || num_channels == 0 || num_frames == 0 || num_frames > max_block_size_) {
        return false;
    }

    for (std::uint32_t channel = 0; channel < num_channels; ++channel) {
        if (outputs[channel] == nullptr) {
            return false;
        }

        std::fill_n(outputs[channel], num_frames, 0.0F);
    }

    MidiEvent event{};
    while (event_queue_.pop(event)) {
        handle_event(event);
    }

    for (std::uint32_t frame = 0; frame < num_frames; ++frame) {
        StereoFrame frame_stereo = voice_pool_.render_stereo();
        if (!std::isfinite(frame_stereo.left)) {
            frame_stereo.left = 0.0F;
            ++non_finite_output_samples_;
        }
        if (!std::isfinite(frame_stereo.right)) {
            frame_stereo.right = 0.0F;
            ++non_finite_output_samples_;
        }
        if (has_prev_output_sample_) {
            frame_stereo.left = clamp_step(frame_stereo.left, prev_output_left_, kOutputSlewMaxDelta);
            frame_stereo.right = clamp_step(frame_stereo.right, prev_output_right_, kOutputSlewMaxDelta);
        }
        prev_output_left_ = frame_stereo.left;
        prev_output_right_ = frame_stereo.right;
        const float mono = 0.5F * (frame_stereo.left + frame_stereo.right);
        if (has_prev_output_sample_) {
            const float delta = std::abs(mono - prev_output_sample_);
            max_output_delta_ = std::max(max_output_delta_, delta);
            if (delta > kHardJumpThreshold) {
                ++hard_jump_events_;
            }
        }
        prev_output_sample_ = mono;
        has_prev_output_sample_ = true;
        if (num_channels == 1) {
            outputs[0][frame] = mono;
            continue;
        }

        outputs[0][frame] = frame_stereo.left;
        outputs[1][frame] = frame_stereo.right;
        for (std::uint32_t channel = 2; channel < num_channels; ++channel) {
            outputs[channel][frame] = mono;
        }
    }

    return true;
}

EngineDiagnostics Engine::diagnostics() const noexcept {
    const VoicePoolDiagnostics voice_stats = voice_pool_.diagnostics();
    return EngineDiagnostics{
        .voice_steals = voice_stats.voice_steals,
        .worst_stolen_activity = voice_stats.worst_stolen_activity,
        .max_output_delta = max_output_delta_,
        .hard_jump_events = hard_jump_events_,
        .non_finite_output_samples = non_finite_output_samples_,
        .active_render_backend = voice_stats.active_render_backend,
        .gpu_fallback_blocks = voice_stats.gpu_fallback_blocks,
    };
}

void Engine::reset_diagnostics() noexcept {
    prev_output_sample_ = 0.0F;
    prev_output_left_ = 0.0F;
    prev_output_right_ = 0.0F;
    has_prev_output_sample_ = false;
    max_output_delta_ = 0.0F;
    hard_jump_events_ = 0;
    non_finite_output_samples_ = 0;
    voice_pool_.reset_diagnostics();
}

void Engine::handle_event(const MidiEvent& event) noexcept {
    const InstrumentType instrument = event.instrument;

    switch (event.type) {
        case MidiEventType::NoteOn:
            if (event.data2 == 0) {
                voice_pool_.note_off(event.data1);
                return;
            }
            voice_pool_.note_on(event.data1, event.data2, instrument, sample_rate_);
            return;

        case MidiEventType::NoteOff:
            voice_pool_.note_off(event.data1);
            return;

        case MidiEventType::ControlChange:
            voice_pool_.control_change(event.data1, event.data2);
            return;
    }

    (void)default_instrument_;
}

}  // namespace vb
