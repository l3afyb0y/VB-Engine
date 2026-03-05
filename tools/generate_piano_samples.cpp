#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "vb_engine/c_api.h"

namespace {

constexpr uint32_t kSampleRate = 48000;
constexpr uint16_t kChannels = 2;
constexpr uint32_t kBlockSize = 128;

struct Event {
    enum class Type {
        NoteOn,
        NoteOff,
        ControlChange
    };

    uint64_t frame;
    Type type;
    uint8_t channel;
    uint8_t data1;
    uint8_t data2;
};

struct SongScenario {
    std::string file_name;
    double duration_sec;
    std::vector<Event> events;
};

uint64_t sec_to_frame(const double sec) {
    return static_cast<uint64_t>(sec * static_cast<double>(kSampleRate));
}

double beat_to_sec(const double beat, const double bpm) {
    return beat * (60.0 / bpm);
}

void write_wav(const std::string& path, const std::vector<int16_t>& interleaved) {
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        throw std::runtime_error("failed to open output file: " + path);
    }

    const uint32_t data_size = static_cast<uint32_t>(interleaved.size() * sizeof(int16_t));
    const uint32_t riff_size = 36 + data_size;
    const uint16_t audio_format = 1;
    const uint16_t bits_per_sample = 16;
    const uint32_t byte_rate = kSampleRate * kChannels * (bits_per_sample / 8);
    const uint16_t block_align = kChannels * (bits_per_sample / 8);

    out.write("RIFF", 4);
    out.write(reinterpret_cast<const char*>(&riff_size), sizeof(riff_size));
    out.write("WAVE", 4);

    out.write("fmt ", 4);
    const uint32_t fmt_chunk_size = 16;
    out.write(reinterpret_cast<const char*>(&fmt_chunk_size), sizeof(fmt_chunk_size));
    out.write(reinterpret_cast<const char*>(&audio_format), sizeof(audio_format));
    out.write(reinterpret_cast<const char*>(&kChannels), sizeof(kChannels));
    out.write(reinterpret_cast<const char*>(&kSampleRate), sizeof(kSampleRate));
    out.write(reinterpret_cast<const char*>(&byte_rate), sizeof(byte_rate));
    out.write(reinterpret_cast<const char*>(&block_align), sizeof(block_align));
    out.write(reinterpret_cast<const char*>(&bits_per_sample), sizeof(bits_per_sample));

    out.write("data", 4);
    out.write(reinterpret_cast<const char*>(&data_size), sizeof(data_size));
    out.write(reinterpret_cast<const char*>(interleaved.data()), static_cast<std::streamsize>(data_size));
}

void add_note(std::vector<Event>& events, const double on_sec, const double off_sec, const uint8_t note, const uint8_t velocity) {
    events.push_back(Event{sec_to_frame(on_sec), Event::Type::NoteOn, 0, note, velocity});
    events.push_back(Event{sec_to_frame(off_sec), Event::Type::NoteOff, 0, note, 0});
}

void add_cc(std::vector<Event>& events, const double sec, const uint8_t cc, const uint8_t value) {
    events.push_back(Event{sec_to_frame(sec), Event::Type::ControlChange, 0, cc, value});
}

float humanize_noise(const double seed) {
    const double x = std::sin((seed * 12.9898) + 78.233) * 43758.5453123;
    const double fract = x - std::floor(x);
    return static_cast<float>((fract * 2.0) - 1.0);
}

void add_note_beats(
    std::vector<Event>& events,
    const double bpm,
    const double start_beat,
    const double duration_beats,
    const int note,
    const int velocity,
    const double gate = 0.92
) {
    if (note < 0) {
        return;
    }

    // De-quantize generated demo performances slightly so rendered previews sound less robotic.
    const double on_base = beat_to_sec(start_beat, bpm);
    const double note_seed = (start_beat * 0.731) + (static_cast<double>(note) * 0.173);
    const double timing_jitter = static_cast<double>(humanize_noise(note_seed)) * 0.0026;  // +/-2.6 ms
    const double on = std::max(0.0, on_base + timing_jitter);

    const double nominal_duration = beat_to_sec(duration_beats * gate, bpm);
    const double duration_shape = 1.0 + (static_cast<double>(humanize_noise(note_seed + 0.51)) * 0.06);
    const double duration = std::max(0.03, nominal_duration * duration_shape);
    const double off = on + duration;

    const int velocity_jitter = static_cast<int>(std::lround(humanize_noise(note_seed + 1.17) * 4.0F));
    const int velocity_humanized = std::clamp(velocity + velocity_jitter, 1, 127);
    add_note(events, on, off, static_cast<uint8_t>(note), static_cast<uint8_t>(velocity_humanized));
}

void add_chord_beats(
    std::vector<Event>& events,
    const double bpm,
    const double start_beat,
    const double duration_beats,
    const std::vector<int>& notes,
    const int velocity,
    const double gate = 0.95
) {
    for (const int note : notes) {
        add_note_beats(events, bpm, start_beat, duration_beats, note, velocity, gate);
    }
}

void render_scenario(const SongScenario& scenario, const float target_peak) {
    auto events = scenario.events;

    std::sort(events.begin(), events.end(), [](const Event& a, const Event& b) {
        if (a.frame == b.frame) {
            return static_cast<int>(a.type) < static_cast<int>(b.type);
        }
        return a.frame < b.frame;
    });

    vb_engine_config config{};
    vb_engine_default_config(&config);
    config.sample_rate = static_cast<double>(kSampleRate);
    config.max_block_size = kBlockSize;
    std::uint32_t max_voices = 192;
    if (const char* env_voices = std::getenv("VB_MAX_VOICES"); env_voices != nullptr && env_voices[0] != '\0') {
        const long parsed = std::strtol(env_voices, nullptr, 10);
        if (parsed > 0) {
            max_voices = static_cast<std::uint32_t>(parsed);
        }
    }
    config.max_voices = max_voices;
    config.default_instrument = VB_INSTRUMENT_ACOUSTIC_GRAND_PIANO;
    config.piano_humanize_timing_ms = 0.9F;
    config.piano_humanize_velocity = 1.2F;
    config.piano_reverb_wet = 0.08F;
    config.piano_mic_mix = 0.28F;
    config.piano_presence = 0.56F;
    config.piano_stretch_strength = 0.96F;

    std::string sfz_path_storage;
    if (const char* env_sfz = std::getenv("VB_PIANO_SFZ_PATH"); env_sfz != nullptr && env_sfz[0] != '\0') {
        sfz_path_storage = env_sfz;
    } else if (std::filesystem::exists("Samples/Piano-Library/default.sfz")) {
        sfz_path_storage = "Samples/Piano-Library/default.sfz";
    }
    if (!sfz_path_storage.empty()) {
        config.piano_sfz_path = sfz_path_storage.c_str();
    }

    vb_engine_handle* handle = nullptr;
    if (vb_engine_create(&config, &handle) != VB_ENGINE_OK) {
        throw std::runtime_error("vb_engine_create failed");
    }
    (void)vb_engine_reset_diagnostics(handle);

    const uint64_t total_frames = sec_to_frame(scenario.duration_sec);
    std::vector<float> left_float;
    std::vector<float> right_float;
    left_float.reserve(static_cast<std::size_t>(total_frames));
    right_float.reserve(static_cast<std::size_t>(total_frames));

    std::size_t event_index = 0;

    uint64_t cursor = 0;
    while (cursor < total_frames) {
        while (event_index < events.size() && events[event_index].frame <= cursor) {
            const Event& event = events[event_index];
            switch (event.type) {
                case Event::Type::NoteOn:
                    (void)vb_engine_note_on(handle, event.channel, event.data1, event.data2);
                    break;
                case Event::Type::NoteOff:
                    (void)vb_engine_note_off(handle, event.channel, event.data1, event.data2);
                    break;
                case Event::Type::ControlChange:
                    (void)vb_engine_control_change(handle, event.channel, event.data1, event.data2);
                    break;
            }
            ++event_index;
        }

        uint64_t chunk_end = std::min<uint64_t>(cursor + kBlockSize, total_frames);
        if (event_index < events.size() && events[event_index].frame < chunk_end) {
            chunk_end = events[event_index].frame;
        }
        if (chunk_end <= cursor) {
            chunk_end = std::min<uint64_t>(cursor + 1, total_frames);
        }

        const uint32_t frames_this_block = static_cast<uint32_t>(chunk_end - cursor);

        float left[kBlockSize]{};
        float right[kBlockSize]{};
        float* outputs[2]{left, right};

        if (vb_engine_process(handle, outputs, kChannels, frames_this_block) != VB_ENGINE_OK) {
            vb_engine_destroy(handle);
            throw std::runtime_error("vb_engine_process failed");
        }

        for (uint32_t i = 0; i < frames_this_block; ++i) {
            left_float.push_back(left[i]);
            right_float.push_back(right[i]);
        }

        cursor = chunk_end;
    }

    vb_engine_diagnostics diagnostics{};
    diagnostics.struct_size = sizeof(vb_engine_diagnostics);
    if (vb_engine_get_diagnostics(handle, &diagnostics) == VB_ENGINE_OK) {
        std::cout << "diagnostics " << scenario.file_name
                  << " voices=" << max_voices
                  << " steals=" << diagnostics.voice_steals
                  << " worst_steal_activity=" << diagnostics.worst_stolen_activity
                  << " max_output_delta=" << diagnostics.max_output_delta
                  << " hard_jump_events=" << diagnostics.hard_jump_events
                  << " non_finite_output_samples=" << diagnostics.non_finite_output_samples
                  << "\n";
    }
    vb_engine_destroy(handle);

    double peak = 1e-12;
    for (std::size_t i = 0; i < left_float.size(); ++i) {
        peak = std::max(peak, static_cast<double>(std::abs(left_float[i])));
        peak = std::max(peak, static_cast<double>(std::abs(right_float[i])));
    }

    const float gain = static_cast<float>(target_peak / peak);

    // Tiny fade-in/fade-out to avoid edge clicks in rendered files.
    const std::size_t fade_samples = static_cast<std::size_t>(kSampleRate * 0.02);
    std::uint32_t dither_state = 2166136261u;
    for (const char c : scenario.file_name) {
        dither_state ^= static_cast<std::uint8_t>(c);
        dither_state *= 16777619u;
    }
    if (dither_state == 0u) {
        dither_state = 0x9E3779B9u;
    }
    auto next_uniform = [&dither_state]() -> float {
        dither_state ^= dither_state << 13;
        dither_state ^= dither_state >> 17;
        dither_state ^= dither_state << 5;
        return static_cast<float>(dither_state & 0x00FFFFFFu) * (1.0F / 16777215.0F);
    };

    std::vector<int16_t> pcm;
    pcm.reserve(left_float.size() * 2);

    for (std::size_t i = 0; i < left_float.size(); ++i) {
        float env = 1.0F;
        if (i < fade_samples) {
            env = static_cast<float>(i) / static_cast<float>(fade_samples);
        } else if (i + fade_samples > left_float.size()) {
            env = static_cast<float>(left_float.size() - i) / static_cast<float>(fade_samples);
        }

        constexpr float kDitherAmplitude = 0.25F / 32768.0F;
        const float dither_l = (next_uniform() - next_uniform()) * kDitherAmplitude;
        const float dither_r = (next_uniform() - next_uniform()) * kDitherAmplitude;
        const float l = std::clamp((left_float[i] * gain * env) + dither_l, -1.0F, 1.0F);
        const float r = std::clamp((right_float[i] * gain * env) + dither_r, -1.0F, 1.0F);
        pcm.push_back(static_cast<int16_t>(l * 32767.0F));
        pcm.push_back(static_cast<int16_t>(r * 32767.0F));
    }

    write_wav("Samples/" + scenario.file_name, pcm);
}

SongScenario fur_elise_excerpt() {
    SongScenario s{"Piano-Fur-Elise-Excerpt.wav", 24.0, {}};
    const double bpm = 88.0;

    struct Step { int note; double beats; int velocity; };
    const std::vector<Step> melody{
        {76, 0.5, 78}, {75, 0.5, 74}, {76, 0.5, 79}, {75, 0.5, 75},
        {76, 0.5, 82}, {71, 0.5, 72}, {74, 0.5, 74}, {72, 0.5, 73},
        {69, 1.0, 86}, {-1, 0.5, 0},
        {60, 0.5, 70}, {64, 0.5, 72}, {69, 0.5, 80}, {71, 1.0, 82},
        {-1, 0.5, 0},
        {64, 0.5, 72}, {68, 0.5, 74}, {71, 0.5, 80}, {72, 1.0, 82},
        {-1, 0.5, 0}
    };

    double t = 0.0;
    for (int rep = 0; rep < 2; ++rep) {
        for (const auto& step : melody) {
            add_note_beats(s.events, bpm, t, step.beats, step.note, step.velocity, 0.88);
            t += step.beats;
        }
    }

    for (double beat = 0.0; beat < t + 2.0; beat += 2.0) {
        add_chord_beats(s.events, bpm, beat, 1.8, {45, 52, 57}, 58, 0.92);
        add_chord_beats(s.events, bpm, beat + 1.0, 0.8, {52, 57, 64}, 54, 0.88);
    }

    s.duration_sec = beat_to_sec(t + 3.0, bpm);
    return s;
}

SongScenario moonlight_excerpt() {
    SongScenario s{"Piano-Moonlight-Sonata-Excerpt.wav", 24.0, {}};
    const double bpm = 52.0;

    add_cc(s.events, 0.04, 64, 127);

    struct BarPattern {
        std::vector<int> triplets;
        std::vector<int> melody_notes;
    };

    const std::vector<BarPattern> opening{
        {{49, 56, 61, 56, 61, 64, 56, 61, 64, 56, 61, 64}, {68}},
        {{49, 56, 61, 56, 61, 64, 56, 61, 64, 56, 61, 64}, {68}},
        {{44, 51, 56, 51, 56, 59, 51, 56, 59, 51, 56, 59}, {66}},
        {{46, 53, 58, 53, 58, 61, 53, 58, 61, 53, 58, 61}, {64}}
    };

    double beat = 0.0;
    for (const auto& bar : opening) {
        for (std::size_t i = 0; i < bar.triplets.size(); ++i) {
            const double pos = beat + (static_cast<double>(i) / 3.0);
            add_note_beats(s.events, bpm, pos, 0.31, bar.triplets[i], 50 + static_cast<int>(i % 2), 0.97);
        }
        for (std::size_t i = 0; i < bar.melody_notes.size(); ++i) {
            add_note_beats(
                s.events,
                bpm,
                beat + 1.25 + (static_cast<double>(i) * 0.75),
                1.45,
                bar.melody_notes[i],
                62,
                0.98
            );
        }
        beat += 4.0;
    }

    add_cc(s.events, beat_to_sec(beat + 0.5, bpm), 64, 0);
    s.duration_sec = beat_to_sec(beat + 2.5, bpm);
    return s;
}

SongScenario turkish_march_excerpt() {
    SongScenario s{"Piano-Turkish-March-Excerpt.wav", 17.0, {}};
    const double bpm = 126.0;

    double beat = 0.0;
    const std::vector<int> motif{69, 71, 72, 74, 76, 74, 72, 71};

    for (int rep = 0; rep < 10; ++rep) {
        for (std::size_t i = 0; i < motif.size(); ++i) {
            add_note_beats(s.events, bpm, beat, 0.25, motif[i], 86 + static_cast<int>(i % 2), 0.70);
            beat += 0.25;
        }

        add_note_beats(s.events, bpm, beat, 0.5, 72, 88, 0.78);
        beat += 0.5;
        add_note_beats(s.events, bpm, beat, 0.5, 69, 84, 0.78);
        beat += 0.5;
    }

    for (double b = 0.0; b < beat + 1.0; b += 1.0) {
        add_note_beats(s.events, bpm, b, 0.45, 45, 52, 0.84);
        add_note_beats(s.events, bpm, b + 0.5, 0.45, 52, 50, 0.84);
    }

    s.duration_sec = beat_to_sec(beat + 2.0, bpm);
    return s;
}

SongScenario clair_de_lune_excerpt() {
    SongScenario s{"Piano-Clair-de-Lune-Excerpt.wav", 24.0, {}};
    const double bpm = 66.0;

    add_cc(s.events, 0.04, 64, 127);

    double beat = 0.0;
    const std::vector<std::vector<int>> chords{
        {61, 65, 68, 73}, {58, 61, 65, 70}, {56, 61, 65, 68}, {54, 58, 61, 66}
    };

    for (int bar = 0; bar < 6; ++bar) {
        const auto& chord = chords[bar % static_cast<int>(chords.size())];
        for (int step = 0; step < 8; ++step) {
            const int n = chord[step % static_cast<int>(chord.size())];
            add_note_beats(s.events, bpm, beat + (step * 0.5), 0.48, n, 52 + (step % 2), 0.95);
        }

        add_note_beats(s.events, bpm, beat + 1.5, 1.2, chord.back() + 7, 66, 0.97);
        beat += 4.0;
    }

    add_cc(s.events, beat_to_sec(beat + 0.2, bpm), 64, 0);
    s.duration_sec = beat_to_sec(beat + 2.0, bpm);
    return s;
}

SongScenario chopin_ballade_excerpt() {
    SongScenario s{"Piano-Chopin-Ballade-No1-Excerpt.wav", 20.0, {}};
    const double bpm = 72.0;

    double beat = 0.0;
    const std::vector<std::vector<int>> dramatic{
        {43, 50, 55, 62}, {39, 46, 51, 58}, {44, 51, 56, 63}, {41, 48, 53, 60}
    };

    for (int i = 0; i < 8; ++i) {
        const auto& chord = dramatic[i % static_cast<int>(dramatic.size())];
        const int vel = 64 + (i % 6) * 5;

        add_chord_beats(s.events, bpm, beat, 1.6, chord, vel, 0.96);
        add_note_beats(s.events, bpm, beat + 0.4, 0.8, chord.back() + 5, vel + 6, 0.92);
        beat += 2.0;
    }

    add_cc(s.events, beat_to_sec(0.2, bpm), 64, 127);
    add_cc(s.events, beat_to_sec(beat - 0.8, bpm), 64, 0);

    s.duration_sec = beat_to_sec(beat + 2.0, bpm);
    return s;
}

SongScenario creep_transposed_excerpt() {
    SongScenario s{"Piano-Creep-Transposed-Excerpt.wav", 18.0, {}};
    const double bpm = 92.0;

    const std::vector<std::vector<int>> progression{
        {48, 55, 60, 64},  // C
        {52, 59, 64, 68},  // E
        {53, 60, 65, 69},  // F
        {53, 60, 65, 68}   // Fm
    };

    double beat = 0.0;

    // Lyric opening lead-in phrase: "when you were here before"
    // requested melody notes: G A G F# G.
    const std::vector<int> lyric_opening_notes{
        67,  // G4
        69,  // A4
        67,  // G4
        66,  // F#4
        67   // G4
    };
    const std::vector<double> lyric_opening_beats{
        0.50, 1.20, 1.90, 2.60, 3.30
    };
    for (std::size_t i = 0; i < lyric_opening_notes.size(); ++i) {
        add_note_beats(s.events, bpm, lyric_opening_beats[i], 0.60, lyric_opening_notes[i], 92, 0.93);
    }

    // Support the vocal line with the standard Creep progression.
    for (int bar = 0; bar < 2; ++bar) {
        for (const auto& chord : progression) {
            add_chord_beats(s.events, bpm, beat, 1.8, chord, 74, 0.98);
            beat += 2.0;
            if (beat >= 8.0) {
                break;
            }
        }
        if (beat >= 8.0) {
            break;
        }
    }

    // Continue with the instrumental texture so this sample still showcases sustain/body.
    while (beat < 16.0) {
        for (const auto& chord : progression) {
            add_chord_beats(s.events, bpm, beat, 1.8, chord, 74, 0.98);
            add_note_beats(s.events, bpm, beat + 0.2, 0.7, chord.back() + 5, 82, 0.9);
            add_note_beats(s.events, bpm, beat + 1.0, 0.7, chord.back() + 3, 78, 0.9);
            beat += 2.0;
            if (beat >= 16.0) {
                break;
            }
        }
    }

    s.duration_sec = beat_to_sec(beat + 2.0, bpm);
    return s;
}

SongScenario bach_prelude_c_excerpt() {
    SongScenario s{"Piano-Bach-Prelude-C-Excerpt.wav", 19.0, {}};
    const double bpm = 78.0;

    const std::vector<std::vector<int>> patterns{
        {48, 52, 55, 60, 64, 67},
        {50, 53, 57, 62, 65, 69},
        {52, 55, 59, 64, 67, 71},
        {53, 57, 60, 65, 69, 72}
    };

    double beat = 0.0;
    for (int bar = 0; bar < 8; ++bar) {
        const auto& pat = patterns[bar % static_cast<int>(patterns.size())];
        for (int step = 0; step < 12; ++step) {
            const int note = pat[step % static_cast<int>(pat.size())];
            add_note_beats(s.events, bpm, beat + (step * 0.25), 0.24, note, 58 + (step % 3), 0.94);
        }
        beat += 3.0;
    }

    s.duration_sec = beat_to_sec(beat + 2.0, bpm);
    return s;
}

void generate_all() {
    std::filesystem::create_directories("Samples");
    for (const auto& entry : std::filesystem::directory_iterator("Samples")) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::string name = entry.path().filename().string();
        if (name.rfind("Piano-", 0) == 0 && entry.path().extension() == ".wav") {
            std::filesystem::remove(entry.path());
        }
    }

    const std::vector<SongScenario> songs{
        fur_elise_excerpt(),
        moonlight_excerpt(),
        turkish_march_excerpt(),
        clair_de_lune_excerpt(),
        chopin_ballade_excerpt(),
        creep_transposed_excerpt(),
        bach_prelude_c_excerpt()
    };

    for (const auto& song : songs) {
        render_scenario(song, 0.42F);
    }
}

}  // namespace

int main() {
    try {
        generate_all();
    } catch (const std::exception& ex) {
        std::cerr << "sample generation failed: " << ex.what() << "\n";
        return 1;
    }

    std::cout << "Generated piano showcase samples in ./Samples\n";
    return 0;
}
