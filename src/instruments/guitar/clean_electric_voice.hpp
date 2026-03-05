#pragma once

#include <cstdint>

namespace vb {

class CleanElectricVoice {
public:
    void start(std::uint8_t note, std::uint8_t velocity, float sample_rate) noexcept;
    void release() noexcept;
    float render_sample() noexcept;

    [[nodiscard]] bool is_active() const noexcept { return active_; }
    [[nodiscard]] std::uint8_t note() const noexcept { return note_; }
    [[nodiscard]] float activity_level() const noexcept { return activity_level_; }

private:
    bool active_{false};
    bool releasing_{false};
    std::uint8_t note_{0};
    float phase_{0.0F};
    float phase_increment_{0.0F};
    float amplitude_{0.0F};
    float envelope_{0.0F};
    float attack_increment_{0.0F};
    float release_increment_{0.0F};
    float activity_level_{0.0F};
};

}  // namespace vb
