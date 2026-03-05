#include <algorithm>
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
        std::cerr << path << " is not a RIFF file\n";
        return false;
    }

    (void)read_u32(in);
    char wave[4]{};
    in.read(wave, 4);
    if (std::string(wave, 4) != "WAVE") {
        std::cerr << path << " is not a WAVE file\n";
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
                std::cerr << path << " format unsupported (need PCM16)\n";
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
                std::cerr << path << " missing fmt chunk before data\n";
                return false;
            }

            out.samples.resize(chunk_size / sizeof(int16_t));
            in.read(reinterpret_cast<char*>(out.samples.data()), static_cast<std::streamsize>(chunk_size));
            have_data = true;
            continue;
        }

        in.seekg(static_cast<std::streamoff>(chunk_size), std::ios::cur);
    }

    if (!have_fmt || !have_data) {
        std::cerr << path << " missing required chunks\n";
        return false;
    }

    return true;
}

int run(const std::filesystem::path& reference_dir, const std::filesystem::path& candidate_dir) {
    if (!std::filesystem::exists(reference_dir) || !std::filesystem::exists(candidate_dir)) {
        std::cerr << "Both directories must exist\n";
        return 2;
    }

    double worst_snr_db = 1e9;
    double worst_peak_diff = 0.0;
    double worst_jump_delta = 0.0;
    double worst_hf_ratio_delta = 0.0;
    double worst_stereo_width_delta = 0.0;

    bool compared_any = false;
    for (const auto& entry : std::filesystem::directory_iterator(reference_dir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".wav") {
            continue;
        }

        const auto candidate_path = candidate_dir / entry.path().filename();
        if (!std::filesystem::exists(candidate_path)) {
            std::cerr << "Missing candidate file: " << candidate_path << "\n";
            return 1;
        }

        WavData ref{};
        WavData cand{};
        if (!load_wav(entry.path(), ref) || !load_wav(candidate_path, cand)) {
            return 1;
        }

        if (ref.channels != cand.channels || ref.sample_rate != cand.sample_rate || ref.samples.size() != cand.samples.size()) {
            std::cerr << "Format mismatch for " << entry.path().filename().string() << "\n";
            return 1;
        }

        compared_any = true;

        double ref_energy = 0.0;
        double err_energy = 0.0;
        double peak_diff = 0.0;
        double ref_max_jump = 0.0;
        double cand_max_jump = 0.0;

        for (std::size_t i = 0; i < ref.samples.size(); ++i) {
            const double a = static_cast<double>(ref.samples[i]) / 32768.0;
            const double b = static_cast<double>(cand.samples[i]) / 32768.0;
            const double e = a - b;

            ref_energy += a * a;
            err_energy += e * e;
            peak_diff = std::max(peak_diff, std::abs(e));

            if (i > 0) {
                const double a_prev = static_cast<double>(ref.samples[i - 1]) / 32768.0;
                const double b_prev = static_cast<double>(cand.samples[i - 1]) / 32768.0;
                ref_max_jump = std::max(ref_max_jump, std::abs(a - a_prev));
                cand_max_jump = std::max(cand_max_jump, std::abs(b - b_prev));
            }
        }

        const double rms_ref = std::sqrt(ref_energy / static_cast<double>(ref.samples.size()));
        const double rms_err = std::sqrt(err_energy / static_cast<double>(ref.samples.size()));
        const double snr_db = (rms_err <= 1e-14) ? 300.0 : 20.0 * std::log10(rms_ref / rms_err);

        // Additional perceptual metrics: brightness proxy and stereo width.
        const std::size_t channels = static_cast<std::size_t>(ref.channels);
        const std::size_t frame_count = channels > 0 ? (ref.samples.size() / channels) : 0;
        double ref_hf_energy = 0.0;
        double ref_total_energy = 0.0;
        double cand_hf_energy = 0.0;
        double cand_total_energy = 0.0;
        double ref_prev1 = 0.0;
        double ref_prev2 = 0.0;
        double cand_prev1 = 0.0;
        double cand_prev2 = 0.0;
        double ref_side_energy = 0.0;
        double ref_mid_energy = 0.0;
        double cand_side_energy = 0.0;
        double cand_mid_energy = 0.0;

        for (std::size_t frame = 0; frame < frame_count; ++frame) {
            double ref_mono = 0.0;
            double cand_mono = 0.0;
            for (std::size_t ch = 0; ch < channels; ++ch) {
                const std::size_t idx = frame * channels + ch;
                ref_mono += static_cast<double>(ref.samples[idx]) / 32768.0;
                cand_mono += static_cast<double>(cand.samples[idx]) / 32768.0;
            }
            ref_mono /= static_cast<double>(channels);
            cand_mono /= static_cast<double>(channels);
            ref_total_energy += ref_mono * ref_mono;
            cand_total_energy += cand_mono * cand_mono;

            if (frame >= 2) {
                const double ref_hp = ref_mono - (2.0 * ref_prev1) + ref_prev2;
                const double cand_hp = cand_mono - (2.0 * cand_prev1) + cand_prev2;
                ref_hf_energy += ref_hp * ref_hp;
                cand_hf_energy += cand_hp * cand_hp;
            }

            ref_prev2 = ref_prev1;
            ref_prev1 = ref_mono;
            cand_prev2 = cand_prev1;
            cand_prev1 = cand_mono;

            if (channels >= 2) {
                const std::size_t idx = frame * channels;
                const double ref_l = static_cast<double>(ref.samples[idx]) / 32768.0;
                const double ref_r = static_cast<double>(ref.samples[idx + 1]) / 32768.0;
                const double cand_l = static_cast<double>(cand.samples[idx]) / 32768.0;
                const double cand_r = static_cast<double>(cand.samples[idx + 1]) / 32768.0;
                const double ref_mid = 0.5 * (ref_l + ref_r);
                const double ref_side = 0.5 * (ref_l - ref_r);
                const double cand_mid = 0.5 * (cand_l + cand_r);
                const double cand_side = 0.5 * (cand_l - cand_r);
                ref_mid_energy += ref_mid * ref_mid;
                ref_side_energy += ref_side * ref_side;
                cand_mid_energy += cand_mid * cand_mid;
                cand_side_energy += cand_side * cand_side;
            }
        }

        const double ref_hf_ratio = ref_hf_energy / (ref_total_energy + 1.0e-12);
        const double cand_hf_ratio = cand_hf_energy / (cand_total_energy + 1.0e-12);
        const double hf_ratio_delta = std::abs(cand_hf_ratio - ref_hf_ratio);
        const double ref_stereo_width = std::sqrt(ref_side_energy / (ref_mid_energy + 1.0e-12));
        const double cand_stereo_width = std::sqrt(cand_side_energy / (cand_mid_energy + 1.0e-12));
        const double stereo_width_delta = std::abs(cand_stereo_width - ref_stereo_width);

        worst_snr_db = std::min(worst_snr_db, snr_db);
        worst_peak_diff = std::max(worst_peak_diff, peak_diff);
        worst_jump_delta = std::max(worst_jump_delta, std::abs(cand_max_jump - ref_max_jump));
        worst_hf_ratio_delta = std::max(worst_hf_ratio_delta, hf_ratio_delta);
        worst_stereo_width_delta = std::max(worst_stereo_width_delta, stereo_width_delta);

        std::cout << entry.path().filename().string() << ": snr_db=" << snr_db
                  << " peak_diff=" << peak_diff
                  << " jump_delta=" << std::abs(cand_max_jump - ref_max_jump)
                  << " hf_ratio_delta=" << hf_ratio_delta
                  << " stereo_width_delta=" << stereo_width_delta << "\n";
    }

    if (!compared_any) {
        std::cerr << "No .wav files found in reference directory\n";
        return 2;
    }

    std::cout << "Summary: worst_snr_db=" << worst_snr_db
              << " worst_peak_diff=" << worst_peak_diff
              << " worst_jump_delta=" << worst_jump_delta
              << " worst_hf_ratio_delta=" << worst_hf_ratio_delta
              << " worst_stereo_width_delta=" << worst_stereo_width_delta << "\n";

    const double min_snr_db = 65.0;
    const double max_peak_diff = 0.03;
    const double max_jump_delta = 0.03;
    const double max_hf_ratio_delta = 0.002;
    const double max_stereo_width_delta = 0.10;

    if (worst_snr_db < min_snr_db
        || worst_peak_diff > max_peak_diff
        || worst_jump_delta > max_jump_delta
        || worst_hf_ratio_delta > max_hf_ratio_delta
        || worst_stereo_width_delta > max_stereo_width_delta) {
        std::cerr << "FAIL quality gate: min_snr_db=" << min_snr_db
                  << " max_peak_diff=" << max_peak_diff
                  << " max_jump_delta=" << max_jump_delta
                  << " max_hf_ratio_delta=" << max_hf_ratio_delta
                  << " max_stereo_width_delta=" << max_stereo_width_delta << "\n";
        return 1;
    }

    std::cout << "PASS quality gate\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: compare_wav_dirs <reference_dir> <candidate_dir>\n";
        return 2;
    }

    return run(argv[1], argv[2]);
}
