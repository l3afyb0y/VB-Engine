#pragma once

#include <array>
#include <cstdint>

#include "instruments/piano/sample_library.hpp"

namespace vb {

class SampledPianoVoice {
public:
    bool start(
        const PianoLayerSelection& selection,
        std::uint8_t note,
        std::uint8_t velocity,
        float sample_rate,
        const float* tuned_frequencies,
        std::uint32_t delay_samples,
        float gain_jitter,
        float soft_pedal_amount
    ) noexcept;

    void release(float pedal_amount = 0.0F) noexcept;
    float render_sample(
        float* close_bus = nullptr,
        float* room_bus = nullptr,
        float* direct_left = nullptr,
        float* direct_right = nullptr
    ) noexcept;

    [[nodiscard]] bool is_active() const noexcept { return active_; }
    [[nodiscard]] std::uint8_t note() const noexcept { return note_; }
    [[nodiscard]] float activity_level() const noexcept { return activity_level_; }

private:
    struct LayerState {
        const PianoSampleRegion* region{nullptr};
        float position{0.0F};
        float ratio{1.0F};
        float start{0.0F};
        float end{0.0F};
        float loop_start{0.0F};
        float loop_end{0.0F};
        bool loop_enabled{false};
        bool loop_until_release{false};
    };
    struct StereoSample {
        float left{0.0F};
        float right{0.0F};
    };

    bool active_{false};
    bool releasing_{false};
    std::uint8_t note_{0};
    std::uint32_t delay_samples_{0};

    LayerState layer_a_{};
    LayerState layer_b_{};
    float layer_b_mix_{0.0F};
    float layer_a_gain_{1.0F};
    float layer_b_gain_{0.0F};

    float base_gain_{1.0F};
    float attack_gain_{0.0F};
    float attack_increment_{0.002F};
    float release_gain_{1.0F};
    float release_multiplier_{0.9995F};
    float configured_release_multiplier_{0.9978F};
    float activity_level_{0.0F};
    std::uint32_t rendered_frames_{0};
    float sample_rate_{48000.0F};
    float hp_x_prev_{0.0F};
    float hp_y_prev_{0.0F};
    float hp_alpha_{0.995F};
    float velocity_norm_{0.0F};
    float timbre_lp_coeff_{0.88F};
    float timbre_target_lp_coeff_{0.88F};
    float timbre_high_mix_{1.0F};
    float timbre_body_mix_{1.0F};
    float timbre_lp_state_l_{0.0F};
    float timbre_lp_state_r_{0.0F};
    bool off_mode_fast_{false};
    float release_fast_gain_{1.0F};
    float release_slow_gain_{1.0F};
    float release_fast_mul_{0.9992F};
    float release_slow_mul_{0.9996F};
    float damper_sizzle_env_{0.0F};
    float damper_sizzle_decay_{0.992F};
    float damper_hp_state_{0.0F};
    float last_mixed_{0.0F};
    std::array<float, 32> room_delay_line_l_{};
    std::array<float, 32> room_delay_line_r_{};
    std::uint32_t room_delay_index_{0};
    float room_lp_state_l_{0.0F};
    float room_lp_state_r_{0.0F};
    float phase_correlation_{0.0F};
    float soft_pedal_amount_{0.0F};
    float hammer_contact_env_{0.0F};
    float hammer_contact_decay_{0.999F};
    float attack_brilliance_env_{0.0F};
    float attack_brilliance_decay_{0.999F};
    float de_synth_noise_lp_{0.0F};
    std::uint32_t rand_state_{0x4D595DF4u};

    static float sample_hermite(const PianoSampleRegion& region, float position, bool right) noexcept;
    static float sample_windowed_sinc(const PianoSampleRegion& region, float position, bool right) noexcept;
    StereoSample render_layer(LayerState& layer, bool render_right_channel) noexcept;
};

}  // namespace vb
