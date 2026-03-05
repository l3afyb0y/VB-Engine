#include "instruments/piano/post_processor.hpp"

#include <algorithm>
#include <cmath>

namespace vb {

namespace {
float soft_limit(const float x) noexcept {
    const float abs_x = std::abs(x);
    if (abs_x <= 0.986F) {
        return x;
    }
    const float excess = abs_x - 0.986F;
    const float compressed = 0.986F + (0.026F * std::tanh(excess * 11.0F));
    return std::copysign(compressed, x);
}
}  // namespace

void PianoPostProcessor::initialize(const float sample_rate, const float reverb_wet) noexcept {
    sample_rate_ = sample_rate;
    wet_ = std::clamp(reverb_wet, 0.0F, 0.20F);

    constexpr std::size_t kBodyTaps = 96;
    constexpr std::size_t kRoomTaps = 12288;
    body_ir_.assign(kBodyTaps, 0.0F);
    body_history_.assign(kBodyTaps, 0.0F);
    body_taps_.clear();
    body_taps_.reserve(kBodyTaps);
    body_history_index_ = 0;

    room_ir_.assign(kRoomTaps, 0.0F);
    room_history_.assign(kRoomTaps, 0.0F);
    room_early_taps_.clear();
    room_mid_taps_.clear();
    room_late_taps_.clear();
    room_early_taps_.reserve(256);
    room_mid_taps_.reserve(2048);
    room_late_taps_.reserve(4096);
    room_history_index_ = 0;

    // Body IR: short modal response (soundboard + case) with immediate attack.
    for (std::size_t i = 0; i < kBodyTaps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kBodyTaps - 1);
        const float env = std::exp(-8.1F * t);
        const float modal = (0.55F * std::sin(2.0F * 3.14159265F * (110.0F / sample_rate_) * static_cast<float>(i)))
            + (0.29F * std::sin(2.0F * 3.14159265F * (220.0F / sample_rate_) * static_cast<float>(i)))
            + (0.18F * std::sin(2.0F * 3.14159265F * (330.0F / sample_rate_) * static_cast<float>(i)));
        const float early = (i == 0) ? 0.47F : 0.0F;
        body_ir_[i] = (early + (0.078F * modal)) * env;
    }
    for (std::size_t i = 0; i < body_ir_.size(); ++i) {
        if (std::abs(body_ir_[i]) > 1.0e-6F) {
            body_taps_.push_back(ConvolutionTap{static_cast<std::uint16_t>(i), body_ir_[i]});
        }
    }

    // Room IR: average bedroom profile (approximately 3.6m x 3.0m x 2.5m).
    // First-order image reflections for early field + frequency-damped diffuse late field.
    constexpr float kSoundSpeed = 343.0F;
    constexpr float kRoomX = 3.6F;
    constexpr float kRoomY = 3.0F;
    constexpr float kRoomZ = 2.5F;
    constexpr float sx = 0.92F;
    constexpr float sy = 0.74F;
    constexpr float sz = 1.02F;
    constexpr float lx = 2.34F;
    constexpr float ly = 1.76F;
    constexpr float lz = 1.18F;
    const auto distance3 = [](const float ax, const float ay, const float az, const float bx, const float by, const float bz) -> float {
        const float dx = ax - bx;
        const float dy = ay - by;
        const float dz = az - bz;
        return std::sqrt((dx * dx) + (dy * dy) + (dz * dz));
    };
    const float direct_distance = distance3(sx, sy, sz, lx, ly, lz);

    for (std::size_t i = 0; i < kRoomTaps; ++i) {
        const float time_seconds = static_cast<float>(i) / sample_rate_;
        // Mid-band RT60 around 0.26s with HF damping for a furnished bedroom-sized room.
        const float rt60_mid = 0.26F;
        const float decay = std::pow(10.0F, (-3.0F * time_seconds) / rt60_mid);
        const float hf_damping = std::exp(-4.8F * time_seconds);
        const float diffuse =
            (std::sin(0.119F * static_cast<float>(i)) * std::sin(0.017F * static_cast<float>(i) + 1.37F)
             * std::sin(0.031F * static_cast<float>(i) + 0.51F))
            * 0.034F;
        room_ir_[i] = diffuse * decay * hf_damping;
    }

    struct Reflection {
        float x;
        float y;
        float z;
        float reflectivity;
    };
    const Reflection first_order[] = {
        {-sx, sy, sz, 0.79F},                   // left wall
        {2.0F * kRoomX - sx, sy, sz, 0.74F},    // right wall
        {sx, -sy, sz, 0.76F},                   // front wall
        {sx, 2.0F * kRoomY - sy, sz, 0.71F},    // rear wall
        {sx, sy, -sz, 0.84F},                   // floor
        {sx, sy, 2.0F * kRoomZ - sz, 0.67F},    // ceiling
    };
    for (const Reflection& reflection : first_order) {
        const float distance = distance3(reflection.x, reflection.y, reflection.z, lx, ly, lz);
        const std::size_t delay = static_cast<std::size_t>(std::round((distance / kSoundSpeed) * sample_rate_));
        if (delay >= room_ir_.size() || delay == 0) {
            continue;
        }
        const float attenuation = (direct_distance / std::max(distance, 0.1F));
        room_ir_[delay] += reflection.reflectivity * attenuation * 0.21F;
    }

    // Add a dense early reflection cloud with micro-spread taps.
    // Keep this smooth and mostly positive-energy to avoid impulse-like spikes.
    for (std::size_t ms = 11; ms <= 33; ms += 2) {
        const std::size_t tap = static_cast<std::size_t>(std::round((static_cast<float>(ms) * 0.001F) * sample_rate_));
        if (tap > 0 && tap + 1 < room_ir_.size()) {
            const float shaping = 0.0060F * std::exp(-0.09F * static_cast<float>(ms - 11));
            room_ir_[tap - 1] += shaping * 0.18F;
            room_ir_[tap] += shaping * 0.64F;
            room_ir_[tap + 1] += shaping * 0.18F;
        }
    }

    room_mid_begin_ = 192;
    room_mid_end_ = 1152;
    room_late_begin_ = 1152;
    constexpr float kRoomTapThreshold = 8.0e-5F;
    const auto append_room_taps = [&](std::vector<ConvolutionTap>& taps, const std::size_t begin, const std::size_t end) {
        for (std::size_t i = begin; i < end; ++i) {
            if (std::abs(room_ir_[i]) > kRoomTapThreshold) {
                taps.push_back(ConvolutionTap{static_cast<std::uint16_t>(i), room_ir_[i]});
            }
        }
    };
    append_room_taps(room_early_taps_, 0, room_mid_begin_);
    append_room_taps(room_mid_taps_, room_mid_begin_, room_mid_end_);
    append_room_taps(room_late_taps_, room_late_begin_, room_ir_.size());
    room_mid_prev_ = 0.0F;
    room_mid_target_ = 0.0F;
    room_late_prev_ = 0.0F;
    room_late_target_ = 0.0F;
    room_mid_phase_ = 0;
    room_late_phase_ = 0;
    // Realtime-safe cadence: evaluate expensive mid/late fields at short strides
    // with interpolation to preserve smoothness while reducing CPU load.
    room_mid_stride_ = 2;
    room_late_stride_ = 8;

    dc_x_ = 0.0F;
    dc_y_ = 0.0F;
    tone_lp_ = 0.0F;
    post_prev_ = 0.0F;
    final_prev_ = 0.0F;
    body_drive_smoothed_ = 0.0F;
    mic_mix_smoothed_ = 0.35F;
    presence_smoothed_ = 0.58F;
    presence_focus_smoothed_ = 0.5F;
    bus_phase_correlation_ = 0.0F;
    room_alignment_smoothed_ = 1.0F;
}

void PianoPostProcessor::set_reverb_wet(const float reverb_wet) noexcept {
    wet_ = std::clamp(reverb_wet, 0.0F, 0.20F);
}

float PianoPostProcessor::process(
    const float input,
    const float body_drive,
    const float close_bus,
    const float room_bus,
    const float mic_mix,
    const float presence,
    const float presence_focus
) noexcept {
    if (body_history_.empty() || body_ir_.empty() || room_history_.empty() || room_ir_.empty()) {
        return input;
    }

    const float drive = std::clamp(body_drive, 0.0F, 1.0F);
    mic_mix_smoothed_ += 0.006F * (std::clamp(mic_mix, 0.0F, 1.0F) - mic_mix_smoothed_);
    presence_smoothed_ += 0.0042F * (std::clamp(presence, 0.0F, 1.0F) - presence_smoothed_);
    presence_focus_smoothed_ += 0.0050F * (std::clamp(presence_focus, 0.0F, 1.0F) - presence_focus_smoothed_);
    body_drive_smoothed_ += 0.0048F * (drive - body_drive_smoothed_);
    const float body_reactive = 1.0F + (0.34F * body_drive_smoothed_);
    const float wet_dynamic = std::clamp(wet_ * (1.0F + (0.20F * body_drive_smoothed_)), 0.0F, 0.24F);

    float direct_input = input;
    if ((std::abs(close_bus) + std::abs(room_bus)) > 1.0e-9F) {
        bus_phase_correlation_ = (0.997F * bus_phase_correlation_) + (0.003F * (close_bus * room_bus));
        // Only tame clearly destructive bus correlation. Keep most room energy intact
        // so the instrument does not collapse into a dim/narrow presentation.
        const float anti_phase = std::clamp((-bus_phase_correlation_ - 0.0020F) * 220.0F, 0.0F, 1.0F);
        const float target_alignment = 1.0F - (0.08F * anti_phase);
        room_alignment_smoothed_ += 0.0035F * (target_alignment - room_alignment_smoothed_);
        const float aligned_room = room_bus * room_alignment_smoothed_;
        const float perspective = std::pow(std::clamp(mic_mix_smoothed_, 0.0F, 1.0F), 1.35F);
        const float bus_mix = (close_bus * (1.0F - perspective)) + (aligned_room * perspective);
        direct_input = (bus_mix * 0.85F) + (input * 0.15F);
    }

    body_history_[body_history_index_] = direct_input;

    float body = 0.0F;
    const auto history_at_delay = [](const std::vector<float>& history, const std::size_t write_index, const std::uint16_t delay) {
        const std::size_t d = static_cast<std::size_t>(delay);
        return history[(write_index >= d) ? (write_index - d) : (history.size() + write_index - d)];
    };
    for (const ConvolutionTap& tap : body_taps_) {
        body += tap.gain * history_at_delay(body_history_, body_history_index_, tap.delay);
    }
    body_history_index_ = (body_history_index_ + 1) % body_history_.size();

    const float body_mixed = (direct_input * (0.90F - (0.05F * body_drive_smoothed_)))
        + (body * (0.22F + (0.18F * body_drive_smoothed_)) * body_reactive);

    room_history_[room_history_index_] = body_mixed;
    float room_early = 0.0F;
    for (const ConvolutionTap& tap : room_early_taps_) {
        room_early += tap.gain * history_at_delay(room_history_, room_history_index_, tap.delay);
    }

    if (room_mid_phase_ == 0) {
        float mid = 0.0F;
        for (const ConvolutionTap& tap : room_mid_taps_) {
            mid += tap.gain * history_at_delay(room_history_, room_history_index_, tap.delay);
        }
        room_mid_prev_ = room_mid_target_;
        room_mid_target_ = mid;
    }
    const float mid_fraction =
        static_cast<float>(room_mid_phase_ + 1U) / static_cast<float>(room_mid_stride_);
    const float room_mid = room_mid_prev_ + ((room_mid_target_ - room_mid_prev_) * mid_fraction);
    room_mid_phase_ += 1U;
    if (room_mid_phase_ >= room_mid_stride_) {
        room_mid_phase_ = 0U;
    }

    if (room_late_phase_ == 0) {
        float late = 0.0F;
        for (const ConvolutionTap& tap : room_late_taps_) {
            late += tap.gain * history_at_delay(room_history_, room_history_index_, tap.delay);
        }
        room_late_prev_ = room_late_target_;
        room_late_target_ = late;
    }
    const float late_fraction =
        static_cast<float>(room_late_phase_ + 1U) / static_cast<float>(room_late_stride_);
    const float room_late = room_late_prev_ + ((room_late_target_ - room_late_prev_) * late_fraction);
    room_late_phase_ += 1U;
    if (room_late_phase_ >= room_late_stride_) {
        room_late_phase_ = 0U;
    }

    const float room = room_early + room_mid + room_late;
    room_history_index_ = (room_history_index_ + 1) % room_history_.size();

    const float mic_room_feed = room_bus * (0.24F + (0.46F * mic_mix_smoothed_));
    const float wet_signal = (0.26F * body) + (0.56F * room) + (mic_room_feed * 0.14F);
    const float mixed = (body_mixed * (1.0F - wet_dynamic)) + (wet_signal * wet_dynamic)
        + (direct_input * (0.08F + (0.06F * (1.0F - mic_mix_smoothed_))));

    // Tone contour: keep presence while avoiding brittle HF edge.
    tone_lp_ = (0.9865F * tone_lp_) + (0.0135F * mixed);
    const float low = tone_lp_;
    const float high = mixed - low;
    const float presence_centered = (presence_smoothed_ - 0.5F) * 2.0F;
    const float focus = std::clamp(presence_focus_smoothed_, 0.0F, 1.0F);
    const float treble_bias = 0.65F + (0.70F * focus);
    const float low_mid_damping = 1.10F - (0.35F * focus);
    const float low_gain = std::clamp(
        (0.004F + (0.010F * body_drive_smoothed_)) - (0.018F * presence_centered * low_mid_damping),
        -0.020F,
        0.018F
    );
    const float high_gain = std::clamp(
        (0.020F - (0.003F * body_drive_smoothed_)) + (0.028F * presence_centered * treble_bias),
        -0.010F,
        0.075F
    );
    const float sparkle_drive = std::clamp((presence_smoothed_ - 0.50F) * 2.0F, 0.0F, 1.0F) * (0.32F + (0.34F * focus));
    const float sparkle = std::tanh(high * (1.08F + (1.45F * sparkle_drive))) - std::tanh(high * 1.08F);
    const float contoured = mixed + (low_gain * low) + (high_gain * high) + (sparkle * (0.036F * sparkle_drive));

    // Transparent peak guard, activated only near full-scale.
    float guarded = contoured * 0.985F;
    const float abs_value = std::abs(guarded);
    if (abs_value > 0.997F) {
        const float overshoot = abs_value - 0.997F;
        const float compressed = 0.997F + (overshoot * 0.92F);
        guarded = std::copysign(compressed, guarded);
    }

    // Guard only truly pathological discontinuities (for example from abrupt state transitions).
    // Keep musical transients intact to avoid adding static-like slew distortion.
    constexpr float kExtremeJump = 0.24F;
    float delta = guarded - post_prev_;
    const float abs_delta = std::abs(delta);
    if (abs_delta > kExtremeJump) {
        const float excess = abs_delta - kExtremeJump;
        const float softened = kExtremeJump + (excess * 0.10F);
        delta = std::copysign(std::min(softened, 0.32F), delta);
    }
    guarded = post_prev_ + delta;

    // Full-scale safety guard with minimal coloration.
    guarded = soft_limit(guarded);
    post_prev_ = guarded;

    // DC blocker to prevent subsonic drift from long tails.
    const float dc = guarded - dc_x_ + (0.997F * dc_y_);
    dc_x_ = guarded;
    dc_y_ = dc;
    if (std::abs(tone_lp_) < 1.0e-12F) {
        tone_lp_ = 0.0F;
    }
    if (std::abs(dc_x_) < 1.0e-12F) {
        dc_x_ = 0.0F;
    }
    if (std::abs(dc_y_) < 1.0e-12F) {
        dc_y_ = 0.0F;
    }
    if (std::abs(post_prev_) < 1.0e-12F) {
        post_prev_ = 0.0F;
    }

    const float prev_final = final_prev_;
    float final = soft_limit(dc * 0.985F);
    // 2x-style oversampled limiting stage to reduce nonlinear roughness on hard transients.
    const float os_mid = soft_limit(0.5F * (prev_final + final));
    const float os_end = soft_limit(final);
    final = 0.5F * (os_mid + os_end);
    const float final_delta = final - final_prev_;
    constexpr float kFinalMaxJump = 0.17F;
    if (std::abs(final_delta) > kFinalMaxJump) {
        final = final_prev_ + std::copysign(kFinalMaxJump, final_delta);
    }
    final_prev_ = final;
    if (std::abs(final_prev_) < 1.0e-12F) {
        final_prev_ = 0.0F;
    }

    return final;
}

}  // namespace vb
