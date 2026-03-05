#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace vb {

class PianoPostProcessor {
public:
    void initialize(float sample_rate, float reverb_wet = 0.12F) noexcept;
    void set_reverb_wet(float reverb_wet) noexcept;
    float process(
        float input,
        float body_drive = 0.0F,
        float close_bus = 0.0F,
        float room_bus = 0.0F,
        float mic_mix = 0.35F,
        float presence = 0.58F,
        float presence_focus = 0.5F
    ) noexcept;

private:
    struct ConvolutionTap {
        std::uint16_t delay{0};
        float gain{0.0F};
    };

    float sample_rate_{48000.0F};
    float wet_{0.12F};

    std::vector<float> body_ir_;
    std::vector<float> body_history_;
    std::vector<ConvolutionTap> body_taps_;
    std::size_t body_history_index_{0};

    std::vector<float> room_ir_;
    std::vector<float> room_history_;
    std::vector<ConvolutionTap> room_early_taps_;
    std::vector<ConvolutionTap> room_mid_taps_;
    std::vector<ConvolutionTap> room_late_taps_;
    std::size_t room_history_index_{0};
    std::size_t room_mid_begin_{0};
    std::size_t room_mid_end_{0};
    std::size_t room_late_begin_{0};
    float room_mid_prev_{0.0F};
    float room_mid_target_{0.0F};
    float room_late_prev_{0.0F};
    float room_late_target_{0.0F};
    std::uint32_t room_mid_phase_{0};
    std::uint32_t room_late_phase_{0};
    std::uint32_t room_mid_stride_{4};
    std::uint32_t room_late_stride_{24};

    float dc_x_{0.0F};
    float dc_y_{0.0F};
    float tone_lp_{0.0F};
    float post_prev_{0.0F};
    float final_prev_{0.0F};
    float body_drive_smoothed_{0.0F};
    float mic_mix_smoothed_{0.35F};
    float presence_smoothed_{0.58F};
    float presence_focus_smoothed_{0.5F};
    float bus_phase_correlation_{0.0F};
    float room_alignment_smoothed_{1.0F};
};

}  // namespace vb
