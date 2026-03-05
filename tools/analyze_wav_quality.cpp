#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

struct WavData {
    uint16_t channels{0};
    uint32_t sample_rate{0};
    std::vector<int16_t> samples;
};

uint32_t read_u32(std::ifstream& in) {
    uint32_t v = 0;
    in.read(reinterpret_cast<char*>(&v), sizeof(v));
    return v;
}

uint16_t read_u16(std::ifstream& in) {
    uint16_t v = 0;
    in.read(reinterpret_cast<char*>(&v), sizeof(v));
    return v;
}

bool load_wav(const std::filesystem::path& path, WavData& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        std::cerr << "Unable to open " << path << "\n";
        return false;
    }

    char riff[4]{};
    in.read(riff, 4);
    if (std::string(riff, 4) != "RIFF") {
        std::cerr << path << " is not RIFF\n";
        return false;
    }

    (void)read_u32(in);
    char wave[4]{};
    in.read(wave, 4);
    if (std::string(wave, 4) != "WAVE") {
        std::cerr << path << " is not WAVE\n";
        return false;
    }

    bool have_fmt = false;
    bool have_data = false;

    while (in.good() && !have_data) {
        char chunk_id[4]{};
        in.read(chunk_id, 4);
        if (in.gcount() != 4) {
            break;
        }

        const uint32_t chunk_size = read_u32(in);
        const std::string id(chunk_id, 4);

        if (id == "fmt ") {
            const uint16_t audio_format = read_u16(in);
            out.channels = read_u16(in);
            out.sample_rate = read_u32(in);
            (void)read_u32(in);
            (void)read_u16(in);
            const uint16_t bits_per_sample = read_u16(in);

            if (audio_format != 1 || bits_per_sample != 16) {
                std::cerr << path << " unsupported format\n";
                return false;
            }

            if (chunk_size > 16) {
                in.seekg(static_cast<std::streamoff>(chunk_size - 16), std::ios::cur);
            }

            have_fmt = true;
            continue;
        }

        if (id == "data") {
            if (!have_fmt) {
                std::cerr << path << " missing fmt chunk\n";
                return false;
            }
            out.samples.resize(chunk_size / sizeof(int16_t));
            in.read(reinterpret_cast<char*>(out.samples.data()), static_cast<std::streamsize>(chunk_size));
            have_data = true;
            continue;
        }

        in.seekg(static_cast<std::streamoff>(chunk_size), std::ios::cur);
    }

    return have_fmt && have_data;
}

int run(const std::filesystem::path& directory) {
    if (!std::filesystem::exists(directory)) {
        std::cerr << "Directory does not exist: " << directory << "\n";
        return 2;
    }

    bool checked_any = false;
    bool failed = false;

    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".wav") {
            continue;
        }

        WavData wav{};
        if (!load_wav(entry.path(), wav)) {
            failed = true;
            continue;
        }

        checked_any = true;

        double peak = 0.0;
        double sum_sq = 0.0;
        double mean = 0.0;
        double max_jump = 0.0;
        std::size_t clip_count = 0;

        double prev = 0.0;
        bool has_prev = false;

        for (int16_t raw : wav.samples) {
            const double s = static_cast<double>(raw) / 32768.0;
            peak = std::max(peak, std::abs(s));
            sum_sq += s * s;
            mean += s;
            if (std::abs(raw) >= 32766) {
                ++clip_count;
            }

            if (has_prev) {
                max_jump = std::max(max_jump, std::abs(s - prev));
            }
            prev = s;
            has_prev = true;
        }

        const double rms = std::sqrt(sum_sq / static_cast<double>(wav.samples.size()));
        const double dc = mean / static_cast<double>(wav.samples.size());

        std::cout << entry.path().filename().string()
                  << ": peak=" << peak
                  << " rms=" << rms
                  << " dc=" << dc
                  << " max_jump=" << max_jump
                  << " clip_samples=" << clip_count << "\n";

        const bool local_fail = peak > 0.92 || clip_count > 0 || std::abs(dc) > 0.02 || max_jump > 0.92;
        if (local_fail) {
            failed = true;
        }
    }

    if (!checked_any) {
        std::cerr << "No WAV files found in " << directory << "\n";
        return 2;
    }

    if (failed) {
        std::cerr << "FAIL wav health gate\n";
        return 1;
    }

    std::cout << "PASS wav health gate\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const std::filesystem::path dir = (argc >= 2) ? argv[1] : "Samples";
    return run(dir);
}
