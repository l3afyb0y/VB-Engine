#include "instruments/piano/sample_library.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#define DR_FLAC_IMPLEMENTATION
#include "third_party/dr_flac.h"

#ifdef __linux__
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace vb {

namespace {

constexpr std::uint64_t kMaxDecodedAudioBytes = 1024ULL * 1024ULL * 1024ULL;  // 1 GiB guardrail

std::string trim(const std::string_view input) {
    std::size_t begin = 0;
    std::size_t end = input.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(input[begin])) != 0) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(input[end - 1])) != 0) {
        --end;
    }
    return std::string(input.substr(begin, end - begin));
}

float db_to_linear(const float db) {
    return std::pow(10.0F, db / 20.0F);
}

bool parse_int(const std::string_view value, int& out_value) {
    const char* begin = value.data();
    const char* end = value.data() + value.size();
    auto [ptr, ec] = std::from_chars(begin, end, out_value);
    return ec == std::errc{} && ptr == end;
}

bool parse_float(const std::string_view value, float& out_value) {
    const std::string tmp(value);
    char* end_ptr = nullptr;
    const float parsed = std::strtof(tmp.c_str(), &end_ptr);
    if (end_ptr == tmp.c_str() || *end_ptr != '\0') {
        return false;
    }
    out_value = parsed;
    return true;
}

std::vector<std::string> split_tokens(const std::string& line) {
    std::vector<std::string> tokens;
    std::istringstream stream(line);
    std::string token;
    while (stream >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

}  // namespace

void PianoSampleLibrary::configure_streaming(const bool enabled, const std::size_t frame_threshold) noexcept {
    disk_streaming_enabled_ = enabled;
    disk_stream_threshold_frames_ = std::max<std::size_t>(4096, frame_threshold);
}

bool PianoSampleLibrary::load_audio_mono(const std::string& file_path, PianoSampleRegion& out_region) {
    std::string extension = std::filesystem::path(file_path).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](const unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    if (extension == ".flac") {
        return load_flac_mono(file_path, out_region);
    }

    return load_wav_mono(file_path, out_region);
}

bool PianoSampleLibrary::load_wav_mono(const std::string& file_path, PianoSampleRegion& out_region) {
    std::ifstream in(file_path, std::ios::binary);
    if (!in.is_open()) {
        return false;
    }
    std::error_code size_ec;
    const std::uint64_t file_size = std::filesystem::file_size(file_path, size_ec);
    if (size_ec || file_size == 0 || file_size > kMaxDecodedAudioBytes) {
        return false;
    }

    auto read_be16 = [](const std::uint8_t* p) -> std::uint16_t {
        return static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[0]) << 8) | static_cast<std::uint16_t>(p[1]));
    };
    auto read_be32 = [](const std::uint8_t* p) -> std::uint32_t {
        return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16)
            | (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
    };
    auto read_extended80 = [&](const std::uint8_t* bytes) -> double {
        const std::uint16_t expon = read_be16(bytes);
        const std::uint32_t hi_mant = read_be32(bytes + 2);
        const std::uint32_t lo_mant = read_be32(bytes + 6);
        if (expon == 0 && hi_mant == 0 && lo_mant == 0) {
            return 0.0;
        }
        const int sign = (expon & 0x8000u) != 0u ? -1 : 1;
        const int exp = static_cast<int>(expon & 0x7FFFu) - 16383;
        const double mant = static_cast<double>(hi_mant) * std::pow(2.0, -31) + static_cast<double>(lo_mant) * std::pow(2.0, -63);
        return static_cast<double>(sign) * std::ldexp(mant, exp + 1);
    };

    std::array<char, 12> header{};
    in.read(header.data(), static_cast<std::streamsize>(header.size()));
    if (!in.good()) {
        return false;
    }

    std::uint16_t audio_format = 0;
    std::uint16_t num_channels = 0;
    std::uint32_t sample_rate = 0;
    std::uint16_t bits_per_sample = 0;
    std::vector<std::uint8_t> raw_data;
    bool big_endian_pcm = false;

    if (std::strncmp(header.data(), "RIFF", 4) == 0 && std::strncmp(header.data() + 8, "WAVE", 4) == 0) {
        while (in.good()) {
            std::array<char, 4> chunk_id{};
            std::uint32_t chunk_size = 0;
            in.read(chunk_id.data(), 4);
            if (!in.good()) {
                break;
            }
            in.read(reinterpret_cast<char*>(&chunk_size), sizeof(chunk_size));
            if (!in.good()) {
                return false;
            }
            if (chunk_size > kMaxDecodedAudioBytes) {
                return false;
            }

            if (std::strncmp(chunk_id.data(), "fmt ", 4) == 0) {
                in.read(reinterpret_cast<char*>(&audio_format), sizeof(audio_format));
                in.read(reinterpret_cast<char*>(&num_channels), sizeof(num_channels));
                in.read(reinterpret_cast<char*>(&sample_rate), sizeof(sample_rate));

                std::uint32_t byte_rate = 0;
                std::uint16_t block_align = 0;
                in.read(reinterpret_cast<char*>(&byte_rate), sizeof(byte_rate));
                in.read(reinterpret_cast<char*>(&block_align), sizeof(block_align));
                in.read(reinterpret_cast<char*>(&bits_per_sample), sizeof(bits_per_sample));
                if (!in.good()) {
                    return false;
                }

                const std::uint32_t consumed = 16;
                if (chunk_size > consumed) {
                    const std::uint32_t remaining = chunk_size - consumed;
                    if (remaining > kMaxDecodedAudioBytes) {
                        return false;
                    }
                    std::vector<std::uint8_t> fmt_extra(remaining);
                    in.read(reinterpret_cast<char*>(fmt_extra.data()), static_cast<std::streamsize>(fmt_extra.size()));
                    if (!in.good()) {
                        return false;
                    }

                    if (audio_format == 0xFFFEu && fmt_extra.size() >= 24) {
                        const std::uint16_t subformat = static_cast<std::uint16_t>(
                            static_cast<std::uint16_t>(fmt_extra[8]) | (static_cast<std::uint16_t>(fmt_extra[9]) << 8)
                        );
                        audio_format = subformat;
                    }
                }
            } else if (std::strncmp(chunk_id.data(), "data", 4) == 0) {
                raw_data.resize(chunk_size);
                in.read(reinterpret_cast<char*>(raw_data.data()), static_cast<std::streamsize>(chunk_size));
                if (!in.good()) {
                    return false;
                }
            } else {
                in.seekg(chunk_size, std::ios::cur);
                if (!in.good()) {
                    return false;
                }
            }

            if ((chunk_size & 1u) != 0u) {
                in.seekg(1, std::ios::cur);
            }
        }
    } else if (std::strncmp(header.data(), "FORM", 4) == 0
        && (std::strncmp(header.data() + 8, "AIFF", 4) == 0 || std::strncmp(header.data() + 8, "AIFC", 4) == 0)) {
        big_endian_pcm = true;
        bool little_endian_aifc = false;
        audio_format = 1;

        while (in.good()) {
            std::array<char, 4> chunk_id{};
            std::array<std::uint8_t, 4> size_bytes{};
            in.read(chunk_id.data(), 4);
            if (!in.good()) {
                break;
            }
            in.read(reinterpret_cast<char*>(size_bytes.data()), static_cast<std::streamsize>(size_bytes.size()));
            if (!in.good()) {
                return false;
            }
            const std::uint32_t chunk_size = read_be32(size_bytes.data());
            if (chunk_size > kMaxDecodedAudioBytes) {
                return false;
            }

            if (std::strncmp(chunk_id.data(), "COMM", 4) == 0) {
                std::vector<std::uint8_t> comm(chunk_size);
                in.read(reinterpret_cast<char*>(comm.data()), static_cast<std::streamsize>(chunk_size));
                if (!in.good()) {
                    return false;
                }
                if (comm.size() < 18) {
                    return false;
                }
                num_channels = read_be16(comm.data() + 0);
                bits_per_sample = read_be16(comm.data() + 6);
                const double parsed_rate = read_extended80(comm.data() + 8);
                sample_rate = static_cast<std::uint32_t>(std::max(1.0, parsed_rate));

                if (comm.size() >= 22) {
                    const std::string compression(reinterpret_cast<const char*>(comm.data() + 18), 4);
                    if (compression == "sowt") {
                        little_endian_aifc = true;
                    } else if (compression != "NONE") {
                        return false;
                    }
                }
            } else if (std::strncmp(chunk_id.data(), "SSND", 4) == 0) {
                if (chunk_size < 8) {
                    return false;
                }

                std::array<std::uint8_t, 8> ssnd_header{};
                in.read(reinterpret_cast<char*>(ssnd_header.data()), static_cast<std::streamsize>(ssnd_header.size()));
                if (!in.good()) {
                    return false;
                }
                const std::uint32_t offset = read_be32(ssnd_header.data() + 0);
                (void)read_be32(ssnd_header.data() + 4);  // blockSize
                if (offset > (chunk_size - 8)) {
                    return false;
                }
                if (offset > 0) {
                    in.seekg(offset, std::ios::cur);
                }

                const std::uint32_t data_size = chunk_size - 8 - offset;
                if (data_size > kMaxDecodedAudioBytes) {
                    return false;
                }
                raw_data.resize(data_size);
                in.read(reinterpret_cast<char*>(raw_data.data()), static_cast<std::streamsize>(data_size));
                if (!in.good()) {
                    return false;
                }
                if (little_endian_aifc) {
                    big_endian_pcm = false;
                }
            } else {
                in.seekg(chunk_size, std::ios::cur);
                if (!in.good()) {
                    return false;
                }
            }

            if ((chunk_size & 1u) != 0u) {
                in.seekg(1, std::ios::cur);
            }
        }
    } else {
        return false;
    }

    if (raw_data.empty() || num_channels == 0 || sample_rate == 0) {
        return false;
    }

    out_region.sample_rate = sample_rate;
    out_region.samples.clear();
    out_region.samples_right.clear();
    out_region.mapped_samples = nullptr;
    out_region.mapped_samples_right = nullptr;
    out_region.mapped_sample_count = 0;
    out_region.mapped_owner.reset();
    out_region.stereo = false;

    const std::size_t bytes_per_sample = bits_per_sample / 8;
    if (bytes_per_sample == 0) {
        return false;
    }
    const std::size_t frame_bytes = bytes_per_sample * num_channels;
    if (frame_bytes == 0) {
        return false;
    }

    const std::size_t frame_count = raw_data.size() / frame_bytes;
    out_region.samples.reserve(frame_count);
    const bool emit_stereo = num_channels >= 2;
    if (emit_stereo) {
        out_region.samples_right.reserve(frame_count);
    }

    for (std::size_t frame = 0; frame < frame_count; ++frame) {
        float mono = 0.0F;
        float left = 0.0F;
        float right = 0.0F;

        for (std::size_t ch = 0; ch < num_channels; ++ch) {
            const std::size_t offset = frame * frame_bytes + (ch * bytes_per_sample);
            float sample = 0.0F;

            if (audio_format == 1 && bits_per_sample == 16) {
                std::int16_t s = 0;
                if (big_endian_pcm) {
                    s = static_cast<std::int16_t>(
                        (static_cast<std::uint16_t>(raw_data[offset + 0]) << 8) | static_cast<std::uint16_t>(raw_data[offset + 1])
                    );
                } else {
                    std::memcpy(&s, raw_data.data() + offset, sizeof(s));
                }
                sample = static_cast<float>(s) / 32768.0F;
            } else if (audio_format == 1 && bits_per_sample == 24) {
                const std::int32_t b0 = static_cast<std::int32_t>(raw_data[offset + (big_endian_pcm ? 2 : 0)]);
                const std::int32_t b1 = static_cast<std::int32_t>(raw_data[offset + 1]);
                const std::int32_t b2 = static_cast<std::int32_t>(raw_data[offset + (big_endian_pcm ? 0 : 2)]);
                std::int32_t packed = b0 | (b1 << 8) | (b2 << 16);
                if ((packed & 0x00800000) != 0) {
                    packed |= ~0x00FFFFFF;
                }
                sample = static_cast<float>(packed) / 8388608.0F;
            } else if (audio_format == 1 && bits_per_sample == 32) {
                std::int32_t s = 0;
                if (big_endian_pcm) {
                    s = static_cast<std::int32_t>(
                        (static_cast<std::uint32_t>(raw_data[offset + 0]) << 24)
                        | (static_cast<std::uint32_t>(raw_data[offset + 1]) << 16)
                        | (static_cast<std::uint32_t>(raw_data[offset + 2]) << 8)
                        | static_cast<std::uint32_t>(raw_data[offset + 3])
                    );
                } else {
                    std::memcpy(&s, raw_data.data() + offset, sizeof(s));
                }
                sample = static_cast<float>(s) / 2147483648.0F;
            } else if (audio_format == 3 && bits_per_sample == 32) {
                if (big_endian_pcm) {
                    return false;
                }
                std::memcpy(&sample, raw_data.data() + offset, sizeof(sample));
            } else {
                return false;
            }

            mono += sample;
            if (ch == 0) {
                left = sample;
            } else if (ch == 1) {
                right = sample;
            }
        }

        mono /= static_cast<float>(num_channels);
        if (emit_stereo) {
            out_region.stereo = true;
            out_region.samples.push_back(std::clamp(left, -1.0F, 1.0F));
            out_region.samples_right.push_back(std::clamp(right, -1.0F, 1.0F));
        } else {
            out_region.samples.push_back(std::clamp(mono, -1.0F, 1.0F));
        }
    }

    return !out_region.samples.empty();
}

bool PianoSampleLibrary::load_flac_mono(const std::string& file_path, PianoSampleRegion& out_region) {
    unsigned int channels = 0;
    unsigned int sample_rate = 0;
    drflac_uint64 frame_count = 0;

    float* decoded = drflac_open_file_and_read_pcm_frames_f32(
        file_path.c_str(),
        &channels,
        &sample_rate,
        &frame_count,
        nullptr
    );
    if (decoded == nullptr || channels == 0 || sample_rate == 0 || frame_count == 0) {
        return false;
    }
    const std::uint64_t decoded_bytes = frame_count * static_cast<std::uint64_t>(channels) * sizeof(float);
    if (decoded_bytes == 0 || decoded_bytes > kMaxDecodedAudioBytes) {
        drflac_free(decoded, nullptr);
        return false;
    }

    if (frame_count > (std::numeric_limits<std::size_t>::max() / static_cast<std::size_t>(channels))) {
        drflac_free(decoded, nullptr);
        return false;
    }

    out_region.sample_rate = sample_rate;
    out_region.samples.clear();
    out_region.samples_right.clear();
    out_region.mapped_samples = nullptr;
    out_region.mapped_samples_right = nullptr;
    out_region.mapped_sample_count = 0;
    out_region.mapped_owner.reset();
    out_region.stereo = false;

    out_region.samples.assign(static_cast<std::size_t>(frame_count), 0.0F);
    const bool emit_stereo = channels >= 2;
    if (emit_stereo) {
        out_region.samples_right.assign(static_cast<std::size_t>(frame_count), 0.0F);
        out_region.stereo = true;
    }

    for (drflac_uint64 frame = 0; frame < frame_count; ++frame) {
        const std::size_t base = static_cast<std::size_t>(frame) * channels;
        if (emit_stereo) {
            out_region.samples[static_cast<std::size_t>(frame)] = std::clamp(decoded[base], -1.0F, 1.0F);
            out_region.samples_right[static_cast<std::size_t>(frame)] = std::clamp(decoded[base + 1], -1.0F, 1.0F);
        } else {
            float mono = 0.0F;
            for (unsigned int ch = 0; ch < channels; ++ch) {
                mono += decoded[base + ch];
            }
            mono /= static_cast<float>(channels);
            out_region.samples[static_cast<std::size_t>(frame)] = std::clamp(mono, -1.0F, 1.0F);
        }
    }

    drflac_free(decoded, nullptr);
    return !out_region.samples.empty();
}

bool PianoSampleLibrary::promote_to_disk_backed(const std::string& sample_path_hint, PianoSampleRegion& region) const {
#ifdef __linux__
    if (!disk_streaming_enabled_) {
        return false;
    }
    if (region.sample_count() < disk_stream_threshold_frames_) {
        return false;
    }
    if (region.samples.empty()) {
        return false;
    }
    if (region.stereo && region.samples_right.size() != region.samples.size()) {
        return false;
    }

    const std::filesystem::path cache_root = std::filesystem::temp_directory_path() / "vb_engine_sample_cache";
    std::error_code mkdir_ec;
    std::filesystem::create_directories(cache_root, mkdir_ec);
    if (mkdir_ec) {
        return false;
    }

    const std::uint64_t hash_input = std::hash<std::string>{}(sample_path_hint)
        ^ (static_cast<std::uint64_t>(region.sample_rate) << 32)
        ^ static_cast<std::uint64_t>(region.samples.size())
        ^ (region.stereo ? 0x9E3779B97F4A7C15ULL : 0ULL);
    const std::filesystem::path cache_file = cache_root / ("sample_" + std::to_string(hash_input) + (region.stereo ? ".f32p2" : ".f32"));

    {
        std::ofstream out(cache_file, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            return false;
        }
        out.write(
            reinterpret_cast<const char*>(region.samples.data()),
            static_cast<std::streamsize>(region.samples.size() * sizeof(float))
        );
        if (region.stereo) {
            out.write(
                reinterpret_cast<const char*>(region.samples_right.data()),
                static_cast<std::streamsize>(region.samples_right.size() * sizeof(float))
            );
        }
        if (!out.good()) {
            return false;
        }
    }

    const int fd = ::open(cache_file.c_str(), O_RDONLY);
    if (fd < 0) {
        return false;
    }

    const std::size_t channel_count = region.stereo ? 2U : 1U;
    const std::size_t byte_count = region.samples.size() * channel_count * sizeof(float);
    void* mapped = ::mmap(nullptr, byte_count, PROT_READ, MAP_PRIVATE, fd, 0);
    ::close(fd);
    if (mapped == MAP_FAILED) {
        return false;
    }

    region.mapped_samples = static_cast<const float*>(mapped);
    if (region.stereo) {
        region.mapped_samples_right = region.mapped_samples + region.samples.size();
    } else {
        region.mapped_samples_right = nullptr;
    }
    region.mapped_sample_count = region.samples.size();
    region.mapped_owner = std::shared_ptr<void>(mapped, [byte_count, cache_file](void* ptr) {
        if (ptr != nullptr && ptr != MAP_FAILED) {
            ::munmap(ptr, byte_count);
        }
        std::error_code remove_ec;
        std::filesystem::remove(cache_file, remove_ec);
    });
    region.samples.clear();
    region.samples.shrink_to_fit();
    region.samples_right.clear();
    region.samples_right.shrink_to_fit();
    return true;
#else
    (void)sample_path_hint;
    (void)region;
    return false;
#endif
}

std::uint8_t PianoSampleLibrary::parse_note_value(const std::string_view value, const std::uint8_t fallback) noexcept {
    int as_int = 0;
    if (parse_int(value, as_int)) {
        return static_cast<std::uint8_t>(std::clamp(as_int, 0, 127));
    }

    if (value.empty()) {
        return fallback;
    }

    static constexpr std::array<std::string_view, 12> kNames{
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };

    const std::string text(value);
    std::string note_name;
    std::string octave_text;
    for (char c : text) {
        if ((c >= '0' && c <= '9') || c == '-') {
            octave_text.push_back(c);
        } else {
            note_name.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
        }
    }

    int octave = 0;
    if (!parse_int(octave_text, octave)) {
        return fallback;
    }

    int semitone = -1;
    for (std::size_t i = 0; i < kNames.size(); ++i) {
        if (note_name == kNames[i]) {
            semitone = static_cast<int>(i);
            break;
        }
    }

    if (semitone < 0) {
        return fallback;
    }

    const int midi = ((octave + 1) * 12) + semitone;
    return static_cast<std::uint8_t>(std::clamp(midi, 0, 127));
}

bool PianoSampleLibrary::load_sfz(const std::string& sfz_path) {
    regions_.clear();
    selection_cache_valid_ = false;
    pedal_down_indices_.clear();
    pedal_up_indices_.clear();
    has_round_robin_regions_ = false;
    has_cc64_conditioned_regions_ = false;
    rr_counters_.fill(0);
    rr_rng_state_ = 0xA5C3F29Du;

    if (sfz_path.empty()) {
        return false;
    }

    std::ifstream in(sfz_path);
    if (!in.is_open()) {
        return false;
    }

    std::error_code ec;
    const std::filesystem::path sfz_file = std::filesystem::canonical(std::filesystem::path(sfz_path), ec);
    const std::filesystem::path sfz_resolved = ec ? std::filesystem::path(sfz_path) : sfz_file;
    const std::filesystem::path sfz_dir = sfz_resolved.parent_path();

    RegionDraft global_defaults{};
    RegionDraft group_defaults = global_defaults;
    RegionDraft current_region = group_defaults;
    bool in_region = false;

    auto apply_token = [&](RegionDraft& region, const std::string& key, const std::string& value) {
        if (key == "sample") {
            region.sample_path = value;
        } else if (key == "key") {
            const std::uint8_t midi = parse_note_value(value, region.root_key);
            region.root_key = midi;
            region.key_low = midi;
            region.key_high = midi;
        } else if (key == "lokey") {
            region.key_low = parse_note_value(value, region.key_low);
        } else if (key == "hikey") {
            region.key_high = parse_note_value(value, region.key_high);
        } else if (key == "pitch_keycenter") {
            region.root_key = parse_note_value(value, region.root_key);
        } else if (key == "lovel") {
            int parsed = static_cast<int>(region.vel_low);
            if (parse_int(value, parsed)) {
                region.vel_low = static_cast<std::uint8_t>(std::clamp(parsed, 1, 127));
            }
        } else if (key == "hivel") {
            int parsed = static_cast<int>(region.vel_high);
            if (parse_int(value, parsed)) {
                region.vel_high = static_cast<std::uint8_t>(std::clamp(parsed, 1, 127));
            }
        } else if (key == "volume") {
            float db = 0.0F;
            if (parse_float(value, db)) {
                region.gain_linear = db_to_linear(db);
            }
        } else if (key == "amp_veltrack" || key == "ampeg_veltrack") {
            float parsed = region.amp_veltrack;
            if (parse_float(value, parsed)) {
                region.amp_veltrack = std::clamp(parsed, 0.0F, 200.0F);
            }
        } else if (key == "amp_velcurve") {
            float parsed = region.amp_velcurve;
            if (parse_float(value, parsed)) {
                region.amp_velcurve = std::clamp(parsed, 0.10F, 4.0F);
            }
        } else if (key == "trigger") {
            region.release_trigger = (value == "release");
        } else if (key == "offset") {
            int parsed = region.sample_offset;
            if (parse_int(value, parsed)) {
                region.sample_offset = std::max(0, parsed);
            }
        } else if (key == "end") {
            int parsed = region.sample_end;
            if (parse_int(value, parsed)) {
                region.sample_end = std::max(0, parsed);
            }
        } else if (key == "loop_start") {
            int parsed = region.loop_start;
            if (parse_int(value, parsed)) {
                region.loop_start = std::max(0, parsed);
            }
        } else if (key == "loop_end") {
            int parsed = region.loop_end;
            if (parse_int(value, parsed)) {
                region.loop_end = std::max(0, parsed);
            }
        } else if (key == "tune") {
            float parsed = region.tune_cents;
            if (parse_float(value, parsed)) {
                region.tune_cents = std::clamp(parsed, -1200.0F, 1200.0F);
            }
        } else if (key == "ampeg_release") {
            float parsed = region.release_seconds;
            if (parse_float(value, parsed)) {
                region.release_seconds = std::clamp(parsed, 0.0F, 20.0F);
            }
        } else if (key == "lorand") {
            float parsed = region.lorand;
            if (parse_float(value, parsed)) {
                region.lorand = std::clamp(parsed, 0.0F, 1.0F);
            }
        } else if (key == "hirand") {
            float parsed = region.hirand;
            if (parse_float(value, parsed)) {
                region.hirand = std::clamp(parsed, 0.0F, 1.0F);
            }
        } else if (key == "seq_length") {
            int parsed = static_cast<int>(region.seq_length);
            if (parse_int(value, parsed)) {
                region.seq_length = static_cast<std::uint16_t>(std::clamp(parsed, 0, 32767));
            }
        } else if (key == "seq_position" || key == "seq_pos") {
            int parsed = static_cast<int>(region.seq_position);
            if (parse_int(value, parsed)) {
                region.seq_position = static_cast<std::uint16_t>(std::clamp(parsed, 1, 32767));
            }
        } else if (key == "locc64" || key == "on_locc64") {
            int parsed = static_cast<int>(region.cc64_low);
            if (parse_int(value, parsed)) {
                region.cc64_low = static_cast<std::uint8_t>(std::clamp(parsed, 0, 127));
            }
        } else if (key == "hicc64" || key == "on_hicc64") {
            int parsed = static_cast<int>(region.cc64_high);
            if (parse_int(value, parsed)) {
                region.cc64_high = static_cast<std::uint8_t>(std::clamp(parsed, 0, 127));
            }
        } else if (key == "loop_mode") {
            if (value == "loop_continuous") {
                region.loop_mode = LoopMode::LoopContinuous;
            } else if (value == "loop_sustain") {
                region.loop_mode = LoopMode::LoopSustain;
            } else if (value == "one_shot") {
                region.loop_mode = LoopMode::OneShot;
            } else {
                region.loop_mode = LoopMode::NoLoop;
            }
        } else if (key == "off_mode") {
            region.off_mode_fast = (value == "fast");
        }
    };

    auto parse_assignment_tokens = [&](const std::string& text, RegionDraft& target) {
        for (const std::string& token : split_tokens(text)) {
            const std::size_t equals = token.find('=');
            if (equals == std::string::npos) {
                continue;
            }
            apply_token(target, token.substr(0, equals), token.substr(equals + 1));
        }
    };

    auto flush_region = [&]() {
        if (!in_region || current_region.sample_path.empty()) {
            return;
        }

        PianoSampleRegion loaded{};
        loaded.key_low = current_region.key_low;
        loaded.key_high = current_region.key_high;
        loaded.vel_low = current_region.vel_low;
        loaded.vel_high = current_region.vel_high;
        loaded.root_key = current_region.root_key;
        loaded.release_trigger = current_region.release_trigger;
        loaded.gain_linear = current_region.gain_linear;
        loaded.amp_veltrack = current_region.amp_veltrack;
        loaded.amp_velcurve = current_region.amp_velcurve;
        loaded.off_mode_fast = current_region.off_mode_fast;
        loaded.lorand = std::clamp(current_region.lorand, 0.0F, 1.0F);
        loaded.hirand = std::clamp(current_region.hirand, 0.0F, 1.0F);
        if (loaded.hirand < loaded.lorand) {
            std::swap(loaded.hirand, loaded.lorand);
        }
        loaded.seq_length = current_region.seq_length;
        loaded.seq_position = current_region.seq_position;
        loaded.cc64_low = current_region.cc64_low;
        loaded.cc64_high = current_region.cc64_high;
        if (loaded.cc64_high < loaded.cc64_low) {
            std::swap(loaded.cc64_high, loaded.cc64_low);
        }
        has_cc64_conditioned_regions_ = has_cc64_conditioned_regions_
            || (loaded.cc64_low != 0U)
            || (loaded.cc64_high != 127U);
        if (loaded.seq_length > 0) {
            loaded.seq_position = static_cast<std::uint16_t>(std::clamp<int>(loaded.seq_position, 1, loaded.seq_length));
        } else {
            loaded.seq_position = 1;
        }
        has_round_robin_regions_ = has_round_robin_regions_ || uses_round_robin(loaded);

        const std::filesystem::path sample_path = sfz_dir / current_region.sample_path;
        if (load_audio_mono(sample_path.string(), loaded)) {
            if (loaded.empty()) {
                return;
            }

            const int max_index = static_cast<int>(loaded.sample_count()) - 1;
            const int sample_start = std::clamp(current_region.sample_offset, 0, max_index);
            const int sample_end = std::clamp(
                current_region.sample_end >= 0 ? current_region.sample_end : max_index,
                sample_start,
                max_index
            );
            loaded.sample_start = static_cast<std::uint32_t>(sample_start);
            loaded.sample_end = static_cast<std::uint32_t>(sample_end);
            loaded.tune_cents = current_region.tune_cents;
            loaded.release_seconds = current_region.release_seconds;

            loaded.loop_enabled = false;
            loaded.loop_until_release = false;
            loaded.loop_start = loaded.sample_start;
            loaded.loop_end = loaded.sample_end;

            if (current_region.loop_mode == LoopMode::LoopContinuous || current_region.loop_mode == LoopMode::LoopSustain) {
                const int loop_start = std::clamp(
                    current_region.loop_start >= 0 ? current_region.loop_start : sample_start,
                    sample_start,
                    sample_end
                );
                const int loop_end = std::clamp(
                    current_region.loop_end >= 0 ? current_region.loop_end : sample_end,
                    loop_start + 1,
                    sample_end
                );
                if (loop_end > loop_start) {
                    loaded.loop_enabled = true;
                    loaded.loop_start = static_cast<std::uint32_t>(loop_start);
                    loaded.loop_end = static_cast<std::uint32_t>(loop_end);
                    loaded.loop_until_release = (current_region.loop_mode == LoopMode::LoopSustain);
                }
            }

            loaded.source_name = sample_path.filename().string();
            loaded.perspective = classify_perspective(loaded.source_name);
            (void)promote_to_disk_backed(sample_path.string(), loaded);
            const std::size_t loaded_index = regions_.size();
            regions_.push_back(std::move(loaded));

            std::string source_lower = regions_.back().source_name;
            std::transform(source_lower.begin(), source_lower.end(), source_lower.begin(), [](const unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            if (source_lower.find("pedald") != std::string::npos && loaded_index <= std::numeric_limits<std::uint16_t>::max()) {
                pedal_down_indices_.push_back(static_cast<std::uint16_t>(loaded_index));
            } else if (source_lower.find("pedalu") != std::string::npos && loaded_index <= std::numeric_limits<std::uint16_t>::max()) {
                pedal_up_indices_.push_back(static_cast<std::uint16_t>(loaded_index));
            }
        }
    };

    std::string line;
    while (std::getline(in, line)) {
        const std::size_t comment_pos = line.find("//");
        if (comment_pos != std::string::npos) {
            line = line.substr(0, comment_pos);
        }

        const std::string trimmed = trim(line);
        if (trimmed.empty()) {
            continue;
        }

        if (trimmed.find("<global>") != std::string::npos) {
            flush_region();
            in_region = false;
            const std::string content = trim(trimmed.substr(trimmed.find("<global>") + 8));
            parse_assignment_tokens(content, global_defaults);
            group_defaults = global_defaults;
            continue;
        }

        if (trimmed.find("<group>") != std::string::npos) {
            flush_region();
            in_region = false;
            group_defaults = global_defaults;
            const std::string content = trim(trimmed.substr(trimmed.find("<group>") + 7));
            parse_assignment_tokens(content, group_defaults);
            continue;
        }

        if (trimmed.find("<region>") != std::string::npos) {
            flush_region();
            in_region = true;
            current_region = group_defaults;
            const std::string content = trim(trimmed.substr(trimmed.find("<region>") + 8));
            parse_assignment_tokens(content, current_region);
            continue;
        }

        if (in_region) {
            parse_assignment_tokens(trimmed, current_region);
        } else {
            // Apply loose top-level assignments as global defaults.
            parse_assignment_tokens(trimmed, global_defaults);
            group_defaults = global_defaults;
        }
    }

    flush_region();
    rebuild_selection_cache();
    return !regions_.empty();
}

bool PianoSampleLibrary::uses_round_robin(const PianoSampleRegion& region) noexcept {
    if (region.seq_length > 0) {
        return true;
    }
    return (region.lorand > 0.0F) || (region.hirand < 0.9999F);
}

PianoMicPerspective PianoSampleLibrary::classify_perspective(const std::string& source_name) noexcept {
    std::string lower = source_name;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](const unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (lower.find("close") != std::string::npos
        || lower.find("_cl") != std::string::npos
        || lower.find("mic1") != std::string::npos
        || lower.find("inside") != std::string::npos) {
        return PianoMicPerspective::Close;
    }
    if (lower.find("player") != std::string::npos
        || lower.find("head") != std::string::npos
        || lower.find("mid") != std::string::npos) {
        return PianoMicPerspective::Player;
    }
    if (lower.find("room") != std::string::npos
        || lower.find("far") != std::string::npos
        || lower.find("ambient") != std::string::npos
        || lower.find("hall") != std::string::npos) {
        return PianoMicPerspective::Room;
    }
    return PianoMicPerspective::Unknown;
}

float PianoSampleLibrary::next_round_robin_random() const noexcept {
    rr_rng_state_ ^= (rr_rng_state_ << 13);
    rr_rng_state_ ^= (rr_rng_state_ >> 17);
    rr_rng_state_ ^= (rr_rng_state_ << 5);
    const std::uint32_t mantissa = rr_rng_state_ & 0x00FFFFFFu;
    return static_cast<float>(mantissa) / static_cast<float>(0x01000000u);
}

bool PianoSampleLibrary::region_matches_round_robin(
    const PianoSampleRegion& region,
    const std::uint8_t note,
    const bool release_trigger,
    const std::uint32_t rr_step,
    const float rr_rand
) const noexcept {
    (void)note;
    (void)release_trigger;

    if (region.seq_length > 0) {
        const std::uint32_t seq_len = static_cast<std::uint32_t>(std::max<std::uint16_t>(1, region.seq_length));
        const std::uint32_t seq_pos = static_cast<std::uint32_t>(std::clamp<std::uint16_t>(region.seq_position, 1, region.seq_length));
        const std::uint32_t step_pos = (rr_step % seq_len) + 1u;
        if (step_pos != seq_pos) {
            return false;
        }
    }

    const float lo = std::clamp(region.lorand, 0.0F, 1.0F);
    const float hi = std::clamp(region.hirand, 0.0F, 1.0F);
    if (hi <= lo) {
        return true;
    }
    return rr_rand >= lo && rr_rand < hi;
}

std::size_t PianoSampleLibrary::cache_offset(
    const std::uint8_t note,
    const std::uint8_t velocity,
    const bool release_trigger
) noexcept {
    return (static_cast<std::size_t>(release_trigger ? 1U : 0U) * (kNoteCount * kVelocityCount))
        + (static_cast<std::size_t>(note) * kVelocityCount)
        + static_cast<std::size_t>(velocity);
}

PianoLayerSelection PianoSampleLibrary::select_layers_uncached(
    const std::uint8_t note,
    const std::uint8_t velocity,
    const bool release_trigger,
    const std::uint8_t cc64_value
) const noexcept {
    struct Candidate {
        const PianoSampleRegion* region;
        int center;
        int key_distance;
    };

    std::vector<Candidate> candidates;
    candidates.reserve(regions_.size());
    const std::size_t rr_index = static_cast<std::size_t>(note) + (release_trigger ? kNoteCount : 0U);
    const std::uint32_t rr_step = ++rr_counters_[rr_index];
    const float rr_rand = next_round_robin_random();

    const auto collect_candidates = [&](const bool strict_round_robin) {
        candidates.clear();
        for (const auto& region : regions_) {
            if (region.release_trigger != release_trigger) {
                continue;
            }
            if (cc64_value < region.cc64_low || cc64_value > region.cc64_high) {
                continue;
            }
            if (note < region.key_low || note > region.key_high) {
                continue;
            }
            if (strict_round_robin
                && uses_round_robin(region)
                && !region_matches_round_robin(region, note, release_trigger, rr_step, rr_rand)) {
                continue;
            }

            const int center = (static_cast<int>(region.vel_low) + static_cast<int>(region.vel_high)) / 2;
            candidates.push_back(Candidate{&region, center, 0});
        }

        if (!candidates.empty()) {
            return;
        }

        for (const auto& region : regions_) {
            if (region.release_trigger != release_trigger) {
                continue;
            }
            if (cc64_value < region.cc64_low || cc64_value > region.cc64_high) {
                continue;
            }
            if (strict_round_robin
                && uses_round_robin(region)
                && !region_matches_round_robin(region, note, release_trigger, rr_step, rr_rand)) {
                continue;
            }
            int key_distance = 0;
            if (note < region.key_low) {
                key_distance = static_cast<int>(region.key_low) - static_cast<int>(note);
            } else if (note > region.key_high) {
                key_distance = static_cast<int>(note) - static_cast<int>(region.key_high);
            }
            const int center = (static_cast<int>(region.vel_low) + static_cast<int>(region.vel_high)) / 2;
            candidates.push_back(Candidate{&region, center, key_distance});
        }
    };

    collect_candidates(true);
    if (candidates.empty()) {
        // Fallback path if strict RR gates produce no match.
        collect_candidates(false);
    }

    if (candidates.empty()) {
        return PianoLayerSelection{};
    }

    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        if (a.key_distance == b.key_distance) {
            if (a.region->vel_low == b.region->vel_low) {
                return a.region->vel_high < b.region->vel_high;
            }
            return a.region->vel_low < b.region->vel_low;
        }
        return a.key_distance < b.key_distance;
    });

    const int best_key_distance = candidates.front().key_distance;

    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        if (a.region->vel_low == b.region->vel_low) {
            return a.region->vel_high < b.region->vel_high;
        }
        return a.region->vel_low < b.region->vel_low;
    });

    const PianoSampleRegion* primary = nullptr;
    for (const Candidate& candidate : candidates) {
        if (candidate.key_distance != best_key_distance) {
            continue;
        }
        if (velocity >= candidate.region->vel_low && velocity <= candidate.region->vel_high) {
            primary = candidate.region;
            break;
        }
    }

    if (primary == nullptr) {
        primary = candidates.front().region;
        int best_distance = 999;
        int best_key = 999;
        for (const Candidate& candidate : candidates) {
            if (candidate.key_distance > best_key_distance) {
                continue;
            }
            const int lower = static_cast<int>(candidate.region->vel_low);
            const int upper = static_cast<int>(candidate.region->vel_high);
            int distance = 0;
            if (velocity < lower) {
                distance = lower - velocity;
            } else if (velocity > upper) {
                distance = velocity - upper;
            }
            if (candidate.key_distance < best_key || (candidate.key_distance == best_key && distance < best_distance)) {
                best_key = candidate.key_distance;
                best_distance = distance;
                primary = candidate.region;
            }
        }
    }

    const int kCrossfadeWidth = 10;
    const PianoSampleRegion* secondary = nullptr;
    float mix = 0.0F;

    const auto perspective_distance = [](const PianoMicPerspective a, const PianoMicPerspective b) -> int {
        if (a == b) {
            return 3;
        }
        if ((a == PianoMicPerspective::Close && b == PianoMicPerspective::Room)
            || (a == PianoMicPerspective::Room && b == PianoMicPerspective::Close)) {
            return 0;
        }
        if ((a == PianoMicPerspective::Close && b == PianoMicPerspective::Player)
            || (a == PianoMicPerspective::Player && b == PianoMicPerspective::Close)
            || (a == PianoMicPerspective::Player && b == PianoMicPerspective::Room)
            || (a == PianoMicPerspective::Room && b == PianoMicPerspective::Player)) {
            return 1;
        }
        return 2;
    };
    int best_mic_score = 999;
    for (const Candidate& candidate : candidates) {
        if (candidate.key_distance > best_key_distance || candidate.region == primary) {
            continue;
        }
        if (candidate.region->perspective == PianoMicPerspective::Unknown
            || primary->perspective == PianoMicPerspective::Unknown
            || candidate.region->perspective == primary->perspective) {
            continue;
        }
        const int vel_distance = std::abs(static_cast<int>(velocity) - candidate.center);
        const int score = (candidate.key_distance * 200)
            + (perspective_distance(primary->perspective, candidate.region->perspective) * 100)
            + vel_distance;
        if (score < best_mic_score) {
            best_mic_score = score;
            secondary = candidate.region;
            mix = 0.35F;
            if (primary->perspective == PianoMicPerspective::Room) {
                mix = 0.70F;
            } else if (primary->perspective == PianoMicPerspective::Player) {
                mix = 0.52F;
            }
        }
    }

    if (secondary != nullptr) {
        return PianoLayerSelection{primary, secondary, std::clamp(mix, 0.0F, 1.0F)};
    }

    for (const Candidate& candidate : candidates) {
        if (candidate.key_distance > best_key_distance) {
            continue;
        }
        if (candidate.region == primary) {
            continue;
        }

        if (candidate.region->vel_low > primary->vel_high) {
            const int distance = static_cast<int>(candidate.region->vel_low) - static_cast<int>(velocity);
            if (distance >= 0 && distance <= kCrossfadeWidth) {
                secondary = candidate.region;
                mix = 1.0F - (static_cast<float>(distance) / static_cast<float>(kCrossfadeWidth));
            }
            break;
        }

        if (candidate.region->vel_high < primary->vel_low) {
            const int distance = static_cast<int>(velocity) - static_cast<int>(candidate.region->vel_high);
            if (distance >= 0 && distance <= kCrossfadeWidth) {
                secondary = candidate.region;
                mix = 1.0F - (static_cast<float>(distance) / static_cast<float>(kCrossfadeWidth));
            }
        }
    }

    if (secondary == nullptr || mix <= 0.0F) {
        return PianoLayerSelection{primary, nullptr, 0.0F};
    }

    return PianoLayerSelection{primary, secondary, std::clamp(mix, 0.0F, 1.0F)};
}

void PianoSampleLibrary::rebuild_selection_cache() noexcept {
    selection_cache_valid_ = false;
    for (bool release_trigger : { false, true }) {
        for (std::size_t note = 0; note < kNoteCount; ++note) {
            for (std::size_t velocity = 0; velocity < kVelocityCount; ++velocity) {
                CachedSelection cached{};
                const PianoLayerSelection selection = select_layers_uncached(
                    static_cast<std::uint8_t>(note),
                    static_cast<std::uint8_t>(velocity),
                    release_trigger,
                    0
                );
                if (selection.primary != nullptr && !regions_.empty()) {
                    const auto* begin = regions_.data();
                    const auto* end = begin + regions_.size();
                    if (selection.primary >= begin && selection.primary < end) {
                        cached.primary = static_cast<std::int16_t>(selection.primary - begin);
                    }
                    if (selection.secondary != nullptr && selection.secondary >= begin && selection.secondary < end) {
                        cached.secondary = static_cast<std::int16_t>(selection.secondary - begin);
                        cached.secondary_mix = selection.secondary_mix;
                    }
                }
                selection_cache_[cache_offset(
                    static_cast<std::uint8_t>(note),
                    static_cast<std::uint8_t>(velocity),
                    release_trigger
                )] = cached;
            }
        }
    }
    selection_cache_valid_ = true;
    rr_counters_.fill(0);
    rr_rng_state_ = 0xA5C3F29Du;
}

PianoLayerSelection PianoSampleLibrary::select_layers(
    const std::uint8_t note,
    const std::uint8_t velocity,
    const bool release_trigger,
    const std::uint8_t cc64_value
) const noexcept {
    if (!selection_cache_valid_ || has_cc64_conditioned_regions_) {
        return select_layers_uncached(note, velocity, release_trigger, cc64_value);
    }

    const CachedSelection& cached = selection_cache_[cache_offset(note, velocity, release_trigger)];
    if (cached.primary < 0 || regions_.empty()) {
        return PianoLayerSelection{};
    }

    const std::size_t primary_index = static_cast<std::size_t>(cached.primary);
    if (primary_index >= regions_.size()) {
        return PianoLayerSelection{};
    }

    const PianoSampleRegion* primary = &regions_[primary_index];
    const PianoSampleRegion* secondary = nullptr;
    if (cached.secondary >= 0) {
        const std::size_t secondary_index = static_cast<std::size_t>(cached.secondary);
        if (secondary_index < regions_.size()) {
            secondary = &regions_[secondary_index];
        }
    }

    if (has_round_robin_regions_
        && ((primary != nullptr && uses_round_robin(*primary))
            || (secondary != nullptr && uses_round_robin(*secondary)))) {
        return select_layers_uncached(note, velocity, release_trigger, cc64_value);
    }

    return PianoLayerSelection{primary, secondary, std::clamp(cached.secondary_mix, 0.0F, 1.0F)};
}

const PianoSampleRegion* PianoSampleLibrary::select_pedal_sample(const bool pedal_down, const std::uint32_t seed) const noexcept {
    const auto& indices = pedal_down ? pedal_down_indices_ : pedal_up_indices_;
    if (indices.empty() || regions_.empty()) {
        return nullptr;
    }

    const std::size_t idx = static_cast<std::size_t>(seed % static_cast<std::uint32_t>(indices.size()));
    const std::size_t region_index = static_cast<std::size_t>(indices[idx]);
    if (region_index >= regions_.size()) {
        return nullptr;
    }
    return &regions_[region_index];
}

}  // namespace vb
