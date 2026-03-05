#pragma once

#include <cstdint>

namespace vb {

enum class InstrumentType : std::uint8_t {
    AcousticGrandPiano = 0,
    CleanElectricGuitar = 1
};

}  // namespace vb
