#pragma once

#include <array>
#include <cstdint>

#include "instruments/piano/sample_library.hpp"

namespace vb {

class PianoMechanicsLayer {
public:
    void initialize(float sample_rate) noexcept;

    void trigger_key_down(const PianoSampleRegion* region, std::uint8_t velocity) noexcept;
    void trigger_damper_fall(const PianoSampleRegion* region, std::uint8_t velocity, float pedal_amount) noexcept;
    void trigger_pedal_whoosh(const PianoSampleRegion* region, bool pedal_down, float intensity) noexcept;

    float render_sample() noexcept;

private:
    enum class VoiceType : std::uint8_t {
        KeyDown,
        DamperFall,
        PedalDown,
        PedalUp,
    };

    struct NoiseVoice {
        bool active{false};
        VoiceType type{VoiceType::KeyDown};
        const PianoSampleRegion* region{nullptr};
        float position{0.0F};
        float ratio{1.0F};
        float end{0.0F};
        std::uint32_t max_frames{0};
        std::uint32_t rendered{0};
        float gain{0.0F};
        float env{1.0F};
        float env_decay{0.999F};
        float lp_coeff{0.95F};
        float lp_state{0.0F};
        bool use_highpass{false};
        float hp_coeff{0.995F};
        float hp_x_prev{0.0F};
        float hp_y_prev{0.0F};
    };

    static constexpr std::size_t kNoiseVoices = 24;
    std::array<NoiseVoice, kNoiseVoices> voices_{};
    float sample_rate_{48000.0F};
    float dc_x_{0.0F};
    float dc_y_{0.0F};

    static float sample_hermite(const PianoSampleRegion& region, float position) noexcept;
    static float lp_coeff_from_hz(float cutoff_hz, float sample_rate) noexcept;
    static float hp_coeff_from_hz(float cutoff_hz, float sample_rate) noexcept;
    NoiseVoice& acquire_voice() noexcept;
    void start_voice(
        VoiceType type,
        const PianoSampleRegion* region,
        float gain,
        float env_decay,
        float max_seconds,
        float lp_cutoff_hz,
        bool use_highpass,
        float hp_cutoff_hz
    ) noexcept;
};

}  // namespace vb

