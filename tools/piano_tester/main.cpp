#include <SDL2/SDL.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#ifdef VB_TESTER_HAS_ALSA_MIDI
#include <alsa/asoundlib.h>
#endif

#include "vb_engine/c_api.h"

namespace {

constexpr int kNoteLow = 45;   // A2
constexpr int kNoteHigh = 72;  // C5
constexpr int kWindowWidth = 1100;
constexpr int kWindowHeight = 360;
constexpr int kTopBarHeight = 64;
constexpr int kPianoMargin = 18;
constexpr int kRequestedBuffer = 256;
constexpr int kDropdownRowHeight = 26;
constexpr int kDropdownMaxRowsVisible = 8;

struct KeyRect {
    int note{0};
    bool black{false};
    SDL_Rect rect{};
};

struct AudioState {
    std::atomic<vb_engine_handle*> engine{nullptr};
    std::uint32_t max_block_size{1024};
    std::vector<float> left{};
    std::vector<float> right{};
    std::atomic<float> stereo_width_target{1.60F};
    std::atomic<float> output_gain_target{1.85F};
    float stereo_width_current{1.60F};
    float output_gain_current{1.85F};
    float limiter_gain_current{1.0F};
    float limiter_env_current{0.0F};
};

struct MidiSharedState {
    std::array<std::atomic_uint8_t, 128> note_down{};
    std::atomic_uint8_t pedal_cc{0};

    MidiSharedState() {
        for (auto& v : note_down) {
            v.store(0, std::memory_order_relaxed);
        }
        pedal_cc.store(0, std::memory_order_relaxed);
    }
};

struct MidiPortInfo {
    int client{-1};
    int port{-1};
    std::string name{};
};

struct EngineLoadResult {
    vb_engine_handle* engine{nullptr};
    std::uint32_t max_block_size{1024};
    bool success{false};
    std::string error{};
};

enum class UiSlider : std::uint8_t {
    None = 0,
    ImagePost,
    GainPost,
    Presence,
    MicMix,
    ReverbWet,
    SoftPedal,
    ImageEngine,
    GainEngine,
    HumanizeTime,
    HumanizeVelocity,
    StretchStrength,
    PedalThreshold,
    PedalMode,
    RenderBackend,
};

enum class EngineLoadState : std::uint8_t {
    Loading,
    Ready,
    Failed,
};

bool is_black_key(const int midi_note) {
    const int pc = midi_note % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}

std::vector<KeyRect> build_keyboard_layout() {
    std::vector<KeyRect> keys;
    keys.reserve(static_cast<std::size_t>(kNoteHigh - kNoteLow + 1));

    int white_count = 0;
    for (int note = kNoteLow; note <= kNoteHigh; ++note) {
        if (!is_black_key(note)) {
            ++white_count;
        }
    }

    const int piano_top = kTopBarHeight + kPianoMargin;
    const int piano_height = kWindowHeight - piano_top - kPianoMargin;
    const int piano_width = kWindowWidth - (2 * kPianoMargin);
    const float white_width_f = static_cast<float>(piano_width) / static_cast<float>(std::max(1, white_count));
    const int white_width = std::max(18, static_cast<int>(white_width_f));
    const int black_width = static_cast<int>(std::round(static_cast<float>(white_width) * 0.62F));
    const int black_height = static_cast<int>(std::round(static_cast<float>(piano_height) * 0.62F));

    int white_index = 0;
    for (int note = kNoteLow; note <= kNoteHigh; ++note) {
        if (!is_black_key(note)) {
            const int x = kPianoMargin + static_cast<int>(std::round(static_cast<float>(white_index) * white_width_f));
            keys.push_back(KeyRect{
                .note = note,
                .black = false,
                .rect = SDL_Rect{x, piano_top, white_width, piano_height},
            });
            ++white_index;
        } else {
            const int prev_white = std::max(0, white_index - 1);
            const int prev_x = kPianoMargin + static_cast<int>(std::round(static_cast<float>(prev_white) * white_width_f));
            const int x = prev_x + white_width - (black_width / 2);
            keys.push_back(KeyRect{
                .note = note,
                .black = true,
                .rect = SDL_Rect{x, piano_top, black_width, black_height},
            });
        }
    }

    return keys;
}

int env_or_default_int(const char* name, const int fallback) {
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return fallback;
    }
    const int parsed = std::atoi(value);
    return parsed > 0 ? parsed : fallback;
}

float env_or_default_float(const char* name, const float fallback) {
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return fallback;
    }
    const float parsed = std::strtof(value, nullptr);
    return std::isfinite(parsed) ? parsed : fallback;
}

bool point_in_rect(const SDL_Rect& rect, const int x, const int y) {
    return x >= rect.x && x < (rect.x + rect.w) && y >= rect.y && y < (rect.y + rect.h);
}

float slider_from_mouse(const SDL_Rect& rect, const int mouse_x, const float minimum, const float maximum) {
    if (rect.w <= 1) {
        return minimum;
    }
    const float t = std::clamp(static_cast<float>(mouse_x - rect.x) / static_cast<float>(rect.w), 0.0F, 1.0F);
    return minimum + ((maximum - minimum) * t);
}

int cc_from_unit(const float unit) {
    return static_cast<int>(std::lround(std::clamp(unit, 0.0F, 1.0F) * 127.0F));
}

int cc_from_range(const float value, const float minimum, const float maximum) {
    if (maximum <= minimum) {
        return 0;
    }
    const float unit = (value - minimum) / (maximum - minimum);
    return cc_from_unit(unit);
}

int cc_from_mode(const int mode_index) {
    const int mode = std::clamp(mode_index, 0, 2);
    if (mode == 0) {
        return 0;
    }
    if (mode == 1) {
        return 64;
    }
    return 127;
}

std::string zero_pad_int(const int value, const int width) {
    if (value < 0) {
        return std::string(static_cast<std::size_t>(std::max(1, width)), '-');
    }
    std::string out = std::to_string(value);
    while (static_cast<int>(out.size()) < width) {
        out = "0" + out;
    }
    return out;
}

int cc_to_percent_rounded(const int cc_value) {
    if (cc_value < 0) {
        return -1;
    }
    const int clamped = std::clamp(cc_value, 0, 127);
    return static_cast<int>(std::lround((static_cast<float>(clamped) / 127.0F) * 100.0F));
}

struct CcDebugEntry {
    const char* label{"CC"};
    int cc{0};
    int target_value{-1};
    int attempted_value{-1};
    int accepted_value{-1};
    int pending_value{-1};
    vb_engine_result last_result{VB_ENGINE_ERROR_INVALID_ARGUMENT};
    std::uint32_t attempts{0};
    std::uint32_t accepted{0};
    std::uint32_t queue_full{0};
};

void audio_callback(void* userdata, Uint8* stream, int len) {
    auto* state = static_cast<AudioState*>(userdata);
    if (state == nullptr || stream == nullptr || len <= 0) {
        if (stream != nullptr && len > 0) {
            SDL_memset(stream, 0, static_cast<size_t>(len));
        }
        return;
    }

    auto* engine = state->engine.load(std::memory_order_acquire);
    if (engine == nullptr) {
        SDL_memset(stream, 0, static_cast<size_t>(len));
        return;
    }

    const int channels = 2;
    const int frame_count = len / static_cast<int>(sizeof(float) * channels);
    float* out = reinterpret_cast<float*>(stream);

    int rendered = 0;
    float width_current = state->stereo_width_current;
    float gain_current = state->output_gain_current;
    float limiter_gain_current = state->limiter_gain_current;
    float limiter_env = state->limiter_env_current;
    const float width_target = std::clamp(state->stereo_width_target.load(std::memory_order_acquire), 0.50F, 2.00F);
    const float gain_target = std::clamp(state->output_gain_target.load(std::memory_order_acquire), 0.20F, 2.00F);
    const float width_step = (width_target - width_current) / static_cast<float>(std::max(1, frame_count));
    const float gain_step = (gain_target - gain_current) / static_cast<float>(std::max(1, frame_count));
    constexpr float kLimiterCeiling = 0.84F;       // ~ -1.5 dBFS target ceiling.
    constexpr float kLimiterAttack = 0.55F;        // Fast clamp on peaks.
    constexpr float kLimiterRelease = 0.0009F;     // Slow recovery to avoid pumping.
    constexpr float kOutputTrim = 0.92F;           // Keeps startup safely below clip.
    while (rendered < frame_count) {
        const std::uint32_t chunk = static_cast<std::uint32_t>(
            std::min<int>(frame_count - rendered, static_cast<int>(state->max_block_size))
        );

        float* outputs[2]{state->left.data(), state->right.data()};
        const vb_engine_result rc = vb_engine_process(engine, outputs, 2, chunk);
        if (rc != VB_ENGINE_OK) {
            for (std::uint32_t i = 0; i < chunk; ++i) {
                out[(rendered + static_cast<int>(i)) * 2] = 0.0F;
                out[(rendered + static_cast<int>(i)) * 2 + 1] = 0.0F;
            }
        } else {
            for (std::uint32_t i = 0; i < chunk; ++i) {
                width_current += width_step;
                gain_current += gain_step;
                const float in_l = state->left[i];
                const float in_r = state->right[i];
                const float mid = 0.5F * (in_l + in_r);
                const float side = 0.5F * (in_l - in_r) * width_current;
                const auto soft_limit = [](const float x) noexcept {
                    const float ax = std::abs(x);
                    if (ax <= 0.985F) {
                        return x;
                    }
                    const float excess = ax - 0.985F;
                    const float y = 0.985F + (0.015F * std::tanh(excess * 18.0F));
                    return std::copysign(y, x);
                };
                float out_l = (mid + side) * gain_current * kOutputTrim;
                float out_r = (mid - side) * gain_current * kOutputTrim;

                const float abs_peak = std::max(std::abs(out_l), std::abs(out_r));
                if (abs_peak > limiter_env) {
                    limiter_env += kLimiterAttack * (abs_peak - limiter_env);
                } else {
                    limiter_env += kLimiterRelease * (abs_peak - limiter_env);
                }
                const float limiter_target = (limiter_env > kLimiterCeiling)
                    ? (kLimiterCeiling / std::max(limiter_env, 1.0e-6F))
                    : 1.0F;
                limiter_gain_current += kLimiterAttack * (limiter_target - limiter_gain_current);

                out_l = soft_limit(out_l * limiter_gain_current);
                out_r = soft_limit(out_r * limiter_gain_current);
                out[(rendered + static_cast<int>(i)) * 2] = out_l;
                out[(rendered + static_cast<int>(i)) * 2 + 1] = out_r;
            }
        }

        rendered += static_cast<int>(chunk);
    }

    state->stereo_width_current = width_current;
    state->output_gain_current = gain_current;
    state->limiter_gain_current = limiter_gain_current;
    state->limiter_env_current = limiter_env;
}

void draw_filled_circle(SDL_Renderer* renderer, const int cx, const int cy, const int r) {
    for (int y = -r; y <= r; ++y) {
        const int span = static_cast<int>(std::sqrt(static_cast<float>(r * r - y * y)));
        SDL_RenderDrawLine(renderer, cx - span, cy + y, cx + span, cy + y);
    }
}

std::optional<int> hit_test_key(const std::vector<KeyRect>& keys, const int x, const int y) {
    for (const auto& key : keys) {
        if (!key.black) {
            continue;
        }
        if (x >= key.rect.x && x < (key.rect.x + key.rect.w) && y >= key.rect.y && y < (key.rect.y + key.rect.h)) {
            return key.note;
        }
    }

    for (const auto& key : keys) {
        if (key.black) {
            continue;
        }
        if (x >= key.rect.x && x < (key.rect.x + key.rect.w) && y >= key.rect.y && y < (key.rect.y + key.rect.h)) {
            return key.note;
        }
    }

    return std::nullopt;
}

void send_note_on(const std::atomic<vb_engine_handle*>& engine, const int note, const int velocity) {
    auto* handle = engine.load(std::memory_order_acquire);
    if (handle == nullptr) {
        return;
    }
    (void)vb_engine_note_on(handle, 0, static_cast<std::uint8_t>(note), static_cast<std::uint8_t>(velocity));
}

void send_note_off(const std::atomic<vb_engine_handle*>& engine, const int note) {
    auto* handle = engine.load(std::memory_order_acquire);
    if (handle == nullptr) {
        return;
    }
    (void)vb_engine_note_off(handle, 0, static_cast<std::uint8_t>(note), 0);
}

vb_engine_result send_cc(const std::atomic<vb_engine_handle*>& engine, const int cc, const int value) {
    auto* handle = engine.load(std::memory_order_acquire);
    if (handle == nullptr) {
        return VB_ENGINE_ERROR_INVALID_ARGUMENT;
    }
    return vb_engine_control_change(handle, 0, static_cast<std::uint8_t>(cc), static_cast<std::uint8_t>(value));
}

const std::array<std::uint8_t, 7>* glyph_for(char c) {
    using G = std::array<std::uint8_t, 7>;
    static const G kSpace{{0, 0, 0, 0, 0, 0, 0}};
    static const G kDash{{0, 0, 0, 0x1F, 0, 0, 0}};
    static const G kColon{{0, 0x04, 0, 0, 0, 0x04, 0}};
    static const G kHash{{0x0A, 0x1F, 0x0A, 0x0A, 0x1F, 0x0A, 0}};

    static const G k0{{0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}};
    static const G k1{{0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}};
    static const G k2{{0x0E, 0x11, 0x01, 0x06, 0x08, 0x10, 0x1F}};
    static const G k3{{0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E}};
    static const G k4{{0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}};
    static const G k5{{0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E}};
    static const G k6{{0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E}};
    static const G k7{{0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}};
    static const G k8{{0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}};
    static const G k9{{0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E}};

    static const G kA{{0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}};
    static const G kB{{0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}};
    static const G kC{{0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}};
    static const G kD{{0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}};
    static const G kE{{0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}};
    static const G kF{{0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}};
    static const G kG{{0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E}};
    static const G kH{{0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}};
    static const G kI{{0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F}};
    static const G kL{{0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}};
    static const G kM{{0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}};
    static const G kN{{0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11}};
    static const G kO{{0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}};
    static const G kP{{0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}};
    static const G kR{{0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}};
    static const G kS{{0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}};
    static const G kT{{0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}};
    static const G kU{{0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}};
    static const G kV{{0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}};
    static const G kW{{0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}};
    static const G kX{{0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}};
    static const G kY{{0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}};

    switch (c) {
        case ' ': return &kSpace;
        case '-': return &kDash;
        case ':': return &kColon;
        case '#': return &kHash;
        case '0': return &k0;
        case '1': return &k1;
        case '2': return &k2;
        case '3': return &k3;
        case '4': return &k4;
        case '5': return &k5;
        case '6': return &k6;
        case '7': return &k7;
        case '8': return &k8;
        case '9': return &k9;
        case 'A': return &kA;
        case 'B': return &kB;
        case 'C': return &kC;
        case 'D': return &kD;
        case 'E': return &kE;
        case 'F': return &kF;
        case 'G': return &kG;
        case 'H': return &kH;
        case 'I': return &kI;
        case 'L': return &kL;
        case 'M': return &kM;
        case 'N': return &kN;
        case 'O': return &kO;
        case 'P': return &kP;
        case 'R': return &kR;
        case 'S': return &kS;
        case 'T': return &kT;
        case 'U': return &kU;
        case 'V': return &kV;
        case 'W': return &kW;
        case 'X': return &kX;
        case 'Y': return &kY;
        default: return &kSpace;
    }
}

void draw_text(
    SDL_Renderer* renderer,
    const int x,
    const int y,
    const int scale,
    const std::string& text,
    const SDL_Color color
) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    int pen_x = x;
    for (char c : text) {
        const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        const auto* glyph = glyph_for(upper);
        for (int row = 0; row < 7; ++row) {
            const std::uint8_t bits = (*glyph)[static_cast<std::size_t>(row)];
            for (int col = 0; col < 5; ++col) {
                if ((bits & (1u << (4 - col))) == 0) {
                    continue;
                }
                SDL_Rect px{pen_x + (col * scale), y + (row * scale), scale, scale};
                SDL_RenderFillRect(renderer, &px);
            }
        }
        pen_x += (6 * scale);
    }
}

void update_window_title(
    SDL_Window* window,
    const bool pedal_on,
    const EngineLoadState load_state,
    const std::string& midi_name
) {
    std::string status = "LOADING";
    if (load_state == EngineLoadState::Ready) {
        status = "READY";
    } else if (load_state == EngineLoadState::Failed) {
        status = "FAILED";
    }
    const std::string title = "VB Engine Piano Tester (A2-C5) | " + status + " | Pedal: "
        + (pedal_on ? "ON" : "OFF") + " | MIDI: " + midi_name;
    SDL_SetWindowTitle(window, title.c_str());
}

#ifdef VB_TESTER_HAS_ALSA_MIDI

std::vector<MidiPortInfo> enumerate_midi_inputs() {
    std::vector<MidiPortInfo> ports;

    snd_seq_t* seq = nullptr;
    if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0 || seq == nullptr) {
        return ports;
    }

    snd_seq_client_info_t* cinfo = nullptr;
    snd_seq_port_info_t* pinfo = nullptr;
    snd_seq_client_info_alloca(&cinfo);
    snd_seq_port_info_alloca(&pinfo);

    snd_seq_client_info_set_client(cinfo, -1);
    while (snd_seq_query_next_client(seq, cinfo) >= 0) {
        const int client = snd_seq_client_info_get_client(cinfo);
        snd_seq_port_info_set_client(pinfo, client);
        snd_seq_port_info_set_port(pinfo, -1);

        while (snd_seq_query_next_port(seq, pinfo) >= 0) {
            const unsigned int caps = snd_seq_port_info_get_capability(pinfo);
            if ((caps & SND_SEQ_PORT_CAP_READ) == 0u || (caps & SND_SEQ_PORT_CAP_SUBS_READ) == 0u) {
                continue;
            }

            MidiPortInfo info{};
            info.client = client;
            info.port = snd_seq_port_info_get_port(pinfo);
            const std::string client_name = snd_seq_client_info_get_name(cinfo);
            const std::string port_name = snd_seq_port_info_get_name(pinfo);
            info.name = client_name + ":" + port_name;
            ports.push_back(std::move(info));
        }
    }

    snd_seq_close(seq);
    std::sort(ports.begin(), ports.end(), [](const MidiPortInfo& a, const MidiPortInfo& b) {
        return a.name < b.name;
    });
    return ports;
}

struct MidiWorker {
    std::vector<MidiPortInfo> ports{};
    std::atomic<int> selected_index{-1};
    std::atomic<bool> running{false};
    std::thread thread{};
    std::atomic<vb_engine_handle*>* engine{nullptr};
    MidiSharedState* shared{nullptr};

    void refresh_ports() {
        ports = enumerate_midi_inputs();
        const int current = selected_index.load(std::memory_order_relaxed);
        if (current >= static_cast<int>(ports.size())) {
            selected_index.store(-1, std::memory_order_relaxed);
        }
    }

    void start(std::atomic<vb_engine_handle*>* engine_ptr, MidiSharedState* state) {
        engine = engine_ptr;
        shared = state;
        running.store(true, std::memory_order_release);
        thread = std::thread([this]() { run(); });
    }

    void stop() {
        running.store(false, std::memory_order_release);
        if (thread.joinable()) {
            thread.join();
        }
    }

private:
    void run() {
        snd_seq_t* seq = nullptr;
        if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_INPUT, 0) < 0 || seq == nullptr) {
            return;
        }
        snd_seq_nonblock(seq, 1);
        snd_seq_set_client_name(seq, "VB Engine Piano Tester MIDI");

        const int in_port = snd_seq_create_simple_port(
            seq,
            "input",
            SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
            SND_SEQ_PORT_TYPE_APPLICATION
        );
        if (in_port < 0) {
            snd_seq_close(seq);
            return;
        }

        int connected_index = -1;
        while (running.load(std::memory_order_acquire)) {
            const int wanted = selected_index.load(std::memory_order_acquire);
            if (wanted != connected_index) {
                if (connected_index >= 0 && connected_index < static_cast<int>(ports.size())) {
                    const auto& old_port = ports[static_cast<std::size_t>(connected_index)];
                    (void)snd_seq_disconnect_from(seq, in_port, old_port.client, old_port.port);
                }

                connected_index = -1;
                if (wanted >= 0 && wanted < static_cast<int>(ports.size())) {
                    const auto& new_port = ports[static_cast<std::size_t>(wanted)];
                    if (snd_seq_connect_from(seq, in_port, new_port.client, new_port.port) >= 0) {
                        connected_index = wanted;
                    }
                }

                if (shared != nullptr) {
                    shared->pedal_cc.store(0, std::memory_order_release);
                }
            }

            snd_seq_event_t* ev = nullptr;
            const int rc = snd_seq_event_input(seq, &ev);
            if (rc <= 0 || ev == nullptr) {
                SDL_Delay(1);
                continue;
            }

            auto* handle = engine != nullptr ? engine->load(std::memory_order_acquire) : nullptr;

            switch (ev->type) {
                case SND_SEQ_EVENT_NOTEON: {
                    const int note = ev->data.note.note;
                    const int vel = ev->data.note.velocity;
                    if (note >= 0 && note < 128 && shared != nullptr) {
                        shared->note_down[static_cast<std::size_t>(note)].store(
                            static_cast<std::uint8_t>(vel > 0 ? 1 : 0),
                            std::memory_order_release
                        );
                    }
                    if (handle != nullptr) {
                        if (vel > 0) {
                            (void)vb_engine_note_on(handle, 0, static_cast<std::uint8_t>(note), static_cast<std::uint8_t>(vel));
                        } else {
                            (void)vb_engine_note_off(handle, 0, static_cast<std::uint8_t>(note), 0);
                        }
                    }
                    break;
                }
                case SND_SEQ_EVENT_NOTEOFF: {
                    const int note = ev->data.note.note;
                    if (note >= 0 && note < 128 && shared != nullptr) {
                        shared->note_down[static_cast<std::size_t>(note)].store(0, std::memory_order_release);
                    }
                    if (handle != nullptr) {
                        (void)vb_engine_note_off(handle, 0, static_cast<std::uint8_t>(note), 0);
                    }
                    break;
                }
                case SND_SEQ_EVENT_CONTROLLER: {
                    const int cc = ev->data.control.param;
                    const int value = ev->data.control.value;
                    if (cc == 64 && shared != nullptr) {
                        shared->pedal_cc.store(static_cast<std::uint8_t>(std::clamp(value, 0, 127)), std::memory_order_release);
                    }
                    if (handle != nullptr) {
                        (void)vb_engine_control_change(
                            handle,
                            0,
                            static_cast<std::uint8_t>(std::clamp(cc, 0, 127)),
                            static_cast<std::uint8_t>(std::clamp(value, 0, 127))
                        );
                    }
                    break;
                }
                default:
                    break;
            }
            snd_seq_free_event(ev);
        }

        if (connected_index >= 0 && connected_index < static_cast<int>(ports.size())) {
            const auto& old_port = ports[static_cast<std::size_t>(connected_index)];
            (void)snd_seq_disconnect_from(seq, in_port, old_port.client, old_port.port);
        }
        snd_seq_close(seq);
    }
};

#else

struct MidiWorker {
    std::vector<MidiPortInfo> ports{};
    std::atomic<int> selected_index{-1};
    void refresh_ports() { ports.clear(); }
    void start(std::atomic<vb_engine_handle*>*, MidiSharedState*) {}
    void stop() {}
};

#endif

}  // namespace

int main() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        std::cerr << "SDL init failed: " << SDL_GetError() << "\n";
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "VB Engine Piano Tester (A2-C5)",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        kWindowWidth,
        kWindowHeight,
        SDL_WINDOW_SHOWN
    );
    if (window == nullptr) {
        std::cerr << "Window create failed: " << SDL_GetError() << "\n";
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (renderer == nullptr) {
        std::cerr << "Renderer create failed: " << SDL_GetError() << "\n";
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    const auto keys = build_keyboard_layout();
    AudioState audio_state{};
    MidiSharedState midi_shared{};
    MidiWorker midi_worker{};
    midi_worker.refresh_ports();

    const SDL_Rect pedal_circle_rect{kWindowWidth - 54, 20, 24, 24};
    const SDL_Rect midi_dropdown_rect{18, 18, 230, 28};
    const SDL_Rect settings_button_rect{272, 18, 132, 28};
    bool dropdown_open = false;
    int selected_midi_index = midi_worker.ports.empty() ? -1 : 0;
    midi_worker.selected_index.store(selected_midi_index, std::memory_order_release);
    UiSlider active_slider = UiSlider::None;
    std::uint32_t active_slider_window_id = 0;
    SDL_Window* settings_window = nullptr;
    SDL_Renderer* settings_renderer = nullptr;
    const std::uint32_t main_window_id = SDL_GetWindowID(window);
    std::uint32_t settings_window_id = 0;

    constexpr float kImageMin = 0.70F;
    constexpr float kImageMax = 1.90F;
    constexpr float kGainMin = 0.75F;
    constexpr float kGainMax = 1.90F;
    constexpr float kPresenceMin = 0.10F;
    constexpr float kPresenceMax = 1.00F;
    constexpr float kSoftPedalMin = 0.0F;
    constexpr float kSoftPedalMax = 1.0F;
    constexpr float kMicMixMin = 0.0F;
    constexpr float kMicMixMax = 1.0F;
    constexpr float kReverbWetMin = 0.0F;
    constexpr float kReverbWetMax = 0.20F;
    constexpr float kImageEngineMin = 0.70F;
    constexpr float kImageEngineMax = 2.00F;
    constexpr float kGainEngineMin = 0.70F;
    constexpr float kGainEngineMax = 2.00F;
    constexpr float kHumanizeTimeMin = 0.0F;
    constexpr float kHumanizeTimeMax = 5.0F;
    constexpr float kHumanizeVelocityMin = 0.0F;
    constexpr float kHumanizeVelocityMax = 6.0F;
    constexpr float kStretchMin = 0.75F;
    constexpr float kStretchMax = 1.50F;
    constexpr float kPedalThresholdMin = 1.0F;
    constexpr float kPedalThresholdMax = 127.0F;
    constexpr float kPedalModeMin = 0.0F;
    constexpr float kPedalModeMax = 2.0F;
    constexpr float kRenderBackendMin = 0.0F;
    constexpr float kRenderBackendMax = 2.0F;
    constexpr float kNaturalImagePost = 1.40F;
    constexpr float kNaturalGainPost = 1.72F;
    constexpr float kNaturalPresence = 0.58F;
    constexpr float kNaturalMicMix = 0.28F;
    constexpr float kNaturalReverbWet = 0.04F;
    constexpr float kNaturalSoftPedal = 0.22F;
    constexpr float kNaturalImageEngine = 1.10F;
    constexpr float kNaturalGainEngine = 1.00F;
    constexpr float kNaturalHumanizeTime = 1.20F;
    constexpr float kNaturalHumanizeVelocity = 1.80F;
    constexpr float kNaturalStretch = 0.96F;
    constexpr float kNaturalPedalThreshold = 64.0F;
    constexpr int kNaturalPedalMode = 0;
    constexpr int kNaturalRenderBackend = 0;
    float image_value = 1.60F;
    float gain_value = 1.45F;
    float presence_value = 0.72F;
    float soft_pedal_value = 0.50F;
    float mic_mix_value = 0.20F;
    float reverb_wet_value = 0.04F;
    float image_engine_value = 1.25F;
    float gain_engine_value = 1.00F;
    float humanize_time_value = 1.20F;
    float humanize_velocity_value = 2.00F;
    float stretch_value = 0.90F;
    float pedal_threshold_value = 64.0F;
    int pedal_mode_value = 0;
    int render_backend_value = 0;
    float custom_image_value = image_value;
    float custom_gain_value = gain_value;
    float custom_presence_value = presence_value;
    float custom_mic_mix_value = mic_mix_value;
    float custom_reverb_wet_value = reverb_wet_value;
    float custom_soft_pedal_value = soft_pedal_value;
    float custom_image_engine_value = image_engine_value;
    float custom_gain_engine_value = gain_engine_value;
    float custom_humanize_time_value = humanize_time_value;
    float custom_humanize_velocity_value = humanize_velocity_value;
    float custom_stretch_value = stretch_value;
    float custom_pedal_threshold_value = pedal_threshold_value;
    int custom_pedal_mode_value = pedal_mode_value;
    int custom_render_backend_value = render_backend_value;

    std::optional<int> mouse_note = std::nullopt;
    SDL_AudioDeviceID device = 0;
    int active_backend_diag = -1;
    std::uint64_t gpu_fallback_diag = 0;

    std::atomic<EngineLoadState> load_state{EngineLoadState::Loading};
    std::atomic<bool> loader_done{false};
    EngineLoadResult load_result{};

    const int requested_rate = env_or_default_int("VB_TESTER_SAMPLE_RATE", 48000);
    const int requested_buffer = env_or_default_int("VB_TESTER_BUFFER", kRequestedBuffer);
    const int requested_voices = env_or_default_int("VB_TESTER_MAX_VOICES", 96);
    const int requested_pedal_noise = env_or_default_int("VB_TESTER_PEDAL_NOISE", 0);
    const int requested_streaming = env_or_default_int("VB_TESTER_STREAMING", 1);
    const int requested_stream_threshold = env_or_default_int("VB_TESTER_STREAM_THRESHOLD", 65536);
    const float requested_reverb_wet = std::clamp(env_or_default_float("VB_TESTER_REVERB_WET", 0.04F), 0.0F, 0.20F);
    const float requested_mic_mix = std::clamp(env_or_default_float("VB_TESTER_MIC_MIX", 0.28F), 0.0F, 1.0F);
    const float requested_presence = std::clamp(env_or_default_float("VB_TESTER_PRESENCE", 0.58F), kPresenceMin, kPresenceMax);
    const float requested_stereo_width = std::clamp(env_or_default_float("VB_TESTER_STEREO_WIDTH", 1.60F), kImageMin, kImageMax);
    const float requested_output_gain = std::clamp(env_or_default_float("VB_TESTER_OUTPUT_GAIN", 1.45F), kGainMin, kGainMax);
    const float requested_soft_pedal = std::clamp(env_or_default_float("VB_TESTER_SOFT_PEDAL", 0.50F), 0.0F, 1.0F);
    const float requested_image_engine = std::clamp(env_or_default_float("VB_TESTER_IMAGE_ENGINE", 1.25F), kImageEngineMin, kImageEngineMax);
    const float requested_gain_engine = std::clamp(env_or_default_float("VB_TESTER_GAIN_ENGINE", 1.00F), kGainEngineMin, kGainEngineMax);
    const float requested_humanize_time = std::clamp(env_or_default_float("VB_TESTER_HUMANIZE_TIME_MS", 1.20F), kHumanizeTimeMin, kHumanizeTimeMax);
    const float requested_humanize_velocity = std::clamp(env_or_default_float("VB_TESTER_HUMANIZE_VELOCITY", 2.00F), kHumanizeVelocityMin, kHumanizeVelocityMax);
    const float requested_stretch = std::clamp(env_or_default_float("VB_TESTER_STRETCH", 0.96F), kStretchMin, kStretchMax);
    const float requested_pedal_threshold = std::clamp(env_or_default_float("VB_TESTER_PEDAL_THRESHOLD", 64.0F), kPedalThresholdMin, kPedalThresholdMax);
    const int requested_pedal_mode = std::clamp(env_or_default_int("VB_TESTER_PEDAL_MODE", 0), 0, 2);
    const int requested_render_backend = std::clamp(env_or_default_int("VB_TESTER_RENDER_BACKEND", 0), 0, 2);
    const float requested_fem_mix = std::clamp(env_or_default_float("VB_TESTER_FEM_MIX", 0.26F), 0.0F, 1.0F);
    const float requested_fem_brightness = std::clamp(env_or_default_float("VB_TESTER_FEM_BRIGHTNESS", 0.54F), 0.0F, 1.0F);
    presence_value = requested_presence;
    image_value = requested_stereo_width;
    gain_value = requested_output_gain;
    soft_pedal_value = requested_soft_pedal;
    mic_mix_value = requested_mic_mix;
    reverb_wet_value = requested_reverb_wet;
    image_engine_value = requested_image_engine;
    gain_engine_value = requested_gain_engine;
    humanize_time_value = requested_humanize_time;
    humanize_velocity_value = requested_humanize_velocity;
    stretch_value = requested_stretch;
    pedal_threshold_value = requested_pedal_threshold;
    pedal_mode_value = requested_pedal_mode;
    render_backend_value = requested_render_backend;
    custom_image_value = image_value;
    custom_gain_value = gain_value;
    custom_presence_value = presence_value;
    custom_mic_mix_value = mic_mix_value;
    custom_reverb_wet_value = reverb_wet_value;
    custom_soft_pedal_value = soft_pedal_value;
    custom_image_engine_value = image_engine_value;
    custom_gain_engine_value = gain_engine_value;
    custom_humanize_time_value = humanize_time_value;
    custom_humanize_velocity_value = humanize_velocity_value;
    custom_stretch_value = stretch_value;
    custom_pedal_threshold_value = pedal_threshold_value;
    custom_pedal_mode_value = pedal_mode_value;
    custom_render_backend_value = render_backend_value;
    audio_state.stereo_width_target.store(image_value, std::memory_order_release);
    audio_state.output_gain_target.store(gain_value, std::memory_order_release);
    audio_state.stereo_width_current = image_value;
    audio_state.output_gain_current = gain_value;
    audio_state.limiter_gain_current = 1.0F;
    audio_state.limiter_env_current = 0.0F;

    std::string sfz_path_storage;
    if (const char* env = std::getenv("VB_PIANO_SFZ_PATH"); env != nullptr && env[0] != '\0') {
        sfz_path_storage = env;
    } else if (std::filesystem::exists("Samples/Piano-Library/default.sfz")) {
        sfz_path_storage = "Samples/Piano-Library/default.sfz";
    }

    std::thread loader([&]() {
        vb_engine_config config{};
        vb_engine_default_config(&config);
        config.max_block_size = 1024;
        config.max_voices = static_cast<std::uint32_t>(requested_voices);
        config.default_instrument = VB_INSTRUMENT_ACOUSTIC_GRAND_PIANO;
        config.piano_reverb_wet = requested_reverb_wet;
        config.piano_mic_mix = requested_mic_mix;
        config.piano_humanize_timing_ms = 0.0F;
        config.piano_humanize_velocity = 0.0F;
        config.piano_disk_streaming_enabled = requested_streaming > 0 ? 1 : 0;
        config.piano_disk_stream_threshold_frames = static_cast<std::uint32_t>(std::clamp(requested_stream_threshold, 4096, 524288));
        config.piano_pedal_noise_enabled = requested_pedal_noise > 0 ? 1 : 0;
        config.piano_render_backend = static_cast<std::uint32_t>(requested_render_backend);
        config.piano_fem_mix = requested_fem_mix;
        config.piano_fem_brightness = requested_fem_brightness;
        config.sample_rate = static_cast<double>(requested_rate);

        std::string loader_sfz = sfz_path_storage;
        if (!loader_sfz.empty()) {
            config.piano_sfz_path = loader_sfz.c_str();
        }

        vb_engine_handle* engine = nullptr;
        if (vb_engine_create(&config, &engine) != VB_ENGINE_OK || engine == nullptr) {
            load_result.success = false;
            load_result.error = "vb_engine_create failed";
            loader_done.store(true, std::memory_order_release);
            return;
        }

        load_result.engine = engine;
        load_result.max_block_size = config.max_block_size;
        load_result.success = true;
        loader_done.store(true, std::memory_order_release);
    });

    bool midi_started = false;
    bool running = true;
    const bool log_enabled = env_or_default_int("VB_TESTER_LOG", 1) > 0;
    int last_logged_active_backend = -999;
    std::uint64_t last_logged_gpu_fallback = 0;
    int render_backend_resend_ticks = 0;
    auto last_diag_poll = std::chrono::steady_clock::now();
    auto backend_label = [](const int value) -> const char* {
        if (value == 0) {
            return "AUTO";
        }
        if (value == 1) {
            return "HYBRID";
        }
        if (value == 2) {
            return "FULL_FEM";
        }
        return "UNKNOWN";
    };

    auto close_settings_window = [&]() {
        if (settings_renderer != nullptr) {
            SDL_DestroyRenderer(settings_renderer);
            settings_renderer = nullptr;
        }
        if (settings_window != nullptr) {
            SDL_DestroyWindow(settings_window);
            settings_window = nullptr;
        }
        settings_window_id = 0;
        active_slider = UiSlider::None;
        active_slider_window_id = 0;
    };

    auto open_settings_window = [&]() {
        if (settings_window != nullptr) {
            return;
        }
        settings_window = SDL_CreateWindow(
            "VB Engine Piano Tester Settings",
            SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED,
            560,
            1080,
            SDL_WINDOW_SHOWN
        );
        if (settings_window == nullptr) {
            return;
        }
        settings_renderer = SDL_CreateRenderer(settings_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (settings_renderer == nullptr) {
            SDL_DestroyWindow(settings_window);
            settings_window = nullptr;
            return;
        }
        settings_window_id = SDL_GetWindowID(settings_window);
    };

    std::array<CcDebugEntry, 13> cc_debug{{
        CcDebugEntry{"PEDAL", 64},
        CcDebugEntry{"SOFT", 67},
        CcDebugEntry{"REVERB", 71},
        CcDebugEntry{"MIX", 73},
        CcDebugEntry{"PRES", 74},
        CcDebugEntry{"IMAGE", 75},
        CcDebugEntry{"GAIN", 76},
        CcDebugEntry{"HTIME", 77},
        CcDebugEntry{"HVEL", 78},
        CcDebugEntry{"STRET", 79},
        CcDebugEntry{"PTHR", 80},
        CcDebugEntry{"PMODE", 81},
        CcDebugEntry{"RMODE", 82},
    }};

    const auto find_cc_debug = [&](const int cc) -> CcDebugEntry* {
        for (auto& entry : cc_debug) {
            if (entry.cc == cc) {
                return &entry;
            }
        }
        return nullptr;
    };

    const auto send_cc_tracked = [&](const int cc, const int value) {
        const int clamped = std::clamp(value, 0, 127);
        CcDebugEntry* entry = find_cc_debug(cc);
        if (entry != nullptr) {
            entry->target_value = clamped;
            entry->attempted_value = clamped;
            ++entry->attempts;
        }

        const vb_engine_result rc = send_cc(audio_state.engine, cc, clamped);
        if (entry != nullptr) {
            entry->last_result = rc;
            if (rc == VB_ENGINE_OK) {
                entry->accepted_value = clamped;
                entry->pending_value = -1;
                ++entry->accepted;
            } else {
                if (rc == VB_ENGINE_ERROR_QUEUE_FULL) {
                    ++entry->queue_full;
                }
                entry->pending_value = clamped;
            }
        }
    };

    const auto flush_pending_cc = [&]() {
        for (auto& entry : cc_debug) {
            if (entry.pending_value < 0) {
                continue;
            }
            entry.attempted_value = entry.pending_value;
            ++entry.attempts;
            const vb_engine_result rc = send_cc(audio_state.engine, entry.cc, entry.pending_value);
            entry.last_result = rc;
            if (rc == VB_ENGINE_OK) {
                entry.accepted_value = entry.pending_value;
                entry.pending_value = -1;
                ++entry.accepted;
            } else if (rc == VB_ENGINE_ERROR_QUEUE_FULL) {
                ++entry.queue_full;
            }
        }
    };

    const auto apply_image_post = [&](const float value, const bool remember_custom) {
        image_value = std::clamp(value, kImageMin, kImageMax);
        audio_state.stereo_width_target.store(image_value, std::memory_order_release);
        if (remember_custom) {
            custom_image_value = image_value;
        }
    };
    const auto apply_gain_post = [&](const float value, const bool remember_custom) {
        gain_value = std::clamp(value, kGainMin, kGainMax);
        audio_state.output_gain_target.store(gain_value, std::memory_order_release);
        if (remember_custom) {
            custom_gain_value = gain_value;
        }
    };
    const auto apply_presence = [&](const float value, const bool remember_custom) {
        presence_value = std::clamp(value, kPresenceMin, kPresenceMax);
        send_cc_tracked(74, cc_from_unit(presence_value));
        if (remember_custom) {
            custom_presence_value = presence_value;
        }
    };
    const auto apply_mic_mix = [&](const float value, const bool remember_custom) {
        mic_mix_value = std::clamp(value, kMicMixMin, kMicMixMax);
        send_cc_tracked(73, cc_from_unit(mic_mix_value));
        if (remember_custom) {
            custom_mic_mix_value = mic_mix_value;
        }
    };
    const auto apply_reverb_wet = [&](const float value, const bool remember_custom) {
        reverb_wet_value = std::clamp(value, kReverbWetMin, kReverbWetMax);
        send_cc_tracked(71, cc_from_range(reverb_wet_value, kReverbWetMin, kReverbWetMax));
        if (remember_custom) {
            custom_reverb_wet_value = reverb_wet_value;
        }
    };
    const auto apply_soft_pedal = [&](const float value, const bool remember_custom) {
        soft_pedal_value = std::clamp(value, kSoftPedalMin, kSoftPedalMax);
        send_cc_tracked(67, cc_from_unit(soft_pedal_value));
        if (remember_custom) {
            custom_soft_pedal_value = soft_pedal_value;
        }
    };
    const auto apply_image_engine = [&](const float value, const bool remember_custom) {
        image_engine_value = std::clamp(value, kImageEngineMin, kImageEngineMax);
        send_cc_tracked(75, cc_from_range(image_engine_value, kImageEngineMin, kImageEngineMax));
        if (remember_custom) {
            custom_image_engine_value = image_engine_value;
        }
    };
    const auto apply_gain_engine = [&](const float value, const bool remember_custom) {
        gain_engine_value = std::clamp(value, kGainEngineMin, kGainEngineMax);
        send_cc_tracked(76, cc_from_range(gain_engine_value, kGainEngineMin, kGainEngineMax));
        if (remember_custom) {
            custom_gain_engine_value = gain_engine_value;
        }
    };
    const auto apply_humanize_time = [&](const float value, const bool remember_custom) {
        humanize_time_value = std::clamp(value, kHumanizeTimeMin, kHumanizeTimeMax);
        send_cc_tracked(77, cc_from_range(humanize_time_value, kHumanizeTimeMin, kHumanizeTimeMax));
        if (remember_custom) {
            custom_humanize_time_value = humanize_time_value;
        }
    };
    const auto apply_humanize_velocity = [&](const float value, const bool remember_custom) {
        humanize_velocity_value = std::clamp(value, kHumanizeVelocityMin, kHumanizeVelocityMax);
        send_cc_tracked(78, cc_from_range(humanize_velocity_value, kHumanizeVelocityMin, kHumanizeVelocityMax));
        if (remember_custom) {
            custom_humanize_velocity_value = humanize_velocity_value;
        }
    };
    const auto apply_stretch = [&](const float value, const bool remember_custom) {
        stretch_value = std::clamp(value, kStretchMin, kStretchMax);
        send_cc_tracked(79, cc_from_range(stretch_value, kStretchMin, kStretchMax));
        if (remember_custom) {
            custom_stretch_value = stretch_value;
        }
    };
    const auto apply_pedal_threshold = [&](const float value, const bool remember_custom) {
        pedal_threshold_value = std::clamp(value, kPedalThresholdMin, kPedalThresholdMax);
        send_cc_tracked(80, cc_from_range(pedal_threshold_value, kPedalThresholdMin, kPedalThresholdMax));
        if (remember_custom) {
            custom_pedal_threshold_value = pedal_threshold_value;
        }
    };
    const auto apply_pedal_mode = [&](const int value, const bool remember_custom) {
        pedal_mode_value = std::clamp(value, 0, 2);
        send_cc_tracked(81, cc_from_mode(pedal_mode_value));
        if (remember_custom) {
            custom_pedal_mode_value = pedal_mode_value;
        }
    };
    const auto apply_render_backend = [&](const int value, const bool remember_custom) {
        render_backend_value = std::clamp(value, 0, 2);
        send_cc_tracked(82, cc_from_mode(render_backend_value));
        render_backend_resend_ticks = 90;
        if (remember_custom) {
            custom_render_backend_value = render_backend_value;
        }
    };

    const auto apply_natural_all = [&]() {
        apply_image_post(kNaturalImagePost, false);
        apply_gain_post(kNaturalGainPost, false);
        apply_presence(kNaturalPresence, false);
        apply_mic_mix(kNaturalMicMix, false);
        apply_reverb_wet(kNaturalReverbWet, false);
        apply_soft_pedal(kNaturalSoftPedal, false);
        apply_image_engine(kNaturalImageEngine, false);
        apply_gain_engine(kNaturalGainEngine, false);
        apply_humanize_time(kNaturalHumanizeTime, false);
        apply_humanize_velocity(kNaturalHumanizeVelocity, false);
        apply_stretch(kNaturalStretch, false);
        apply_pedal_threshold(kNaturalPedalThreshold, false);
        apply_pedal_mode(kNaturalPedalMode, false);
        apply_render_backend(kNaturalRenderBackend, false);
    };
    const auto apply_custom_all = [&]() {
        apply_image_post(custom_image_value, false);
        apply_gain_post(custom_gain_value, false);
        apply_presence(custom_presence_value, false);
        apply_mic_mix(custom_mic_mix_value, false);
        apply_reverb_wet(custom_reverb_wet_value, false);
        apply_soft_pedal(custom_soft_pedal_value, false);
        apply_image_engine(custom_image_engine_value, false);
        apply_gain_engine(custom_gain_engine_value, false);
        apply_humanize_time(custom_humanize_time_value, false);
        apply_humanize_velocity(custom_humanize_velocity_value, false);
        apply_stretch(custom_stretch_value, false);
        apply_pedal_threshold(custom_pedal_threshold_value, false);
        apply_pedal_mode(custom_pedal_mode_value, false);
        apply_render_backend(custom_render_backend_value, false);
    };

    while (running) {
        if (loader_done.load(std::memory_order_acquire) && load_state.load(std::memory_order_relaxed) == EngineLoadState::Loading) {
            if (!load_result.success || load_result.engine == nullptr) {
                load_state.store(EngineLoadState::Failed, std::memory_order_release);
            } else {
                SDL_AudioSpec desired{};
                desired.freq = requested_rate;
                desired.format = AUDIO_F32SYS;
                desired.channels = 2;
                desired.samples = static_cast<Uint16>(std::clamp(requested_buffer, 128, 2048));
                desired.callback = audio_callback;
                desired.userdata = &audio_state;

                SDL_AudioSpec obtained{};
                device = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
                if (device == 0) {
                    vb_engine_destroy(load_result.engine);
                    load_result.engine = nullptr;
                    load_result.success = false;
                    load_result.error = std::string("OpenAudioDevice failed: ") + SDL_GetError();
                    load_state.store(EngineLoadState::Failed, std::memory_order_release);
                } else {
                    audio_state.max_block_size = load_result.max_block_size;
                    audio_state.left.assign(audio_state.max_block_size, 0.0F);
                    audio_state.right.assign(audio_state.max_block_size, 0.0F);
                    SDL_LockAudioDevice(device);
                    audio_state.engine.store(load_result.engine, std::memory_order_release);
                    SDL_UnlockAudioDevice(device);
                    send_cc_tracked(74, cc_from_unit(presence_value));
                    send_cc_tracked(67, cc_from_unit(soft_pedal_value));
                    send_cc_tracked(73, cc_from_unit(mic_mix_value));
                    send_cc_tracked(71, cc_from_range(reverb_wet_value, kReverbWetMin, kReverbWetMax));
                    send_cc_tracked(75, cc_from_range(image_engine_value, kImageEngineMin, kImageEngineMax));
                    send_cc_tracked(76, cc_from_range(gain_engine_value, kGainEngineMin, kGainEngineMax));
                    send_cc_tracked(77, cc_from_range(humanize_time_value, kHumanizeTimeMin, kHumanizeTimeMax));
                    send_cc_tracked(78, cc_from_range(humanize_velocity_value, kHumanizeVelocityMin, kHumanizeVelocityMax));
                    send_cc_tracked(79, cc_from_range(stretch_value, kStretchMin, kStretchMax));
                    send_cc_tracked(80, cc_from_range(pedal_threshold_value, kPedalThresholdMin, kPedalThresholdMax));
                    send_cc_tracked(81, cc_from_mode(pedal_mode_value));
                    send_cc_tracked(82, cc_from_mode(render_backend_value));
                    SDL_PauseAudioDevice(device, 0);
                    if (log_enabled) {
                        std::cerr
                            << "[tester] ready"
                            << " sample_rate=" << requested_rate
                            << " buffer=" << requested_buffer
                            << " voices=" << requested_voices
                            << " req_backend=" << backend_label(render_backend_value)
                            << " gain_post=" << gain_value
                            << " image_post=" << image_value
                            << " presence=" << presence_value
                            << "\n";
                    }
                    load_state.store(EngineLoadState::Ready, std::memory_order_release);
                }
            }
        }

        if (!midi_started && load_state.load(std::memory_order_acquire) == EngineLoadState::Ready) {
            midi_worker.start(&audio_state.engine, &midi_shared);
            midi_started = true;
        }

        flush_pending_cc();

        if (render_backend_resend_ticks > 0) {
            if ((render_backend_resend_ticks % 6) == 0) {
                send_cc_tracked(82, cc_from_mode(render_backend_value));
            }
            --render_backend_resend_ticks;
        }

        const auto now = std::chrono::steady_clock::now();
        if (load_state.load(std::memory_order_acquire) == EngineLoadState::Ready
            && std::chrono::duration_cast<std::chrono::milliseconds>(now - last_diag_poll).count() >= 80) {
            last_diag_poll = now;
            auto* handle = audio_state.engine.load(std::memory_order_acquire);
            if (handle != nullptr) {
                vb_engine_diagnostics diagnostics{};
                diagnostics.struct_size = sizeof(vb_engine_diagnostics);
                if (vb_engine_get_diagnostics(handle, &diagnostics) == VB_ENGINE_OK) {
                    active_backend_diag = static_cast<int>(diagnostics.active_render_backend);
                    gpu_fallback_diag = diagnostics.gpu_fallback_blocks;
                    if (log_enabled) {
                        if (active_backend_diag != last_logged_active_backend) {
                            std::cerr
                                << "[tester] active_backend=" << backend_label(active_backend_diag)
                                << " req_backend=" << backend_label(render_backend_value)
                                << " gpu_fallback_blocks=" << gpu_fallback_diag
                                << "\n";
                            last_logged_active_backend = active_backend_diag;
                        }
                        if (gpu_fallback_diag != last_logged_gpu_fallback) {
                            std::cerr
                                << "[tester] gpu_fallback_blocks="
                                << gpu_fallback_diag
                                << " (+"
                                << (gpu_fallback_diag - last_logged_gpu_fallback)
                                << ") active_backend="
                                << backend_label(active_backend_diag)
                                << "\n";
                            last_logged_gpu_fallback = gpu_fallback_diag;
                        }
                    }
                }
            }
        }

        const std::uint8_t pedal_cc = midi_shared.pedal_cc.load(std::memory_order_acquire);
        const bool pedal_on = pedal_cc >= 64;
        std::string midi_name = "NONE";
        if (selected_midi_index >= 0 && selected_midi_index < static_cast<int>(midi_worker.ports.size())) {
            midi_name = midi_worker.ports[static_cast<std::size_t>(selected_midi_index)].name;
        }
        update_window_title(window, pedal_on, load_state.load(std::memory_order_acquire), midi_name);

        const SDL_Rect global_nat_rect{354, 14, 84, 24};
        const SDL_Rect global_cus_rect{446, 14, 84, 24};
        const SDL_Rect set_image_post_rect{20, 52, 416, 34};
        const SDL_Rect set_gain_post_rect{20, 96, 416, 34};
        const SDL_Rect set_presence_rect{20, 140, 416, 34};
        const SDL_Rect set_mic_mix_rect{20, 184, 416, 34};
        const SDL_Rect set_reverb_rect{20, 228, 416, 34};
        const SDL_Rect set_soft_pedal_rect{20, 272, 416, 34};
        const SDL_Rect set_image_engine_rect{20, 316, 416, 34};
        const SDL_Rect set_gain_engine_rect{20, 360, 416, 34};
        const SDL_Rect set_humanize_time_rect{20, 404, 416, 34};
        const SDL_Rect set_humanize_velocity_rect{20, 448, 416, 34};
        const SDL_Rect set_stretch_rect{20, 492, 416, 34};
        const SDL_Rect set_pedal_threshold_rect{20, 536, 416, 34};
        const SDL_Rect set_pedal_mode_rect{20, 580, 416, 34};
        const SDL_Rect set_render_backend_rect{20, 624, 416, 34};
        const SDL_Rect button_mode_auto_rect{20, 666, 132, 28};
        const SDL_Rect button_mode_hybrid_rect{162, 666, 132, 28};
        const SDL_Rect button_mode_full_rect{304, 666, 132, 28};
        const SDL_Rect button_nat_image_post_rect{444, 52, 44, 34};
        const SDL_Rect button_cus_image_post_rect{492, 52, 44, 34};
        const SDL_Rect button_nat_gain_post_rect{444, 96, 44, 34};
        const SDL_Rect button_cus_gain_post_rect{492, 96, 44, 34};
        const SDL_Rect button_nat_presence_rect{444, 140, 44, 34};
        const SDL_Rect button_cus_presence_rect{492, 140, 44, 34};
        const SDL_Rect button_nat_mic_mix_rect{444, 184, 44, 34};
        const SDL_Rect button_cus_mic_mix_rect{492, 184, 44, 34};
        const SDL_Rect button_nat_reverb_rect{444, 228, 44, 34};
        const SDL_Rect button_cus_reverb_rect{492, 228, 44, 34};
        const SDL_Rect button_nat_soft_pedal_rect{444, 272, 44, 34};
        const SDL_Rect button_cus_soft_pedal_rect{492, 272, 44, 34};
        const SDL_Rect button_nat_image_engine_rect{444, 316, 44, 34};
        const SDL_Rect button_cus_image_engine_rect{492, 316, 44, 34};
        const SDL_Rect button_nat_gain_engine_rect{444, 360, 44, 34};
        const SDL_Rect button_cus_gain_engine_rect{492, 360, 44, 34};
        const SDL_Rect button_nat_humanize_time_rect{444, 404, 44, 34};
        const SDL_Rect button_cus_humanize_time_rect{492, 404, 44, 34};
        const SDL_Rect button_nat_humanize_velocity_rect{444, 448, 44, 34};
        const SDL_Rect button_cus_humanize_velocity_rect{492, 448, 44, 34};
        const SDL_Rect button_nat_stretch_rect{444, 492, 44, 34};
        const SDL_Rect button_cus_stretch_rect{492, 492, 44, 34};
        const SDL_Rect button_nat_pedal_threshold_rect{444, 536, 44, 34};
        const SDL_Rect button_cus_pedal_threshold_rect{492, 536, 44, 34};
        const SDL_Rect button_nat_pedal_mode_rect{444, 580, 44, 34};
        const SDL_Rect button_cus_pedal_mode_rect{492, 580, 44, 34};
        const SDL_Rect button_nat_render_backend_rect{444, 624, 44, 34};
        const SDL_Rect button_cus_render_backend_rect{492, 624, 44, 34};

        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
                break;
            }

            if (event.type == SDL_WINDOWEVENT) {
                if (event.window.windowID == settings_window_id
                    && event.window.event == SDL_WINDOWEVENT_CLOSE) {
                    close_settings_window();
                    continue;
                }
            }

            if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                const std::uint32_t wid = event.button.windowID;
                const int mx = event.button.x;
                const int my = event.button.y;

                if (wid == settings_window_id && settings_window != nullptr) {
                    if (point_in_rect(global_nat_rect, mx, my)) {
                        apply_natural_all();
                        continue;
                    }
                    if (point_in_rect(global_cus_rect, mx, my)) {
                        apply_custom_all();
                        continue;
                    }
                    if (point_in_rect(button_nat_image_post_rect, mx, my)) {
                        apply_image_post(kNaturalImagePost, false);
                        continue;
                    }
                    if (point_in_rect(button_cus_image_post_rect, mx, my)) {
                        apply_image_post(custom_image_value, false);
                        continue;
                    }
                    if (point_in_rect(button_nat_gain_post_rect, mx, my)) {
                        apply_gain_post(kNaturalGainPost, false);
                        continue;
                    }
                    if (point_in_rect(button_cus_gain_post_rect, mx, my)) {
                        apply_gain_post(custom_gain_value, false);
                        continue;
                    }
                    if (point_in_rect(button_nat_presence_rect, mx, my)) {
                        apply_presence(kNaturalPresence, false);
                        continue;
                    }
                    if (point_in_rect(button_cus_presence_rect, mx, my)) {
                        apply_presence(custom_presence_value, false);
                        continue;
                    }
                    if (point_in_rect(button_nat_mic_mix_rect, mx, my)) {
                        apply_mic_mix(kNaturalMicMix, false);
                        continue;
                    }
                    if (point_in_rect(button_cus_mic_mix_rect, mx, my)) {
                        apply_mic_mix(custom_mic_mix_value, false);
                        continue;
                    }
                    if (point_in_rect(button_nat_reverb_rect, mx, my)) {
                        apply_reverb_wet(kNaturalReverbWet, false);
                        continue;
                    }
                    if (point_in_rect(button_cus_reverb_rect, mx, my)) {
                        apply_reverb_wet(custom_reverb_wet_value, false);
                        continue;
                    }
                    if (point_in_rect(button_nat_soft_pedal_rect, mx, my)) {
                        apply_soft_pedal(kNaturalSoftPedal, false);
                        continue;
                    }
                    if (point_in_rect(button_cus_soft_pedal_rect, mx, my)) {
                        apply_soft_pedal(custom_soft_pedal_value, false);
                        continue;
                    }
                    if (point_in_rect(button_nat_image_engine_rect, mx, my)) {
                        apply_image_engine(kNaturalImageEngine, false);
                        continue;
                    }
                    if (point_in_rect(button_cus_image_engine_rect, mx, my)) {
                        apply_image_engine(custom_image_engine_value, false);
                        continue;
                    }
                    if (point_in_rect(button_nat_gain_engine_rect, mx, my)) {
                        apply_gain_engine(kNaturalGainEngine, false);
                        continue;
                    }
                    if (point_in_rect(button_cus_gain_engine_rect, mx, my)) {
                        apply_gain_engine(custom_gain_engine_value, false);
                        continue;
                    }
                    if (point_in_rect(button_nat_humanize_time_rect, mx, my)) {
                        apply_humanize_time(kNaturalHumanizeTime, false);
                        continue;
                    }
                    if (point_in_rect(button_cus_humanize_time_rect, mx, my)) {
                        apply_humanize_time(custom_humanize_time_value, false);
                        continue;
                    }
                    if (point_in_rect(button_nat_humanize_velocity_rect, mx, my)) {
                        apply_humanize_velocity(kNaturalHumanizeVelocity, false);
                        continue;
                    }
                    if (point_in_rect(button_cus_humanize_velocity_rect, mx, my)) {
                        apply_humanize_velocity(custom_humanize_velocity_value, false);
                        continue;
                    }
                    if (point_in_rect(button_nat_stretch_rect, mx, my)) {
                        apply_stretch(kNaturalStretch, false);
                        continue;
                    }
                    if (point_in_rect(button_cus_stretch_rect, mx, my)) {
                        apply_stretch(custom_stretch_value, false);
                        continue;
                    }
                    if (point_in_rect(button_nat_pedal_threshold_rect, mx, my)) {
                        apply_pedal_threshold(kNaturalPedalThreshold, false);
                        continue;
                    }
                    if (point_in_rect(button_cus_pedal_threshold_rect, mx, my)) {
                        apply_pedal_threshold(custom_pedal_threshold_value, false);
                        continue;
                    }
                    if (point_in_rect(button_nat_pedal_mode_rect, mx, my)) {
                        apply_pedal_mode(kNaturalPedalMode, false);
                        continue;
                    }
                    if (point_in_rect(button_cus_pedal_mode_rect, mx, my)) {
                        apply_pedal_mode(custom_pedal_mode_value, false);
                        continue;
                    }
                    if (point_in_rect(button_nat_render_backend_rect, mx, my)) {
                        apply_render_backend(kNaturalRenderBackend, false);
                        continue;
                    }
                    if (point_in_rect(button_cus_render_backend_rect, mx, my)) {
                        apply_render_backend(custom_render_backend_value, false);
                        continue;
                    }

                    if (point_in_rect(set_image_post_rect, mx, my)) {
                        apply_image_post(slider_from_mouse(set_image_post_rect, mx, kImageMin, kImageMax), true);
                        active_slider = UiSlider::ImagePost;
                        active_slider_window_id = settings_window_id;
                        continue;
                    }
                    if (point_in_rect(set_gain_post_rect, mx, my)) {
                        apply_gain_post(slider_from_mouse(set_gain_post_rect, mx, kGainMin, kGainMax), true);
                        active_slider = UiSlider::GainPost;
                        active_slider_window_id = settings_window_id;
                        continue;
                    }
                    if (point_in_rect(set_presence_rect, mx, my)) {
                        apply_presence(slider_from_mouse(set_presence_rect, mx, kPresenceMin, kPresenceMax), true);
                        active_slider = UiSlider::Presence;
                        active_slider_window_id = settings_window_id;
                        continue;
                    }
                    if (point_in_rect(set_mic_mix_rect, mx, my)) {
                        apply_mic_mix(slider_from_mouse(set_mic_mix_rect, mx, kMicMixMin, kMicMixMax), true);
                        active_slider = UiSlider::MicMix;
                        active_slider_window_id = settings_window_id;
                        continue;
                    }
                    if (point_in_rect(set_reverb_rect, mx, my)) {
                        apply_reverb_wet(slider_from_mouse(set_reverb_rect, mx, kReverbWetMin, kReverbWetMax), true);
                        active_slider = UiSlider::ReverbWet;
                        active_slider_window_id = settings_window_id;
                        continue;
                    }
                    if (point_in_rect(set_soft_pedal_rect, mx, my)) {
                        apply_soft_pedal(slider_from_mouse(set_soft_pedal_rect, mx, kSoftPedalMin, kSoftPedalMax), true);
                        active_slider = UiSlider::SoftPedal;
                        active_slider_window_id = settings_window_id;
                        continue;
                    }
                    if (point_in_rect(set_image_engine_rect, mx, my)) {
                        apply_image_engine(slider_from_mouse(set_image_engine_rect, mx, kImageEngineMin, kImageEngineMax), true);
                        active_slider = UiSlider::ImageEngine;
                        active_slider_window_id = settings_window_id;
                        continue;
                    }
                    if (point_in_rect(set_gain_engine_rect, mx, my)) {
                        apply_gain_engine(slider_from_mouse(set_gain_engine_rect, mx, kGainEngineMin, kGainEngineMax), true);
                        active_slider = UiSlider::GainEngine;
                        active_slider_window_id = settings_window_id;
                        continue;
                    }
                    if (point_in_rect(set_humanize_time_rect, mx, my)) {
                        apply_humanize_time(slider_from_mouse(set_humanize_time_rect, mx, kHumanizeTimeMin, kHumanizeTimeMax), true);
                        active_slider = UiSlider::HumanizeTime;
                        active_slider_window_id = settings_window_id;
                        continue;
                    }
                    if (point_in_rect(set_humanize_velocity_rect, mx, my)) {
                        apply_humanize_velocity(slider_from_mouse(set_humanize_velocity_rect, mx, kHumanizeVelocityMin, kHumanizeVelocityMax), true);
                        active_slider = UiSlider::HumanizeVelocity;
                        active_slider_window_id = settings_window_id;
                        continue;
                    }
                    if (point_in_rect(set_stretch_rect, mx, my)) {
                        apply_stretch(slider_from_mouse(set_stretch_rect, mx, kStretchMin, kStretchMax), true);
                        active_slider = UiSlider::StretchStrength;
                        active_slider_window_id = settings_window_id;
                        continue;
                    }
                    if (point_in_rect(set_pedal_threshold_rect, mx, my)) {
                        apply_pedal_threshold(slider_from_mouse(set_pedal_threshold_rect, mx, kPedalThresholdMin, kPedalThresholdMax), true);
                        active_slider = UiSlider::PedalThreshold;
                        active_slider_window_id = settings_window_id;
                        continue;
                    }
                    if (point_in_rect(set_pedal_mode_rect, mx, my)) {
                        const float mode_slider = slider_from_mouse(set_pedal_mode_rect, mx, kPedalModeMin, kPedalModeMax);
                        apply_pedal_mode(static_cast<int>(std::lround(mode_slider)), true);
                        active_slider = UiSlider::PedalMode;
                        active_slider_window_id = settings_window_id;
                        continue;
                    }
                    if (point_in_rect(set_render_backend_rect, mx, my)) {
                        const float mode_slider = slider_from_mouse(set_render_backend_rect, mx, kRenderBackendMin, kRenderBackendMax);
                        apply_render_backend(static_cast<int>(std::lround(mode_slider)), true);
                        active_slider = UiSlider::RenderBackend;
                        active_slider_window_id = settings_window_id;
                        continue;
                    }
                    if (point_in_rect(button_mode_auto_rect, mx, my)) {
                        apply_render_backend(0, true);
                        continue;
                    }
                    if (point_in_rect(button_mode_hybrid_rect, mx, my)) {
                        apply_render_backend(1, true);
                        continue;
                    }
                    if (point_in_rect(button_mode_full_rect, mx, my)) {
                        apply_render_backend(2, true);
                        continue;
                    }
                }

                if (wid == main_window_id) {
                    if (mx >= midi_dropdown_rect.x && mx < (midi_dropdown_rect.x + midi_dropdown_rect.w)
                        && my >= midi_dropdown_rect.y && my < (midi_dropdown_rect.y + midi_dropdown_rect.h)) {
                        dropdown_open = !dropdown_open;
                        continue;
                    }

                    if (point_in_rect(settings_button_rect, mx, my)) {
                        if (settings_window == nullptr) {
                            open_settings_window();
                        } else {
                            close_settings_window();
                        }
                        continue;
                    }

                    if (dropdown_open) {
                        bool selected = false;
                        const int rows = std::min<int>(kDropdownMaxRowsVisible, static_cast<int>(midi_worker.ports.size()));
                        for (int i = 0; i < rows; ++i) {
                            SDL_Rect row{
                                midi_dropdown_rect.x,
                                midi_dropdown_rect.y + midi_dropdown_rect.h + 4 + (i * kDropdownRowHeight),
                                midi_dropdown_rect.w,
                                kDropdownRowHeight
                            };
                            if (mx >= row.x && mx < (row.x + row.w) && my >= row.y && my < (row.y + row.h)) {
                                selected_midi_index = i;
                                midi_worker.selected_index.store(i, std::memory_order_release);
                                dropdown_open = false;
                                selected = true;
                                break;
                            }
                        }
                        if (!selected) {
                            dropdown_open = false;
                        }
                        continue;
                    }

                    if (mx >= pedal_circle_rect.x && mx < (pedal_circle_rect.x + pedal_circle_rect.w)
                        && my >= pedal_circle_rect.y && my < (pedal_circle_rect.y + pedal_circle_rect.h)) {
                        const std::uint8_t next = pedal_on ? 0 : 127;
                        midi_shared.pedal_cc.store(next, std::memory_order_release);
                        send_cc_tracked(64, next);
                        continue;
                    }

                    if (load_state.load(std::memory_order_acquire) != EngineLoadState::Ready) {
                        continue;
                    }

                    if (auto note = hit_test_key(keys, mx, my); note.has_value()) {
                        if (mouse_note.has_value() && mouse_note.value() != note.value()) {
                            midi_shared.note_down[static_cast<std::size_t>(mouse_note.value())].store(0, std::memory_order_release);
                            send_note_off(audio_state.engine, mouse_note.value());
                        }
                        mouse_note = note;
                        midi_shared.note_down[static_cast<std::size_t>(note.value())].store(1, std::memory_order_release);
                        send_note_on(audio_state.engine, note.value(), 96);
                    }
                }
            }

            if (event.type == SDL_MOUSEMOTION && (event.motion.state & SDL_BUTTON_LMASK) != 0) {
                if (event.motion.windowID == active_slider_window_id && active_slider_window_id != 0) {
                    if (active_slider == UiSlider::ImagePost) {
                        apply_image_post(slider_from_mouse(set_image_post_rect, event.motion.x, kImageMin, kImageMax), true);
                        continue;
                    }
                    if (active_slider == UiSlider::GainPost) {
                        apply_gain_post(slider_from_mouse(set_gain_post_rect, event.motion.x, kGainMin, kGainMax), true);
                        continue;
                    }
                    if (active_slider == UiSlider::Presence) {
                        apply_presence(slider_from_mouse(set_presence_rect, event.motion.x, kPresenceMin, kPresenceMax), true);
                        continue;
                    }
                    if (active_slider == UiSlider::MicMix) {
                        apply_mic_mix(slider_from_mouse(set_mic_mix_rect, event.motion.x, kMicMixMin, kMicMixMax), true);
                        continue;
                    }
                    if (active_slider == UiSlider::ReverbWet) {
                        apply_reverb_wet(slider_from_mouse(set_reverb_rect, event.motion.x, kReverbWetMin, kReverbWetMax), true);
                        continue;
                    }
                    if (active_slider == UiSlider::SoftPedal) {
                        apply_soft_pedal(slider_from_mouse(set_soft_pedal_rect, event.motion.x, kSoftPedalMin, kSoftPedalMax), true);
                        continue;
                    }
                    if (active_slider == UiSlider::ImageEngine) {
                        apply_image_engine(slider_from_mouse(set_image_engine_rect, event.motion.x, kImageEngineMin, kImageEngineMax), true);
                        continue;
                    }
                    if (active_slider == UiSlider::GainEngine) {
                        apply_gain_engine(slider_from_mouse(set_gain_engine_rect, event.motion.x, kGainEngineMin, kGainEngineMax), true);
                        continue;
                    }
                    if (active_slider == UiSlider::HumanizeTime) {
                        apply_humanize_time(slider_from_mouse(set_humanize_time_rect, event.motion.x, kHumanizeTimeMin, kHumanizeTimeMax), true);
                        continue;
                    }
                    if (active_slider == UiSlider::HumanizeVelocity) {
                        apply_humanize_velocity(
                            slider_from_mouse(set_humanize_velocity_rect, event.motion.x, kHumanizeVelocityMin, kHumanizeVelocityMax),
                            true
                        );
                        continue;
                    }
                    if (active_slider == UiSlider::StretchStrength) {
                        apply_stretch(slider_from_mouse(set_stretch_rect, event.motion.x, kStretchMin, kStretchMax), true);
                        continue;
                    }
                    if (active_slider == UiSlider::PedalThreshold) {
                        apply_pedal_threshold(
                            slider_from_mouse(set_pedal_threshold_rect, event.motion.x, kPedalThresholdMin, kPedalThresholdMax),
                            true
                        );
                        continue;
                    }
                    if (active_slider == UiSlider::PedalMode) {
                        const float mode_slider = slider_from_mouse(set_pedal_mode_rect, event.motion.x, kPedalModeMin, kPedalModeMax);
                        apply_pedal_mode(static_cast<int>(std::lround(mode_slider)), true);
                        continue;
                    }
                    if (active_slider == UiSlider::RenderBackend) {
                        const float mode_slider = slider_from_mouse(set_render_backend_rect, event.motion.x, kRenderBackendMin, kRenderBackendMax);
                        apply_render_backend(static_cast<int>(std::lround(mode_slider)), true);
                        continue;
                    }
                }

                if (event.motion.windowID != main_window_id) {
                    continue;
                }
                if (!mouse_note.has_value() || load_state.load(std::memory_order_acquire) != EngineLoadState::Ready) {
                    continue;
                }
                const int mx = event.motion.x;
                const int my = event.motion.y;
                const auto note = hit_test_key(keys, mx, my);
                if (!note.has_value()) {
                    midi_shared.note_down[static_cast<std::size_t>(mouse_note.value())].store(0, std::memory_order_release);
                    send_note_off(audio_state.engine, mouse_note.value());
                    mouse_note.reset();
                } else if (note.value() != mouse_note.value()) {
                    midi_shared.note_down[static_cast<std::size_t>(mouse_note.value())].store(0, std::memory_order_release);
                    send_note_off(audio_state.engine, mouse_note.value());
                    mouse_note = note;
                    midi_shared.note_down[static_cast<std::size_t>(note.value())].store(1, std::memory_order_release);
                    send_note_on(audio_state.engine, note.value(), 96);
                }
            }

            if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT) {
                active_slider = UiSlider::None;
                active_slider_window_id = 0;
                if (mouse_note.has_value()) {
                    midi_shared.note_down[static_cast<std::size_t>(mouse_note.value())].store(0, std::memory_order_release);
                    send_note_off(audio_state.engine, mouse_note.value());
                    mouse_note.reset();
                }
            }
        }

        SDL_SetRenderDrawColor(renderer, 26, 27, 30, 255);
        SDL_RenderClear(renderer);

        SDL_Rect top_bar{0, 0, kWindowWidth, kTopBarHeight};
        SDL_SetRenderDrawColor(renderer, 10, 10, 11, 255);
        SDL_RenderFillRect(renderer, &top_bar);

        SDL_SetRenderDrawColor(renderer, 48, 48, 52, 255);
        SDL_RenderFillRect(renderer, &midi_dropdown_rect);
        SDL_SetRenderDrawColor(renderer, 92, 92, 96, 255);
        SDL_RenderDrawRect(renderer, &midi_dropdown_rect);
        draw_text(renderer, midi_dropdown_rect.x + 8, midi_dropdown_rect.y + 7, 2, "MIDI", SDL_Color{212, 212, 212, 255});

        const int shown_port = selected_midi_index >= 0 ? (selected_midi_index + 1) : 0;
        draw_text(
            renderer,
            midi_dropdown_rect.x + 68,
            midi_dropdown_rect.y + 7,
            2,
            "PORT " + std::to_string(shown_port),
            SDL_Color{196, 196, 196, 255}
        );
        SDL_SetRenderDrawColor(renderer, 180, 180, 182, 255);
        SDL_RenderDrawLine(
            renderer,
            midi_dropdown_rect.x + midi_dropdown_rect.w - 18,
            midi_dropdown_rect.y + 12,
            midi_dropdown_rect.x + midi_dropdown_rect.w - 10,
            midi_dropdown_rect.y + 12
        );
        SDL_RenderDrawLine(
            renderer,
            midi_dropdown_rect.x + midi_dropdown_rect.w - 17,
            midi_dropdown_rect.y + 13,
            midi_dropdown_rect.x + midi_dropdown_rect.w - 11,
            midi_dropdown_rect.y + 13
        );
        SDL_RenderDrawLine(
            renderer,
            midi_dropdown_rect.x + midi_dropdown_rect.w - 16,
            midi_dropdown_rect.y + 14,
            midi_dropdown_rect.x + midi_dropdown_rect.w - 12,
            midi_dropdown_rect.y + 14
        );

        SDL_SetRenderDrawColor(renderer, 48, 48, 52, 255);
        SDL_RenderFillRect(renderer, &settings_button_rect);
        SDL_SetRenderDrawColor(renderer, 92, 92, 96, 255);
        SDL_RenderDrawRect(renderer, &settings_button_rect);
        draw_text(renderer, settings_button_rect.x + 10, settings_button_rect.y + 7, 2, "SETTINGS", SDL_Color{214, 214, 216, 255});

        if (dropdown_open) {
            const int rows = std::min<int>(kDropdownMaxRowsVisible, static_cast<int>(midi_worker.ports.size()));
            SDL_Rect panel{
                midi_dropdown_rect.x,
                midi_dropdown_rect.y + midi_dropdown_rect.h + 4,
                midi_dropdown_rect.w,
                std::max(1, rows) * kDropdownRowHeight
            };
            SDL_SetRenderDrawColor(renderer, 18, 18, 20, 255);
            SDL_RenderFillRect(renderer, &panel);
            SDL_SetRenderDrawColor(renderer, 92, 92, 96, 255);
            SDL_RenderDrawRect(renderer, &panel);

            if (rows == 0) {
                draw_text(renderer, panel.x + 8, panel.y + 7, 2, "NONE", SDL_Color{170, 170, 170, 255});
            } else {
                for (int i = 0; i < rows; ++i) {
                    SDL_Rect row{
                        panel.x,
                        panel.y + (i * kDropdownRowHeight),
                        panel.w,
                        kDropdownRowHeight
                    };
                    if (i == selected_midi_index) {
                        SDL_SetRenderDrawColor(renderer, 42, 64, 96, 255);
                        SDL_RenderFillRect(renderer, &row);
                    }
                    SDL_SetRenderDrawColor(renderer, 78, 78, 82, 255);
                    SDL_RenderDrawRect(renderer, &row);
                    draw_text(
                        renderer,
                        row.x + 8,
                        row.y + 7,
                        2,
                        "PORT " + std::to_string(i + 1),
                        SDL_Color{210, 210, 210, 255}
                    );
                }
            }
        }

        SDL_SetRenderDrawColor(renderer, 75, 75, 78, 255);
        if (pedal_on) {
            SDL_SetRenderDrawColor(renderer, 72, 168, 79, 255);
        }
        draw_filled_circle(renderer, pedal_circle_rect.x + (pedal_circle_rect.w / 2), pedal_circle_rect.y + (pedal_circle_rect.h / 2), 12);

        EngineLoadState state = load_state.load(std::memory_order_acquire);
        if (state == EngineLoadState::Loading) {
            draw_text(renderer, kWindowWidth / 2 - 76, kTopBarHeight + 40, 3, "LOADING", SDL_Color{215, 215, 215, 255});
        } else if (state == EngineLoadState::Failed) {
            draw_text(renderer, kWindowWidth / 2 - 58, kTopBarHeight + 40, 3, "FAILED", SDL_Color{210, 90, 90, 255});
        }

        for (const auto& key : keys) {
            if (key.black) {
                continue;
            }
            const bool pressed = midi_shared.note_down[static_cast<std::size_t>(key.note)].load(std::memory_order_acquire) != 0;
            if (pressed) {
                SDL_SetRenderDrawColor(renderer, 205, 225, 255, 255);
            } else {
                SDL_SetRenderDrawColor(renderer, 245, 245, 245, 255);
            }
            SDL_RenderFillRect(renderer, &key.rect);
            SDL_SetRenderDrawColor(renderer, 40, 40, 42, 255);
            SDL_RenderDrawRect(renderer, &key.rect);
        }

        for (const auto& key : keys) {
            if (!key.black) {
                continue;
            }
            const bool pressed = midi_shared.note_down[static_cast<std::size_t>(key.note)].load(std::memory_order_acquire) != 0;
            if (pressed) {
                SDL_SetRenderDrawColor(renderer, 72, 102, 149, 255);
            } else {
                SDL_SetRenderDrawColor(renderer, 22, 22, 24, 255);
            }
            SDL_RenderFillRect(renderer, &key.rect);
            SDL_SetRenderDrawColor(renderer, 8, 8, 10, 255);
            SDL_RenderDrawRect(renderer, &key.rect);
        }

        SDL_RenderPresent(renderer);

        if (settings_window != nullptr && settings_renderer != nullptr) {
            SDL_SetRenderDrawColor(settings_renderer, 20, 21, 24, 255);
            SDL_RenderClear(settings_renderer);

            draw_text(settings_renderer, 20, 16, 2, "SETTINGS", SDL_Color{220, 220, 220, 255});

            const auto draw_slider = [&](const SDL_Rect& rect,
                                         const float value,
                                         const float minimum,
                                         const float maximum,
                                         const char* label,
                                         const std::string& value_text) {
                SDL_SetRenderDrawColor(settings_renderer, 44, 44, 48, 255);
                SDL_RenderFillRect(settings_renderer, &rect);
                SDL_SetRenderDrawColor(settings_renderer, 84, 84, 88, 255);
                SDL_RenderDrawRect(settings_renderer, &rect);

                const float norm = std::clamp((value - minimum) / (maximum - minimum), 0.0F, 1.0F);
                const int fill_w = std::max(2, static_cast<int>(std::round(norm * static_cast<float>(rect.w - 4))));
                SDL_Rect fill{rect.x + 2, rect.y + rect.h - 8, fill_w, 5};
                SDL_SetRenderDrawColor(settings_renderer, 88, 130, 188, 255);
                SDL_RenderFillRect(settings_renderer, &fill);

                const int knob_x = rect.x + 2 + fill_w;
                SDL_Rect knob{knob_x - 3, rect.y + rect.h - 11, 6, 11};
                SDL_SetRenderDrawColor(settings_renderer, 210, 214, 224, 255);
                SDL_RenderFillRect(settings_renderer, &knob);

                draw_text(
                    settings_renderer,
                    rect.x + 8,
                    rect.y + 8,
                    2,
                    std::string(label) + " " + value_text,
                    SDL_Color{216, 216, 218, 255}
                );
            };
            const auto draw_button = [&](const SDL_Rect& rect, const char* label, const bool highlighted) {
                if (highlighted) {
                    SDL_SetRenderDrawColor(settings_renderer, 64, 102, 156, 255);
                } else {
                    SDL_SetRenderDrawColor(settings_renderer, 46, 46, 50, 255);
                }
                SDL_RenderFillRect(settings_renderer, &rect);
                SDL_SetRenderDrawColor(settings_renderer, 92, 92, 98, 255);
                SDL_RenderDrawRect(settings_renderer, &rect);
                draw_text(
                    settings_renderer,
                    rect.x + 8,
                    rect.y + 12,
                    1,
                    label,
                    SDL_Color{220, 220, 224, 255}
                );
            };

            draw_slider(set_image_post_rect, image_value, kImageMin, kImageMax, "IMAGEPOST", std::to_string(static_cast<int>(std::lround(image_value * 100.0F))));
            draw_slider(set_gain_post_rect, gain_value, kGainMin, kGainMax, "GAINPOST", std::to_string(static_cast<int>(std::lround(gain_value * 100.0F))));
            draw_slider(set_presence_rect, presence_value, kPresenceMin, kPresenceMax, "PRES", std::to_string(static_cast<int>(std::lround(presence_value * 100.0F))));
            draw_slider(set_mic_mix_rect, mic_mix_value, kMicMixMin, kMicMixMax, "MIX", std::to_string(static_cast<int>(std::lround(mic_mix_value * 100.0F))));
            draw_slider(set_reverb_rect, reverb_wet_value, kReverbWetMin, kReverbWetMax, "REVERB", std::to_string(static_cast<int>(std::lround(reverb_wet_value * 1000.0F))));
            draw_slider(set_soft_pedal_rect, soft_pedal_value, kSoftPedalMin, kSoftPedalMax, "SOFT", std::to_string(static_cast<int>(std::lround(soft_pedal_value * 100.0F))));
            draw_slider(set_image_engine_rect, image_engine_value, kImageEngineMin, kImageEngineMax, "IMAGEENG", std::to_string(static_cast<int>(std::lround(image_engine_value * 100.0F))));
            draw_slider(set_gain_engine_rect, gain_engine_value, kGainEngineMin, kGainEngineMax, "GAINENG", std::to_string(static_cast<int>(std::lround(gain_engine_value * 100.0F))));
            draw_slider(set_humanize_time_rect, humanize_time_value, kHumanizeTimeMin, kHumanizeTimeMax, "HTIME", std::to_string(static_cast<int>(std::lround(humanize_time_value * 100.0F))));
            draw_slider(set_humanize_velocity_rect, humanize_velocity_value, kHumanizeVelocityMin, kHumanizeVelocityMax, "HVEL", std::to_string(static_cast<int>(std::lround(humanize_velocity_value * 100.0F))));
            draw_slider(set_stretch_rect, stretch_value, kStretchMin, kStretchMax, "STRETCH", std::to_string(static_cast<int>(std::lround(stretch_value * 100.0F))));
            draw_slider(set_pedal_threshold_rect, pedal_threshold_value, kPedalThresholdMin, kPedalThresholdMax, "PTHR", std::to_string(static_cast<int>(std::lround(pedal_threshold_value))));
            const char* mode_label = (pedal_mode_value == 1) ? "BIN" : ((pedal_mode_value == 2) ? "CONT" : "AUTO");
            draw_slider(set_pedal_mode_rect, static_cast<float>(pedal_mode_value), kPedalModeMin, kPedalModeMax, "PMODE", mode_label);
            const char* render_mode_label = (render_backend_value == 1) ? "HYBR" : ((render_backend_value == 2) ? "FULL" : "AUTO");
            draw_slider(set_render_backend_rect, static_cast<float>(render_backend_value), kRenderBackendMin, kRenderBackendMax, "RMODE", render_mode_label);
            draw_button(button_mode_auto_rect, "AUTO", render_backend_value == 0);
            draw_button(button_mode_hybrid_rect, "HYBRID", render_backend_value == 1);
            draw_button(button_mode_full_rect, "FULL FEM", render_backend_value == 2);

            draw_button(global_nat_rect, "NAT ALL", false);
            draw_button(global_cus_rect, "CUS ALL", false);
            draw_button(button_nat_image_post_rect, "NAT", false);
            draw_button(button_cus_image_post_rect, "CUS", false);
            draw_button(button_nat_gain_post_rect, "NAT", false);
            draw_button(button_cus_gain_post_rect, "CUS", false);
            draw_button(button_nat_presence_rect, "NAT", false);
            draw_button(button_cus_presence_rect, "CUS", false);
            draw_button(button_nat_mic_mix_rect, "NAT", false);
            draw_button(button_cus_mic_mix_rect, "CUS", false);
            draw_button(button_nat_reverb_rect, "NAT", false);
            draw_button(button_cus_reverb_rect, "CUS", false);
            draw_button(button_nat_soft_pedal_rect, "NAT", false);
            draw_button(button_cus_soft_pedal_rect, "CUS", false);
            draw_button(button_nat_image_engine_rect, "NAT", false);
            draw_button(button_cus_image_engine_rect, "CUS", false);
            draw_button(button_nat_gain_engine_rect, "NAT", false);
            draw_button(button_cus_gain_engine_rect, "CUS", false);
            draw_button(button_nat_humanize_time_rect, "NAT", false);
            draw_button(button_cus_humanize_time_rect, "CUS", false);
            draw_button(button_nat_humanize_velocity_rect, "NAT", false);
            draw_button(button_cus_humanize_velocity_rect, "CUS", false);
            draw_button(button_nat_stretch_rect, "NAT", false);
            draw_button(button_cus_stretch_rect, "CUS", false);
            draw_button(button_nat_pedal_threshold_rect, "NAT", false);
            draw_button(button_cus_pedal_threshold_rect, "CUS", false);
            draw_button(button_nat_pedal_mode_rect, "NAT", false);
            draw_button(button_cus_pedal_mode_rect, "CUS", false);
            draw_button(button_nat_render_backend_rect, "NAT", false);
            draw_button(button_cus_render_backend_rect, "CUS", false);

            SDL_Rect debug_panel{20, 706, 520, 354};
            SDL_SetRenderDrawColor(settings_renderer, 26, 26, 30, 255);
            SDL_RenderFillRect(settings_renderer, &debug_panel);
            SDL_SetRenderDrawColor(settings_renderer, 84, 84, 90, 255);
            SDL_RenderDrawRect(settings_renderer, &debug_panel);
            draw_text(
                settings_renderer,
                debug_panel.x + 8,
                debug_panel.y + 8,
                1,
                "CC DEBUG MIDI0127 UI0100",
                SDL_Color{198, 198, 202, 255}
            );
            draw_text(
                settings_renderer,
                debug_panel.x + 8,
                debug_panel.y + 24,
                1,
                "FIELDS TAR TRY ACC PNG FUL RC",
                SDL_Color{170, 170, 174, 255}
            );
            draw_text(
                settings_renderer,
                debug_panel.x + 8,
                debug_panel.y + 40,
                1,
                "POSTIMG TAR" + zero_pad_int(static_cast<int>(std::lround(image_value * 100.0F)), 3)
                    + " POSTGAIN TAR" + zero_pad_int(static_cast<int>(std::lround(gain_value * 100.0F)), 3),
                SDL_Color{170, 170, 174, 255}
            );
            const char* active_backend_label = "UNK";
            if (active_backend_diag == 0) {
                active_backend_label = "AUTO";
            } else if (active_backend_diag == 1) {
                active_backend_label = "HYBR";
            } else if (active_backend_diag == 2) {
                active_backend_label = "FULL";
            }
            const char* requested_backend_label = (render_backend_value == 1)
                ? "HYBR"
                : ((render_backend_value == 2) ? "FULL" : "AUTO");
            draw_text(
                settings_renderer,
                debug_panel.x + 8,
                debug_panel.y + 56,
                1,
                std::string("REQ ") + requested_backend_label + " ACTIVE " + active_backend_label
                    + " GPUFALL " + std::to_string(gpu_fallback_diag),
                SDL_Color{170, 170, 174, 255}
            );

            int debug_y = debug_panel.y + 74;
            for (const auto& entry : cc_debug) {
                const std::string line = std::string(entry.label)
                    + " C" + zero_pad_int(entry.cc, 3)
                    + " TAR" + zero_pad_int(entry.target_value, 3) + "-" + zero_pad_int(cc_to_percent_rounded(entry.target_value), 3)
                    + " TRY" + zero_pad_int(entry.attempted_value, 3) + "-" + zero_pad_int(cc_to_percent_rounded(entry.attempted_value), 3)
                    + " ACC" + zero_pad_int(entry.accepted_value, 3) + "-" + zero_pad_int(cc_to_percent_rounded(entry.accepted_value), 3)
                    + " PNG" + zero_pad_int(entry.pending_value, 3) + "-" + zero_pad_int(cc_to_percent_rounded(entry.pending_value), 3)
                    + " FUL" + zero_pad_int(static_cast<int>(entry.queue_full), 3)
                    + " RC" + zero_pad_int(static_cast<int>(entry.last_result), 3);
                draw_text(settings_renderer, debug_panel.x + 8, debug_y, 1, line, SDL_Color{206, 206, 210, 255});
                debug_y += 18;
            }

            SDL_RenderPresent(settings_renderer);
        }
        SDL_Delay(8);
    }

    if (loader.joinable()) {
        loader.join();
    }

    if (midi_started) {
        midi_worker.stop();
    }

    send_cc(audio_state.engine, 64, 0);
    for (int note = 0; note < 128; ++note) {
        if (midi_shared.note_down[static_cast<std::size_t>(note)].load(std::memory_order_acquire) != 0) {
            send_note_off(audio_state.engine, note);
        }
    }

    if (device != 0) {
        SDL_CloseAudioDevice(device);
    }

    close_settings_window();

    auto* engine = audio_state.engine.exchange(nullptr, std::memory_order_acq_rel);
    if (engine != nullptr) {
        vb_engine_destroy(engine);
    } else if (load_result.engine != nullptr && load_state.load(std::memory_order_acquire) != EngineLoadState::Ready) {
        vb_engine_destroy(load_result.engine);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
