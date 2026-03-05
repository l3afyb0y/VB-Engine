#include <cmath>
#include <cstdint>
#include <iostream>

#include "core/engine.hpp"

int main() {
    vb::EngineConfig config{};
    config.sample_rate = 48000.0F;
    config.max_block_size = 256;
    config.max_voices = 32;
    config.default_instrument = vb::InstrumentType::AcousticGrandPiano;

    vb::Engine engine(config);

    const vb::MidiEvent note_on{
        .type = vb::MidiEventType::NoteOn,
        .channel = 0,
        .data1 = 60,
        .data2 = 100,
        .instrument = vb::InstrumentType::AcousticGrandPiano,
    };
    if (!engine.enqueue_event(note_on)) {
        std::cerr << "event queue full\n";
        return 1;
    }

    float left[256]{};
    float right[256]{};
    float* outputs[2]{left, right};

    if (!engine.process(outputs, 2, 256)) {
        std::cerr << "render failed\n";
        return 1;
    }

    double energy = 0.0;
    for (float sample : left) {
        energy += std::abs(sample);
    }

    std::cout << "Rendered block energy: " << energy << "\n";
    return 0;
}
