#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>
#include <string>

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

std::size_t read_rss_kb_linux() {
#ifdef __linux__
    std::ifstream in("/proc/self/status");
    if (!in.is_open()) {
        return 0;
    }

    std::string key;
    while (in >> key) {
        if (key == "VmRSS:") {
            std::size_t value = 0;
            std::string unit;
            in >> value >> unit;
            return value;
        }
        std::string rest;
        std::getline(in, rest);
    }
#endif
    return 0;
}

vb_engine_handle* make_engine(const uint32_t block_size, const uint32_t voices) {
    vb_engine_config config{};
    vb_engine_default_config(&config);
    config.max_block_size = block_size;
    config.max_voices = voices;
    config.default_instrument = VB_INSTRUMENT_ACOUSTIC_GRAND_PIANO;

    vb_engine_handle* handle = nullptr;
    const auto result = vb_engine_create(&config, &handle);
    expect_true(result == VB_ENGINE_OK, "engine creation should succeed");
    return handle;
}

void test_randomized_long_running_stability() {
    constexpr uint32_t kBlockSize = 256;
    constexpr uint32_t kBlocks = 320;

    vb_engine_handle* handle = make_engine(kBlockSize, 96);
    if (handle == nullptr) {
        return;
    }

    std::mt19937 rng(0xC0FFEEu);
    std::uniform_int_distribution<int> note_dist(21, 108);
    std::uniform_int_distribution<int> velocity_dist(20, 122);
    std::uniform_int_distribution<int> event_dist(0, 99);

    float left[kBlockSize]{};
    float right[kBlockSize]{};
    float* outputs[2]{left, right};

    float previous = 0.0F;
    double max_abs = 0.0;
    double max_jump = 0.0;

    for (uint32_t block = 0; block < kBlocks; ++block) {
        const int roll = event_dist(rng);
        if (roll < 12) {
            (void)vb_engine_note_on(
                handle,
                0,
                static_cast<uint8_t>(note_dist(rng)),
                static_cast<uint8_t>(velocity_dist(rng))
            );
        } else if (roll < 20) {
            (void)vb_engine_note_off(handle, 0, static_cast<uint8_t>(note_dist(rng)), 0);
        } else if (roll < 23) {
            const uint8_t pedal = (roll % 2 == 0) ? 127 : 0;
            (void)vb_engine_control_change(handle, 0, 64, pedal);
        }

        const auto result = vb_engine_process(handle, outputs, 2, kBlockSize);
        expect_true(result == VB_ENGINE_OK, "process should succeed in soak test");

        for (uint32_t i = 0; i < kBlockSize; ++i) {
            const float s = left[i];
            expect_true(std::isfinite(s), "sample must be finite");

            max_abs = std::max(max_abs, static_cast<double>(std::abs(s)));
            const double jump = std::abs(static_cast<double>(s - previous));
            max_jump = std::max(max_jump, jump);
            previous = s;
        }
    }

    // Hard guard against clipping and obvious glitch spikes.
    std::cout << "soak_metrics max_abs=" << max_abs << " max_jump=" << max_jump << "\n";
    expect_true(max_abs < 0.992, "max absolute sample should remain below clipping margin");
    expect_true(max_jump < 0.94, "sample-to-sample jump should stay below glitch threshold");

    vb_engine_destroy(handle);
}

void test_repeated_create_destroy_cycles() {
    constexpr uint32_t kBlockSize = 128;
    constexpr int kCycles = 100;

    float left[kBlockSize]{};
    float right[kBlockSize]{};
    float* outputs[2]{left, right};

    for (int cycle = 0; cycle < kCycles; ++cycle) {
        vb_engine_handle* handle = make_engine(kBlockSize, 64);
        if (handle == nullptr) {
            return;
        }

        for (int i = 0; i < 12; ++i) {
            const uint8_t note = static_cast<uint8_t>(48 + ((cycle + i) % 24));
            (void)vb_engine_note_on(handle, 0, note, 80);
            (void)vb_engine_note_off(handle, 0, note, 0);
            expect_true(vb_engine_process(handle, outputs, 2, kBlockSize) == VB_ENGINE_OK, "process in cycle test");
        }

        vb_engine_destroy(handle);
    }
}

void test_memory_growth_bound() {
#ifdef __linux__
    constexpr uint32_t kBlockSize = 256;

    const std::size_t rss_before = read_rss_kb_linux();
    vb_engine_handle* handle = make_engine(kBlockSize, 128);
    if (handle == nullptr) {
        return;
    }

    std::mt19937 rng(0xBAD5EEDu);
    std::uniform_int_distribution<int> note_dist(21, 108);

    float left[kBlockSize]{};
    float right[kBlockSize]{};
    float* outputs[2]{left, right};

    for (int i = 0; i < 600; ++i) {
        const uint8_t note = static_cast<uint8_t>(note_dist(rng));
        (void)vb_engine_note_on(handle, 0, note, 90);
        if ((i % 3) == 0) {
            (void)vb_engine_note_off(handle, 0, note, 0);
        }
        if ((i % 120) == 0) {
            (void)vb_engine_control_change(handle, 0, 64, (i % 240 == 0) ? 127 : 0);
        }

        expect_true(vb_engine_process(handle, outputs, 2, kBlockSize) == VB_ENGINE_OK, "process in memory test");
    }

    vb_engine_destroy(handle);

    const std::size_t rss_after = read_rss_kb_linux();
    if (rss_before > 0 && rss_after > 0) {
        const std::size_t growth_kb = (rss_after > rss_before) ? (rss_after - rss_before) : 0;
        expect_true(growth_kb < 65536, "RSS growth should stay under 64MB in stress run");
    }
#endif
}

}  // namespace

int main() {
    test_randomized_long_running_stability();
    test_repeated_create_destroy_cycles();
    test_memory_growth_bound();

    if (g_failures == 0) {
        std::cout << "All soak tests passed\n";
        return 0;
    }

    std::cerr << g_failures << " soak test(s) failed\n";
    return 1;
}
