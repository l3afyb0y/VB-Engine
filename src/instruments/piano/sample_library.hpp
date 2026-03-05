#pragma once

#include <array>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace vb {

enum class PianoMicPerspective : std::uint8_t {
    Unknown = 0,
    Close = 1,
    Player = 2,
    Room = 3,
};

struct PianoSampleRegion {
    std::vector<float> samples;
    std::vector<float> samples_right;
    std::shared_ptr<void> mapped_owner{};
    const float* mapped_samples{nullptr};
    const float* mapped_samples_right{nullptr};
    std::size_t mapped_sample_count{0};
    bool stereo{false};
    std::uint32_t sample_rate{48000};
    std::uint8_t key_low{0};
    std::uint8_t key_high{127};
    std::uint8_t vel_low{1};
    std::uint8_t vel_high{127};
    std::uint8_t root_key{60};
    bool release_trigger{false};
    float gain_linear{1.0F};
    float amp_veltrack{100.0F};
    float amp_velcurve{1.0F};
    float tune_cents{0.0F};
    float release_seconds{0.0F};
    float lorand{0.0F};
    float hirand{1.0F};
    std::uint16_t seq_length{0};
    std::uint16_t seq_position{1};
    std::uint8_t cc64_low{0};
    std::uint8_t cc64_high{127};
    std::uint32_t sample_start{0};
    std::uint32_t sample_end{0};
    std::uint32_t loop_start{0};
    std::uint32_t loop_end{0};
    bool loop_enabled{false};
    bool loop_until_release{false};
    bool off_mode_fast{false};
    PianoMicPerspective perspective{PianoMicPerspective::Unknown};
    std::string source_name{};

    [[nodiscard]] std::size_t sample_count() const noexcept {
        return mapped_samples != nullptr ? mapped_sample_count : samples.size();
    }

    [[nodiscard]] bool empty() const noexcept {
        return sample_count() == 0;
    }

    [[nodiscard]] float sample_at_left(const int index) const noexcept {
        const std::size_t count = sample_count();
        if (count == 0) {
            return 0.0F;
        }
        const int clamped = std::clamp(index, 0, static_cast<int>(count) - 1);
        if (mapped_samples != nullptr) {
            return mapped_samples[static_cast<std::size_t>(clamped)];
        }
        return samples[static_cast<std::size_t>(clamped)];
    }

    [[nodiscard]] float sample_at_right(const int index) const noexcept {
        const std::size_t count = sample_count();
        if (count == 0) {
            return 0.0F;
        }
        const int clamped = std::clamp(index, 0, static_cast<int>(count) - 1);
        if (stereo && mapped_samples_right != nullptr) {
            return mapped_samples_right[static_cast<std::size_t>(clamped)];
        }
        if (stereo && !samples_right.empty()) {
            return samples_right[static_cast<std::size_t>(clamped)];
        }
        return sample_at_left(clamped);
    }

    [[nodiscard]] float sample_at(const int index) const noexcept {
        if (!stereo) {
            return sample_at_left(index);
        }
        return 0.5F * (sample_at_left(index) + sample_at_right(index));
    }
};

struct PianoLayerSelection {
    const PianoSampleRegion* primary{nullptr};
    const PianoSampleRegion* secondary{nullptr};
    float secondary_mix{0.0F};
};

class PianoSampleLibrary {
public:
    bool load_sfz(const std::string& sfz_path);
    void configure_streaming(bool enabled, std::size_t frame_threshold) noexcept;

    [[nodiscard]] bool empty() const noexcept { return regions_.empty(); }
    [[nodiscard]] std::size_t region_count() const noexcept { return regions_.size(); }

    [[nodiscard]] PianoLayerSelection select_layers(
        std::uint8_t note,
        std::uint8_t velocity,
        bool release_trigger,
        std::uint8_t cc64_value = 0
    ) const noexcept;

    [[nodiscard]] const PianoSampleRegion* select_pedal_sample(bool pedal_down, std::uint32_t seed) const noexcept;

private:
    static constexpr std::size_t kNoteCount = 128;
    static constexpr std::size_t kVelocityCount = 128;

    enum class LoopMode {
        NoLoop,
        LoopContinuous,
        LoopSustain,
        OneShot,
    };

    struct RegionDraft {
        std::string sample_path;
        std::uint8_t key_low{0};
        std::uint8_t key_high{127};
        std::uint8_t vel_low{1};
        std::uint8_t vel_high{127};
        std::uint8_t root_key{60};
        bool release_trigger{false};
        float gain_linear{1.0F};
        float amp_veltrack{100.0F};
        float amp_velcurve{1.0F};
        float tune_cents{0.0F};
        float release_seconds{0.0F};
        float lorand{0.0F};
        float hirand{1.0F};
        std::uint16_t seq_length{0};
        std::uint16_t seq_position{1};
        std::uint8_t cc64_low{0};
        std::uint8_t cc64_high{127};
        int sample_offset{0};
        int sample_end{-1};
        int loop_start{-1};
        int loop_end{-1};
        LoopMode loop_mode{LoopMode::NoLoop};
        bool off_mode_fast{false};
    };

    std::vector<PianoSampleRegion> regions_;
    bool disk_streaming_enabled_{true};
    std::size_t disk_stream_threshold_frames_{192000};  // 4 sec @ 48 kHz
    struct CachedSelection {
        std::int16_t primary{-1};
        std::int16_t secondary{-1};
        float secondary_mix{0.0F};
    };
    std::array<CachedSelection, kNoteCount * kVelocityCount * 2> selection_cache_{};
    bool selection_cache_valid_{false};
    std::vector<std::uint16_t> pedal_down_indices_{};
    std::vector<std::uint16_t> pedal_up_indices_{};
    bool has_round_robin_regions_{false};
    bool has_cc64_conditioned_regions_{false};
    mutable std::array<std::uint32_t, kNoteCount * 2> rr_counters_{};
    mutable std::uint32_t rr_rng_state_{0xA5C3F29Du};

    static bool load_audio_mono(const std::string& file_path, PianoSampleRegion& out_region);
    static bool load_wav_mono(const std::string& file_path, PianoSampleRegion& out_region);
    static bool load_flac_mono(const std::string& file_path, PianoSampleRegion& out_region);
    bool promote_to_disk_backed(const std::string& sample_path_hint, PianoSampleRegion& region) const;
    static std::uint8_t parse_note_value(std::string_view value, std::uint8_t fallback) noexcept;
    [[nodiscard]] static std::size_t cache_offset(std::uint8_t note, std::uint8_t velocity, bool release_trigger) noexcept;
    [[nodiscard]] PianoLayerSelection select_layers_uncached(
        std::uint8_t note,
        std::uint8_t velocity,
        bool release_trigger,
        std::uint8_t cc64_value
    ) const noexcept;
    [[nodiscard]] static PianoMicPerspective classify_perspective(const std::string& source_name) noexcept;
    void rebuild_selection_cache() noexcept;
    [[nodiscard]] static bool uses_round_robin(const PianoSampleRegion& region) noexcept;
    [[nodiscard]] bool region_matches_round_robin(
        const PianoSampleRegion& region,
        std::uint8_t note,
        bool release_trigger,
        std::uint32_t rr_step,
        float rr_rand
    ) const noexcept;
    [[nodiscard]] float next_round_robin_random() const noexcept;
};

}  // namespace vb
