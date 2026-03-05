#include "instruments/guitar/clean_electric_voice.hpp"

#include <cmath>

namespace vb {

namespace {
constexpr float kPi = 3.14159265358979323846F;

float midi_note_to_frequency(const std::uint8_t note) {
    const float semitones_from_a4 = static_cast<float>(static_cast<int>(note) - 69);
    return 440.0F * std::pow(2.0F, semitones_from_a4 / 12.0F);
}
}  // namespace

void CleanElectricVoice::start(const std::uint8_t note, const std::uint8_t velocity, const float sample_rate) noexcept {
    active_ = true;
    releasing_ = false;
    note_ = note;
    phase_ = 0.0F;
    envelope_ = 0.0F;

    const float frequency = midi_note_to_frequency(note);
    phase_increment_ = (2.0F * kPi * frequency) / sample_rate;

    amplitude_ = static_cast<float>(velocity) / 127.0F;
    attack_increment_ = 1.0F / (0.0015F * sample_rate);
    release_increment_ = 1.0F / (0.180F * sample_rate);
    activity_level_ = 0.0F;
}

void CleanElectricVoice::release() noexcept {
    releasing_ = true;
}

float CleanElectricVoice::render_sample() noexcept {
    if (!active_) {
        return 0.0F;
    }

    if (releasing_) {
        envelope_ -= release_increment_;
        if (envelope_ <= 0.0F) {
            envelope_ = 0.0F;
            active_ = false;
            activity_level_ = 0.0F;
            return 0.0F;
        }
    } else {
        envelope_ += attack_increment_;
        if (envelope_ > 1.0F) {
            envelope_ = 1.0F;
        }
    }

    const float s1 = std::sin(phase_);
    const float s2 = std::sin(2.0F * phase_);
    const float s3 = std::sin(3.0F * phase_);
    const float harmonic = (0.75F * s1) + (0.2F * s2) + (0.05F * s3);
    const float sample = harmonic * amplitude_ * envelope_ * 0.30F;
    activity_level_ = (activity_level_ * 0.99F) + (std::abs(sample) * 0.01F);

    phase_ += phase_increment_;
    if (phase_ >= 2.0F * kPi) {
        phase_ -= 2.0F * kPi;
    }

    return sample;
}

}  // namespace vb
