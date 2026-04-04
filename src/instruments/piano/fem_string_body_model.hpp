#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "core/piano_render_backend.hpp"

namespace vb {

class PianoFemStringBodyModel {
public:
    void initialize(float sample_rate, PianoRenderBackend requested_backend, float mix, float brightness) noexcept;
    void set_backend(PianoRenderBackend requested_backend) noexcept;
    void set_voicing(float mix, float brightness) noexcept;
    void set_pedal(float amount) noexcept;
    void note_on(std::uint8_t note, std::uint8_t velocity, float tuned_frequency_hz) noexcept;
    void note_off(std::uint8_t note) noexcept;
    void render_sample(float excitation, float& out_l, float& out_r) noexcept;

    [[nodiscard]] PianoRenderBackend active_backend() const noexcept { return active_backend_; }
    [[nodiscard]] bool gpu_available() const noexcept { return gpu_available_; }
    [[nodiscard]] std::uint64_t gpu_fallback_blocks() const noexcept { return gpu_fallback_blocks_; }

private:
    static constexpr int kGridW = 30;
    static constexpr int kGridH = 18;
    static constexpr std::size_t kGridN = static_cast<std::size_t>(kGridW * kGridH);

    struct StringState {
        bool active{false};
        float phase{0.0F};
        float phase_increment{0.0F};
        float amplitude{0.0F};
        float decay{0.9999F};
        float coupling{0.0F};
        std::uint16_t bridge_index{0};
    };

    float sample_rate_{48000.0F};
    PianoRenderBackend requested_backend_{PianoRenderBackend::Auto};
    PianoRenderBackend active_backend_{PianoRenderBackend::CpuHybrid};
    bool gpu_available_{false};
    std::uint64_t gpu_fallback_blocks_{0};

    float mix_{0.26F};
    float brightness_{0.54F};
    float pedal_amount_{0.0F};

    float c2_{0.14F};
    float damping_{0.0014F};
    std::array<float, kGridN> board_prev_{};
    std::array<float, kGridN> board_curr_{};
    std::array<float, kGridN> board_next_{};

    std::array<StringState, 128> strings_{};
    std::uint16_t active_strings_{0};
    float board_activity_{0.0F};
    std::uint32_t noise_state_{0xC001CAFEu};

    float body_lp_l_{0.0F};
    float body_lp_r_{0.0F};
    float body_hp_l_{0.0F};
    float body_hp_r_{0.0F};
    float body_hp_prev_l_{0.0F};
    float body_hp_prev_r_{0.0F};
    std::uint32_t step_interval_{2};
    std::uint32_t step_phase_{0};
    float cached_l_{0.0F};
    float cached_r_{0.0F};
    std::uint32_t instability_events_{0};
    std::uint32_t consecutive_gpu_step_failures_{0};

    void select_active_backend() noexcept;
    void cpu_step(float excitation) noexcept;
    void collect_output(float excitation, float& out_l, float& out_r) noexcept;
    void reset_board() noexcept;
};

}  // namespace vb
