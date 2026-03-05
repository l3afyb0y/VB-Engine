#include "instruments/piano/sampled_piano_voice.hpp"

#include <algorithm>
#include <cmath>

namespace vb {

namespace {
constexpr float kPi = 3.14159265358979323846F;

float midi_note_to_frequency(const std::uint8_t note) {
    const float semitones_from_a4 = static_cast<float>(static_cast<int>(note) - 69);
    return 440.0F * std::pow(2.0F, semitones_from_a4 / 12.0F);
}

float cents_to_ratio(const float cents) {
    return std::pow(2.0F, cents / 1200.0F);
}

std::uint32_t xorshift32(std::uint32_t& state) noexcept {
    if (state == 0U) {
        state = 0x9E3779B9U;
    }
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

float random_unit_signed(std::uint32_t& state) noexcept {
    const std::uint32_t bits = xorshift32(state) & 0x00FFFFFFu;
    const float u = static_cast<float>(bits) / static_cast<float>(0x00FFFFFFu);
    return (u * 2.0F) - 1.0F;
}

float safe_at(const PianoSampleRegion& region, const int index, const bool right) {
    return right ? region.sample_at_right(index) : region.sample_at_left(index);
}

}  // namespace

float SampledPianoVoice::sample_hermite(const PianoSampleRegion& region, const float position, const bool right) noexcept {
    const int i1 = static_cast<int>(position);
    const float frac = position - static_cast<float>(i1);

    const float xm1 = safe_at(region, i1 - 1, right);
    const float x0 = safe_at(region, i1, right);
    const float x1 = safe_at(region, i1 + 1, right);
    const float x2 = safe_at(region, i1 + 2, right);

    const float c0 = x0;
    const float c1 = 0.5F * (x1 - xm1);
    const float c2 = xm1 - (2.5F * x0) + (2.0F * x1) - (0.5F * x2);
    const float c3 = (0.5F * (x2 - xm1)) + (1.5F * (x0 - x1));

    return ((c3 * frac + c2) * frac + c1) * frac + c0;
}

float SampledPianoVoice::sample_windowed_sinc(const PianoSampleRegion& region, const float position, const bool right) noexcept {
    constexpr int kRadius = 4;
    const int center = static_cast<int>(std::floor(position));
    auto sinc = [](float x) noexcept {
        if (std::abs(x) < 1.0e-6F) {
            return 1.0F;
        }
        const float px = kPi * x;
        return std::sin(px) / px;
    };

    float sum = 0.0F;
    float norm = 0.0F;
    for (int i = -kRadius + 1; i <= kRadius; ++i) {
        const int sample_index = center + i;
        const float x = static_cast<float>(sample_index) - position;
        const float ax = std::abs(x);
        if (ax >= static_cast<float>(kRadius)) {
            continue;
        }
        const float window = sinc(x / static_cast<float>(kRadius));
        const float weight = sinc(x) * window;
        sum += safe_at(region, sample_index, right) * weight;
        norm += weight;
    }
    if (std::abs(norm) < 1.0e-6F) {
        return safe_at(region, center, right);
    }
    return sum / norm;
}

bool SampledPianoVoice::start(
    const PianoLayerSelection& selection,
    const std::uint8_t note,
    const std::uint8_t velocity,
    const float sample_rate,
    const float* tuned_frequencies,
    const std::uint32_t delay_samples,
    const float gain_jitter,
    const float soft_pedal_amount
) noexcept {
    if (selection.primary == nullptr || selection.primary->empty()) {
        active_ = false;
        return false;
    }

    active_ = true;
    releasing_ = false;
    note_ = note;
    const float note_norm = std::clamp(static_cast<float>(note_) / 127.0F, 0.0F, 1.0F);
    sample_rate_ = sample_rate;
    delay_samples_ = delay_samples;
    layer_a_.region = selection.primary;
    layer_a_.start = static_cast<float>(selection.primary->sample_start);
    layer_a_.end = static_cast<float>(selection.primary->sample_end);
    layer_a_.position = layer_a_.start;
    layer_a_.loop_enabled = selection.primary->loop_enabled;
    layer_a_.loop_until_release = selection.primary->loop_until_release;
    layer_a_.loop_start = static_cast<float>(selection.primary->loop_start);
    layer_a_.loop_end = static_cast<float>(selection.primary->loop_end);

    const float target_frequency = tuned_frequencies != nullptr ? tuned_frequencies[note] : midi_note_to_frequency(note);
    const float root_frequency = tuned_frequencies != nullptr ? tuned_frequencies[layer_a_.region->root_key]
                                                              : midi_note_to_frequency(layer_a_.region->root_key);
    layer_a_.ratio = (target_frequency / std::max(1.0F, root_frequency))
        * (static_cast<float>(layer_a_.region->sample_rate) / sample_rate)
        * cents_to_ratio(layer_a_.region->tune_cents);
    const float upper_weight = std::clamp((note_norm - 0.48F) / 0.52F, 0.0F, 1.0F);
    const float detune_cents_a = upper_weight * random_unit_signed(rand_state_) * 0.90F;
    layer_a_.ratio *= cents_to_ratio(detune_cents_a);

    layer_b_.region = selection.secondary;
    layer_b_.position = 0.0F;
    layer_b_mix_ = std::clamp(selection.secondary_mix, 0.0F, 1.0F);
    layer_a_gain_ = std::cos(0.5F * kPi * layer_b_mix_);
    layer_b_gain_ = std::sin(0.5F * kPi * layer_b_mix_);

    if (layer_b_.region != nullptr && !layer_b_.region->empty()) {
        layer_b_.start = static_cast<float>(layer_b_.region->sample_start);
        layer_b_.end = static_cast<float>(layer_b_.region->sample_end);
        layer_b_.position = layer_b_.start;
        layer_b_.loop_enabled = layer_b_.region->loop_enabled;
        layer_b_.loop_until_release = layer_b_.region->loop_until_release;
        layer_b_.loop_start = static_cast<float>(layer_b_.region->loop_start);
        layer_b_.loop_end = static_cast<float>(layer_b_.region->loop_end);
        const float root_b = tuned_frequencies != nullptr ? tuned_frequencies[layer_b_.region->root_key]
                                                          : midi_note_to_frequency(layer_b_.region->root_key);
        layer_b_.ratio = (target_frequency / std::max(1.0F, root_b))
            * (static_cast<float>(layer_b_.region->sample_rate) / sample_rate)
            * cents_to_ratio(layer_b_.region->tune_cents);
        const float detune_cents_b = upper_weight * random_unit_signed(rand_state_) * 1.10F;
        layer_b_.ratio *= cents_to_ratio(detune_cents_b);
    } else {
        layer_b_ = LayerState{};
    }

    attack_gain_ = 0.0F;
    const float velocity_norm = std::clamp(static_cast<float>(velocity) / 127.0F, 0.0F, 1.0F);
    velocity_norm_ = velocity_norm;
    off_mode_fast_ = layer_a_.region->off_mode_fast;
    const float attack_seconds = std::clamp(
        0.0048F - (0.0020F * velocity_norm) + ((1.0F - note_norm) * 0.0012F),
        0.0022F,
        0.0062F
    );
    attack_increment_ = 1.0F / std::max(1.0F, sample_rate * attack_seconds);
    release_gain_ = 1.0F;
    const float default_release_seconds = 0.23F + ((1.0F - note_norm) * 0.22F);
    configured_release_multiplier_ = std::exp(-1.0F / std::max(1.0F, sample_rate * default_release_seconds));
    const float release_a = std::max(0.0F, layer_a_.region->release_seconds);
    const float release_b = (layer_b_.region != nullptr) ? std::max(0.0F, layer_b_.region->release_seconds) : 0.0F;
    const float weighted_release = (release_a * (1.0F - layer_b_mix_)) + (release_b * layer_b_mix_);
    if (weighted_release > 0.0F) {
        const float effective_release = std::max(weighted_release, default_release_seconds * 0.7F);
        configured_release_multiplier_ = std::exp(-1.0F / std::max(1.0F, sample_rate * effective_release));
    }
    release_multiplier_ = configured_release_multiplier_;
    release_fast_gain_ = 1.0F;
    release_slow_gain_ = 1.0F;
    release_fast_mul_ = configured_release_multiplier_;
    release_slow_mul_ = configured_release_multiplier_;
    activity_level_ = 0.0F;
    rendered_frames_ = 0;
    hp_x_prev_ = 0.0F;
    hp_y_prev_ = 0.0F;
    const float cutoff_hz = 18.0F;
    const float rc = 1.0F / (2.0F * kPi * cutoff_hz);
    const float dt = 1.0F / std::max(1.0F, sample_rate_);
    hp_alpha_ = rc / (rc + dt);
    const float register_warmth = std::clamp(1.0F - std::pow(note_norm, 1.45F), 0.0F, 1.0F);
    const float timbre_cutoff_hz = std::clamp(
        (1500.0F + (11200.0F * std::pow(velocity_norm_, 0.84F)) + (2200.0F * note_norm))
            * (0.88F + (0.18F * register_warmth)),
        1100.0F,
        17400.0F
    );
    timbre_target_lp_coeff_ = std::exp((-2.0F * kPi * timbre_cutoff_hz) / std::max(1.0F, sample_rate_));
    timbre_lp_coeff_ = timbre_target_lp_coeff_;
    timbre_high_mix_ = std::clamp(
        (0.56F + (0.52F * std::sqrt(velocity_norm_))) * (0.90F + (0.10F * register_warmth)),
        0.45F,
        1.08F
    );
    timbre_body_mix_ = std::clamp(0.95F + (0.22F * note_norm) + (0.06F * register_warmth), 0.86F, 1.20F);
    timbre_high_mix_ *= (1.0F - (0.40F * soft_pedal_amount_));
    timbre_body_mix_ *= (1.0F - (0.20F * soft_pedal_amount_));
    timbre_lp_state_l_ = 0.0F;
    timbre_lp_state_r_ = 0.0F;
    damper_sizzle_env_ = 0.0F;
    damper_sizzle_decay_ = 0.992F;
    damper_hp_state_ = 0.0F;
    last_mixed_ = 0.0F;
    room_delay_line_l_.fill(0.0F);
    room_delay_line_r_.fill(0.0F);
    room_delay_index_ = 0;
    room_lp_state_l_ = 0.0F;
    room_lp_state_r_ = 0.0F;
    phase_correlation_ = 0.0F;
    soft_pedal_amount_ = std::clamp(soft_pedal_amount, 0.0F, 1.0F);
    hammer_contact_env_ = 1.0F;
    const float hammer_decay_seconds = std::clamp(
        0.010F + (0.020F * (1.0F - velocity_norm_)) + (0.010F * note_norm),
        0.008F,
        0.050F
    );
    hammer_contact_decay_ = std::exp(-1.0F / std::max(1.0F, sample_rate_ * hammer_decay_seconds));
    attack_brilliance_env_ = 0.35F + (0.65F * velocity_norm_);
    const float attack_brilliance_seconds = std::clamp(
        0.018F + (0.022F * (1.0F - velocity_norm_)) + (0.009F * note_norm),
        0.012F,
        0.055F
    );
    attack_brilliance_decay_ = std::exp(-1.0F / std::max(1.0F, sample_rate_ * attack_brilliance_seconds));
    de_synth_noise_lp_ = 0.0F;

    const float layer_a_weight = 1.0F - layer_b_mix_;
    const float layer_b_weight = layer_b_mix_;
    const float amp_veltrack = (layer_a_.region->amp_veltrack * layer_a_weight)
        + ((layer_b_.region != nullptr ? layer_b_.region->amp_veltrack : layer_a_.region->amp_veltrack) * layer_b_weight);
    const float amp_velcurve = (layer_a_.region->amp_velcurve * layer_a_weight)
        + ((layer_b_.region != nullptr ? layer_b_.region->amp_velcurve : layer_a_.region->amp_velcurve) * layer_b_weight);
    const float vel_track_norm = std::clamp(amp_veltrack * 0.01F, 0.2F, 2.0F);
    const float vel_curve_norm = std::clamp(amp_velcurve, 0.25F, 3.0F);
    const float velocity_power = std::clamp((1.36F / vel_track_norm) * (1.0F / std::sqrt(vel_curve_norm)), 0.52F, 2.35F);
    const float velocity_gain = std::pow(velocity_norm, velocity_power);
    const float dynamic_floor = 0.10F + (0.06F * (1.0F - note_norm));
    base_gain_ = (dynamic_floor + (velocity_gain * (0.98F - dynamic_floor)))
        * std::max(0.2F, gain_jitter)
        * layer_a_.region->gain_linear;
    base_gain_ *= (1.0F - (0.30F * soft_pedal_amount_));

    return true;
}

void SampledPianoVoice::release(const float pedal_amount) noexcept {
    releasing_ = true;
    const float pedal = std::clamp(pedal_amount, 0.0F, 1.0F);
    if (off_mode_fast_ && pedal < 0.05F) {
        release_fast_mul_ = std::exp(-1.0F / std::max(1.0F, sample_rate_ * 0.010F));
        release_slow_mul_ = std::exp(-1.0F / std::max(1.0F, sample_rate_ * 0.045F));
        release_multiplier_ = release_fast_mul_;
        return;
    }
    const float note_norm = std::clamp(static_cast<float>(note_) / 127.0F, 0.0F, 1.0F);
    const float damped_release_seconds = 0.024F + ((1.0F - note_norm) * 0.052F);
    const float fast_release = std::exp(-1.0F / std::max(1.0F, sample_rate_ * damped_release_seconds));
    const float pedal_blend = std::sqrt(pedal);
    release_fast_mul_ = fast_release;
    release_slow_mul_ = configured_release_multiplier_;
    release_multiplier_ = fast_release + ((configured_release_multiplier_ - fast_release) * pedal_blend);
    const float sizzle_intensity = (1.0F - pedal) * (0.015F + (0.050F * velocity_norm_));
    damper_sizzle_env_ = std::max(damper_sizzle_env_, sizzle_intensity);
    damper_sizzle_decay_ = std::exp(-1.0F / std::max(1.0F, sample_rate_ * (0.020F + ((1.0F - note_norm) * 0.016F))));
}

SampledPianoVoice::StereoSample SampledPianoVoice::render_layer(LayerState& layer, const bool render_right_channel) noexcept {
    if (layer.region == nullptr || layer.region->empty()) {
        return StereoSample{};
    }

    if (layer.position >= (layer.end - 1.0F)) {
        return StereoSample{};
    }

    const float transposition = std::abs(layer.ratio - 1.0F);
    const bool high_quality = transposition > 0.080F;
    float sample_left = high_quality ? sample_windowed_sinc(*layer.region, layer.position, false)
                                     : sample_hermite(*layer.region, layer.position, false);
    float sample_right = sample_left;
    if (render_right_channel) {
        sample_right = high_quality ? sample_windowed_sinc(*layer.region, layer.position, true)
                                    : sample_hermite(*layer.region, layer.position, true);
    }
    float next_position = layer.position + layer.ratio;

    if (layer.loop_enabled && layer.loop_end > layer.loop_start) {
        const bool keep_looping = !releasing_ || !layer.loop_until_release;
        if (keep_looping) {
            // Longer loop crossfade reduces subtle loop-edge chatter in sustained notes.
            constexpr float kLoopCrossfade = 96.0F;
            if (layer.position >= (layer.loop_end - kLoopCrossfade)) {
                const float loop_pos = layer.loop_start + (layer.position - (layer.loop_end - kLoopCrossfade));
                const float loop_left = high_quality ? sample_windowed_sinc(*layer.region, loop_pos, false)
                                                     : sample_hermite(*layer.region, loop_pos, false);
                float loop_right = loop_left;
                if (render_right_channel) {
                    loop_right = high_quality ? sample_windowed_sinc(*layer.region, loop_pos, true)
                                              : sample_hermite(*layer.region, loop_pos, true);
                }
                const float alpha = std::clamp((layer.position - (layer.loop_end - kLoopCrossfade)) / kLoopCrossfade, 0.0F, 1.0F);
                sample_left = ((1.0F - alpha) * sample_left) + (alpha * loop_left);
                sample_right = ((1.0F - alpha) * sample_right) + (alpha * loop_right);
            }

            if (next_position >= layer.loop_end) {
                const float loop_length = std::max(1.0F, layer.loop_end - layer.loop_start);
                next_position = layer.loop_start + std::fmod(next_position - layer.loop_start, loop_length);
            }
        }
    }

    layer.position = next_position;
    return StereoSample{sample_left, sample_right};
}

float SampledPianoVoice::render_sample(
    float* close_bus,
    float* room_bus,
    float* direct_left,
    float* direct_right
) noexcept {
    if (!active_) {
        if (close_bus != nullptr) {
            *close_bus = 0.0F;
        }
        if (room_bus != nullptr) {
            *room_bus = 0.0F;
        }
        if (direct_left != nullptr) {
            *direct_left = 0.0F;
        }
        if (direct_right != nullptr) {
            *direct_right = 0.0F;
        }
        return 0.0F;
    }

    if (delay_samples_ > 0) {
        --delay_samples_;
        if (close_bus != nullptr) {
            *close_bus = 0.0F;
        }
        if (room_bus != nullptr) {
            *room_bus = 0.0F;
        }
        if (direct_left != nullptr) {
            *direct_left = 0.0F;
        }
        if (direct_right != nullptr) {
            *direct_right = 0.0F;
        }
        return 0.0F;
    }

    if (releasing_) {
        release_fast_gain_ *= release_fast_mul_;
        release_slow_gain_ *= release_slow_mul_;
        const float note_norm = std::clamp(static_cast<float>(note_) / 127.0F, 0.0F, 1.0F);
        const float slow_weight = std::clamp(0.24F + (0.34F * (1.0F - note_norm)), 0.24F, 0.58F);
        const float fast_weight = 1.0F - slow_weight;
        release_gain_ = (fast_weight * release_fast_gain_) + (slow_weight * release_slow_gain_);
    }

    const bool want_stereo_direct = (direct_left != nullptr && direct_right != nullptr);
    const StereoSample sample_a = render_layer(layer_a_, want_stereo_direct);
    const StereoSample sample_b = render_layer(layer_b_, want_stereo_direct);
    const bool a_active = (layer_a_.region != nullptr) && (layer_a_.position < (layer_a_.end - 1.0F));
    const bool b_active = (layer_b_.region != nullptr) && (layer_b_.position < (layer_b_.end - 1.0F));

    if (!a_active && !b_active) {
        active_ = false;
        activity_level_ = 0.0F;
        if (close_bus != nullptr) {
            *close_bus = 0.0F;
        }
        if (room_bus != nullptr) {
            *room_bus = 0.0F;
        }
        if (direct_left != nullptr) {
            *direct_left = 0.0F;
        }
        if (direct_right != nullptr) {
            *direct_right = 0.0F;
        }
        return 0.0F;
    }

    attack_gain_ = std::min(1.0F, attack_gain_ + attack_increment_);
    constexpr float kPi = 3.14159265358979323846F;
    const float attack_shaper = 0.5F - (0.5F * std::cos(kPi * attack_gain_));
    const float mixed_l = (sample_a.left * layer_a_gain_) + (sample_b.left * layer_b_gain_);
    const float mixed_r = (sample_a.right * layer_a_gain_) + (sample_b.right * layer_b_gain_);
    const float mixed = 0.5F * (mixed_l + mixed_r);

    const bool stereo_native_mode = want_stereo_direct
        && (layer_a_.region != nullptr && layer_a_.region->stereo)
        && (layer_b_.region == nullptr || layer_b_.region->stereo);
    if (stereo_native_mode) {
        float output_l = mixed_l * base_gain_ * attack_shaper * release_gain_;
        float output_r = mixed_r * base_gain_ * attack_shaper * release_gain_;

        constexpr std::uint32_t kBoundaryFadeSamples = 72;
        if (rendered_frames_ < kBoundaryFadeSamples) {
            constexpr float kPi = 3.14159265358979323846F;
            const float x = std::clamp(static_cast<float>(rendered_frames_) / static_cast<float>(kBoundaryFadeSamples), 0.0F, 1.0F);
            const float fade = 0.5F - (0.5F * std::cos(kPi * x));
            output_l *= fade;
            output_r *= fade;
        }
        const float remaining_a = std::max(0.0F, layer_a_.end - layer_a_.position);
        const float remaining_b = std::max(0.0F, layer_b_.end - layer_b_.position);
        const float remaining = std::max(remaining_a, remaining_b);
        if (remaining < static_cast<float>(kBoundaryFadeSamples)) {
            constexpr float kPi = 3.14159265358979323846F;
            const float x = std::clamp(remaining / static_cast<float>(kBoundaryFadeSamples), 0.0F, 1.0F);
            const float tail_fade = 0.5F - (0.5F * std::cos(kPi * x));
            output_l *= tail_fade;
            output_r *= tail_fade;
        }

        if (damper_sizzle_env_ > 1.0e-5F) {
            const float high_src = (mixed - last_mixed_);
            damper_hp_state_ += 0.14F * (high_src - damper_hp_state_);
            const float sizzle = damper_hp_state_ * damper_sizzle_env_ * 0.10F;
            output_l += sizzle;
            output_r += sizzle;
            damper_sizzle_env_ *= damper_sizzle_decay_;
        }
        // Velocity-coupled brilliance burst + micro-irregularity to avoid static synth-like attacks.
        if (attack_brilliance_env_ > 1.0e-5F) {
            const float note_norm = std::clamp(static_cast<float>(note_) / 127.0F, 0.0F, 1.0F);
            const float sparkle_drive = (0.006F + (0.018F * velocity_norm_)) * attack_brilliance_env_;
            const float transient_l = (output_l - last_mixed_) * sparkle_drive;
            const float transient_r = (output_r - last_mixed_) * sparkle_drive;
            output_l += transient_l;
            output_r += transient_r;
            const float white = random_unit_signed(rand_state_);
            de_synth_noise_lp_ += 0.20F * (white - de_synth_noise_lp_);
            const float breath = (white - de_synth_noise_lp_) * attack_brilliance_env_ * (0.0007F + (0.0006F * note_norm));
            output_l += breath;
            output_r -= breath;
            attack_brilliance_env_ *= attack_brilliance_decay_;
        }
        last_mixed_ = mixed;

        const float output = 0.5F * (output_l + output_r);
        const float hp_in = output;
        const float hp_out = hp_alpha_ * (hp_y_prev_ + hp_in - hp_x_prev_);
        const float hp_delta = hp_out - output;
        output_l += hp_delta;
        output_r += hp_delta;
        hp_x_prev_ = hp_in;
        hp_y_prev_ = hp_out;
        if (std::abs(hp_x_prev_) < 1.0e-12F) {
            hp_x_prev_ = 0.0F;
        }
        if (std::abs(hp_y_prev_) < 1.0e-12F) {
            hp_y_prev_ = 0.0F;
        }
        ++rendered_frames_;

        const float mono_output = 0.5F * (output_l + output_r);
        activity_level_ = (activity_level_ * 0.99F) + (std::abs(mono_output) * 0.01F);
        if (activity_level_ < 1.0e-9F) {
            activity_level_ = 0.0F;
        }
        if (release_gain_ < 0.00005F && activity_level_ < 0.000015F) {
            active_ = false;
            activity_level_ = 0.0F;
        }

        if (close_bus != nullptr) {
            *close_bus = 0.0F;
        }
        if (room_bus != nullptr) {
            *room_bus = 0.0F;
        }
        if (direct_left != nullptr) {
            *direct_left = output_l;
        }
        if (direct_right != nullptr) {
            *direct_right = output_r;
        }
        return mono_output;
    }

    // Continuous timbre smoothing: velocity-tracked LP + spectral tilt.
    timbre_lp_coeff_ += 0.0009F * (timbre_target_lp_coeff_ - timbre_lp_coeff_);
    timbre_lp_state_l_ = ((1.0F - timbre_lp_coeff_) * mixed_l) + (timbre_lp_coeff_ * timbre_lp_state_l_);
    timbre_lp_state_r_ = ((1.0F - timbre_lp_coeff_) * mixed_r) + (timbre_lp_coeff_ * timbre_lp_state_r_);
    const float high_band_l = mixed_l - timbre_lp_state_l_;
    const float high_band_r = mixed_r - timbre_lp_state_r_;
    hammer_contact_env_ *= hammer_contact_decay_;
    const float sustain_darkening = 1.0F - (0.33F * std::clamp(
        static_cast<float>(rendered_frames_) / std::max(1.0F, sample_rate_ * 1.10F),
        0.0F,
        1.0F
    ));
    const float hammer_presence = 0.64F + (0.36F * hammer_contact_env_);
    const float note_norm = std::clamp(static_cast<float>(note_) / 127.0F, 0.0F, 1.0F);
    const float upper_soften = 1.0F - (0.12F * note_norm * note_norm);
    const float attack_brilliance = 1.0F + ((0.16F + (0.24F * velocity_norm_)) * attack_brilliance_env_);
    const float timbre_high_l = high_band_l * (timbre_high_mix_ * sustain_darkening * hammer_presence * attack_brilliance * upper_soften);
    const float timbre_high_r = high_band_r * (timbre_high_mix_ * sustain_darkening * hammer_presence * attack_brilliance * upper_soften);
    const float timbre_body_l = timbre_lp_state_l_ * timbre_body_mix_;
    const float timbre_body_r = timbre_lp_state_r_ * timbre_body_mix_;
    // Two-bus voice decomposition: direct/close keeps attack definition;
    // room bus emphasizes body and delayed bloom for perspective mixing.
    const float close_dry_l = (mixed_l * (0.72F + (0.22F * (1.0F - layer_b_mix_)))) + (timbre_high_l * 0.38F);
    const float close_dry_r = (mixed_r * (0.72F + (0.22F * (1.0F - layer_b_mix_)))) + (timbre_high_r * 0.38F);
    room_lp_state_l_ += 0.024F * (timbre_body_l - room_lp_state_l_);
    room_lp_state_r_ += 0.024F * (timbre_body_r - room_lp_state_r_);
    const float room_delayed_l = room_delay_line_l_[room_delay_index_];
    const float room_delayed_r = room_delay_line_r_[room_delay_index_];
    room_delay_line_l_[room_delay_index_] = room_lp_state_l_;
    room_delay_line_r_[room_delay_index_] = room_lp_state_r_;
    room_delay_index_ = (room_delay_index_ + 1U) % static_cast<std::uint32_t>(room_delay_line_l_.size());
    float room_dry_l = (room_lp_state_l_ * 0.90F) + (room_delayed_l * 0.28F) + (timbre_body_l * 0.20F);
    float room_dry_r = (room_lp_state_r_ * 0.90F) + (room_delayed_r * 0.28F) + (timbre_body_r * 0.20F);

    const float close_mid = 0.5F * (close_dry_l + close_dry_r);
    const float room_mid = 0.5F * (room_dry_l + room_dry_r);
    phase_correlation_ = (phase_correlation_ * 0.996F) + (0.004F * (close_mid * room_mid));
    const float phase_scale = 1.0F - (0.38F * std::clamp((-phase_correlation_ - 0.00012F) * 1600.0F, 0.0F, 1.0F));
    room_dry_l *= phase_scale;
    room_dry_r *= phase_scale;

    if (attack_brilliance_env_ > 1.0e-5F) {
        const float white = random_unit_signed(rand_state_);
        de_synth_noise_lp_ += 0.20F * (white - de_synth_noise_lp_);
        const float breath = (white - de_synth_noise_lp_) * attack_brilliance_env_ * (0.0007F + (0.0005F * note_norm));
        room_dry_l += breath * 0.30F;
        room_dry_r -= breath * 0.30F;
    }

    float output_l = ((close_dry_l * (1.0F - layer_b_mix_)) + (room_dry_l * layer_b_mix_)) * base_gain_ * attack_shaper * release_gain_;
    float output_r = ((close_dry_r * (1.0F - layer_b_mix_)) + (room_dry_r * layer_b_mix_)) * base_gain_ * attack_shaper * release_gain_;

    // Micro fade-in/out to avoid boundary clicks for short or tightly edited samples.
    constexpr std::uint32_t kBoundaryFadeSamples = 72;
    if (rendered_frames_ < kBoundaryFadeSamples) {
        constexpr float kPi = 3.14159265358979323846F;
        const float x = std::clamp(static_cast<float>(rendered_frames_) / static_cast<float>(kBoundaryFadeSamples), 0.0F, 1.0F);
        const float fade = 0.5F - (0.5F * std::cos(kPi * x));
        output_l *= fade;
        output_r *= fade;
    }

    const float remaining_a = std::max(0.0F, layer_a_.end - layer_a_.position);
    const float remaining_b = std::max(0.0F, layer_b_.end - layer_b_.position);
    const float remaining = std::max(remaining_a, remaining_b);
    if (remaining < static_cast<float>(kBoundaryFadeSamples)) {
        constexpr float kPi = 3.14159265358979323846F;
        const float x = std::clamp(remaining / static_cast<float>(kBoundaryFadeSamples), 0.0F, 1.0F);
        const float tail_fade = 0.5F - (0.5F * std::cos(kPi * x));
        output_l *= tail_fade;
        output_r *= tail_fade;
    }

    if (damper_sizzle_env_ > 1.0e-5F) {
        const float high_src = (mixed - last_mixed_);
        damper_hp_state_ += 0.14F * (high_src - damper_hp_state_);
        const float sizzle = damper_hp_state_ * damper_sizzle_env_ * 0.10F;
        output_l += sizzle;
        output_r += sizzle;
        damper_sizzle_env_ *= damper_sizzle_decay_;
    }
    last_mixed_ = mixed;
    attack_brilliance_env_ *= attack_brilliance_decay_;

    // Gentle per-voice high-pass to remove DC/very-low drift from sampled low-register tails.
    const float output = 0.5F * (output_l + output_r);
    const float hp_in = output;
    const float hp_out = hp_alpha_ * (hp_y_prev_ + hp_in - hp_x_prev_);
    const float hp_delta = hp_out - output;
    output_l += hp_delta;
    output_r += hp_delta;
    hp_x_prev_ = hp_in;
    hp_y_prev_ = hp_out;
    if (std::abs(hp_x_prev_) < 1.0e-12F) {
        hp_x_prev_ = 0.0F;
    }
    if (std::abs(hp_y_prev_) < 1.0e-12F) {
        hp_y_prev_ = 0.0F;
    }
    ++rendered_frames_;

    const float mono_output = 0.5F * (output_l + output_r);
    activity_level_ = (activity_level_ * 0.99F) + (std::abs(mono_output) * 0.01F);
    if (activity_level_ < 1.0e-9F) {
        activity_level_ = 0.0F;
    }

    if (release_gain_ < 0.00005F && activity_level_ < 0.000015F) {
        active_ = false;
        activity_level_ = 0.0F;
    }

    if (close_bus != nullptr || room_bus != nullptr) {
        const float voice_gain = base_gain_ * attack_shaper * release_gain_;
        const float close_out = (0.5F * (close_dry_l + close_dry_r)) * voice_gain;
        const float room_out = (0.5F * (room_dry_l + room_dry_r)) * voice_gain;
        if (close_bus != nullptr) {
            *close_bus = close_out;
        }
        if (room_bus != nullptr) {
            *room_bus = room_out;
        }
    }
    if (direct_left != nullptr) {
        *direct_left = output_l;
    }
    if (direct_right != nullptr) {
        *direct_right = output_r;
    }

    return mono_output;
}

}  // namespace vb
