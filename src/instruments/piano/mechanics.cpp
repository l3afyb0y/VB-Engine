#include "instruments/piano/mechanics.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace vb {

namespace {
constexpr float kPi = 3.14159265358979323846F;

float clamp_velocity(const std::uint8_t velocity) {
    return std::clamp(static_cast<float>(velocity) / 127.0F, 0.0F, 1.0F);
}

}  // namespace

void PianoMechanicsLayer::initialize(const float sample_rate) noexcept {
    sample_rate_ = std::max(1.0F, sample_rate);
    for (auto& voice : voices_) {
        voice = NoiseVoice{};
    }
    dc_x_ = 0.0F;
    dc_y_ = 0.0F;
}

float PianoMechanicsLayer::sample_hermite(const PianoSampleRegion& region, const float position) noexcept {
    const int i1 = static_cast<int>(position);
    const float frac = position - static_cast<float>(i1);

    const float xm1 = region.sample_at(i1 - 1);
    const float x0 = region.sample_at(i1);
    const float x1 = region.sample_at(i1 + 1);
    const float x2 = region.sample_at(i1 + 2);

    const float c0 = x0;
    const float c1 = 0.5F * (x1 - xm1);
    const float c2 = xm1 - (2.5F * x0) + (2.0F * x1) - (0.5F * x2);
    const float c3 = (0.5F * (x2 - xm1)) + (1.5F * (x0 - x1));
    return ((c3 * frac + c2) * frac + c1) * frac + c0;
}

float PianoMechanicsLayer::lp_coeff_from_hz(const float cutoff_hz, const float sample_rate) noexcept {
    const float f = std::clamp(cutoff_hz, 20.0F, 20000.0F);
    const float sr = std::max(1.0F, sample_rate);
    return std::exp((-2.0F * kPi * f) / sr);
}

float PianoMechanicsLayer::hp_coeff_from_hz(const float cutoff_hz, const float sample_rate) noexcept {
    const float f = std::clamp(cutoff_hz, 10.0F, 4000.0F);
    const float sr = std::max(1.0F, sample_rate);
    const float rc = 1.0F / (2.0F * kPi * f);
    const float dt = 1.0F / sr;
    return rc / (rc + dt);
}

PianoMechanicsLayer::NoiseVoice& PianoMechanicsLayer::acquire_voice() noexcept {
    std::size_t quietest = 0;
    float quietest_energy = std::numeric_limits<float>::max();
    for (std::size_t i = 0; i < voices_.size(); ++i) {
        if (!voices_[i].active) {
            return voices_[i];
        }
        const float energy = std::abs(voices_[i].gain * voices_[i].env);
        if (energy < quietest_energy) {
            quietest_energy = energy;
            quietest = i;
        }
    }
    return voices_[quietest];
}

void PianoMechanicsLayer::start_voice(
    const VoiceType type,
    const PianoSampleRegion* region,
    const float gain,
    const float env_decay,
    const float max_seconds,
    const float lp_cutoff_hz,
    const bool use_highpass,
    const float hp_cutoff_hz
) noexcept {
    if (region == nullptr || region->empty()) {
        return;
    }

    NoiseVoice& voice = acquire_voice();
    voice = NoiseVoice{};
    voice.active = true;
    voice.type = type;
    voice.region = region;
    voice.position = static_cast<float>(region->sample_start);
    voice.ratio = static_cast<float>(region->sample_rate) / sample_rate_;
    voice.end = static_cast<float>(region->sample_end);
    voice.max_frames = static_cast<std::uint32_t>(std::max(1.0F, max_seconds * sample_rate_));
    voice.gain = std::max(0.0F, gain);
    voice.env = 1.0F;
    voice.env_decay = std::clamp(env_decay, 0.850F, 0.99995F);
    voice.lp_coeff = lp_coeff_from_hz(lp_cutoff_hz, sample_rate_);
    voice.use_highpass = use_highpass;
    voice.hp_coeff = hp_coeff_from_hz(hp_cutoff_hz, sample_rate_);
}

void PianoMechanicsLayer::trigger_key_down(const PianoSampleRegion* region, const std::uint8_t velocity) noexcept {
    if (region == nullptr) {
        return;
    }

    const float vel = clamp_velocity(velocity);
    const float gain = 0.010F + (0.030F * std::pow(vel, 1.35F));
    const float env_decay = std::exp(-1.0F / std::max(1.0F, sample_rate_ * (0.010F + ((1.0F - vel) * 0.010F))));
    const float lp_cutoff = 1000.0F + (vel * 3000.0F);
    start_voice(
        VoiceType::KeyDown,
        region,
        gain,
        env_decay,
        0.018F,
        lp_cutoff,
        true,
        55.0F
    );
}

void PianoMechanicsLayer::trigger_damper_fall(
    const PianoSampleRegion* region,
    const std::uint8_t velocity,
    const float pedal_amount
) noexcept {
    if (region == nullptr) {
        return;
    }

    const float vel = clamp_velocity(velocity);
    const float pedal = std::clamp(pedal_amount, 0.0F, 1.0F);
    const float gain = (0.0075F + (0.018F * std::sqrt(vel))) * (0.80F + (0.35F * pedal));
    const float env_decay = std::exp(-1.0F / std::max(1.0F, sample_rate_ * (0.018F + (pedal * 0.016F))));
    const float lp_cutoff = 1200.0F + (vel * 2200.0F);
    start_voice(
        VoiceType::DamperFall,
        region,
        gain,
        env_decay,
        0.040F,
        lp_cutoff,
        true,
        40.0F
    );
}

void PianoMechanicsLayer::trigger_pedal_whoosh(
    const PianoSampleRegion* region,
    const bool pedal_down,
    const float intensity
) noexcept {
    if (region == nullptr) {
        return;
    }

    const float amt = std::clamp(intensity, 0.0F, 1.0F);
    const float direction_scale = pedal_down ? 1.07F : 0.93F;
    const float gain = direction_scale * (0.010F + (0.034F * amt));
    const float env_decay = std::exp(-1.0F / std::max(1.0F, sample_rate_ * 0.085F));
    start_voice(
        pedal_down ? VoiceType::PedalDown : VoiceType::PedalUp,
        region,
        gain,
        env_decay,
        0.200F,
        5200.0F,
        false,
        30.0F
    );
}

float PianoMechanicsLayer::render_sample() noexcept {
    float sum = 0.0F;
    for (auto& voice : voices_) {
        if (!voice.active || voice.region == nullptr) {
            continue;
        }
        if (voice.position >= (voice.end - 1.0F) || voice.rendered >= voice.max_frames) {
            voice.active = false;
            continue;
        }

        float raw = sample_hermite(*voice.region, voice.position);
        voice.position += voice.ratio;
        ++voice.rendered;

        voice.lp_state = ((1.0F - voice.lp_coeff) * raw) + (voice.lp_coeff * voice.lp_state);
        float shaped = voice.lp_state;
        if (voice.use_highpass) {
            const float hp = voice.hp_coeff * (voice.hp_y_prev + shaped - voice.hp_x_prev);
            voice.hp_x_prev = shaped;
            voice.hp_y_prev = hp;
            shaped = hp;
        }

        const float voiced = shaped * voice.gain * voice.env;
        sum += voiced;
        voice.env *= voice.env_decay;

        if (voice.env < 1.0e-4F) {
            voice.active = false;
        }
    }

    // Softly constrain dense overlapping mechanics bursts.
    const float limited = std::tanh(sum * 1.45F) * 0.68F;
    const float dc = limited - dc_x_ + (0.996F * dc_y_);
    dc_x_ = limited;
    dc_y_ = dc;
    if (std::abs(dc_x_) < 1.0e-12F) {
        dc_x_ = 0.0F;
    }
    if (std::abs(dc_y_) < 1.0e-12F) {
        dc_y_ = 0.0F;
    }
    return dc;
}

}  // namespace vb
