#pragma once

#include <cstdint>
#include <string>

#include "core/instrument_type.hpp"
#include "core/midi_event.hpp"
#include "core/spsc_queue.hpp"
#include "core/voice_pool.hpp"

namespace vb {

struct EngineConfig {
    float sample_rate{48000.0F};
    std::uint32_t max_block_size{512};
    std::uint32_t max_voices{64};
    InstrumentType default_instrument{InstrumentType::AcousticGrandPiano};
    std::string piano_sfz_path{};
    float piano_humanize_timing_ms{1.2F};
    float piano_humanize_velocity{2.0F};
    float piano_reverb_wet{0.08F};
    float piano_mic_mix{0.28F};
    float piano_presence{0.56F};
    float piano_stretch_strength{0.96F};
    bool piano_disk_streaming_enabled{false};
    std::uint32_t piano_disk_stream_threshold_frames{192000};
    PedalMode piano_pedal_mode{PedalMode::Auto};
    std::uint8_t piano_pedal_binary_threshold{64};
    bool piano_pedal_noise_enabled{false};
};

struct EngineDiagnostics {
    std::uint64_t voice_steals{0};
    float worst_stolen_activity{0.0F};
    float max_output_delta{0.0F};
    std::uint64_t hard_jump_events{0};
    std::uint64_t non_finite_output_samples{0};
};

class Engine {
public:
    explicit Engine(const EngineConfig& config);

    [[nodiscard]] bool enqueue_event(const MidiEvent& event) noexcept;
    [[nodiscard]] bool process(float* const* outputs, std::uint32_t num_channels, std::uint32_t num_frames) noexcept;
    [[nodiscard]] EngineDiagnostics diagnostics() const noexcept;
    void reset_diagnostics() noexcept;

    [[nodiscard]] std::uint32_t max_block_size() const noexcept { return max_block_size_; }

private:
    static constexpr std::size_t kEventQueueCapacity = 4096;

    float sample_rate_;
    std::uint32_t max_block_size_;
    InstrumentType default_instrument_;
    SpscQueue<MidiEvent, kEventQueueCapacity> event_queue_;
    VoicePool voice_pool_;
    float prev_output_sample_{0.0F};
    float prev_output_left_{0.0F};
    float prev_output_right_{0.0F};
    bool has_prev_output_sample_{false};
    float max_output_delta_{0.0F};
    std::uint64_t hard_jump_events_{0};
    std::uint64_t non_finite_output_samples_{0};

    void handle_event(const MidiEvent& event) noexcept;
};

}  // namespace vb
