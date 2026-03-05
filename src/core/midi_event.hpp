#pragma once

#include <cstdint>

#include "core/instrument_type.hpp"

namespace vb {

enum class MidiEventType : std::uint8_t {
    NoteOn,
    NoteOff,
    ControlChange
};

struct MidiEvent {
    MidiEventType type;
    std::uint8_t channel;
    std::uint8_t data1;
    std::uint8_t data2;
    InstrumentType instrument;
};

}  // namespace vb
