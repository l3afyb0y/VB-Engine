#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <vector>

#include "vb_engine/c_api.h"

namespace {

constexpr std::uint32_t kSampleRate = 48000;
constexpr std::uint32_t kChannels = 2;
constexpr std::uint32_t kBlockSize = 64;
constexpr std::size_t kBlocks = 6000;
constexpr std::array<std::uint8_t, 20> kBurstNotes{
    24, 27, 31, 34, 36, 39, 43, 46, 48, 51, 55, 58, 60, 63, 67, 70, 72, 75, 79, 82
};

double percentile(std::vector<double> values, const double p) {
    if (values.empty()) {
        return 0.0;
    }
    const std::size_t idx = static_cast<std::size_t>(std::clamp(p, 0.0, 1.0) * static_cast<double>(values.size() - 1));
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(idx), values.end());
    return values[idx];
}

}  // namespace

int main() {
    vb_engine_config config{};
    vb_engine_default_config(&config);
    config.sample_rate = static_cast<double>(kSampleRate);
    config.max_block_size = kBlockSize;
    std::uint32_t max_voices = 224;
    if (const char* env_voices = std::getenv("VB_MAX_VOICES"); env_voices != nullptr && env_voices[0] != '\0') {
        const long parsed = std::strtol(env_voices, nullptr, 10);
        if (parsed > 0) {
            max_voices = static_cast<std::uint32_t>(parsed);
        }
    }
    config.max_voices = max_voices;
    config.default_instrument = VB_INSTRUMENT_ACOUSTIC_GRAND_PIANO;
    config.piano_humanize_timing_ms = 0.0F;
    config.piano_humanize_velocity = 0.0F;

    vb_engine_handle* handle = nullptr;
    if (vb_engine_create(&config, &handle) != VB_ENGINE_OK || handle == nullptr) {
        std::cerr << "Failed to create engine\n";
        return 1;
    }
    (void)vb_engine_reset_diagnostics(handle);

    float left[kBlockSize]{};
    float right[kBlockSize]{};
    float* outputs[kChannels]{left, right};

    std::vector<double> block_ms;
    block_ms.reserve(kBlocks);
    constexpr double kBlockBudgetMs = (static_cast<double>(kBlockSize) / static_cast<double>(kSampleRate)) * 1000.0;
    std::size_t over_budget_blocks = 0;
    double checksum = 0.0;

    for (std::size_t block = 0; block < kBlocks; ++block) {
        if ((block % 12) == 0) {
            (void)vb_engine_control_change(handle, 0, 64, 127);
            for (const std::uint8_t note : kBurstNotes) {
                (void)vb_engine_note_on(handle, 0, note, 98);
            }
        }
        if ((block % 12) == 6) {
            for (const std::uint8_t note : kBurstNotes) {
                (void)vb_engine_note_off(handle, 0, note, 0);
            }
            (void)vb_engine_control_change(handle, 0, 64, 0);
        }

        const auto t0 = std::chrono::steady_clock::now();
        if (vb_engine_process(handle, outputs, kChannels, kBlockSize) != VB_ENGINE_OK) {
            std::cerr << "process failed\n";
            vb_engine_destroy(handle);
            return 1;
        }
        const auto t1 = std::chrono::steady_clock::now();
        const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        block_ms.push_back(ms);
        if (ms > kBlockBudgetMs) {
            ++over_budget_blocks;
        }

        checksum += std::abs(static_cast<double>(left[block % kBlockSize]));
    }

    double max_ms = 0.0;
    for (const double ms : block_ms) {
        max_ms = std::max(max_ms, ms);
    }

    const double p95 = percentile(block_ms, 0.95);
    const double p99 = percentile(block_ms, 0.99);
    const double p999 = percentile(block_ms, 0.999);

    std::cout << std::fixed << std::setprecision(4);
    std::cout << "Onset stress benchmark (64f @ 48k)\n";
    std::cout << "block_budget_ms=" << kBlockBudgetMs << "\n";
    std::cout << "max_block_ms=" << max_ms << "\n";
    std::cout << "p95_block_ms=" << p95 << "\n";
    std::cout << "p99_block_ms=" << p99 << "\n";
    std::cout << "p99.9_block_ms=" << p999 << "\n";
    std::cout << "over_budget_blocks=" << over_budget_blocks << "/" << kBlocks << "\n";
    std::cout << "checksum=" << checksum << "\n";
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
