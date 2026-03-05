#include "instruments/piano/resonance_matrix.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace vb {

namespace {
constexpr float kPi = 3.14159265358979323846F;
constexpr float kTwoPi = 2.0F * kPi;

std::size_t key_to_index(const std::uint8_t midi_note) {
    const int index = static_cast<int>(midi_note) - 21;
    return static_cast<std::size_t>(std::clamp(index, 0, 87));
}

float interval_coupling(const int semitones) {
    const int abs_semitones = std::abs(semitones);
    if (abs_semitones == 0) {
        return 0.20F;
    }

    const int mod = abs_semitones % 12;
    const float octave_scale = 1.0F / (1.0F + static_cast<float>(abs_semitones / 12));

    if (mod == 0) {
        return 0.095F * octave_scale;
    }
    if (mod == 7) {
        return 0.073F * octave_scale;
    }
    if (mod == 5) {
        return 0.054F * octave_scale;
    }
    if (mod == 4 || mod == 3) {
        return 0.038F * octave_scale;
    }
    if (abs_semitones <= 2) {
        return 0.030F * octave_scale;
    }
    return 0.011F * octave_scale;
}

float estimate_inharmonicity(const int midi_note) {
    // Bass strings exhibit greater inharmonicity due to stiffness and speaking length.
    const float x = static_cast<float>(std::clamp(midi_note, 21, 108) - 21) / 87.0F;
    const float bass = 0.0032F * std::pow(1.0F - x, 2.2F);
    const float treble = 0.00008F * std::pow(x, 1.3F);
    return std::clamp(bass + treble, 0.00002F, 0.0065F);
}

}  // namespace

void PianoResonanceMatrix::initialize(const float sample_rate, const float* tuned_frequencies) noexcept {
    sample_rate_ = std::max(1.0F, sample_rate);
    delay_lines_.fill(0.0F);
    delay_lengths_.fill(96);
    write_indices_.fill(0);
    delay_fracs_.fill(0.0F);
    energies_.fill(0.0F);
    pending_energies_.fill(0.0F);
    lp_states_.fill(0.0F);
    prev_delayed_.fill(0.0F);
    fundamentals_.fill(0.0F);
    duplex_phases_.fill(0.0F);
    duplex_increments_.fill(0.0F);
    inharmonicity_.fill(0.0F);
    coupling_.fill(0.0F);
    bridge_coupling_.fill(0.0F);
    register_skew_.fill(0.0F);
    board_lines_.fill(0.0F);
    board_indices_.fill(0);
    board_modes_.fill(0.0F);
    board_mode_velocity_.fill(0.0F);
    board_mode_damping_ = {0.9956F, 0.9969F, 0.9978F};
    board_band_energy_.fill(0.0F);
    board_lf_ = 0.0F;
    board_hf_ = 0.0F;
    damper_drop_envelope_ = 0.0F;
    pedal_amount_ = 0.0F;
    pedal_target_ = 0.0F;
    excitation_alpha_ = 1.0F - std::exp(-1.0F / std::max(1.0F, sample_rate_ * 0.0025F));

    constexpr std::array<std::uint16_t, kBoardLineCount> kBoardLengths{151, 223, 313, 443};
    board_lengths_ = kBoardLengths;

    for (std::size_t i = 0; i < kKeyCount; ++i) {
        const std::size_t midi_note = i + 21;
        const float frequency = tuned_frequencies != nullptr
            ? tuned_frequencies[midi_note]
            : 440.0F * std::pow(2.0F, (static_cast<float>(midi_note) - 69.0F) / 12.0F);
        fundamentals_[i] = frequency;

        const float inharm = estimate_inharmonicity(static_cast<int>(midi_note));
        inharmonicity_[i] = inharm;

        const float delay_samples = sample_rate_ / std::max(20.0F, frequency);
        const float stiffness_warp = 1.0F + (inharm * 42.0F);
        const float effective_delay = std::clamp(delay_samples / stiffness_warp, 24.0F, static_cast<float>(kMaxDelay - 4));
        const std::uint16_t whole = static_cast<std::uint16_t>(std::floor(effective_delay));
        delay_lengths_[i] = std::max<std::uint16_t>(24, whole);
        delay_fracs_[i] = std::clamp(effective_delay - static_cast<float>(whole), 0.0F, 0.999F);
        const float duplex_frequency = std::clamp(
            (frequency * 2.63F * std::sqrt(1.0F + (inharm * 28.0F))),
            1200.0F,
            sample_rate_ * 0.45F
        );
        duplex_increments_[i] = (kTwoPi * duplex_frequency) / sample_rate_;
        duplex_phases_[i] = static_cast<float>(i) * 0.173F;

        const float note_norm = std::clamp(static_cast<float>(i) / 87.0F, 0.0F, 1.0F);
        register_skew_[i] = note_norm;
        const float bass_coupling = std::clamp(1.25F - (1.62F * note_norm), 0.0F, 1.0F);
        const float treble_coupling = std::clamp((note_norm - 0.36F) * 1.54F, 0.0F, 1.0F);
        const float mid_coupling = std::clamp(1.0F - bass_coupling - treble_coupling, 0.10F, 1.0F);
        bridge_coupling_[(i * 3) + 0] = 0.0011F * bass_coupling;
        bridge_coupling_[(i * 3) + 1] = 0.0010F * mid_coupling;
        bridge_coupling_[(i * 3) + 2] = 0.00085F * treble_coupling;
    }

    for (std::size_t row = 0; row < kKeyCount; ++row) {
        for (std::size_t col = 0; col < kKeyCount; ++col) {
            const int semitones = static_cast<int>(row) - static_cast<int>(col);
            coupling_[row * kKeyCount + col] = interval_coupling(semitones);
        }
    }

    initialized_ = true;
}

void PianoResonanceMatrix::set_pedal(const float amount) noexcept {
    pedal_target_ = std::clamp(amount, 0.0F, 1.0F);
}

void PianoResonanceMatrix::note_on(const std::uint8_t note, const std::uint8_t velocity) noexcept {
    if (!initialized_) {
        return;
    }

    const std::size_t index = key_to_index(note);
    const float velocity_norm = std::clamp(static_cast<float>(velocity) / 127.0F, 0.0F, 1.0F);
    pending_energies_[index] += 0.0095F * std::pow(velocity_norm, 1.10F);
    pending_energies_[index] = std::min(pending_energies_[index], 0.08F);

    const float spread = (0.0011F + (pedal_amount_ * 0.0023F)) * velocity_norm;
    for (std::size_t i = 0; i < kKeyCount; ++i) {
        if (i == index) {
            continue;
        }
        pending_energies_[i] += coupling_[index * kKeyCount + i] * spread;
        pending_energies_[i] = std::min(pending_energies_[i], 0.055F);
    }

    // Hammer-to-board excitation for low-order body modes.
    const float note_norm = register_skew_[index];
    const float hammer_energy = velocity_norm * velocity_norm;
    const float mid_weight = std::clamp(1.0F - std::abs((note_norm - 0.52F) * 1.9F), 0.0F, 1.0F);
    board_modes_[0] += hammer_energy * (0.0028F + (0.0035F * (1.0F - note_norm)));
    board_modes_[1] += hammer_energy * (0.0021F + (0.0031F * mid_weight));
    board_modes_[2] += hammer_energy * (0.0017F + (0.0027F * note_norm));

    // Spectral sympathetic excitation: inject energy into strings aligned with played-note harmonics.
    // Compensate with simple inharmonic warping to better match real stretched partials.
    const float fundamental = std::max(10.0F, fundamentals_[index]);
    for (std::size_t i = 0; i < kKeyCount; ++i) {
        if (i == index) {
            continue;
        }
        const float target = std::max(10.0F, fundamentals_[i]);
        const float ratio = target / fundamental;
        const float nearest = std::round(ratio);
        if (nearest < 1.0F || nearest > 8.0F) {
            continue;
        }
        const float inharmonic_warp = std::sqrt(1.0F + (inharmonicity_[index] * nearest * nearest));
        const float warped_ratio = ratio / std::max(0.35F, inharmonic_warp);
        const float detune = std::abs(warped_ratio - nearest);
        const float harmonic_focus = std::max(0.0F, 1.0F - (detune * 11.5F));
        if (harmonic_focus <= 0.0F) {
            continue;
        }
        const float register_weight = 0.85F + (0.30F * std::sqrt(register_skew_[i]));
        const float spectral_inject =
            harmonic_focus * velocity_norm * register_weight * (0.00030F + (pedal_amount_ * 0.0010F));
        pending_energies_[i] += spectral_inject;
        pending_energies_[i] = std::min(pending_energies_[i], 0.055F);
    }
}

void PianoResonanceMatrix::trigger_damper_drop(const std::uint8_t released_notes, const float pedal_before_release) noexcept {
    if (!initialized_) {
        return;
    }
    const float released_norm = std::clamp(static_cast<float>(released_notes) / 20.0F, 0.0F, 1.0F);
    const float pedal = std::clamp(pedal_before_release, 0.0F, 1.0F);
    const float impulse = std::clamp(0.16F + (0.38F * pedal) + (0.30F * released_norm), 0.0F, 1.0F);
    damper_drop_envelope_ = std::max(damper_drop_envelope_, impulse);
}

float PianoResonanceMatrix::render_sample() noexcept {
    if (!initialized_) {
        return 0.0F;
    }
    // Smooth pedal transitions to prevent edge clicks from abrupt feedback changes.
    pedal_amount_ += 0.0035F * (pedal_target_ - pedal_amount_);
    pedal_amount_ = std::clamp(pedal_amount_, 0.0F, 1.0F);
    damper_drop_envelope_ *= 0.9995F;
    if (damper_drop_envelope_ < 1.0e-6F) {
        damper_drop_envelope_ = 0.0F;
    }
    const float damper_drop = std::clamp(damper_drop_envelope_, 0.0F, 1.0F);

    constexpr std::uint16_t kDelayMask = static_cast<std::uint16_t>(kMaxDelay - 1);
    float strings_sum = 0.0F;
    float board_drive = 0.0F;
    std::array<float, 3> mode_drive{};

    for (std::size_t i = 0; i < kKeyCount; ++i) {
        const std::size_t base = i * kMaxDelay;
        const std::uint16_t write = write_indices_[i];
        const std::uint16_t delay = delay_lengths_[i];
        const std::uint16_t read0 = static_cast<std::uint16_t>((write + kMaxDelay - delay) & kDelayMask);
        const std::uint16_t read1 = static_cast<std::uint16_t>((read0 + kMaxDelay - 1) & kDelayMask);

        float d0 = delay_lines_[base + read0];
        float d1 = delay_lines_[base + read1];
        if (!std::isfinite(d0)) {
            d0 = 0.0F;
            delay_lines_[base + read0] = 0.0F;
        }
        if (!std::isfinite(d1)) {
            d1 = 0.0F;
            delay_lines_[base + read1] = 0.0F;
        }
        float delayed = d0 + ((d1 - d0) * delay_fracs_[i]);
        if (!std::isfinite(delayed)) {
            delayed = 0.0F;
        }

        float injected = 0.0F;
        if (pending_energies_[i] > 0.0F) {
            injected = pending_energies_[i] * excitation_alpha_;
            pending_energies_[i] -= injected;
            pending_energies_[i] *= (1.0F - (0.020F * damper_drop));
            if (pending_energies_[i] < 1.0e-9F) {
                pending_energies_[i] = 0.0F;
            }
        }

        const float freq_norm = std::clamp(static_cast<float>(i) / 87.0F, 0.0F, 1.0F);
        float warped = delayed + ((delayed - prev_delayed_[i]) * (inharmonicity_[i] * 6.8F));
        if (!std::isfinite(warped)) {
            warped = 0.0F;
        }
        prev_delayed_[i] = delayed;

        const float lp_blend = 0.070F + (freq_norm * 0.16F);
        lp_states_[i] += lp_blend * (warped - lp_states_[i]);
        if (!std::isfinite(lp_states_[i])) {
            lp_states_[i] = 0.0F;
        }

        const float loss_pedal_up = 0.9960F - (freq_norm * 0.0016F);
        const float loss_pedal_down = 0.99946F - (freq_norm * 0.00038F);
        const float feedback =
            (loss_pedal_up + ((loss_pedal_down - loss_pedal_up) * pedal_amount_)) * (1.0F - (0.24F * damper_drop));
        const float bridge_l = bridge_coupling_[(i * 3) + 0];
        const float bridge_m = bridge_coupling_[(i * 3) + 1];
        const float bridge_h = bridge_coupling_[(i * 3) + 2];
        const float board_coupling = (
            (board_lf_ * 0.00072F)
            + (board_hf_ * 0.00056F)
            + (board_modes_[0] * bridge_l)
            + (board_modes_[1] * bridge_m)
            + (board_modes_[2] * bridge_h)
        ) * (0.34F + (0.62F * pedal_amount_));

        float write_sample = (lp_states_[i] * feedback) + injected + board_coupling;
        if (!std::isfinite(write_sample)) {
            write_sample = 0.0F;
        }
        write_sample = std::tanh(write_sample * 1.12F) * 0.93F;
        delay_lines_[base + write] = write_sample;
        write_indices_[i] = static_cast<std::uint16_t>((write + 1) & kDelayMask);

        energies_[i] = (energies_[i] * 0.9975F) + (std::abs(write_sample) * 0.18F) + (std::abs(injected) * 0.82F);
        energies_[i] = std::min(energies_[i], 2.0F);
        if (energies_[i] < 1.0e-9F) {
            energies_[i] = 0.0F;
        }

        const float voiced = lp_states_[i] * (0.18F + ((1.0F - freq_norm) * 0.13F));
        strings_sum += voiced;
        board_drive += voiced * (0.24F + (0.27F * pedal_amount_));
        const float bass_w = std::clamp(1.25F - (1.60F * freq_norm), 0.0F, 1.0F);
        const float treble_w = std::clamp((freq_norm - 0.36F) * 1.65F, 0.0F, 1.0F);
        const float mid_w = std::clamp(1.0F - bass_w - treble_w, 0.08F, 1.0F);
        mode_drive[0] += voiced * bass_w * (0.56F + (0.44F * pedal_amount_));
        mode_drive[1] += voiced * mid_w * (0.52F + (0.38F * pedal_amount_));
        mode_drive[2] += voiced * treble_w * (0.49F + (0.47F * pedal_amount_));

        // Duplex/aliquot-style shimmer in upper register while pedal is open.
        if (i >= 51 && pedal_amount_ > 0.12F) {  // MIDI 72+
            duplex_phases_[i] += duplex_increments_[i];
            if (duplex_phases_[i] >= kTwoPi) {
                duplex_phases_[i] -= kTwoPi;
            }
            const float duplex_env = energies_[i] * (0.04F + (0.14F * pedal_amount_));
            const float duplex = std::sin(duplex_phases_[i] + (inharmonicity_[i] * 280.0F)) * duplex_env;
            strings_sum += duplex * 0.020F;
            board_drive += duplex * 0.054F;
            mode_drive[2] += duplex * (0.026F + (0.034F * pedal_amount_));
        }
    }

    for (std::size_t band = 0; band < board_modes_.size(); ++band) {
        const float damping = std::clamp(
            board_mode_damping_[band] + (pedal_amount_ * (0.00016F + (0.00005F * static_cast<float>(band)))),
            0.990F,
            0.99975F
        );
        const float previous = board_modes_[band];
        board_modes_[band] = (board_modes_[band] * damping) + (mode_drive[band] * 0.044F);
        const float slew = board_modes_[band] - previous;
        board_mode_velocity_[band] = (board_mode_velocity_[band] * 0.93F) + (slew * 0.07F);
        board_modes_[band] += board_mode_velocity_[band] * 0.18F;
        board_band_energy_[band] = (board_band_energy_[band] * 0.995F) + (std::abs(board_modes_[band]) * 0.005F);
        board_modes_[band] = std::tanh(board_modes_[band] * (1.0F + (board_band_energy_[band] * 0.31F)));
        if (!std::isfinite(board_modes_[band])) {
            board_modes_[band] = 0.0F;
        }
        if (!std::isfinite(board_mode_velocity_[band])) {
            board_mode_velocity_[band] = 0.0F;
        }
        if (!std::isfinite(board_band_energy_[band])) {
            board_band_energy_[band] = 0.0F;
        }
    }

    std::array<float, kBoardLineCount> board_out{};
    float board_sum = 0.0F;
    for (std::size_t i = 0; i < kBoardLineCount; ++i) {
        const std::size_t base = i * kBoardMaxDelay;
        const std::uint16_t idx = board_indices_[i];
        float tap = board_lines_[base + idx];
        if (!std::isfinite(tap)) {
            tap = 0.0F;
            board_lines_[base + idx] = 0.0F;
        }
        board_out[i] = tap;
        board_sum += board_out[i];
    }

    const float scattering_scale = 0.5F;
    const float board_feedback = 0.73F + (pedal_amount_ * 0.13F);
    const float modal_sum = (board_modes_[0] * 0.58F) + (board_modes_[1] * 0.74F) + (board_modes_[2] * 0.66F);
    const float board_in = (board_drive * 0.062F) + (modal_sum * 0.046F);
    for (std::size_t i = 0; i < kBoardLineCount; ++i) {
        const float reflected = (board_sum * scattering_scale) - board_out[i];
        float next = (reflected * board_feedback) + (board_in * 0.25F);
        if (!std::isfinite(next)) {
            next = 0.0F;
        }

        const std::size_t base = i * kBoardMaxDelay;
        const std::uint16_t idx = board_indices_[i];
        board_lines_[base + idx] = next;

        std::uint16_t next_index = static_cast<std::uint16_t>(idx + 1);
        if (next_index >= board_lengths_[i]) {
            next_index = 0;
        }
        board_indices_[i] = next_index;
    }

    const float board_resonance = board_sum * 0.20F;
    float raw = strings_sum + (board_resonance * 0.60F) + (modal_sum * 0.16F);
    if (!std::isfinite(raw)) {
        raw = 0.0F;
    }

    board_lf_ = (0.986F * board_lf_) + (0.014F * raw);
    const float hf = raw - board_lf_;
    board_hf_ = (0.71F * board_hf_) + (0.29F * hf);
    if (!std::isfinite(board_lf_)) {
        board_lf_ = 0.0F;
    }
    if (!std::isfinite(board_hf_)) {
        board_hf_ = 0.0F;
    }

    const float spectral_body = (board_modes_[0] * 0.72F) + (board_modes_[1] * 0.62F) + (board_modes_[2] * 0.38F);
    const float shaped = (0.78F * board_lf_) + (0.32F * board_hf_) + (spectral_body * 0.15F);
    const float gain = (0.020F + (pedal_amount_ * 0.041F)) * (1.0F - (0.18F * damper_drop));
    const float out = std::tanh(shaped * 1.08F) * gain;
    return std::isfinite(out) ? out : 0.0F;
}

}  // namespace vb
