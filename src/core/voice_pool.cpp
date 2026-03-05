#include "core/voice_pool.hpp"

#include <algorithm>
#include <cmath>

namespace vb {

namespace {

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

float note_to_keyboard_pan(const std::uint8_t note) noexcept {
    // Acoustic piano image: low register left, high register right.
    constexpr float kLow = 21.0F;   // A0
    constexpr float kHigh = 108.0F; // C8
    const float key_pos = std::clamp((static_cast<float>(note) - kLow) / (kHigh - kLow), 0.0F, 1.0F);
    return (2.0F * key_pos) - 1.0F;
}

void equal_power_pan(const float pan, float& gain_left, float& gain_right) noexcept {
    const float p = std::clamp(pan, -1.0F, 1.0F);
    const float theta = (p + 1.0F) * 0.25F * 3.14159265358979323846F;
    gain_left = std::cos(theta);
    gain_right = std::sin(theta);
}

float soft_ceiling(const float x) noexcept {
    const float ax = std::abs(x);
    if (ax <= 0.980F) {
        return x;
    }
    const float excess = ax - 0.980F;
    const float y = 0.980F + (0.010F * std::tanh(excess * 18.0F));
    return std::copysign(y, x);
}

}  // namespace

VoicePool::VoicePool(const std::size_t max_voices, const float sample_rate, const PianoRenderOptions& options)
    : voices_(std::max<std::size_t>(1, max_voices)), sample_rate_(sample_rate), options_(options) {
    sustained_notes_.fill(false);
    active_voice_slot_by_note_.fill(-1);
    last_velocity_by_note_.fill(96);
    active_voice_indices_.reserve(voices_.size());
    active_slot_flags_.assign(voices_.size(), static_cast<std::uint8_t>(0));
    slot_last_sample_.assign(voices_.size(), 0.0F);
    steal_tails_.fill(0.0F);
    release_prefetch_.fill(ReleasePrefetchEntry{});

    std::string sfz_path = options_.sfz_path;
    if (!sfz_path.empty()) {
        sample_library_.configure_streaming(options_.disk_streaming_enabled, options_.disk_stream_threshold_frames);
        (void)sample_library_.load_sfz(sfz_path);
        options_.sfz_path = sfz_path;
    }

    initialize_tuning_table();
}

void VoicePool::initialize_tuning_table() noexcept {
    update_stretch_tuning();
    rebuild_pan_tables();

    mechanics_.initialize(sample_rate_);
    post_processor_left_.initialize(sample_rate_, options_.reverb_wet);
    post_processor_right_.initialize(sample_rate_, options_.reverb_wet);
}

void VoicePool::rebuild_pan_tables() noexcept {
    for (std::size_t note = 0; note < note_norm_table_.size(); ++note) {
        const std::uint8_t midi_note = static_cast<std::uint8_t>(note);
        note_norm_table_[note] = std::clamp((static_cast<float>(midi_note) - 21.0F) / 87.0F, 0.0F, 1.0F);
        const float pan = note_to_keyboard_pan(midi_note);
        equal_power_pan(pan * 0.52F, close_pan_left_[note], close_pan_right_[note]);
        equal_power_pan(pan * 0.92F, room_pan_left_[note], room_pan_right_[note]);
    }
}

void VoicePool::update_stretch_tuning() noexcept {
    for (std::size_t note = 0; note < tuned_frequency_.size(); ++note) {
        const float eq = midi_note_to_frequency(static_cast<std::uint8_t>(note));
        const float delta = static_cast<float>(static_cast<int>(note) - 69);
        const float direction = (delta >= 0.0F) ? 1.0F : -1.0F;
        const float stretch_cents = options_.stretch_strength * direction * 0.006F * delta * delta;
        tuned_frequency_[note] = eq * cents_to_ratio(stretch_cents);
    }

    resonance_.initialize(sample_rate_, tuned_frequency_.data());
}

void VoicePool::note_on(
    const std::uint8_t note,
    const std::uint8_t velocity,
    const InstrumentType instrument,
    const float sample_rate
) noexcept {
    sustained_notes_[note] = false;

    if (sample_rate_ != sample_rate) {
        sample_rate_ = sample_rate;
        initialize_tuning_table();
    }

    std::uniform_real_distribution<float> vel_jitter(-options_.humanize_velocity, options_.humanize_velocity);
    const int vel = std::clamp(
        static_cast<int>(std::lround(static_cast<float>(velocity) + vel_jitter(rng_))),
        1,
        127
    );
    const std::uint8_t humanized_velocity = static_cast<std::uint8_t>(vel);

    const float max_delay = (options_.humanize_timing_ms / 1000.0F) * sample_rate_;
    std::uniform_int_distribution<std::uint32_t> time_jitter(0, static_cast<std::uint32_t>(std::max(0.0F, max_delay)));
    const std::uint32_t start_delay = time_jitter(rng_);

    last_velocity_by_note_[note] = humanized_velocity;

    const std::size_t slot_index = pick_slot_index();
    VoiceSlot& slot = voices_[slot_index];
    slot.type = instrument;

    if (instrument == InstrumentType::AcousticGrandPiano) {
        const std::uint8_t pedal_cc = static_cast<std::uint8_t>(std::clamp(std::lround(pedal_amount_ * 127.0F), 0L, 127L));
        PianoLayerSelection selection = sample_library_.select_layers(note, humanized_velocity, false, pedal_cc);
        if (selection.secondary != nullptr) {
            const float user_mix = std::clamp(options_.mic_mix, 0.0F, 1.0F);
            selection.secondary_mix = std::clamp((selection.secondary_mix * 0.72F) + (user_mix * 0.28F), 0.0F, 1.0F);
        }
        if (selection.primary != nullptr && selection.primary->off_mode_fast) {
            sustained_notes_[note] = false;
            const int mapped_slot = active_voice_slot_by_note_[note];
            if (mapped_slot >= 0 && static_cast<std::size_t>(mapped_slot) < voices_.size()) {
                release_voice(voices_[static_cast<std::size_t>(mapped_slot)], 0.0F);
            }
        }
        mechanics_.trigger_key_down(selection.primary, humanized_velocity);
        float repetition_duck = 1.0F;
        const int mapped_slot = active_voice_slot_by_note_[note];
        if (mapped_slot >= 0 && static_cast<std::size_t>(mapped_slot) < voices_.size()) {
            const VoiceSlot& prior = voices_[static_cast<std::size_t>(mapped_slot)];
            if (is_active(prior) && note_number(prior) == note) {
                const float retained_energy = std::clamp(voice_activity(prior), 0.0F, 1.0F);
                // Re-striking a still-vibrating string slightly softens hammer bite.
                repetition_duck = 1.0F - (0.20F * retained_energy);
            }
        }
        const float gain_jitter = (1.0F + (vel_jitter(rng_) * 0.015F)) * repetition_duck;
        const bool started = slot.piano.start(
            selection,
            note,
            humanized_velocity,
            sample_rate_,
            tuned_frequency_.data(),
            start_delay,
            gain_jitter,
            soft_pedal_amount_
        );
        if (!started) {
            slot.piano = SampledPianoVoice{};
            slot.piano_fallback.start(note, humanized_velocity, sample_rate_);
        } else {
            slot.piano_fallback = AcousticGrandVoice{};
        }
        resonance_.note_on(note, humanized_velocity);

        const PianoLayerSelection release_prefetch = sample_library_.select_layers(note, humanized_velocity, true, pedal_cc);
        release_prefetch_[note] = ReleasePrefetchEntry{
            .primary = release_prefetch.primary,
            .secondary = release_prefetch.secondary,
            .secondary_mix = release_prefetch.secondary_mix,
            .velocity = humanized_velocity,
        };
        if (release_prefetch.primary != nullptr) {
            volatile float warm = release_prefetch.primary->sample_at(static_cast<int>(release_prefetch.primary->sample_start));
            (void)warm;
        }
    } else {
        slot.guitar.start(note, humanized_velocity, sample_rate_);
    }

    active_voice_slot_by_note_[note] = static_cast<int>(slot_index);
    if (active_slot_flags_[slot_index] == 0) {
        active_voice_indices_.push_back(slot_index);
        active_slot_flags_[slot_index] = 1;
    }
}

void VoicePool::note_off(const std::uint8_t note) noexcept {
    if (pedal_should_hold(pedal_amount_)) {
        sustained_notes_[note] = true;
        const std::uint8_t release_velocity = static_cast<std::uint8_t>(std::min<int>(last_velocity_by_note_[note], 72));
        const PianoLayerSelection release_prefetch = sample_library_.select_layers(note, release_velocity, true, 0);
        release_prefetch_[note] = ReleasePrefetchEntry{
            .primary = release_prefetch.primary,
            .secondary = release_prefetch.secondary,
            .secondary_mix = release_prefetch.secondary_mix,
            .velocity = release_velocity,
        };
        return;
    }

    release_note(note);
    apply_damper_release(note, pedal_amount_);
    resonance_.trigger_damper_drop(1, pedal_amount_);
}

void VoicePool::control_change(const std::uint8_t control, const std::uint8_t value) noexcept {
    if (control == 77) {
        options_.humanize_timing_ms = 5.0F * std::clamp(static_cast<float>(value) / 127.0F, 0.0F, 1.0F);
        return;
    }
    if (control == 78) {
        options_.humanize_velocity = 6.0F * std::clamp(static_cast<float>(value) / 127.0F, 0.0F, 1.0F);
        return;
    }
    if (control == 79) {
        const float x = std::clamp(static_cast<float>(value) / 127.0F, 0.0F, 1.0F);
        options_.stretch_strength = std::clamp(0.75F + (0.75F * x), 0.75F, 1.50F);
        update_stretch_tuning();
        return;
    }
    if (control == 80) {
        options_.pedal_binary_threshold = static_cast<std::uint8_t>(std::clamp<int>(value, 1, 127));
        return;
    }
    if (control == 81) {
        if (value < 43) {
            options_.pedal_mode = PedalMode::Auto;
        } else if (value < 86) {
            options_.pedal_mode = PedalMode::Binary;
        } else {
            options_.pedal_mode = PedalMode::Continuous;
        }
        return;
    }
    if (control == 67) {
        soft_pedal_amount_ = std::clamp(static_cast<float>(value) / 127.0F, 0.0F, 1.0F);
        return;
    }
    if (control == 71) {
        options_.reverb_wet = std::clamp(static_cast<float>(value) / 127.0F, 0.0F, 1.0F) * 0.20F;
        post_processor_left_.set_reverb_wet(options_.reverb_wet);
        post_processor_right_.set_reverb_wet(options_.reverb_wet);
        return;
    }
    if (control == 73) {
        options_.mic_mix = std::clamp(static_cast<float>(value) / 127.0F, 0.0F, 1.0F);
        return;
    }
    if (control == 74) {
        options_.presence = std::clamp(static_cast<float>(value) / 127.0F, 0.0F, 1.0F);
        return;
    }
    if (control == 75) {
        const float x = std::clamp(static_cast<float>(value) / 127.0F, 0.0F, 1.0F);
        options_.image_width = std::clamp(0.70F + (1.30F * x), 0.70F, 2.00F);
        return;
    }
    if (control == 76) {
        const float x = std::clamp(static_cast<float>(value) / 127.0F, 0.0F, 1.0F);
        options_.output_gain = std::clamp(0.70F + (1.30F * x), 0.70F, 2.00F);
        return;
    }
    if (control != 64) {
        return;
    }

    if (options_.pedal_mode == PedalMode::Auto && value > 0 && value < 127) {
        pedal_auto_mid_min_ = std::min(pedal_auto_mid_min_, value);
        pedal_auto_mid_max_ = std::max(pedal_auto_mid_max_, value);
        if (static_cast<int>(pedal_auto_mid_max_) - static_cast<int>(pedal_auto_mid_min_) >= 8) {
            pedal_auto_continuous_ = true;
        }
    }

    const bool sustain_before = sustain_enabled_;
    const std::uint8_t previous = previous_pedal_cc_;
    previous_pedal_cc_ = value;
    pedal_amount_ = std::clamp(static_cast<float>(value) / 127.0F, 0.0F, 1.0F);
    sustain_enabled_ = pedal_should_hold(pedal_amount_);
    resonance_.set_pedal(pedal_amount_);

    if (options_.pedal_noise_enabled && value != previous) {
        const bool sustain_edge_down = !sustain_before && sustain_enabled_;
        const bool sustain_edge_up = sustain_before && !sustain_enabled_;
        const int delta_cc = std::abs(static_cast<int>(value) - static_cast<int>(previous));
        // Prevent chatter from dense CC64 streams: trigger only for real pedal moves or hold-state edges.
        if (sustain_edge_down || sustain_edge_up || delta_cc >= 10) {
            const bool pedal_down = (value > previous) || sustain_edge_down;
            const float intensity = std::clamp(
                static_cast<float>(delta_cc) / 127.0F,
                0.0F,
                1.0F
            );
            const std::uint32_t seed = xorshift32(mechanics_seed_);
            const PianoSampleRegion* pedal_region = sample_library_.select_pedal_sample(pedal_down, seed);
            mechanics_.trigger_pedal_whoosh(pedal_region, pedal_down, intensity);
        }
    }

    if (sustain_before && !sustain_enabled_) {
        std::uint8_t released_notes = 0;
        const float pedal_before_release = std::clamp(static_cast<float>(previous) / 127.0F, 0.0F, 1.0F);
        for (std::size_t note = 0; note < sustained_notes_.size(); ++note) {
            if (sustained_notes_[note]) {
                release_note(static_cast<std::uint8_t>(note));
                apply_damper_release(static_cast<std::uint8_t>(note), 0.0F);
                sustained_notes_[note] = false;
                released_notes = static_cast<std::uint8_t>(std::min<int>(released_notes + 1, 127));
            }
        }
        if (released_notes > 0) {
            resonance_.trigger_damper_drop(released_notes, pedal_before_release);
        }
    }
}

StereoFrame VoicePool::render_stereo() noexcept {
    float sum_l = 0.0F;
    float sum_r = 0.0F;
    float close_l = 0.0F;
    float close_r = 0.0F;
    float room_l = 0.0F;
    float room_r = 0.0F;
    float register_weight_sum = 0.0F;
    float register_focus_sum = 0.0F;

    std::size_t i = 0;
    while (i < active_voice_indices_.size()) {
        const std::size_t slot_index = active_voice_indices_[i];
        VoiceSlot& slot = voices_[slot_index];
        float voice_sample = 0.0F;
        float voice_close = 0.0F;
        float voice_room = 0.0F;
        float voice_direct_l = 0.0F;
        float voice_direct_r = 0.0F;

        if (slot.type == InstrumentType::AcousticGrandPiano) {
            voice_sample = slot.piano.render_sample(&voice_close, &voice_room, &voice_direct_l, &voice_direct_r);
            const float fallback = slot.piano_fallback.render_sample();
            voice_sample += fallback;
            const float fallback_center = fallback * 0.70710678F;
            voice_direct_l += fallback_center;
            voice_direct_r += fallback_center;
            voice_close += fallback;
        } else {
            voice_sample = slot.guitar.render_sample();
            voice_close = voice_sample;
        }

        if (!std::isfinite(voice_sample)) {
            voice_sample = 0.0F;
        }
        if (!std::isfinite(voice_close)) {
            voice_close = 0.0F;
        }
        if (!std::isfinite(voice_room)) {
            voice_room = 0.0F;
        }

        const std::uint8_t note = note_number(slot);
        const float note_norm = note_norm_table_[note];
        const float activity_estimate = std::clamp(std::abs(voice_sample) * 3.0F, 0.0F, 1.0F);
        register_weight_sum += activity_estimate;
        register_focus_sum += activity_estimate * (note_norm * note_norm);
        const float close_gl = close_pan_left_[note];
        const float close_gr = close_pan_right_[note];
        const float room_gl = room_pan_left_[note];
        const float room_gr = room_pan_right_[note];

        const float bus_l = (voice_close * close_gl) + (voice_room * room_gl);
        const float bus_r = (voice_close * close_gr) + (voice_room * room_gr);
        float voice_l = bus_l;
        float voice_r = bus_r;
        if (slot.type == InstrumentType::AcousticGrandPiano) {
            // Keep a stable wide image: blend true stereo sample content with the close/room bus.
            constexpr float kDirectStereoWeight = 0.64F;
            constexpr float kBusBlendWeight = 0.36F;
            const float direct_stereo_width = 1.28F * std::clamp(options_.image_width, 0.70F, 2.00F);
            const float direct_mid = 0.5F * (voice_direct_l + voice_direct_r);
            const float direct_side = 0.5F * (voice_direct_l - voice_direct_r) * direct_stereo_width;
            const float direct_wide_l = direct_mid + direct_side;
            const float direct_wide_r = direct_mid - direct_side;
            voice_l = (direct_wide_l * kDirectStereoWeight) + (bus_l * kBusBlendWeight);
            voice_r = (direct_wide_r * kDirectStereoWeight) + (bus_r * kBusBlendWeight);
        }
        const float mono_residual = (slot.type == InstrumentType::AcousticGrandPiano)
            ? 0.0F
            : (voice_sample - (0.5F * (voice_close + voice_room)));
        sum_l += voice_l + (mono_residual * 0.50F);
        sum_r += voice_r + (mono_residual * 0.50F);
        close_l += voice_close * close_gl;
        close_r += voice_close * close_gr;
        room_l += voice_room * room_gl;
        room_r += voice_room * room_gr;
        slot_last_sample_[slot_index] = voice_sample;

        if (!is_active(slot)) {
            if (active_voice_slot_by_note_[note] == static_cast<int>(slot_index)) {
                active_voice_slot_by_note_[note] = -1;
            }
            active_slot_flags_[slot_index] = 0;
            slot_last_sample_[slot_index] = 0.0F;
            active_voice_indices_[i] = active_voice_indices_.back();
            active_voice_indices_.pop_back();
            continue;
        }

        ++i;
    }

    for (float& tail : steal_tails_) {
        const float center = tail * 0.70710678F;
        sum_l += center;
        sum_r += center;
        close_l += center * 0.86F;
        close_r += center * 0.86F;
        room_l += center * 0.14F;
        room_r += center * 0.14F;
        tail *= 0.965F;
        if (std::abs(tail) < 1.0e-6F) {
            tail = 0.0F;
        }
    }

    float resonance_sample = resonance_.render_sample();
    float mechanics_sample = mechanics_.render_sample();
    if (!std::isfinite(resonance_sample)) {
        resonance_sample = 0.0F;
    }
    if (!std::isfinite(mechanics_sample)) {
        mechanics_sample = 0.0F;
    }
    const float resonance_center = resonance_sample * 0.70710678F;
    const float mechanics_center = mechanics_sample * 0.70710678F;
    sum_l += resonance_center + mechanics_center;
    sum_r += resonance_center + mechanics_center;
    close_l += (resonance_center * 0.22F) + (mechanics_center * 0.95F);
    close_r += (resonance_center * 0.22F) + (mechanics_center * 0.95F);
    room_l += resonance_center * 0.78F;
    room_r += resonance_center * 0.78F;
    resonance_activity_ += 0.006F * ((std::abs(resonance_sample) * 16.0F) - resonance_activity_);
    resonance_activity_ = std::clamp(resonance_activity_, 0.0F, 1.0F);
    const float register_focus_target = (register_weight_sum > 1.0e-6F)
        ? (register_focus_sum / register_weight_sum)
        : 0.5F;
    presence_focus_smoothed_ += 0.030F * (register_focus_target - presence_focus_smoothed_);
    presence_focus_smoothed_ = std::clamp(presence_focus_smoothed_, 0.0F, 1.0F);

    const std::size_t active_voices = active_voice_indices_.size();
    const float polyphony_trim = 1.0F / (1.0F + (0.022F * static_cast<float>(active_voices > 0 ? active_voices - 1 : 0)));
    constexpr float kMasterOutputTrim = 0.86F;
    const float master_gain = kMasterOutputTrim * std::clamp(options_.output_gain, 0.70F, 2.00F);
    const float staged_l = sum_l * polyphony_trim * master_gain;
    const float staged_r = sum_r * polyphony_trim * master_gain;
    const float close_staged_l = close_l * polyphony_trim * master_gain;
    const float close_staged_r = close_r * polyphony_trim * master_gain;
    const float room_staged_l = room_l * polyphony_trim * master_gain;
    const float room_staged_r = room_r * polyphony_trim * master_gain;

    float out_l = post_processor_left_.process(
        staged_l,
        resonance_activity_,
        close_staged_l,
        room_staged_l,
        options_.mic_mix,
        options_.presence,
        presence_focus_smoothed_
    );
    float out_r = post_processor_right_.process(
        staged_r,
        resonance_activity_,
        close_staged_r,
        room_staged_r,
        options_.mic_mix,
        options_.presence,
        presence_focus_smoothed_
    );
    if (!std::isfinite(out_l)) {
        out_l = 0.0F;
    }
    if (!std::isfinite(out_r)) {
        out_r = 0.0F;
    }

    // Dynamic side-energy bloom: increase spatial air when pedal and resonance are active.
    const float bloom_drive = std::clamp(
        (0.48F * pedal_amount_) + (0.42F * resonance_activity_) + (0.30F * presence_focus_smoothed_),
        0.0F,
        1.0F
    );
    float mid = 0.5F * (out_l + out_r);
    float side = 0.5F * (out_l - out_r) * (1.0F + (0.13F * bloom_drive));
    const float side_abs = std::abs(side);
    if (side_abs > 0.85F) {
        side = std::copysign(0.85F + (0.15F * std::tanh((side_abs - 0.85F) * 6.0F)), side);
    }

    return StereoFrame{
        .left = soft_ceiling(mid + side),
        .right = soft_ceiling(mid - side),
    };
}

float VoicePool::render_sample() noexcept {
    const StereoFrame frame = render_stereo();
    return 0.5F * (frame.left + frame.right);
}

bool VoicePool::is_active(const VoiceSlot& slot) const noexcept {
    if (slot.type == InstrumentType::AcousticGrandPiano) {
        return slot.piano.is_active() || slot.piano_fallback.is_active();
    }

    return slot.guitar.is_active();
}

std::uint8_t VoicePool::note_number(const VoiceSlot& slot) const noexcept {
    if (slot.type == InstrumentType::AcousticGrandPiano) {
        if (slot.piano.is_active()) {
            return slot.piano.note();
        }
        return slot.piano_fallback.note();
    }

    return slot.guitar.note();
}

float VoicePool::voice_activity(const VoiceSlot& slot) const noexcept {
    if (slot.type == InstrumentType::AcousticGrandPiano) {
        return std::max(slot.piano.activity_level(), slot.piano_fallback.activity_level());
    }

    return slot.guitar.activity_level();
}

void VoicePool::release_voice(VoiceSlot& slot, const float pedal_amount) noexcept {
    if (slot.type == InstrumentType::AcousticGrandPiano) {
        slot.piano.release(pedal_amount);
        slot.piano_fallback.release(pedal_amount);
        return;
    }

    slot.guitar.release();
}

void VoicePool::release_note(const std::uint8_t note) noexcept {
    const int mapped_slot = active_voice_slot_by_note_[note];
    if (mapped_slot >= 0 && static_cast<std::size_t>(mapped_slot) < voices_.size()) {
        VoiceSlot& slot = voices_[static_cast<std::size_t>(mapped_slot)];
        if (is_active(slot) && note_number(slot) == note) {
            release_voice(slot, pedal_amount_);
        }
    }
}

void VoicePool::apply_damper_release(const std::uint8_t note, const float pedal_amount_for_release) noexcept {
    const std::uint8_t release_velocity = static_cast<std::uint8_t>(std::min<int>(last_velocity_by_note_[note], 72));
    const std::uint8_t pedal_cc = static_cast<std::uint8_t>(
        std::clamp(std::lround(std::clamp(pedal_amount_for_release, 0.0F, 1.0F) * 127.0F), 0L, 127L)
    );
    const PianoLayerSelection release_layers = sample_library_.select_layers(note, release_velocity, true, pedal_cc);
    mechanics_.trigger_damper_fall(release_layers.primary, release_velocity, pedal_amount_for_release);
    spawn_release_sample(note);
}

void VoicePool::spawn_release_sample(const std::uint8_t note) noexcept {
    if (sample_library_.empty()) {
        return;
    }

    const ReleasePrefetchEntry prefetched = release_prefetch_[note];
    const std::uint8_t release_velocity = static_cast<std::uint8_t>(std::min<int>(last_velocity_by_note_[note], 72));
    PianoLayerSelection release_layers{};
    if (prefetched.primary != nullptr) {
        release_layers = PianoLayerSelection{
            prefetched.primary,
            prefetched.secondary,
            std::clamp(prefetched.secondary_mix, 0.0F, 1.0F),
        };
    } else {
        const std::uint8_t pedal_cc = static_cast<std::uint8_t>(std::clamp(std::lround(pedal_amount_ * 127.0F), 0L, 127L));
        release_layers = sample_library_.select_layers(note, release_velocity, true, pedal_cc);
    }
    release_prefetch_[note] = ReleasePrefetchEntry{};

    if (release_layers.primary == nullptr) {
        return;
    }

    const std::size_t slot_index = pick_slot_index();
    VoiceSlot& slot = voices_[slot_index];
    slot.type = InstrumentType::AcousticGrandPiano;

    const bool started = slot.piano.start(
        release_layers,
        note,
        release_velocity,
        sample_rate_,
        tuned_frequency_.data(),
        0,
        0.38F,
        soft_pedal_amount_
    );
    if (!started) {
        return;
    }
    slot.piano.release(0.0F);

    slot.piano_fallback = AcousticGrandVoice{};

    if (active_slot_flags_[slot_index] == 0) {
        active_voice_indices_.push_back(slot_index);
        active_slot_flags_[slot_index] = 1;
    }
}

std::size_t VoicePool::pick_slot_index() noexcept {
    for (std::size_t i = 0; i < voices_.size(); ++i) {
        if (!is_active(voices_[i])) {
            return i;
        }
    }

    std::size_t quietest_index = round_robin_index_;
    float quietest_activity = voice_activity(voices_[quietest_index]);
    for (std::size_t i = 0; i < voices_.size(); ++i) {
        const float activity = voice_activity(voices_[i]);
        if (activity < quietest_activity) {
            quietest_activity = activity;
            quietest_index = i;
        }
    }

    VoiceSlot& stolen = voices_[quietest_index];
    const float stolen_activity = std::max(0.0F, voice_activity(stolen));
    ++voice_steal_count_;
    worst_stolen_activity_ = std::max(worst_stolen_activity_, stolen_activity);
    add_steal_tail(slot_last_sample_[quietest_index]);

    const std::uint8_t stolen_note = note_number(stolen);
    if (active_voice_slot_by_note_[stolen_note] == static_cast<int>(quietest_index)) {
        active_voice_slot_by_note_[stolen_note] = -1;
    }
    slot_last_sample_[quietest_index] = 0.0F;
    round_robin_index_ = (quietest_index + 1) % voices_.size();
    return quietest_index;
}

void VoicePool::add_steal_tail(const float value) noexcept {
    if (!std::isfinite(value) || std::abs(value) < 1.0e-6F) {
        return;
    }

    std::size_t best = 0;
    float smallest = std::abs(steal_tails_[0]);
    for (std::size_t i = 0; i < steal_tails_.size(); ++i) {
        if (steal_tails_[i] == 0.0F) {
            steal_tails_[i] = value;
            return;
        }
        const float mag = std::abs(steal_tails_[i]);
        if (mag < smallest) {
            smallest = mag;
            best = i;
        }
    }
    steal_tails_[best] = value;
}

VoicePoolDiagnostics VoicePool::diagnostics() const noexcept {
    return VoicePoolDiagnostics{
        .voice_steals = voice_steal_count_,
        .worst_stolen_activity = worst_stolen_activity_,
    };
}

void VoicePool::reset_diagnostics() noexcept {
    voice_steal_count_ = 0;
    worst_stolen_activity_ = 0.0F;
}

PedalMode VoicePool::resolved_pedal_mode() const noexcept {
    if (options_.pedal_mode == PedalMode::Auto) {
        return pedal_auto_continuous_ ? PedalMode::Continuous : PedalMode::Binary;
    }
    return options_.pedal_mode;
}

bool VoicePool::pedal_should_hold(const float pedal_amount) const noexcept {
    const PedalMode mode = resolved_pedal_mode();
    if (mode == PedalMode::Continuous) {
        // Half-damper controllers tend to jitter near thresholds; hysteresis avoids chatter.
        const float hold_on = 0.90F;
        const float hold_off = 0.82F;
        return sustain_enabled_ ? (pedal_amount >= hold_off) : (pedal_amount >= hold_on);
    }
    const float threshold = std::clamp(static_cast<float>(options_.pedal_binary_threshold) / 127.0F, 0.0F, 1.0F);
    constexpr float kBinaryHysteresis = 3.0F / 127.0F;
    const float hold_on = threshold;
    const float hold_off = std::max(0.0F, threshold - kBinaryHysteresis);
    return sustain_enabled_ ? (pedal_amount >= hold_off) : (pedal_amount >= hold_on);
}
}  // namespace vb
