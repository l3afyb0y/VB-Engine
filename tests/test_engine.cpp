#include <algorithm>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#include "instruments/piano/sample_library.hpp"
#include "vb_engine/c_api.h"

namespace {

int g_failures = 0;

void expect_true(const bool condition, const char* message) {
    if (condition) {
        return;
    }

    std::cerr << "FAIL: " << message << "\n";
    ++g_failures;
}

void write_test_wav(const std::filesystem::path& path, const float amplitude_scale) {
    constexpr uint32_t kSampleRate = 48000;
    constexpr uint16_t kChannels = 1;
    constexpr uint16_t kBitsPerSample = 16;
    constexpr uint32_t kNumFrames = 4800;
    constexpr double kPi = 3.14159265358979323846;

    std::vector<int16_t> pcm;
    pcm.reserve(kNumFrames);
    for (uint32_t i = 0; i < kNumFrames; ++i) {
        const double phase = (2.0 * kPi * 440.0 * static_cast<double>(i)) / static_cast<double>(kSampleRate);
        const float sample = std::sin(phase) * amplitude_scale;
        pcm.push_back(static_cast<int16_t>(std::clamp(sample, -1.0F, 1.0F) * 32767.0F));
    }

    std::ofstream out(path, std::ios::binary);
    const uint32_t data_size = static_cast<uint32_t>(pcm.size() * sizeof(int16_t));
    const uint32_t riff_size = 36 + data_size;
    const uint16_t audio_format = 1;
    const uint32_t byte_rate = kSampleRate * kChannels * (kBitsPerSample / 8);
    const uint16_t block_align = kChannels * (kBitsPerSample / 8);
    const uint32_t fmt_size = 16;

    out.write("RIFF", 4);
    out.write(reinterpret_cast<const char*>(&riff_size), sizeof(riff_size));
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    out.write(reinterpret_cast<const char*>(&fmt_size), sizeof(fmt_size));
    out.write(reinterpret_cast<const char*>(&audio_format), sizeof(audio_format));
    out.write(reinterpret_cast<const char*>(&kChannels), sizeof(kChannels));
    out.write(reinterpret_cast<const char*>(&kSampleRate), sizeof(kSampleRate));
    out.write(reinterpret_cast<const char*>(&byte_rate), sizeof(byte_rate));
    out.write(reinterpret_cast<const char*>(&block_align), sizeof(block_align));
    out.write(reinterpret_cast<const char*>(&kBitsPerSample), sizeof(kBitsPerSample));
    out.write("data", 4);
    out.write(reinterpret_cast<const char*>(&data_size), sizeof(data_size));
    out.write(reinterpret_cast<const char*>(pcm.data()), static_cast<std::streamsize>(data_size));
}

void write_stereo_test_wav(const std::filesystem::path& path, const float left_scale, const float right_scale) {
    constexpr uint32_t kSampleRate = 48000;
    constexpr uint16_t kChannels = 2;
    constexpr uint16_t kBitsPerSample = 16;
    constexpr uint32_t kNumFrames = 4800;
    constexpr double kPi = 3.14159265358979323846;

    std::vector<int16_t> pcm;
    pcm.reserve(static_cast<std::size_t>(kNumFrames) * 2U);
    for (uint32_t i = 0; i < kNumFrames; ++i) {
        const double phase = (2.0 * kPi * 440.0 * static_cast<double>(i)) / static_cast<double>(kSampleRate);
        const float left = std::sin(phase) * left_scale;
        const float right = std::sin(phase) * right_scale;
        pcm.push_back(static_cast<int16_t>(std::clamp(left, -1.0F, 1.0F) * 32767.0F));
        pcm.push_back(static_cast<int16_t>(std::clamp(right, -1.0F, 1.0F) * 32767.0F));
    }

    std::ofstream out(path, std::ios::binary);
    const uint32_t data_size = static_cast<uint32_t>(pcm.size() * sizeof(int16_t));
    const uint32_t riff_size = 36 + data_size;
    const uint16_t audio_format = 1;
    const uint32_t byte_rate = kSampleRate * kChannels * (kBitsPerSample / 8);
    const uint16_t block_align = kChannels * (kBitsPerSample / 8);
    const uint32_t fmt_size = 16;

    out.write("RIFF", 4);
    out.write(reinterpret_cast<const char*>(&riff_size), sizeof(riff_size));
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    out.write(reinterpret_cast<const char*>(&fmt_size), sizeof(fmt_size));
    out.write(reinterpret_cast<const char*>(&audio_format), sizeof(audio_format));
    out.write(reinterpret_cast<const char*>(&kChannels), sizeof(kChannels));
    out.write(reinterpret_cast<const char*>(&kSampleRate), sizeof(kSampleRate));
    out.write(reinterpret_cast<const char*>(&byte_rate), sizeof(byte_rate));
    out.write(reinterpret_cast<const char*>(&block_align), sizeof(block_align));
    out.write(reinterpret_cast<const char*>(&kBitsPerSample), sizeof(kBitsPerSample));
    out.write("data", 4);
    out.write(reinterpret_cast<const char*>(&data_size), sizeof(data_size));
    out.write(reinterpret_cast<const char*>(pcm.data()), static_cast<std::streamsize>(data_size));
}

void write_impulse_wav(const std::filesystem::path& path, const float amplitude, const uint32_t impulse_index, const uint32_t frame_count) {
    constexpr uint32_t kSampleRate = 48000;
    constexpr uint16_t kChannels = 1;
    constexpr uint16_t kBitsPerSample = 16;

    std::vector<int16_t> pcm(frame_count, 0);
    if (impulse_index < frame_count) {
        pcm[impulse_index] = static_cast<int16_t>(std::clamp(amplitude, -1.0F, 1.0F) * 32767.0F);
    }

    std::ofstream out(path, std::ios::binary);
    const uint32_t data_size = static_cast<uint32_t>(pcm.size() * sizeof(int16_t));
    const uint32_t riff_size = 36 + data_size;
    const uint16_t audio_format = 1;
    const uint32_t byte_rate = kSampleRate * kChannels * (kBitsPerSample / 8);
    const uint16_t block_align = kChannels * (kBitsPerSample / 8);
    const uint32_t fmt_size = 16;

    out.write("RIFF", 4);
    out.write(reinterpret_cast<const char*>(&riff_size), sizeof(riff_size));
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    out.write(reinterpret_cast<const char*>(&fmt_size), sizeof(fmt_size));
    out.write(reinterpret_cast<const char*>(&audio_format), sizeof(audio_format));
    out.write(reinterpret_cast<const char*>(&kChannels), sizeof(kChannels));
    out.write(reinterpret_cast<const char*>(&kSampleRate), sizeof(kSampleRate));
    out.write(reinterpret_cast<const char*>(&byte_rate), sizeof(byte_rate));
    out.write(reinterpret_cast<const char*>(&block_align), sizeof(block_align));
    out.write(reinterpret_cast<const char*>(&kBitsPerSample), sizeof(kBitsPerSample));
    out.write("data", 4);
    out.write(reinterpret_cast<const char*>(&data_size), sizeof(data_size));
    out.write(reinterpret_cast<const char*>(pcm.data()), static_cast<std::streamsize>(data_size));
}

double render_note_block_energy(const std::filesystem::path& sfz_path, const uint8_t note, const uint8_t velocity) {
    vb_engine_config config{};
    vb_engine_default_config(&config);
    config.max_block_size = 128;
    const std::string sfz_string = sfz_path.string();
    config.piano_sfz_path = sfz_string.c_str();

    vb_engine_handle* handle = nullptr;
    expect_true(vb_engine_create(&config, &handle) == VB_ENGINE_OK, "engine create in render_note_block_energy");
    if (handle == nullptr) {
        return 0.0;
    }

    float left[128]{};
    float right[128]{};
    float* outputs[2]{left, right};

    expect_true(vb_engine_note_on(handle, 0, note, velocity) == VB_ENGINE_OK, "note on in render_note_block_energy");
    expect_true(vb_engine_process(handle, outputs, 2, 128) == VB_ENGINE_OK, "process in render_note_block_energy");

    double energy = 0.0;
    for (const float sample : left) {
        energy += std::abs(static_cast<double>(sample));
    }

    vb_engine_destroy(handle);
    return energy;
}

double render_note_block_energy_with_mic_mix(
    const std::filesystem::path& sfz_path,
    const uint8_t note,
    const uint8_t velocity,
    const float mic_mix
) {
    vb_engine_config config{};
    vb_engine_default_config(&config);
    config.max_block_size = 128;
    config.piano_humanize_timing_ms = 0.0F;
    config.piano_humanize_velocity = 0.0F;
    config.piano_reverb_wet = 0.0F;
    config.piano_mic_mix = mic_mix;
    const std::string sfz_string = sfz_path.string();
    config.piano_sfz_path = sfz_string.c_str();

    vb_engine_handle* handle = nullptr;
    expect_true(vb_engine_create(&config, &handle) == VB_ENGINE_OK, "engine create in render_note_block_energy_with_mic_mix");
    if (handle == nullptr) {
        return 0.0;
    }

    float left[128]{};
    float right[128]{};
    float* outputs[2]{left, right};

    expect_true(vb_engine_note_on(handle, 0, note, velocity) == VB_ENGINE_OK, "note on in render_note_block_energy_with_mic_mix");
    expect_true(vb_engine_process(handle, outputs, 2, 128) == VB_ENGINE_OK, "process in render_note_block_energy_with_mic_mix");

    double energy = 0.0;
    for (const float sample : left) {
        energy += std::abs(static_cast<double>(sample));
    }

    vb_engine_destroy(handle);
    return energy;
}

struct RoundRobinStats {
    int low_bucket{0};
    int high_bucket{0};
    double min_energy{1.0e12};
    double max_energy{0.0};
};

RoundRobinStats render_round_robin_stats(const std::filesystem::path& sfz_path) {
    vb_engine_config config{};
    vb_engine_default_config(&config);
    config.max_block_size = 128;
    config.max_voices = 32;
    config.piano_humanize_timing_ms = 0.0F;
    config.piano_humanize_velocity = 0.0F;
    config.piano_reverb_wet = 0.0F;
    config.piano_pedal_noise_enabled = 1;
    const std::string sfz_string = sfz_path.string();
    config.piano_sfz_path = sfz_string.c_str();

    vb_engine_handle* handle = nullptr;
    expect_true(vb_engine_create(&config, &handle) == VB_ENGINE_OK, "engine create in render_round_robin_stats");
    if (handle == nullptr) {
        return RoundRobinStats{};
    }

    float left[128]{};
    float right[128]{};
    float* outputs[2]{left, right};
    RoundRobinStats stats{};
    std::vector<double> energies;
    energies.reserve(36);

    for (int i = 0; i < 36; ++i) {
        expect_true(vb_engine_note_on(handle, 0, 60, 100) == VB_ENGINE_OK, "note on in round robin stats");
        expect_true(vb_engine_process(handle, outputs, 2, 128) == VB_ENGINE_OK, "process in round robin stats");

        double energy = 0.0;
        for (const float sample : left) {
            energy += std::abs(static_cast<double>(sample));
        }
        energies.push_back(energy);

        expect_true(vb_engine_note_off(handle, 0, 60, 0) == VB_ENGINE_OK, "note off in round robin stats");
        for (int block = 0; block < 6; ++block) {
            expect_true(vb_engine_process(handle, outputs, 2, 128) == VB_ENGINE_OK, "release process in round robin stats");
        }
    }

    vb_engine_destroy(handle);
    for (const double e : energies) {
        stats.min_energy = std::min(stats.min_energy, e);
        stats.max_energy = std::max(stats.max_energy, e);
    }
    const double split = (stats.min_energy + stats.max_energy) * 0.5;
    for (const double e : energies) {
        if (e > split) {
            ++stats.high_bucket;
        } else {
            ++stats.low_bucket;
        }
    }
    return stats;
}

double render_tail_energy_with_pedal(const uint32_t pedal_mode, const uint8_t pedal_value, const uint8_t pedal_threshold) {
    vb_engine_config config{};
    vb_engine_default_config(&config);
    config.max_block_size = 128;
    config.max_voices = 64;
    config.piano_humanize_timing_ms = 0.0F;
    config.piano_humanize_velocity = 0.0F;
    config.piano_reverb_wet = 0.0F;
    config.piano_pedal_mode = pedal_mode;
    config.piano_pedal_binary_threshold = pedal_threshold;

    vb_engine_handle* handle = nullptr;
    expect_true(vb_engine_create(&config, &handle) == VB_ENGINE_OK, "engine create in render_tail_energy_with_pedal");
    if (handle == nullptr) {
        return 0.0;
    }

    float left[128]{};
    float right[128]{};
    float* outputs[2]{left, right};

    expect_true(vb_engine_note_on(handle, 0, 60, 100) == VB_ENGINE_OK, "pedal test note on");
    for (int i = 0; i < 8; ++i) {
        expect_true(vb_engine_process(handle, outputs, 2, 128) == VB_ENGINE_OK, "pedal test pre-process");
    }

    expect_true(vb_engine_control_change(handle, 0, 64, pedal_value) == VB_ENGINE_OK, "pedal test control change");
    expect_true(vb_engine_note_off(handle, 0, 60, 0) == VB_ENGINE_OK, "pedal test note off");

    double tail_energy = 0.0;
    for (int block = 0; block < 50; ++block) {
        expect_true(vb_engine_process(handle, outputs, 2, 128) == VB_ENGINE_OK, "pedal test tail process");
        if (block >= 24) {
            for (const float sample : left) {
                tail_energy += std::abs(static_cast<double>(sample));
            }
        }
    }

    vb_engine_destroy(handle);
    return tail_energy;
}

double render_note_energy_simple(const uint8_t velocity) {
    vb_engine_config config{};
    vb_engine_default_config(&config);
    config.max_block_size = 128;
    config.max_voices = 64;
    config.piano_humanize_timing_ms = 0.0F;
    config.piano_humanize_velocity = 0.0F;
    config.piano_reverb_wet = 0.0F;

    vb_engine_handle* handle = nullptr;
    expect_true(vb_engine_create(&config, &handle) == VB_ENGINE_OK, "engine create in render_note_energy_simple");
    if (handle == nullptr) {
        return 0.0;
    }

    float left[128]{};
    float right[128]{};
    float* outputs[2]{left, right};

    expect_true(vb_engine_note_on(handle, 0, 60, velocity) == VB_ENGINE_OK, "note on in render_note_energy_simple");
    double energy = 0.0;
    for (int block = 0; block < 14; ++block) {
        expect_true(vb_engine_process(handle, outputs, 2, 128) == VB_ENGINE_OK, "process in render_note_energy_simple");
        if (block >= 2 && block <= 8) {
            for (const float sample : left) {
                energy += std::abs(static_cast<double>(sample));
            }
        }
    }

    vb_engine_destroy(handle);
    return energy;
}

double render_note_block_energy_with_soft_pedal(
    const std::filesystem::path& sfz_path,
    const uint8_t note,
    const uint8_t velocity,
    const uint8_t soft_pedal_cc
) {
    vb_engine_config config{};
    vb_engine_default_config(&config);
    config.max_block_size = 128;
    config.piano_humanize_timing_ms = 0.0F;
    config.piano_humanize_velocity = 0.0F;
    config.piano_reverb_wet = 0.0F;
    const std::string sfz_string = sfz_path.string();
    config.piano_sfz_path = sfz_string.c_str();

    vb_engine_handle* handle = nullptr;
    expect_true(vb_engine_create(&config, &handle) == VB_ENGINE_OK, "engine create in render_note_block_energy_with_soft_pedal");
    if (handle == nullptr) {
        return 0.0;
    }

    float left[128]{};
    float right[128]{};
    float* outputs[2]{left, right};

    expect_true(vb_engine_control_change(handle, 0, 67, soft_pedal_cc) == VB_ENGINE_OK, "soft pedal cc67");
    expect_true(vb_engine_note_on(handle, 0, note, velocity) == VB_ENGINE_OK, "note on in render_note_block_energy_with_soft_pedal");
    expect_true(vb_engine_process(handle, outputs, 2, 128) == VB_ENGINE_OK, "process in render_note_block_energy_with_soft_pedal");

    double energy = 0.0;
    for (const float s : left) {
        energy += std::abs(static_cast<double>(s));
    }

    vb_engine_destroy(handle);
    return energy;
}

double render_note_hf_ratio(const std::filesystem::path& sfz_path, const uint8_t velocity) {
    vb_engine_config config{};
    vb_engine_default_config(&config);
    config.max_block_size = 128;
    config.max_voices = 32;
    config.piano_humanize_timing_ms = 0.0F;
    config.piano_humanize_velocity = 0.0F;
    config.piano_reverb_wet = 0.0F;
    config.piano_pedal_noise_enabled = 1;
    const std::string sfz_string = sfz_path.string();
    config.piano_sfz_path = sfz_string.c_str();

    vb_engine_handle* handle = nullptr;
    expect_true(vb_engine_create(&config, &handle) == VB_ENGINE_OK, "engine create in render_note_hf_ratio");
    if (handle == nullptr) {
        return 0.0;
    }

    std::vector<float> mono;
    mono.reserve(8192);

    float left[128]{};
    float right[128]{};
    float* outputs[2]{left, right};

    expect_true(vb_engine_note_on(handle, 0, 60, velocity) == VB_ENGINE_OK, "note on in render_note_hf_ratio");
    for (int block = 0; block < 48; ++block) {
        expect_true(vb_engine_process(handle, outputs, 2, 128) == VB_ENGINE_OK, "process in render_note_hf_ratio");
        for (float sample : left) {
            mono.push_back(sample);
        }
    }

    vb_engine_destroy(handle);
    if (mono.size() < 2048) {
        return 0.0;
    }

    double hf_energy = 0.0;
    double total_energy = 0.0;
    for (std::size_t i = 2; i < mono.size(); ++i) {
        const double hp = static_cast<double>(mono[i]) - (2.0 * static_cast<double>(mono[i - 1])) + static_cast<double>(mono[i - 2]);
        hf_energy += hp * hp;
        const double s = static_cast<double>(mono[i]);
        total_energy += s * s;
    }
    return hf_energy / (total_energy + 1.0e-12);
}

double render_note_hf_ratio_with_presence_cc(const uint8_t velocity, const uint8_t presence_cc) {
    vb_engine_config config{};
    vb_engine_default_config(&config);
    config.max_block_size = 128;
    config.max_voices = 32;
    config.piano_humanize_timing_ms = 0.0F;
    config.piano_humanize_velocity = 0.0F;
    config.piano_reverb_wet = 0.0F;

    vb_engine_handle* handle = nullptr;
    expect_true(vb_engine_create(&config, &handle) == VB_ENGINE_OK, "engine create in render_note_hf_ratio_with_presence_cc");
    if (handle == nullptr) {
        return 0.0;
    }

    std::vector<float> mono;
    mono.reserve(8192);

    float left[128]{};
    float right[128]{};
    float* outputs[2]{left, right};

    expect_true(vb_engine_control_change(handle, 0, 74, presence_cc) == VB_ENGINE_OK, "cc74 in render_note_hf_ratio_with_presence_cc");
    expect_true(vb_engine_note_on(handle, 0, 60, velocity) == VB_ENGINE_OK, "note on in render_note_hf_ratio_with_presence_cc");
    for (int block = 0; block < 48; ++block) {
        expect_true(vb_engine_process(handle, outputs, 2, 128) == VB_ENGINE_OK, "process in render_note_hf_ratio_with_presence_cc");
        for (float sample : left) {
            mono.push_back(sample);
        }
    }

    vb_engine_destroy(handle);
    if (mono.size() < 2048) {
        return 0.0;
    }

    double hf_energy = 0.0;
    double total_energy = 0.0;
    for (std::size_t i = 2; i < mono.size(); ++i) {
        const double hp = static_cast<double>(mono[i]) - (2.0 * static_cast<double>(mono[i - 1])) + static_cast<double>(mono[i - 2]);
        hf_energy += hp * hp;
        const double s = static_cast<double>(mono[i]);
        total_energy += s * s;
    }
    return hf_energy / (total_energy + 1.0e-12);
}

struct PedalEdgeMetrics {
    double energy{0.0};
    double max_jump{0.0};
};

PedalEdgeMetrics render_pedal_edge_metrics(const std::filesystem::path& sfz_path) {
    vb_engine_config config{};
    vb_engine_default_config(&config);
    config.max_block_size = 128;
    config.max_voices = 32;
    config.piano_humanize_timing_ms = 0.0F;
    config.piano_humanize_velocity = 0.0F;
    config.piano_reverb_wet = 0.0F;
    config.piano_pedal_noise_enabled = 1;
    const std::string sfz_string = sfz_path.string();
    config.piano_sfz_path = sfz_string.c_str();

    vb_engine_handle* handle = nullptr;
    expect_true(vb_engine_create(&config, &handle) == VB_ENGINE_OK, "engine create in render_pedal_edge_metrics");
    if (handle == nullptr) {
        return {};
    }

    float left[128]{};
    float right[128]{};
    float* outputs[2]{left, right};
    float prev = 0.0F;

    PedalEdgeMetrics metrics{};
    for (int i = 0; i < 24; ++i) {
        const uint8_t value = (i % 2 == 0) ? 127 : 0;
        expect_true(vb_engine_control_change(handle, 0, 64, value) == VB_ENGINE_OK, "cc64 in render_pedal_edge_metrics");
        expect_true(vb_engine_process(handle, outputs, 2, 128) == VB_ENGINE_OK, "process in render_pedal_edge_metrics");
        for (float sample : left) {
            metrics.energy += std::abs(static_cast<double>(sample));
            metrics.max_jump = std::max(metrics.max_jump, std::abs(static_cast<double>(sample - prev)));
            prev = sample;
        }
    }

    vb_engine_destroy(handle);
    return metrics;
}

void test_create_destroy() {
    vb_engine_config config{};
    vb_engine_default_config(&config);

    vb_engine_handle* handle = nullptr;
    const vb_engine_result created = vb_engine_create(&config, &handle);
    expect_true(created == VB_ENGINE_OK, "engine should create");
    expect_true(handle != nullptr, "handle should not be null after create");

    vb_engine_destroy(handle);
}

void test_create_with_legacy_config_size() {
    vb_engine_config config{};
    vb_engine_default_config(&config);
    config.struct_size = static_cast<uint32_t>(offsetof(vb_engine_config, piano_sfz_path));

    vb_engine_handle* handle = nullptr;
    const vb_engine_result created = vb_engine_create(&config, &handle);
    expect_true(created == VB_ENGINE_OK, "engine should accept legacy config struct size");
    expect_true(handle != nullptr, "legacy-size handle should be created");
    vb_engine_destroy(handle);
}

void test_note_generates_audio() {
    vb_engine_config config{};
    vb_engine_default_config(&config);
    config.max_block_size = 256;

    vb_engine_handle* handle = nullptr;
    expect_true(vb_engine_create(&config, &handle) == VB_ENGINE_OK, "engine create for audio test");

    float left[256]{};
    float right[256]{};
    float* outputs[2]{left, right};

    expect_true(vb_engine_process(handle, outputs, 2, 256) == VB_ENGINE_OK, "process silence block");

    double silence_sum = 0.0;
    for (const float sample : left) {
        silence_sum += std::abs(sample);
    }
    expect_true(silence_sum < 1e-6, "initial block should be silent");

    expect_true(vb_engine_note_on(handle, 0, 60, 100) == VB_ENGINE_OK, "note on should enqueue");
    expect_true(vb_engine_process(handle, outputs, 2, 256) == VB_ENGINE_OK, "process sounding block");

    double active_sum = 0.0;
    for (const float sample : left) {
        active_sum += std::abs(sample);
    }
    expect_true(active_sum > 0.01, "block after note-on should contain signal");

    expect_true(vb_engine_note_off(handle, 0, 60, 0) == VB_ENGINE_OK, "note off should enqueue");
    for (int i = 0; i < 180; ++i) {
        expect_true(vb_engine_process(handle, outputs, 2, 256) == VB_ENGINE_OK, "process during release");
    }

    double tail_sum = 0.0;
    for (const float sample : left) {
        tail_sum += std::abs(sample);
    }
    expect_true(tail_sum < 0.005, "tail should decay near silence");

    vb_engine_destroy(handle);
}

void test_queue_backpressure() {
    vb_engine_config config{};
    vb_engine_default_config(&config);

    vb_engine_handle* handle = nullptr;
    expect_true(vb_engine_create(&config, &handle) == VB_ENGINE_OK, "engine create for queue test");

    int queue_full_hits = 0;
    for (int i = 0; i < 6000; ++i) {
        const auto result = vb_engine_note_on(handle, 0, static_cast<uint8_t>(40 + (i % 40)), 90);
        if (result == VB_ENGINE_ERROR_QUEUE_FULL) {
            ++queue_full_hits;
        }
    }

    expect_true(queue_full_hits > 0, "queue should eventually report backpressure");
    vb_engine_destroy(handle);
}

void test_sustain_pedal_behavior() {
    vb_engine_config config{};
    vb_engine_default_config(&config);
    config.max_block_size = 128;

    vb_engine_handle* handle = nullptr;
    expect_true(vb_engine_create(&config, &handle) == VB_ENGINE_OK, "engine create for sustain test");

    float left[128]{};
    float right[128]{};
    float* outputs[2]{left, right};

    expect_true(vb_engine_note_on(handle, 0, 60, 100) == VB_ENGINE_OK, "sustain test note-on");
    expect_true(vb_engine_process(handle, outputs, 2, 128) == VB_ENGINE_OK, "sustain test first process");

    expect_true(vb_engine_control_change(handle, 0, 64, 127) == VB_ENGINE_OK, "pedal down");
    expect_true(vb_engine_note_off(handle, 0, 60, 0) == VB_ENGINE_OK, "note off while pedal down");

    for (int i = 0; i < 12; ++i) {
        expect_true(vb_engine_process(handle, outputs, 2, 128) == VB_ENGINE_OK, "process with pedal down");
    }

    double held_sum = 0.0;
    for (const float sample : left) {
        held_sum += std::abs(sample);
    }
    expect_true(held_sum > 0.01, "pedal-down release should still ring");

    expect_true(vb_engine_control_change(handle, 0, 64, 0) == VB_ENGINE_OK, "pedal up");
    for (int i = 0; i < 220; ++i) {
        expect_true(vb_engine_process(handle, outputs, 2, 128) == VB_ENGINE_OK, "process after pedal up");
    }

    double released_sum = 0.0;
    for (const float sample : left) {
        released_sum += std::abs(sample);
    }
    expect_true(released_sum < 0.02, "signal should decay after pedal release");

    vb_engine_destroy(handle);
}

void test_default_instrument_selection() {
    vb_engine_config config{};
    vb_engine_default_config(&config);
    config.default_instrument = VB_INSTRUMENT_CLEAN_ELECTRIC_GUITAR;
    config.max_block_size = 128;

    vb_engine_handle* handle = nullptr;
    expect_true(vb_engine_create(&config, &handle) == VB_ENGINE_OK, "engine create for default instrument test");

    expect_true(vb_engine_note_on(handle, 0, 64, 96) == VB_ENGINE_OK, "note-on with guitar default");

    float left[128]{};
    float right[128]{};
    float* outputs[2]{left, right};
    expect_true(vb_engine_process(handle, outputs, 2, 128) == VB_ENGINE_OK, "process with guitar default");

    double sum = 0.0;
    for (const float sample : left) {
        sum += std::abs(sample);
    }

    expect_true(sum > 0.005, "default guitar instrument should produce signal");
    vb_engine_destroy(handle);
}

void test_transition_spike_bound() {
    vb_engine_config config{};
    vb_engine_default_config(&config);
    config.max_block_size = 128;
    config.max_voices = 96;

    vb_engine_handle* handle = nullptr;
    expect_true(vb_engine_create(&config, &handle) == VB_ENGINE_OK, "engine create for spike test");

    float left[128]{};
    float right[128]{};
    float* outputs[2]{left, right};

    float prev = 0.0F;
    double max_jump = 0.0;

    for (int i = 0; i < 180; ++i) {
        const uint8_t note = static_cast<uint8_t>(48 + (i % 24));
        expect_true(vb_engine_note_on(handle, 0, note, 84) == VB_ENGINE_OK, "note on in spike test");
        expect_true(vb_engine_process(handle, outputs, 2, 128) == VB_ENGINE_OK, "process after note on in spike test");
        expect_true(vb_engine_note_off(handle, 0, note, 0) == VB_ENGINE_OK, "note off in spike test");
        expect_true(vb_engine_process(handle, outputs, 2, 128) == VB_ENGINE_OK, "process after note off in spike test");

        for (float sample : left) {
            const double jump = std::abs(static_cast<double>(sample - prev));
            max_jump = std::max(max_jump, jump);
            prev = sample;
        }
    }

    expect_true(max_jump < 0.90, "transition spikes should stay below glitch threshold");
    vb_engine_destroy(handle);
}

void test_dense_polyphony_headroom() {
    vb_engine_config config{};
    vb_engine_default_config(&config);
    config.max_block_size = 256;
    config.max_voices = 192;

    vb_engine_handle* handle = nullptr;
    expect_true(vb_engine_create(&config, &handle) == VB_ENGINE_OK, "engine create for headroom test");

    for (uint8_t note = 36; note <= 96; note += 2) {
        expect_true(vb_engine_note_on(handle, 0, note, 92) == VB_ENGINE_OK, "note on in headroom test");
    }
    expect_true(vb_engine_control_change(handle, 0, 64, 127) == VB_ENGINE_OK, "pedal down in headroom test");

    float left[256]{};
    float right[256]{};
    float* outputs[2]{left, right};

    double peak = 0.0;
    for (int block = 0; block < 40; ++block) {
        expect_true(vb_engine_process(handle, outputs, 2, 256) == VB_ENGINE_OK, "process in headroom test");
        for (float sample : left) {
            peak = std::max(peak, std::abs(static_cast<double>(sample)));
        }
    }

    expect_true(peak < 0.995, "dense polyphony should stay below hard clipping");
    vb_engine_destroy(handle);
}

void test_twenty_note_pedal_low_register_glitch_bound() {
    vb_engine_config config{};
    vb_engine_default_config(&config);
    config.max_block_size = 64;
    config.max_voices = 192;
    config.piano_humanize_timing_ms = 0.0F;
    config.piano_humanize_velocity = 0.0F;

    vb_engine_handle* handle = nullptr;
    expect_true(vb_engine_create(&config, &handle) == VB_ENGINE_OK, "engine create for 20-note pedal glitch test");
    if (handle == nullptr) {
        return;
    }

    expect_true(vb_engine_control_change(handle, 0, 64, 127) == VB_ENGINE_OK, "pedal down in 20-note pedal glitch test");
    const uint8_t notes[20]{
        24, 27, 31, 34, 36, 39, 43, 46, 48, 51, 55, 58, 60, 63, 67, 70, 72, 75, 79, 82
    };
    for (const uint8_t note : notes) {
        expect_true(vb_engine_note_on(handle, 0, note, 98) == VB_ENGINE_OK, "note on in 20-note pedal glitch test");
    }

    float left[64]{};
    float right[64]{};
    float* outputs[2]{left, right};

    double peak = 0.0;
    double max_jump = 0.0;
    double onset_max_jump = 0.0;
    double max_hp = 0.0;
    float prev = 0.0F;
    float prev2 = 0.0F;
    bool have_prev = false;
    bool have_prev2 = false;

    for (int block = 0; block < 96; ++block) {
        expect_true(vb_engine_process(handle, outputs, 2, 64) == VB_ENGINE_OK, "process in 20-note pedal glitch test");
        for (const float sample : left) {
            peak = std::max(peak, std::abs(static_cast<double>(sample)));
            const double jump = std::abs(static_cast<double>(sample - prev));
            max_jump = std::max(max_jump, jump);
            if (have_prev && have_prev2) {
                const double hp = std::abs(static_cast<double>(sample) - (2.0 * static_cast<double>(prev)) + static_cast<double>(prev2));
                max_hp = std::max(max_hp, hp);
            }
            if (block < 20) {
                onset_max_jump = std::max(onset_max_jump, jump);
            }
            prev2 = prev;
            prev = sample;
            have_prev2 = have_prev;
            have_prev = true;
        }
    }

    expect_true(peak < 0.995, "20-note pedal texture should stay below hard clipping");
    expect_true(onset_max_jump < 0.22, "20-note pedal onset should stay below glitch jump bound");
    expect_true(max_jump < 0.22, "20-note pedal sustain should stay below glitch jump bound");
    expect_true(max_hp < 0.24, "20-note pedal texture should avoid impulsive crackle spikes");
    vb_engine_destroy(handle);
}

void test_mid_register_attack_brightness_decay() {
    vb_engine_config config{};
    vb_engine_default_config(&config);
    config.max_block_size = 128;

    vb_engine_handle* handle = nullptr;
    expect_true(vb_engine_create(&config, &handle) == VB_ENGINE_OK, "engine create for brightness decay test");
    expect_true(vb_engine_note_on(handle, 0, 60, 100) == VB_ENGINE_OK, "brightness test note-on");

    constexpr int kFrames = 96000;  // 2 seconds at 48 kHz.
    std::vector<float> mono;
    mono.reserve(kFrames);

    float left[128]{};
    float right[128]{};
    float* outputs[2]{left, right};

    for (int produced = 0; produced < kFrames; produced += 128) {
        const int frames = std::min(128, kFrames - produced);
        expect_true(
            vb_engine_process(handle, outputs, 2, static_cast<uint32_t>(frames)) == VB_ENGINE_OK,
            "process during brightness decay test"
        );
        for (int i = 0; i < frames; ++i) {
            mono.push_back(left[i]);
        }
    }

    auto high_frequency_ratio = [&](const int start, const int length) {
        double hf_energy = 0.0;
        double total_energy = 0.0;
        for (int i = start + 2; i < start + length; ++i) {
            const double hp = static_cast<double>(mono[static_cast<std::size_t>(i)])
                - (2.0 * static_cast<double>(mono[static_cast<std::size_t>(i - 1)]))
                + static_cast<double>(mono[static_cast<std::size_t>(i - 2)]);
            hf_energy += hp * hp;
            const double s = static_cast<double>(mono[static_cast<std::size_t>(i)]);
            total_energy += s * s;
        }
        return hf_energy / (total_energy + 1.0e-12);
    };

    const double early_ratio = high_frequency_ratio(0, 12000);
    const double late_ratio = high_frequency_ratio(48000, 12000);
    const double attack_to_sustain = early_ratio / (late_ratio + 1.0e-12);

    expect_true(
        attack_to_sustain > 1.0,
        "mid-register note should begin brighter than late sustain"
    );

    vb_engine_destroy(handle);
}

void test_stereo_output_has_keyboard_spatial_width() {
    vb_engine_config config{};
    vb_engine_default_config(&config);
    config.max_block_size = 128;
    config.piano_humanize_timing_ms = 0.0F;
    config.piano_humanize_velocity = 0.0F;

    vb_engine_handle* handle = nullptr;
    expect_true(vb_engine_create(&config, &handle) == VB_ENGINE_OK, "engine create for stereo width test");
    if (handle == nullptr) {
        return;
    }

    float left[128]{};
    float right[128]{};
    float* outputs[2]{left, right};

    expect_true(vb_engine_note_on(handle, 0, 45, 100) == VB_ENGINE_OK, "stereo width test low note on");
    expect_true(vb_engine_note_on(handle, 0, 72, 100) == VB_ENGINE_OK, "stereo width test high note on");

    double side_energy = 0.0;
    double mid_energy = 0.0;
    for (int block = 0; block < 12; ++block) {
        expect_true(vb_engine_process(handle, outputs, 2, 128) == VB_ENGINE_OK, "stereo width test process");
        for (int i = 0; i < 128; ++i) {
            const double l = static_cast<double>(left[i]);
            const double r = static_cast<double>(right[i]);
            side_energy += std::abs(l - r);
            mid_energy += std::abs(0.5 * (l + r));
        }
    }

    const double width_ratio = side_energy / (mid_energy + 1.0e-12);
    expect_true(width_ratio > 0.04, "stereo rendering should produce non-trivial L/R width");
    vb_engine_destroy(handle);
}

void test_sfz_velocity_layers_load_and_respond() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "vb_engine_test_sfz";
    std::filesystem::create_directories(root);
    const std::filesystem::path low_wav = root / "low.wav";
    const std::filesystem::path high_wav = root / "high.wav";
    const std::filesystem::path sfz = root / "test.sfz";

    write_test_wav(low_wav, 0.12F);
    write_test_wav(high_wav, 0.70F);

    std::ofstream sfz_out(sfz);
    sfz_out << "<group> lokey=60 hikey=60 pitch_keycenter=60\n";
    sfz_out << "<region> sample=low.wav lovel=1 hivel=63\n";
    sfz_out << "<region> sample=high.wav lovel=64 hivel=127\n";
    sfz_out.close();

    vb_engine_config config{};
    vb_engine_default_config(&config);
    config.max_block_size = 128;
    const std::string sfz_path_string = sfz.string();
    config.piano_sfz_path = sfz_path_string.c_str();

    vb_engine_handle* handle = nullptr;
    expect_true(vb_engine_create(&config, &handle) == VB_ENGINE_OK, "engine create for sfz load test");

    float left[128]{};
    float right[128]{};
    float* outputs[2]{left, right};

    expect_true(vb_engine_note_on(handle, 0, 60, 40) == VB_ENGINE_OK, "sfz low velocity note on");
    expect_true(vb_engine_process(handle, outputs, 2, 128) == VB_ENGINE_OK, "sfz low velocity process");
    double low_energy = 0.0;
    for (float s : left) {
        low_energy += std::abs(static_cast<double>(s));
    }

    expect_true(vb_engine_note_off(handle, 0, 60, 0) == VB_ENGINE_OK, "sfz low velocity note off");
    for (int i = 0; i < 20; ++i) {
        expect_true(vb_engine_process(handle, outputs, 2, 128) == VB_ENGINE_OK, "sfz release process");
    }

    expect_true(vb_engine_note_on(handle, 0, 60, 110) == VB_ENGINE_OK, "sfz high velocity note on");
    expect_true(vb_engine_process(handle, outputs, 2, 128) == VB_ENGINE_OK, "sfz high velocity process");
    double high_energy = 0.0;
    for (float s : left) {
        high_energy += std::abs(static_cast<double>(s));
    }

    expect_true(high_energy > (low_energy * 1.8), "higher velocity layer should produce higher energy");
    vb_engine_destroy(handle);
    std::filesystem::remove_all(root);
}

void test_stereo_sample_region_preserves_channel_asymmetry() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "vb_engine_test_stereo_sample";
    std::filesystem::create_directories(root);
    const std::filesystem::path stereo_wav = root / "stereo.wav";
    const std::filesystem::path sfz = root / "stereo.sfz";

    write_stereo_test_wav(stereo_wav, 0.90F, 0.06F);
    {
        std::ofstream sfz_out(sfz);
        sfz_out << "<group> lokey=69 hikey=69 pitch_keycenter=69\n";
        sfz_out << "<region> sample=stereo.wav lovel=1 hivel=127\n";
    }

    vb_engine_config config{};
    vb_engine_default_config(&config);
    config.max_block_size = 128;
    config.piano_humanize_timing_ms = 0.0F;
    config.piano_humanize_velocity = 0.0F;
    config.piano_reverb_wet = 0.0F;
    const std::string sfz_path_string = sfz.string();
    config.piano_sfz_path = sfz_path_string.c_str();

    vb_engine_handle* handle = nullptr;
    expect_true(vb_engine_create(&config, &handle) == VB_ENGINE_OK, "engine create for stereo sample test");
    if (handle == nullptr) {
        std::filesystem::remove_all(root);
        return;
    }

    float left[128]{};
    float right[128]{};
    float* outputs[2]{left, right};
    expect_true(vb_engine_note_on(handle, 0, 69, 100) == VB_ENGINE_OK, "stereo sample note on");
    expect_true(vb_engine_process(handle, outputs, 2, 128) == VB_ENGINE_OK, "stereo sample process");

    double left_energy = 0.0;
    double right_energy = 0.0;
    for (int i = 0; i < 128; ++i) {
        left_energy += std::abs(static_cast<double>(left[i]));
        right_energy += std::abs(static_cast<double>(right[i]));
    }

    expect_true(left_energy > (right_energy * 1.20), "stereo sample playback should preserve left/right asymmetry");
    vb_engine_destroy(handle);
    std::filesystem::remove_all(root);
}

void test_sample_library_loads_flac_sfz_region() {
    const std::filesystem::path source_root = std::filesystem::path(__FILE__).parent_path().parent_path();
    const std::filesystem::path fixture_flac = source_root / "tests" / "fixtures" / "sine440.flac";
    expect_true(std::filesystem::exists(fixture_flac), "FLAC fixture should exist");

    const std::filesystem::path root = std::filesystem::temp_directory_path() / "vb_engine_test_flac_sfz";
    std::filesystem::create_directories(root);

    const std::filesystem::path sfz = root / "flac-test.sfz";
    std::ofstream sfz_out(sfz);
    sfz_out << "<group> lokey=60 hikey=60 pitch_keycenter=60\n";
    sfz_out << "<region> sample=" << fixture_flac.string() << " lovel=1 hivel=127\n";
    sfz_out.close();

    vb::PianoSampleLibrary library{};
    const bool loaded = library.load_sfz(sfz.string());
    expect_true(loaded, "sample library should load FLAC region from SFZ");
    expect_true(library.region_count() > 0, "sample library should expose FLAC-backed region");

    const auto layers = library.select_layers(60, 100, false);
    expect_true(layers.primary != nullptr, "FLAC-backed region should be selectable");
    std::filesystem::remove_all(root);
}

void test_sfz_offset_reduces_impulse_start_energy() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "vb_engine_test_sfz_offset";
    std::filesystem::create_directories(root);
    const std::filesystem::path impulse_wav = root / "impulse.wav";
    const std::filesystem::path sfz_no_offset = root / "no-offset.sfz";
    const std::filesystem::path sfz_with_offset = root / "with-offset.sfz";

    write_impulse_wav(impulse_wav, 0.95F, 80, 1024);

    {
        std::ofstream sfz_out(sfz_no_offset);
        sfz_out << "<group> lokey=60 hikey=60 pitch_keycenter=60\n";
        sfz_out << "<region> sample=impulse.wav lovel=1 hivel=127\n";
    }
    {
        std::ofstream sfz_out(sfz_with_offset);
        sfz_out << "<group> lokey=60 hikey=60 pitch_keycenter=60\n";
        sfz_out << "<region> sample=impulse.wav lovel=1 hivel=127 offset=96\n";
    }

    const double no_offset_energy = render_note_block_energy(sfz_no_offset, 60, 100);
    const double with_offset_energy = render_note_block_energy(sfz_with_offset, 60, 100);

    expect_true(
        with_offset_energy < (no_offset_energy * 0.72),
        "SFZ offset should materially reduce attack energy from early impulse content"
    );
    std::filesystem::remove_all(root);
}

void test_mic_mix_control_influences_layer_balance() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "vb_engine_test_mic_mix";
    std::filesystem::create_directories(root);
    const std::filesystem::path low_wav = root / "low.wav";
    const std::filesystem::path high_wav = root / "high.wav";
    const std::filesystem::path sfz = root / "micmix.sfz";

    write_test_wav(low_wav, 0.15F);
    write_test_wav(high_wav, 0.90F);

    {
        std::ofstream sfz_out(sfz);
        sfz_out << "<group> lokey=60 hikey=60 pitch_keycenter=60\n";
        sfz_out << "<region> sample=low.wav lovel=1 hivel=63\n";
        sfz_out << "<region> sample=high.wav lovel=64 hivel=127\n";
    }

    const double low_mix_energy = render_note_block_energy_with_mic_mix(sfz, 60, 64, 0.0F);
    const double high_mix_energy = render_note_block_energy_with_mic_mix(sfz, 60, 64, 1.0F);

    expect_true(
        low_mix_energy > (high_mix_energy * 1.10),
        "mic mix control should materially alter blended layer energy"
    );

    std::filesystem::remove_all(root);
}

void test_sfz_round_robin_lorand_distribution() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "vb_engine_test_sfz_rr";
    std::filesystem::create_directories(root);
    const std::filesystem::path low_wav = root / "rr_low.wav";
    const std::filesystem::path high_wav = root / "rr_high.wav";
    const std::filesystem::path sfz = root / "rr.sfz";

    write_test_wav(low_wav, 0.20F);
    write_test_wav(high_wav, 0.85F);

    {
        std::ofstream sfz_out(sfz);
        sfz_out << "<group> lokey=60 hikey=60 lovel=1 hivel=127 pitch_keycenter=60\n";
        sfz_out << "<region> sample=rr_low.wav lorand=0.0 hirand=0.5\n";
        sfz_out << "<region> sample=rr_high.wav lorand=0.5 hirand=1.0\n";
    }

    const RoundRobinStats stats = render_round_robin_stats(sfz);
    expect_true(stats.low_bucket > 6, "round robin random gate should hit lower-energy region multiple times");
    expect_true(stats.high_bucket > 6, "round robin random gate should hit higher-energy region multiple times");
    expect_true(stats.max_energy > (stats.min_energy * 1.55), "round robin variants should produce materially different energies");

    std::filesystem::remove_all(root);
}

void test_sfz_cc64_pedal_conditioned_region_selection() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "vb_engine_test_sfz_cc64";
    std::filesystem::create_directories(root);
    const std::filesystem::path up_wav = root / "pedal_up.wav";
    const std::filesystem::path down_wav = root / "pedal_down.wav";
    const std::filesystem::path sfz = root / "cc64.sfz";

    write_test_wav(up_wav, 0.25F);
    write_test_wav(down_wav, 0.75F);

    {
        std::ofstream sfz_out(sfz);
        sfz_out << "<group> lokey=60 hikey=60 lovel=1 hivel=127 pitch_keycenter=60\n";
        sfz_out << "<region> sample=pedal_up.wav locc64=0 hicc64=63\n";
        sfz_out << "<region> sample=pedal_down.wav locc64=64 hicc64=127\n";
    }

    vb::PianoSampleLibrary library{};
    expect_true(library.load_sfz(sfz.string()), "cc64 sfz should load");

    const auto up = library.select_layers(60, 100, false, 0);
    const auto down = library.select_layers(60, 100, false, 127);
    expect_true(up.primary != nullptr, "cc64 up layer should be selectable");
    expect_true(down.primary != nullptr, "cc64 down layer should be selectable");
    expect_true(
        up.primary != nullptr && down.primary != nullptr && up.primary != down.primary,
        "different cc64 values should select different regions"
    );

    std::filesystem::remove_all(root);
}

void test_binary_pedal_mode_threshold_behavior() {
    const double below = render_tail_energy_with_pedal(VB_PEDAL_MODE_BINARY, 63, 64);
    const double above = render_tail_energy_with_pedal(VB_PEDAL_MODE_BINARY, 127, 64);
    expect_true(
        above > (below * 1.25),
        "binary pedal mode should sustain more above threshold than below threshold"
    );
}

void test_continuous_pedal_half_damper_behavior() {
    const double no_pedal = render_tail_energy_with_pedal(VB_PEDAL_MODE_CONTINUOUS, 0, 64);
    const double half = render_tail_energy_with_pedal(VB_PEDAL_MODE_CONTINUOUS, 64, 64);
    const double full = render_tail_energy_with_pedal(VB_PEDAL_MODE_CONTINUOUS, 127, 64);

    expect_true(half > (no_pedal * 1.10), "continuous half pedal should retain more tail than pedal up");
    expect_true(full > (half * 1.10), "continuous full pedal should retain more tail than half pedal");
}

void test_auto_pedal_mode_preserves_binary_0_64_behavior() {
    const double auto_64 = render_tail_energy_with_pedal(VB_PEDAL_MODE_AUTO, 64, 64);
    const double binary_64 = render_tail_energy_with_pedal(VB_PEDAL_MODE_BINARY, 64, 64);
    const double ratio = auto_64 / (binary_64 + 1.0e-12);
    expect_true(ratio > 0.85 && ratio < 1.15, "auto pedal mode should behave like binary mode for 0/64 pedals");
}

void test_dynamics_preserved_without_heavy_compression_character() {
    const double low = render_note_energy_simple(38);
    const double high = render_note_energy_simple(118);
    expect_true(high > (low * 1.65), "high velocity should retain substantial dynamic advantage over low velocity");
}

void test_soft_pedal_reduces_attack_energy() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "vb_engine_test_soft_pedal";
    std::filesystem::create_directories(root);
    const std::filesystem::path tone_wav = root / "tone.wav";
    const std::filesystem::path sfz = root / "soft.sfz";

    write_test_wav(tone_wav, 0.82F);
    {
        std::ofstream sfz_out(sfz);
        sfz_out << "<group> lokey=60 hikey=60 pitch_keycenter=60 lovel=1 hivel=127\n";
        sfz_out << "<region> sample=tone.wav\n";
    }

    const double no_soft = render_note_block_energy_with_soft_pedal(sfz, 60, 100, 0);
    const double full_soft = render_note_block_energy_with_soft_pedal(sfz, 60, 100, 127);
    expect_true(full_soft < (no_soft * 0.90), "soft pedal should reduce attack energy and brightness");
    std::filesystem::remove_all(root);
}

void test_presence_cc_modulates_high_frequency_balance() {
    const double soft_presence_ratio = render_note_hf_ratio_with_presence_cc(100, 16);
    const double bright_presence_ratio = render_note_hf_ratio_with_presence_cc(100, 118);
    expect_true(
        bright_presence_ratio > (soft_presence_ratio * 1.005),
        "presence CC74 should increase high-frequency ratio"
    );
}

void test_large_region_promotes_to_disk_backed_storage() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "vb_engine_test_streaming";
    std::filesystem::create_directories(root);
    const std::filesystem::path long_wav = root / "long.wav";
    const std::filesystem::path sfz = root / "long.sfz";

    write_impulse_wav(long_wav, 0.8F, 20000, 240000);
    {
        std::ofstream sfz_out(sfz);
        sfz_out << "<group> lokey=60 hikey=60 pitch_keycenter=60\n";
        sfz_out << "<region> sample=long.wav lovel=1 hivel=127\n";
    }

    vb::PianoSampleLibrary library{};
    library.configure_streaming(true, 4096);
    expect_true(library.load_sfz(sfz.string()), "large sfz should load for streaming promotion test");

    const auto layers = library.select_layers(60, 100, false);
    expect_true(layers.primary != nullptr, "large region should be selectable");
    expect_true(layers.primary != nullptr && layers.primary->mapped_samples != nullptr, "large region should be disk-backed mapped");
    std::filesystem::remove_all(root);
}

void test_velocity_boundary_timbre_transition_is_smooth() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "vb_engine_test_timbre_boundary";
    std::filesystem::create_directories(root);
    const std::filesystem::path low_wav = root / "low.wav";
    const std::filesystem::path high_wav = root / "high.wav";
    const std::filesystem::path sfz = root / "boundary.sfz";

    write_test_wav(low_wav, 0.42F);
    write_impulse_wav(high_wav, 0.85F, 4, 4800);

    {
        std::ofstream sfz_out(sfz);
        sfz_out << "<group> lokey=60 hikey=60 pitch_keycenter=60\n";
        sfz_out << "<region> sample=low.wav lovel=1 hivel=63\n";
        sfz_out << "<region> sample=high.wav lovel=64 hivel=127\n";
    }

    const double ratio_63 = render_note_hf_ratio(sfz, 63);
    const double ratio_64 = render_note_hf_ratio(sfz, 64);
    const double smoothness = ratio_64 / (ratio_63 + 1.0e-12);

    expect_true(
        smoothness < 1.55,
        "velocity boundary should not produce abrupt timbre jump between 63 and 64"
    );

    std::filesystem::remove_all(root);
}

void test_pedal_edge_mechanics_emit_energy_without_spikes() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "vb_engine_test_pedal_edges";
    std::filesystem::create_directories(root);
    const std::filesystem::path pedal_down_wav = root / "pedalD1.wav";
    const std::filesystem::path pedal_up_wav = root / "pedalU1.wav";
    const std::filesystem::path sfz = root / "pedal.sfz";

    write_impulse_wav(pedal_down_wav, 0.9F, 2, 2400);
    write_impulse_wav(pedal_up_wav, 0.9F, 3, 2400);

    {
        std::ofstream sfz_out(sfz);
        sfz_out << "<group> lokey=60 hikey=60 pitch_keycenter=60\n";
        sfz_out << "<region> sample=pedalD1.wav lovel=1 hivel=127\n";
        sfz_out << "<region> sample=pedalU1.wav lovel=1 hivel=127\n";
    }

    const PedalEdgeMetrics metrics = render_pedal_edge_metrics(sfz);
    expect_true(metrics.energy > 0.01, "pedal edge mechanics should produce audible energy");
    expect_true(metrics.max_jump < 0.30, "pedal edge mechanics should avoid abrupt crackle-like jumps");
    std::filesystem::remove_all(root);
}

}  // namespace

int main() {
    test_create_destroy();
    test_create_with_legacy_config_size();
    test_note_generates_audio();
    test_queue_backpressure();
    test_sustain_pedal_behavior();
    test_default_instrument_selection();
    test_transition_spike_bound();
    test_dense_polyphony_headroom();
    test_twenty_note_pedal_low_register_glitch_bound();
    test_mid_register_attack_brightness_decay();
    test_stereo_output_has_keyboard_spatial_width();
    test_sfz_velocity_layers_load_and_respond();
    test_stereo_sample_region_preserves_channel_asymmetry();
    test_sample_library_loads_flac_sfz_region();
    test_sfz_offset_reduces_impulse_start_energy();
    test_mic_mix_control_influences_layer_balance();
    test_sfz_round_robin_lorand_distribution();
    test_sfz_cc64_pedal_conditioned_region_selection();
    test_binary_pedal_mode_threshold_behavior();
    test_continuous_pedal_half_damper_behavior();
    test_auto_pedal_mode_preserves_binary_0_64_behavior();
    test_dynamics_preserved_without_heavy_compression_character();
    test_soft_pedal_reduces_attack_energy();
    test_presence_cc_modulates_high_frequency_balance();
    test_large_region_promotes_to_disk_backed_storage();
    test_velocity_boundary_timbre_transition_is_smooth();
    test_pedal_edge_mechanics_emit_energy_without_spikes();

    if (g_failures == 0) {
        std::cout << "All tests passed\n";
        return 0;
    }

    std::cerr << g_failures << " test(s) failed\n";
    return 1;
}
