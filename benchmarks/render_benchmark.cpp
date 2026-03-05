#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <iostream>

#include "vb_engine/c_api.h"

int main() {
    constexpr uint32_t block_size = 128;
    constexpr uint32_t sample_rate = 48000;
    constexpr uint32_t channels = 2;
    constexpr int blocks = 10000;

    vb_engine_config config{};
    vb_engine_default_config(&config);
    config.max_block_size = block_size;
    config.sample_rate = static_cast<double>(sample_rate);
    std::uint32_t max_voices = 128;
    if (const char* env_voices = std::getenv("VB_MAX_VOICES"); env_voices != nullptr && env_voices[0] != '\0') {
        const long parsed = std::strtol(env_voices, nullptr, 10);
        if (parsed > 0) {
            max_voices = static_cast<std::uint32_t>(parsed);
        }
    }
    config.max_voices = max_voices;

    vb_engine_handle* handle = nullptr;
    if (vb_engine_create(&config, &handle) != VB_ENGINE_OK) {
        std::cerr << "Failed to create engine\n";
        return 1;
    }
    (void)vb_engine_reset_diagnostics(handle);

    for (uint8_t note = 48; note < 72; ++note) {
        (void)vb_engine_note_on(handle, 0, note, 90);
    }

    float left[block_size]{};
    float right[block_size]{};
    float* outputs[channels]{left, right};

    const auto start = std::chrono::steady_clock::now();

    double checksum = 0.0;
    for (int i = 0; i < blocks; ++i) {
        if (vb_engine_process(handle, outputs, channels, block_size) != VB_ENGINE_OK) {
            std::cerr << "process failed\n";
            vb_engine_destroy(handle);
            return 1;
        }
        checksum += std::abs(left[i % block_size]);
    }

    const auto end = std::chrono::steady_clock::now();
    const std::chrono::duration<double> elapsed = end - start;

    const double rendered_seconds = static_cast<double>(blocks * block_size) / static_cast<double>(sample_rate);
    const double realtime_factor = rendered_seconds / elapsed.count();

    std::cout << "Rendered " << rendered_seconds << " s audio in " << elapsed.count() << " s\n";
    std::cout << "Realtime factor: x" << realtime_factor << "\n";
    std::cout << "Checksum: " << checksum << "\n";
    vb_engine_diagnostics diagnostics{};
    diagnostics.struct_size = sizeof(vb_engine_diagnostics);
    if (vb_engine_get_diagnostics(handle, &diagnostics) == VB_ENGINE_OK) {
        std::cout << "voices=" << max_voices << "\n";
        std::cout << "voice_steals=" << diagnostics.voice_steals << "\n";
        std::cout << "worst_stolen_activity=" << diagnostics.worst_stolen_activity << "\n";
        std::cout << "max_output_delta=" << diagnostics.max_output_delta << "\n";
        std::cout << "hard_jump_events=" << diagnostics.hard_jump_events << "\n";
        std::cout << "non_finite_output_samples=" << diagnostics.non_finite_output_samples << "\n";
    }

    vb_engine_destroy(handle);
    return 0;
}
