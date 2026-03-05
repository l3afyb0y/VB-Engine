#include "instruments/piano/acoustic_grand_voice.hpp"

#include <algorithm>
#include <cmath>

#include "core/fast_math.hpp"

namespace vb {

namespace {
constexpr float kPi = 3.14159265358979323846F;
constexpr float kTwoPi = 2.0F * kPi;

float midi_note_to_frequency(const std::uint8_t note) {
    const float semitones_from_a4 = static_cast<float>(static_cast<int>(note) - 69);
    return 440.0F * std::pow(2.0F, semitones_from_a4 / 12.0F);
}

float cents_to_ratio(const float cents) {
    return std::pow(2.0F, cents / 1200.0F);
}

float clamp01(const float value) {
    return std::clamp(value, 0.0F, 1.0F);
}

float next_noise(std::uint32_t& state) {
    state = (state * 1664525u) + 1013904223u;
    return (static_cast<float>((state >> 9) & 0x7FFFFFu) / 4194303.5F) - 1.0F;
}

}  // namespace

void AcousticGrandVoice::start(const std::uint8_t note, const std::uint8_t velocity, const float sample_rate) noexcept {
    active_ = true;
    releasing_ = false;
    note_ = note;
    sample_rate_ = sample_rate;

    velocity_norm_ = clamp01(static_cast<float>(velocity) / 127.0F);
    const float note_norm = clamp01(static_cast<float>(note) / 127.0F);

    // Reduce low-note boom and slightly favor upper clarity.
    note_gain_ = 0.62F + (note_norm * 0.48F);

    // Real pianos transition from 1 to 2 to 3 strings across the keyboard.
    std::size_t string_count = 2;
    if (note < 40) {
        string_count = 1;
    } else if (note >= 88) {
        string_count = 3;
    }
    string_count_ = string_count;
    string_activity_.fill(0.0F);
    string_exchange_rate_.fill(0.0F);

    static constexpr std::array<float, kMaxStrings> kDetuneCents{ -1.6F, 0.0F, 1.9F };
    static constexpr std::array<float, kMaxStrings> kDetuneCentsTwo{ -1.2F, 1.2F, 0.0F };

    mode_count_ = 0;
    const float base_frequency = midi_note_to_frequency(note);
    const float inharmonicity = 0.000015F + (note_norm * note_norm * 0.00018F);
    const float nyquist = sample_rate_ * 0.5F;
    const float strike_position = 0.19F - (note_norm * 0.08F);

    // Hammer-contact-time nonlinearity: hard strikes shorten contact and brighten attack.
    const float hammer_contact_ms = std::clamp(2.85F - (2.05F * std::pow(velocity_norm_, 0.86F)), 0.72F, 2.90F);
    hammer_contact_decay_ = std::exp(-1.0F / std::max(1.0F, sample_rate_ * (hammer_contact_ms * 0.001F)));
    const float spectral_rolloff = 2.26F - (velocity_norm_ * 1.10F);
    const float base_decay_seconds = 5.2F - (note_norm * 3.0F);

    noise_state_ = 0xA341316Cu ^ static_cast<std::uint32_t>(note * 7919u + velocity * 104729u);

    for (std::size_t s = 0; s < string_count; ++s) {
        const float detune_cents = (string_count == 2) ? kDetuneCentsTwo[s] : kDetuneCents[s];
        const float detune_ratio = cents_to_ratio(detune_cents);

        for (std::size_t p = 1; p <= kPartialCount; ++p) {
            const float partial = static_cast<float>(p);
            const float stiff_factor = std::sqrt(1.0F + (inharmonicity * partial * partial));
            const float mode_frequency = base_frequency * detune_ratio * partial * stiff_factor;
            if (mode_frequency >= nyquist * 0.96F) {
                continue;
            }

            if (mode_count_ >= kMaxModes) {
                break;
            }
            const std::size_t mode_index = mode_count_++;

            // Per-mode random phase avoids static phase lock and synthetic "organ" tone.
            phases_[mode_index] = (0.5F + (0.5F * next_noise(noise_state_))) * kTwoPi;
            increments_[mode_index] = (kTwoPi * mode_frequency) / sample_rate_;

            // Hammer strike position imprints comb-like weighting across partials.
            const float strike_weight = std::max(0.02F, std::abs(std::sin(kPi * partial * strike_position)));
            const float harmonic_weight = 1.0F / std::pow(partial, spectral_rolloff);
            const float string_weight = 1.0F / static_cast<float>(string_count);
            amplitudes_[mode_index] = harmonic_weight * string_weight * strike_weight;
            brightness_weights_[mode_index] = std::clamp((partial - 1.0F) / static_cast<float>(kPartialCount), 0.0F, 1.0F);
            mode_string_index_[mode_index] = static_cast<std::uint8_t>(s);

            const float partial_decay = base_decay_seconds / (1.0F + (partial - 1.0F) * 0.88F);
            decays_[mode_index] = std::exp(-1.0F / (sample_rate_ * std::max(0.05F, partial_decay)));
        }
        // Slightly different exchange rates per string emulate imperfect bridge coupling.
        string_exchange_rate_[s] = 0.0028F + (0.0024F * (velocity_norm_ + (0.17F * static_cast<float>(s))));
    }

    if (mode_count_ == 0) {
        active_ = false;
        return;
    }

    attack_gain_ = 0.0F;
    attack_increment_ = 1.0F / (sample_rate_ * (0.0052F - (velocity_norm_ * 0.0032F)));
    release_gain_ = 1.0F;
    release_multiplier_ = 0.99972F;
    sustain_tail_gain_ = 1.0F;
    const float tail_seconds = 0.95F + ((1.0F - note_norm) * 1.20F);
    sustain_tail_decay_ = std::exp(-1.0F / std::max(1.0F, sample_rate_ * tail_seconds));

    attack_noise_env_ = 0.20F + (velocity_norm_ * 0.16F);
    key_off_noise_env_ = 0.0F;
    brilliance_env_ = 0.70F + (velocity_norm_ * 1.00F);
    brilliance_decay_ = 0.9985F - (velocity_norm_ * 0.00030F);
    hammer_env_ = 1.0F;
    hammer_decay_ = std::clamp(0.9960F - (velocity_norm_ * 0.0007F), 0.9920F, 0.9970F);

    attack_noise_lp_ = 0.0F;
    key_off_noise_lp_ = 0.0F;
    body_lp_state_ = 0.0F;
    body_hp_state_ = 0.0F;
    body_prev_input_ = 0.0F;
    activity_level_ = 0.0F;

    hp_x_prev_ = 0.0F;
    hp_y_prev_ = 0.0F;
    output_prev_ = 0.0F;
    const float cutoff_hz = 36.0F;
    const float rc = 1.0F / (2.0F * kPi * cutoff_hz);
    const float dt = 1.0F / sample_rate_;
    hp_alpha_ = rc / (rc + dt);
}

void AcousticGrandVoice::release(const float pedal_amount) noexcept {
    if (!active_) {
        return;
    }

    releasing_ = true;
    const float note_norm = clamp01(static_cast<float>(note_) / 127.0F);
    const float damped_release_seconds = 0.030F + ((1.0F - note_norm) * 0.055F);
    const float sustained_release_seconds = 0.16F + ((1.0F - note_norm) * 0.18F);
    const float fast_release = std::exp(-1.0F / std::max(1.0F, sample_rate_ * damped_release_seconds));
    const float slow_release = std::exp(-1.0F / std::max(1.0F, sample_rate_ * sustained_release_seconds));
    const float blend = std::sqrt(clamp01(pedal_amount));
    release_multiplier_ = fast_release + ((slow_release - fast_release) * blend);
    sustain_tail_decay_ = std::exp(-1.0F / std::max(1.0F, sample_rate_ * (0.060F + (blend * 0.30F))));
    key_off_noise_env_ = 0.8F;
}

float AcousticGrandVoice::render_sample() noexcept {
    if (!active_) {
        return 0.0F;
    }

    if (releasing_) {
        release_gain_ *= release_multiplier_;
    }
    sustain_tail_gain_ *= sustain_tail_decay_;

    // Bridge-mediated exchange between unison strings creates beating/bloom in the sustain.
    if (string_count_ > 1) {
        for (std::size_t s = 0; s < string_count_; ++s) {
            float others = 0.0F;
            for (std::size_t o = 0; o < string_count_; ++o) {
                if (o == s) {
                    continue;
                }
                others += string_activity_[o];
            }
            others /= std::max(1.0F, static_cast<float>(string_count_ - 1));
            string_activity_[s] += (others - string_activity_[s]) * string_exchange_rate_[s];
        }
    }

    float summed = 0.0F;
    float energy = 0.0F;
    std::array<float, kMaxStrings> instantaneous{};
    const float transient_brightness = (hammer_env_ * (1.4F + velocity_norm_)) + (brilliance_env_ * 0.36F);

    for (std::size_t i = 0; i < mode_count_; ++i) {
        const std::size_t s = static_cast<std::size_t>(mode_string_index_[i]);
        const float brightness = brightness_weights_[i];
        const float contact_brightness = 1.0F + (brightness * hammer_env_ * 2.15F);
        const float bloom = 1.0F + (string_activity_[s] * 0.60F);
        const float mode_gain = (1.0F + (brightness * transient_brightness * 0.42F)) * contact_brightness * bloom;
        const float mode_sample = FastSinTable::sample_normalized(phases_[i]) * amplitudes_[i] * mode_gain;
        summed += mode_sample;
        instantaneous[s] += std::abs(mode_sample);

        phases_[i] += increments_[i];
        if (phases_[i] >= kTwoPi) {
            phases_[i] -= kTwoPi;
        }

        amplitudes_[i] *= decays_[i];
        if (amplitudes_[i] < 1.0e-9F) {
            amplitudes_[i] = 0.0F;
        }
        energy += amplitudes_[i];
    }

    for (std::size_t s = 0; s < string_count_; ++s) {
        string_activity_[s] = (string_activity_[s] * 0.993F) + (instantaneous[s] * 0.026F);
        if (string_activity_[s] < 1.0e-9F) {
            string_activity_[s] = 0.0F;
        }
    }

    attack_gain_ = std::min(1.0F, attack_gain_ + attack_increment_);
    brilliance_env_ *= brilliance_decay_;
    hammer_env_ *= hammer_decay_;
    hammer_env_ *= hammer_contact_decay_;

    const float double_slope_env = (0.82F * release_gain_) + (0.18F * sustain_tail_gain_);
    float output = summed * attack_gain_ * double_slope_env * note_gain_ * (0.0045F + (velocity_norm_ * 0.0070F));

    if (attack_noise_env_ > 0.00003F) {
        const float strike_noise = next_noise(noise_state_);
        attack_noise_lp_ += attack_noise_lp_coeff_ * (strike_noise - attack_noise_lp_);
        const float band_noise = strike_noise - attack_noise_lp_;
        output += band_noise * attack_noise_env_ * (0.0017F + (velocity_norm_ * 0.0010F));
        attack_noise_env_ *= 0.82F;
    }

    if (key_off_noise_env_ > 0.00001F) {
        const float noise = next_noise(noise_state_);
        key_off_noise_lp_ += key_off_noise_lp_coeff_ * (noise - key_off_noise_lp_);
        const float band_noise = noise - key_off_noise_lp_;
        output += band_noise * key_off_noise_env_ * 0.0020F;
        key_off_noise_env_ *= 0.92F;
    }

    body_lp_state_ += body_lp_coeff_ * (output - body_lp_state_);
    body_hp_state_ = body_hp_coeff_ * (body_hp_state_ + output - body_prev_input_);
    body_prev_input_ = output;
    const float body_colored = (output * 0.58F) + (body_lp_state_ * 0.24F) + (body_hp_state_ * 0.18F);

    // Gentle high-pass to reduce low-end buildup and subsonic rumble.
    const float hp = hp_alpha_ * (hp_y_prev_ + body_colored - hp_x_prev_);
    hp_x_prev_ = body_colored;
    hp_y_prev_ = hp;
    if (std::abs(hp_x_prev_) < 1.0e-12F) {
        hp_x_prev_ = 0.0F;
    }
    if (std::abs(hp_y_prev_) < 1.0e-12F) {
        hp_y_prev_ = 0.0F;
    }
    float out = hp;
    constexpr float kMaxStep = 0.58F;
    const float step = out - output_prev_;
    if (std::abs(step) > kMaxStep) {
        out = output_prev_ + std::copysign(kMaxStep, step);
    }
    output_prev_ = out;

    activity_level_ = (activity_level_ * 0.995F) + (std::abs(out) * 0.005F);
    if (activity_level_ < 1.0e-10F) {
        activity_level_ = 0.0F;
    }

    if (energy < 0.0002F && release_gain_ < 0.001F && sustain_tail_gain_ < 0.0012F
        && key_off_noise_env_ < 0.00005F && attack_noise_env_ < 0.00005F) {
        active_ = false;
        activity_level_ = 0.0F;
    }

    return out;
}

}  // namespace vb
