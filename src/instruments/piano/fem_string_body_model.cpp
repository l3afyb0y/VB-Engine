#include "instruments/piano/fem_string_body_model.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#if defined(VB_ENGINE_ENABLE_OPENCL) && __has_include(<CL/cl.h>)
#include <CL/cl.h>
#define VB_ENGINE_HAS_OPENCL 1
#else
#define VB_ENGINE_HAS_OPENCL 0
#endif

#include "core/fast_math.hpp"

namespace vb {

namespace {
constexpr float kPi = 3.14159265358979323846F;
constexpr float kTwoPi = 2.0F * kPi;

float clamp01(const float x) noexcept {
    return std::clamp(x, 0.0F, 1.0F);
}

float soft_limit(const float x) noexcept {
    const float ax = std::abs(x);
    if (ax <= 0.98F) {
        return x;
    }
    const float excess = ax - 0.98F;
    const float y = 0.98F + (0.02F * std::tanh(excess * 10.0F));
    return std::copysign(y, x);
}

std::uint32_t xorshift32(std::uint32_t& state) noexcept {
    if (state == 0U) {
        state = 0x9E3779B9U;
    }
    state ^= state << 13U;
    state ^= state >> 17U;
    state ^= state << 5U;
    return state;
}

float noise_bipolar(std::uint32_t& state) noexcept {
    const std::uint32_t bits = xorshift32(state) & 0x00FFFFFFU;
    return (static_cast<float>(bits) / 8388607.5F) - 1.0F;
}

#if VB_ENGINE_HAS_OPENCL
constexpr const char* kFemKernelSource = R"CLC(
__kernel void fem_step(
    __global const float* prev,
    __global const float* curr,
    __global const float* force,
    __global float* next,
    const int width,
    const int height,
    const float c2,
    const float damping,
    const float boundary_loss
) {
    const int idx = (int)get_global_id(0);
    const int total = width * height;
    if (idx >= total) {
        return;
    }

    const int x = idx % width;
    const int y = idx / width;
    if (x == 0 || y == 0 || x == (width - 1) || y == (height - 1)) {
        next[idx] = curr[idx] * boundary_loss;
        return;
    }

    const float lap =
        curr[idx - 1] +
        curr[idx + 1] +
        curr[idx - width] +
        curr[idx + width] -
        (4.0f * curr[idx]);

    next[idx] =
        ((2.0f - damping) * curr[idx]) -
        ((1.0f - damping) * prev[idx]) +
        (c2 * lap) +
        force[idx];
}
)CLC";

struct OpenClFemRuntime {
    bool attempted_init{false};
    bool ready{false};
    cl_platform_id platform{nullptr};
    cl_device_id device{nullptr};
    cl_context context{nullptr};
    cl_command_queue queue{nullptr};
    cl_program program{nullptr};
    cl_kernel kernel{nullptr};
    cl_mem prev_buf{nullptr};
    cl_mem curr_buf{nullptr};
    cl_mem force_buf{nullptr};
    cl_mem next_buf{nullptr};
    int buffer_len{0};
};

OpenClFemRuntime& opencl_runtime() {
    static OpenClFemRuntime rt{};
    return rt;
}

void opencl_release_buffers(OpenClFemRuntime& rt) {
    if (rt.prev_buf != nullptr) clReleaseMemObject(rt.prev_buf);
    if (rt.curr_buf != nullptr) clReleaseMemObject(rt.curr_buf);
    if (rt.force_buf != nullptr) clReleaseMemObject(rt.force_buf);
    if (rt.next_buf != nullptr) clReleaseMemObject(rt.next_buf);
    rt.prev_buf = nullptr;
    rt.curr_buf = nullptr;
    rt.force_buf = nullptr;
    rt.next_buf = nullptr;
    rt.buffer_len = 0;
}

void opencl_release_all(OpenClFemRuntime& rt) {
    opencl_release_buffers(rt);
    if (rt.kernel != nullptr) clReleaseKernel(rt.kernel);
    if (rt.program != nullptr) clReleaseProgram(rt.program);
    if (rt.queue != nullptr) clReleaseCommandQueue(rt.queue);
    if (rt.context != nullptr) clReleaseContext(rt.context);
    rt.kernel = nullptr;
    rt.program = nullptr;
    rt.queue = nullptr;
    rt.context = nullptr;
    rt.device = nullptr;
    rt.platform = nullptr;
    rt.ready = false;
    rt.attempted_init = false;
}

bool opencl_init(OpenClFemRuntime& rt, const int buffer_len) {
    if (rt.ready && rt.buffer_len == buffer_len) {
        return true;
    }

    opencl_release_buffers(rt);
    rt.ready = false;

    if (!rt.attempted_init) {
        rt.attempted_init = true;
        cl_int err = CL_SUCCESS;
        cl_uint platform_count = 0;
        err = clGetPlatformIDs(1, &rt.platform, &platform_count);
        if (err != CL_SUCCESS || platform_count == 0) {
            return false;
        }
        cl_uint device_count = 0;
        err = clGetDeviceIDs(rt.platform, CL_DEVICE_TYPE_GPU, 1, &rt.device, &device_count);
        if (err != CL_SUCCESS || device_count == 0) {
            return false;
        }

        rt.context = clCreateContext(nullptr, 1, &rt.device, nullptr, nullptr, &err);
        if (err != CL_SUCCESS || rt.context == nullptr) {
            opencl_release_all(rt);
            return false;
        }

#if defined(CL_VERSION_2_0)
        rt.queue = clCreateCommandQueueWithProperties(rt.context, rt.device, nullptr, &err);
#else
        rt.queue = clCreateCommandQueue(rt.context, rt.device, 0, &err);
#endif
        if (err != CL_SUCCESS || rt.queue == nullptr) {
            opencl_release_all(rt);
            return false;
        }

        const char* src = kFemKernelSource;
        const std::size_t src_len = std::strlen(src);
        rt.program = clCreateProgramWithSource(rt.context, 1, &src, &src_len, &err);
        if (err != CL_SUCCESS || rt.program == nullptr) {
            opencl_release_all(rt);
            return false;
        }

        err = clBuildProgram(rt.program, 1, &rt.device, nullptr, nullptr, nullptr);
        if (err != CL_SUCCESS) {
            opencl_release_all(rt);
            return false;
        }

        rt.kernel = clCreateKernel(rt.program, "fem_step", &err);
        if (err != CL_SUCCESS || rt.kernel == nullptr) {
            opencl_release_all(rt);
            return false;
        }
    }

    cl_int err = CL_SUCCESS;
    const std::size_t bytes = static_cast<std::size_t>(buffer_len) * sizeof(float);
    rt.prev_buf = clCreateBuffer(rt.context, CL_MEM_READ_ONLY, bytes, nullptr, &err);
    if (err != CL_SUCCESS || rt.prev_buf == nullptr) {
        opencl_release_buffers(rt);
        return false;
    }
    rt.curr_buf = clCreateBuffer(rt.context, CL_MEM_READ_ONLY, bytes, nullptr, &err);
    if (err != CL_SUCCESS || rt.curr_buf == nullptr) {
        opencl_release_buffers(rt);
        return false;
    }
    rt.force_buf = clCreateBuffer(rt.context, CL_MEM_READ_ONLY, bytes, nullptr, &err);
    if (err != CL_SUCCESS || rt.force_buf == nullptr) {
        opencl_release_buffers(rt);
        return false;
    }
    rt.next_buf = clCreateBuffer(rt.context, CL_MEM_WRITE_ONLY, bytes, nullptr, &err);
    if (err != CL_SUCCESS || rt.next_buf == nullptr) {
        opencl_release_buffers(rt);
        return false;
    }

    rt.buffer_len = buffer_len;
    rt.ready = true;
    return true;
}

bool opencl_step(
    OpenClFemRuntime& rt,
    const float* prev,
    const float* curr,
    const float* force,
    float* next,
    const int width,
    const int height,
    const float c2,
    const float damping,
    const float boundary_loss
) {
    if (!opencl_init(rt, width * height)) {
        return false;
    }

    const std::size_t count = static_cast<std::size_t>(width * height);
    const std::size_t bytes = count * sizeof(float);
    cl_int err = CL_SUCCESS;
    err = clEnqueueWriteBuffer(rt.queue, rt.prev_buf, CL_FALSE, 0, bytes, prev, 0, nullptr, nullptr);
    if (err != CL_SUCCESS) return false;
    err = clEnqueueWriteBuffer(rt.queue, rt.curr_buf, CL_FALSE, 0, bytes, curr, 0, nullptr, nullptr);
    if (err != CL_SUCCESS) return false;
    err = clEnqueueWriteBuffer(rt.queue, rt.force_buf, CL_FALSE, 0, bytes, force, 0, nullptr, nullptr);
    if (err != CL_SUCCESS) return false;

    err |= clSetKernelArg(rt.kernel, 0, sizeof(cl_mem), &rt.prev_buf);
    err |= clSetKernelArg(rt.kernel, 1, sizeof(cl_mem), &rt.curr_buf);
    err |= clSetKernelArg(rt.kernel, 2, sizeof(cl_mem), &rt.force_buf);
    err |= clSetKernelArg(rt.kernel, 3, sizeof(cl_mem), &rt.next_buf);
    err |= clSetKernelArg(rt.kernel, 4, sizeof(int), &width);
    err |= clSetKernelArg(rt.kernel, 5, sizeof(int), &height);
    err |= clSetKernelArg(rt.kernel, 6, sizeof(float), &c2);
    err |= clSetKernelArg(rt.kernel, 7, sizeof(float), &damping);
    err |= clSetKernelArg(rt.kernel, 8, sizeof(float), &boundary_loss);
    if (err != CL_SUCCESS) {
        return false;
    }

    const std::size_t global = count;
    err = clEnqueueNDRangeKernel(rt.queue, rt.kernel, 1, nullptr, &global, nullptr, 0, nullptr, nullptr);
    if (err != CL_SUCCESS) return false;

    err = clEnqueueReadBuffer(rt.queue, rt.next_buf, CL_TRUE, 0, bytes, next, 0, nullptr, nullptr);
    if (err != CL_SUCCESS) return false;
    return true;
}
#endif

bool gpu_fem_enabled_by_env() noexcept {
    const char* env = std::getenv("VB_ENGINE_FEM_GPU");
    if (env == nullptr || env[0] == '\0') {
        return true;
    }
    return std::atoi(env) != 0;
}

}  // namespace

void PianoFemStringBodyModel::initialize(
    const float sample_rate,
    const PianoRenderBackend requested_backend,
    const float mix,
    const float brightness
) noexcept {
    sample_rate_ = std::max(22050.0F, sample_rate);
    requested_backend_ = requested_backend;
    set_voicing(mix, brightness);

    const float sr_norm = std::clamp(sample_rate_ / 48000.0F, 0.5F, 2.0F);
    c2_ = 0.12F / sr_norm;
    damping_ = std::clamp(0.0013F * sr_norm, 0.0009F, 0.0022F);
    step_interval_ = (sample_rate_ > 48000.0F) ? 4U : 2U;
    step_phase_ = 0;
    cached_l_ = 0.0F;
    cached_r_ = 0.0F;
    gpu_fallback_blocks_ = 0;
    instability_events_ = 0;
    consecutive_gpu_step_failures_ = 0;
    reset_board();
    select_active_backend();
}

void PianoFemStringBodyModel::set_backend(const PianoRenderBackend requested_backend) noexcept {
    requested_backend_ = requested_backend;
    instability_events_ = 0;
    consecutive_gpu_step_failures_ = 0;
    select_active_backend();
}

void PianoFemStringBodyModel::set_voicing(const float mix, const float brightness) noexcept {
    mix_ = std::clamp(mix, 0.0F, 1.0F);
    brightness_ = std::clamp(brightness, 0.0F, 1.0F);
}

void PianoFemStringBodyModel::set_pedal(const float amount) noexcept {
    pedal_amount_ = clamp01(amount);
}

void PianoFemStringBodyModel::note_on(
    const std::uint8_t note,
    const std::uint8_t velocity,
    const float tuned_frequency_hz
) noexcept {
    StringState& s = strings_[note];
    if (!s.active) {
        active_strings_ = static_cast<std::uint16_t>(std::min<std::uint32_t>(active_strings_ + 1U, 65535U));
    }
    const float vel = clamp01(static_cast<float>(velocity) / 127.0F);
    const float note_norm = clamp01((static_cast<float>(note) - 21.0F) / 87.0F);
    const float inharm = 1.0F + (0.00004F * (note_norm * note_norm) * (1.0F + vel));
    const float frequency = std::max(12.0F, tuned_frequency_hz * inharm);

    s.active = true;
    s.phase = noise_bipolar(noise_state_) * kPi;
    s.phase_increment = (kTwoPi * frequency) / sample_rate_;
    s.amplitude = 0.10F + (vel * 0.34F);
    const float decay_seconds = 0.95F + ((1.0F - note_norm) * 2.8F) + (pedal_amount_ * 1.8F);
    s.decay = std::exp(-1.0F / std::max(1.0F, sample_rate_ * decay_seconds));
    s.coupling = (0.00025F + ((1.0F - note_norm) * 0.00085F)) * (0.65F + (vel * 0.35F));

    const int bridge_x = 2 + ((static_cast<int>(note) * (kGridW - 4)) / 127);
    const int bridge_y = (kGridH / 3) + ((note < 60) ? 2 : -1);
    s.bridge_index = static_cast<std::uint16_t>(std::clamp(bridge_y, 1, kGridH - 2) * kGridW + std::clamp(bridge_x, 1, kGridW - 2));
}

void PianoFemStringBodyModel::note_off(const std::uint8_t note) noexcept {
    StringState& s = strings_[note];
    if (!s.active) {
        return;
    }
    const float release_seconds = 0.12F + (pedal_amount_ * 0.36F);
    s.decay = std::exp(-1.0F / std::max(1.0F, sample_rate_ * release_seconds));
}

void PianoFemStringBodyModel::render_sample(const float excitation, float& out_l, float& out_r) noexcept {
    if (active_strings_ == 0U
        && board_activity_ < 1.0e-6F
        && std::abs(excitation) < 1.0e-6F
        && std::abs(cached_l_) < 1.0e-6F
        && std::abs(cached_r_) < 1.0e-6F) {
        out_l = 0.0F;
        out_r = 0.0F;
        return;
    }

    if (step_phase_ == 0U) {
        cpu_step(excitation);
        collect_output(excitation, cached_l_, cached_r_);
    }
    step_phase_ = (step_phase_ + 1U) % std::max(1U, step_interval_);

    const float direct_air = excitation * (0.0045F * mix_);
    out_l = cached_l_ + direct_air;
    out_r = cached_r_ + direct_air;
}

void PianoFemStringBodyModel::select_active_backend() noexcept {
#if VB_ENGINE_HAS_OPENCL
    if (gpu_fem_enabled_by_env()) {
        OpenClFemRuntime& runtime = opencl_runtime();
        gpu_available_ = opencl_init(runtime, static_cast<int>(kGridN));
    } else {
        gpu_available_ = false;
    }
#else
    gpu_available_ = false;
#endif
    if (requested_backend_ == PianoRenderBackend::GpuFem) {
        active_backend_ = gpu_available_ ? PianoRenderBackend::GpuFem : PianoRenderBackend::CpuHybrid;
        return;
    }
    if (requested_backend_ == PianoRenderBackend::CpuHybrid) {
        active_backend_ = PianoRenderBackend::CpuHybrid;
    } else {
        active_backend_ = gpu_available_ ? PianoRenderBackend::GpuFem : PianoRenderBackend::CpuHybrid;
    }
    const std::uint32_t base_interval = (sample_rate_ > 48000.0F) ? 4U : 2U;
    step_interval_ = (active_backend_ == PianoRenderBackend::GpuFem) ? std::max(4U, base_interval) : base_interval;
    step_phase_ = 0U;
}

void PianoFemStringBodyModel::cpu_step(const float excitation) noexcept {
    constexpr float kBoundaryLoss = 0.9965F;

    std::array<float, kGridN> force{};
    float aggregate_string_force = 0.0F;

    for (std::size_t note = 0; note < strings_.size(); ++note) {
        StringState& s = strings_[note];
        if (!s.active) {
            continue;
        }

        const float string_sample = FastSinTable::sample_normalized(s.phase) * s.amplitude;
        aggregate_string_force += std::abs(string_sample);
        force[s.bridge_index] += string_sample * s.coupling;

        s.phase += s.phase_increment;
        if (s.phase >= kTwoPi) {
            s.phase -= kTwoPi;
        }
        s.amplitude *= s.decay;
        if (s.amplitude < 1.0e-5F) {
            s.active = false;
            s.amplitude = 0.0F;
            if (active_strings_ > 0U) {
                --active_strings_;
            }
        }
    }

    const int center_idx = (kGridH / 2) * kGridW + (kGridW / 2);
    const float excitation_drive = excitation * (0.00018F + (0.00052F * mix_));
    force[center_idx] += excitation_drive;
    force[center_idx - 1] += excitation_drive * 0.62F;
    force[center_idx + 1] += excitation_drive * 0.62F;

    const float dynamic_damping = damping_ + (0.0009F * clamp01(aggregate_string_force * 0.7F));
    const float pedal_relief = 0.0006F * pedal_amount_;
    const float total_damping = std::max(0.0004F, dynamic_damping - pedal_relief);

    bool stepped_on_gpu = false;
#if VB_ENGINE_HAS_OPENCL
    if (active_backend_ == PianoRenderBackend::GpuFem) {
        OpenClFemRuntime& runtime = opencl_runtime();
        stepped_on_gpu = opencl_step(
            runtime,
            board_prev_.data(),
            board_curr_.data(),
            force.data(),
            board_next_.data(),
            kGridW,
            kGridH,
            c2_,
            total_damping,
            kBoundaryLoss
        );
        if (!stepped_on_gpu) {
            ++gpu_fallback_blocks_;
            ++consecutive_gpu_step_failures_;
            // Repeated runtime failures mean GPU path is not viable right now.
            // Demote to CPU hybrid to avoid constant retry overhead and CPU thrash.
            const std::uint32_t fail_threshold = (requested_backend_ == PianoRenderBackend::GpuFem) ? 1U : 8U;
            if (consecutive_gpu_step_failures_ >= fail_threshold) {
                active_backend_ = PianoRenderBackend::CpuHybrid;
                gpu_available_ = false;
                const std::uint32_t base_interval = (sample_rate_ > 48000.0F) ? 4U : 2U;
                step_interval_ = base_interval;
                step_phase_ = 0U;
            }
        } else {
            consecutive_gpu_step_failures_ = 0;
        }
    }
#endif

    if (!stepped_on_gpu) {
        for (int y = 1; y < (kGridH - 1); ++y) {
            const int row = y * kGridW;
            for (int x = 1; x < (kGridW - 1); ++x) {
                const int idx = row + x;
                const float laplacian =
                    board_curr_[static_cast<std::size_t>(idx - 1)] +
                    board_curr_[static_cast<std::size_t>(idx + 1)] +
                    board_curr_[static_cast<std::size_t>(idx - kGridW)] +
                    board_curr_[static_cast<std::size_t>(idx + kGridW)] -
                    (4.0F * board_curr_[static_cast<std::size_t>(idx)]);

                const float next =
                    ((2.0F - total_damping) * board_curr_[static_cast<std::size_t>(idx)]) -
                    ((1.0F - total_damping) * board_prev_[static_cast<std::size_t>(idx)]) +
                    (c2_ * laplacian) +
                    force[static_cast<std::size_t>(idx)];
                board_next_[static_cast<std::size_t>(idx)] = next;
            }
        }

        for (int x = 0; x < kGridW; ++x) {
            board_next_[static_cast<std::size_t>(x)] = board_curr_[static_cast<std::size_t>(x)] * kBoundaryLoss;
            board_next_[static_cast<std::size_t>((kGridH - 1) * kGridW + x)] =
                board_curr_[static_cast<std::size_t>((kGridH - 1) * kGridW + x)] * kBoundaryLoss;
        }
        for (int y = 0; y < kGridH; ++y) {
            board_next_[static_cast<std::size_t>(y * kGridW)] =
                board_curr_[static_cast<std::size_t>(y * kGridW)] * kBoundaryLoss;
            board_next_[static_cast<std::size_t>(y * kGridW + (kGridW - 1))] =
                board_curr_[static_cast<std::size_t>(y * kGridW + (kGridW - 1))] * kBoundaryLoss;
        }
    }

    for (float& v : board_next_) {
        if (!std::isfinite(v) || std::abs(v) > 32.0F) {
            reset_board();
            ++instability_events_;
            if (active_backend_ == PianoRenderBackend::GpuFem) {
                ++gpu_fallback_blocks_;
                // In AUTO, fall back after repeated instability.
                if (requested_backend_ == PianoRenderBackend::Auto && instability_events_ >= 3U) {
                    active_backend_ = PianoRenderBackend::CpuHybrid;
                }
            }
            return;
        }
        v = std::clamp(v, -2.0F, 2.0F);
    }
    instability_events_ = 0;

    board_prev_ = board_curr_;
    board_curr_ = board_next_;
}

void PianoFemStringBodyModel::collect_output(const float excitation, float& out_l, float& out_r) noexcept {
    const int pickup_l = (12 * kGridW) + 8;
    const int pickup_r = (11 * kGridW) + 21;
    const float raw_l = board_curr_[static_cast<std::size_t>(pickup_l)];
    const float raw_r = board_curr_[static_cast<std::size_t>(pickup_r)];

    const float room_weight = 0.70F + (0.22F * mix_);
    const float direct_weight = 0.09F + (0.12F * brightness_);
    float wet_l = (raw_l * room_weight) + (excitation * direct_weight);
    float wet_r = (raw_r * room_weight) + (excitation * direct_weight);

    const float lp_coeff = 0.018F + (0.024F * (1.0F - brightness_));
    body_lp_l_ += lp_coeff * (wet_l - body_lp_l_);
    body_lp_r_ += lp_coeff * (wet_r - body_lp_r_);
    const float hp_coeff = 0.986F + (0.010F * brightness_);
    body_hp_l_ = hp_coeff * (body_hp_l_ + wet_l - body_hp_prev_l_);
    body_hp_r_ = hp_coeff * (body_hp_r_ + wet_r - body_hp_prev_r_);
    body_hp_prev_l_ = wet_l;
    body_hp_prev_r_ = wet_r;

    const float steinway_vertical_color = 0.58F;
    wet_l = (body_lp_l_ * steinway_vertical_color) + (body_hp_l_ * (1.0F - steinway_vertical_color));
    wet_r = (body_lp_r_ * steinway_vertical_color) + (body_hp_r_ * (1.0F - steinway_vertical_color));

    out_l = soft_limit(wet_l * mix_);
    out_r = soft_limit(wet_r * mix_);
    const float activity = std::max(std::abs(out_l), std::abs(out_r));
    board_activity_ += 0.02F * (activity - board_activity_);
    board_activity_ = std::clamp(board_activity_, 0.0F, 2.0F);
}

void PianoFemStringBodyModel::reset_board() noexcept {
    board_prev_.fill(0.0F);
    board_curr_.fill(0.0F);
    board_next_.fill(0.0F);
    strings_.fill(StringState{});
    active_strings_ = 0U;
    board_activity_ = 0.0F;
    body_lp_l_ = 0.0F;
    body_lp_r_ = 0.0F;
    body_hp_l_ = 0.0F;
    body_hp_r_ = 0.0F;
    body_hp_prev_l_ = 0.0F;
    body_hp_prev_r_ = 0.0F;
    step_phase_ = 0;
    cached_l_ = 0.0F;
    cached_r_ = 0.0F;
    instability_events_ = 0;
    consecutive_gpu_step_failures_ = 0;
}

}  // namespace vb
