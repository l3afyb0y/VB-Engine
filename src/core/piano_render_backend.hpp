#pragma once

#include <cstdint>

namespace vb {

enum class PianoRenderBackend : std::uint8_t {
    Auto = 0,
    CpuHybrid = 1,
    GpuFem = 2,
};

}  // namespace vb

