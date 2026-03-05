#pragma once

#include <array>
#include <cmath>
#include <cstddef>

namespace vb {

class FastSinTable {
public:
    static constexpr std::size_t kSize = 32768;
    static constexpr std::size_t kMask = kSize - 1u;
    static constexpr float kPi = 3.14159265358979323846F;
    static constexpr float kTwoPi = 2.0F * kPi;
    static constexpr float kScale = static_cast<float>(kSize) / kTwoPi;

    static float sample_normalized(const float phase) noexcept {
        const auto& t = table();
        const float table_pos = phase * kScale;
        const auto idx = static_cast<std::size_t>(table_pos);
        const float frac = table_pos - static_cast<float>(idx);
        const std::size_t i0 = idx & kMask;
        const std::size_t idx_next = (i0 + 1u) & kMask;

        const float a = t[i0];
        const float b = t[idx_next];
        return a + ((b - a) * frac);
    }

private:
    static const std::array<float, kSize>& table() noexcept {
        static const std::array<float, kSize> kTable = [] {
            std::array<float, kSize> values{};
            for (std::size_t i = 0; i < kSize; ++i) {
                const float angle = (static_cast<float>(i) / static_cast<float>(kSize)) * kTwoPi;
                values[i] = std::sin(angle);
            }
            return values;
        }();
        return kTable;
    }
};

}  // namespace vb
