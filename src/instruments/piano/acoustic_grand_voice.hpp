#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace vb {

class AcousticGrandVoice {
public:
    void start(std::uint8_t note, std::uint8_t velocity, float sample_rate) noexcept;
    void release(float pedal_amount = 0.0F) noexcept;
    float render_sample() noexcept;

    [[nodiscard]] bool is_active() const noexcept { return active_; }
    [[nodiscard]] std::uint8_t note() const noexcept { return note_; }
    [[nodiscard]] float activity_level() const noexcept { return activity_level_; }

private:
    static constexpr std::size_t kMaxStrings = 3;
    static constexpr std::size_t kPartialCount = 8;
    static constexpr std::size_t kMaxModes = kMaxStrings * kPartialCount;

    bool active_{false};
    bool releasing_{false};
    std::uint8_t note_{0};
    std::size_t mode_count_{0};
    std::size_t string_count_{1};

    float sample_rate_{48000.0F};
    float velocity_norm_{0.0F};
    float note_gain_{1.0F};

    std::array<float, kMaxModes> phases_{};
    std::array<float, kMaxModes> increments_{};
    std::array<float, kMaxModes> amplitudes_{};
    std::array<float, kMaxModes> decays_{};
    std::array<float, kMaxModes> brightness_weights_{};
    std::array<std::uint8_t, kMaxModes> mode_string_index_{};

    std::array<float, kMaxStrings> string_activity_{};
    std::array<float, kMaxStrings> string_exchange_rate_{};

    float attack_gain_{0.0F};
    float attack_increment_{0.0F};
    float release_gain_{1.0F};
    float release_multiplier_{0.9995F};
    float sustain_tail_gain_{1.0F};
    float sustain_tail_decay_{0.9999F};

    float attack_noise_env_{0.0F};
    float key_off_noise_env_{0.0F};
    std::uint32_t noise_state_{0x12345678u};
    float brilliance_env_{0.0F};
    float brilliance_decay_{0.9995F};
    float hammer_env_{0.0F};
    float hammer_decay_{0.9975F};
    float hammer_contact_decay_{0.995F};

    float attack_noise_lp_{0.0F};
    float key_off_noise_lp_{0.0F};
    float attack_noise_lp_coeff_{0.24F};
    float key_off_noise_lp_coeff_{0.18F};

    // Simple soundboard/body coloration states.
    float body_lp_state_{0.0F};
    float body_hp_state_{0.0F};
    float body_lp_coeff_{0.16F};
    float body_hp_coeff_{0.985F};
    float body_prev_input_{0.0F};
    float activity_level_{0.0F};

    // High-pass state to reduce boominess/DC buildup.
    float hp_x_prev_{0.0F};
    float hp_y_prev_{0.0F};
    float hp_alpha_{0.0F};
    float output_prev_{0.0F};
};

}  // namespace vb
