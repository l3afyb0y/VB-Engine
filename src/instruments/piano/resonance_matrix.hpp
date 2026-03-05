#pragma once

#include <array>
#include <cstdint>

namespace vb {

class PianoResonanceMatrix {
public:
    void initialize(float sample_rate, const float* tuned_frequencies) noexcept;
    void set_pedal(float amount) noexcept;
    void note_on(std::uint8_t note, std::uint8_t velocity) noexcept;
    void trigger_damper_drop(std::uint8_t released_notes, float pedal_before_release) noexcept;
    float render_sample() noexcept;

private:
    static constexpr std::size_t kKeyCount = 88;
    static constexpr std::size_t kMaxDelay = 4096;
    static constexpr std::size_t kBoardLineCount = 4;
    static constexpr std::size_t kBoardMaxDelay = 512;

    bool initialized_{false};
    float pedal_amount_{0.0F};
    float pedal_target_{0.0F};
    float sample_rate_{48000.0F};

    std::array<float, kKeyCount * kMaxDelay> delay_lines_{};
    std::array<std::uint16_t, kKeyCount> delay_lengths_{};
    std::array<std::uint16_t, kKeyCount> write_indices_{};
    std::array<float, kKeyCount> delay_fracs_{};
    std::array<float, kKeyCount> energies_{};
    std::array<float, kKeyCount> pending_energies_{};
    std::array<float, kKeyCount> lp_states_{};
    std::array<float, kKeyCount> prev_delayed_{};
    std::array<float, kKeyCount> fundamentals_{};
    std::array<float, kKeyCount> duplex_phases_{};
    std::array<float, kKeyCount> duplex_increments_{};
    std::array<float, kKeyCount> inharmonicity_{};
    std::array<float, kKeyCount * kKeyCount> coupling_{};
    std::array<float, kKeyCount * 3> bridge_coupling_{};
    std::array<float, kKeyCount> register_skew_{};
    std::array<float, kBoardLineCount * kBoardMaxDelay> board_lines_{};
    std::array<std::uint16_t, kBoardLineCount> board_lengths_{};
    std::array<std::uint16_t, kBoardLineCount> board_indices_{};
    std::array<float, 3> board_modes_{};
    std::array<float, 3> board_mode_velocity_{};
    std::array<float, 3> board_mode_damping_{};
    std::array<float, 3> board_band_energy_{};
    float board_lf_{0.0F};
    float board_hf_{0.0F};
    float excitation_alpha_{0.0F};
    float damper_drop_envelope_{0.0F};
};

}  // namespace vb
