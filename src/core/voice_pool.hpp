#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "core/instrument_type.hpp"
#include "instruments/guitar/clean_electric_voice.hpp"
#include "instruments/piano/acoustic_grand_voice.hpp"
#include "instruments/piano/mechanics.hpp"
#include "instruments/piano/post_processor.hpp"
#include "instruments/piano/resonance_matrix.hpp"
#include "instruments/piano/sample_library.hpp"
#include "instruments/piano/sampled_piano_voice.hpp"

namespace vb {

enum class PedalMode : std::uint8_t {
    Auto = 0,
    Binary = 1,
    Continuous = 2,
};

struct PianoRenderOptions {
    std::string sfz_path{};
    float humanize_timing_ms{1.2F};
    float humanize_velocity{2.0F};
    float reverb_wet{0.12F};
    float mic_mix{0.35F};
    float presence{0.58F};
    float image_width{1.0F};
    float output_gain{1.0F};
    float stretch_strength{1.0F};
    bool disk_streaming_enabled{true};
    std::uint32_t disk_stream_threshold_frames{192000};
    PedalMode pedal_mode{PedalMode::Auto};
    std::uint8_t pedal_binary_threshold{64};
    bool pedal_noise_enabled{false};
};

struct VoicePoolDiagnostics {
    std::uint64_t voice_steals{0};
    float worst_stolen_activity{0.0F};
};

struct StereoFrame {
    float left{0.0F};
    float right{0.0F};
};

class VoicePool {
public:
    VoicePool(std::size_t max_voices, float sample_rate, const PianoRenderOptions& options);

    void note_on(std::uint8_t note, std::uint8_t velocity, InstrumentType instrument, float sample_rate) noexcept;
    void note_off(std::uint8_t note) noexcept;
    void control_change(std::uint8_t control, std::uint8_t value) noexcept;
    StereoFrame render_stereo() noexcept;
    float render_sample() noexcept;
    [[nodiscard]] VoicePoolDiagnostics diagnostics() const noexcept;
    void reset_diagnostics() noexcept;

private:
    struct VoiceSlot {
        InstrumentType type{InstrumentType::AcousticGrandPiano};
        SampledPianoVoice piano{};
        AcousticGrandVoice piano_fallback{};
        CleanElectricVoice guitar{};
    };

    std::vector<VoiceSlot> voices_;
    std::vector<std::size_t> active_voice_indices_;
    std::vector<std::uint8_t> active_slot_flags_;
    std::array<int, 128> active_voice_slot_by_note_{};
    std::array<std::uint8_t, 128> last_velocity_by_note_{};
    struct ReleasePrefetchEntry {
        const PianoSampleRegion* primary{nullptr};
        const PianoSampleRegion* secondary{nullptr};
        float secondary_mix{0.0F};
        std::uint8_t velocity{64};
    };
    std::array<ReleasePrefetchEntry, 128> release_prefetch_{};
    std::size_t round_robin_index_{0};
    std::vector<float> slot_last_sample_;
    std::array<float, 8> steal_tails_{};
    float resonance_activity_{0.0F};
    float presence_focus_smoothed_{0.5F};
    std::uint64_t voice_steal_count_{0};
    float worst_stolen_activity_{0.0F};
    bool sustain_enabled_{false};
    float pedal_amount_{0.0F};
    float soft_pedal_amount_{0.0F};
    bool pedal_auto_continuous_{false};
    std::uint8_t pedal_auto_mid_min_{127};
    std::uint8_t pedal_auto_mid_max_{0};
    float sample_rate_{48000.0F};
    std::uint8_t previous_pedal_cc_{0};
    std::uint32_t mechanics_seed_{0xC001D00Du};
    std::array<bool, 128> sustained_notes_{};
    std::array<float, 128> note_norm_table_{};
    std::array<float, 128> close_pan_left_{};
    std::array<float, 128> close_pan_right_{};
    std::array<float, 128> room_pan_left_{};
    std::array<float, 128> room_pan_right_{};
    PianoRenderOptions options_{};
    std::array<float, 128> tuned_frequency_{};
    PianoSampleLibrary sample_library_{};
    PianoMechanicsLayer mechanics_{};
    PianoResonanceMatrix resonance_{};
    PianoPostProcessor post_processor_left_{};
    PianoPostProcessor post_processor_right_{};
    std::mt19937 rng_{0xBADC0DEu};

    [[nodiscard]] bool is_active(const VoiceSlot& slot) const noexcept;
    [[nodiscard]] std::uint8_t note_number(const VoiceSlot& slot) const noexcept;
    [[nodiscard]] float voice_activity(const VoiceSlot& slot) const noexcept;
    [[nodiscard]] PedalMode resolved_pedal_mode() const noexcept;
    [[nodiscard]] bool pedal_should_hold(float pedal_amount) const noexcept;
    void apply_damper_release(std::uint8_t note, float pedal_amount_for_release) noexcept;
    void release_voice(VoiceSlot& slot, float pedal_amount) noexcept;
    void release_note(std::uint8_t note) noexcept;
    void spawn_release_sample(std::uint8_t note) noexcept;
    [[nodiscard]] std::size_t pick_slot_index() noexcept;
    void add_steal_tail(float value) noexcept;

    void initialize_tuning_table() noexcept;
    void update_stretch_tuning() noexcept;
    void rebuild_pan_tables() noexcept;
};

}  // namespace vb
