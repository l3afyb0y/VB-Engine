# Embedding VB-Engine in Your App

## 1. Quick Start
```bash
git clone https://github.com/l3afyb0y/VB-Engine.git
cd VB-Engine
cmake --preset release
cmake --build --preset release
```

Outputs:
- Shared library: `build/release/libvb_engine.so*`
- Public header: `include/vb_engine/c_api.h`

## 2. Integration Models

### Model A: Vendor as subdirectory (recommended for app repos)
In your app `CMakeLists.txt`:
```cmake
add_subdirectory(external/VB-Engine)
target_link_libraries(my_app PRIVATE vb_engine)
target_include_directories(my_app PRIVATE external/VB-Engine/include)
```

### Model B: System-installed shared library
Install VB-Engine:
```bash
cmake --preset release
cmake --build --preset release
sudo cmake --install build/release
```

Then link in your app build:
- Include path: `/usr/local/include/vb_engine`
- Library: `-lvb_engine`

## 3. Minimal Runtime Example (C API)
```c
#include "vb_engine/c_api.h"

static vb_engine_handle* g_engine = 0;

void init_engine(void) {
    vb_engine_config cfg;
    vb_engine_default_config(&cfg);
    cfg.sample_rate = 48000.0;
    cfg.max_block_size = 256;
    cfg.max_voices = 96;
    cfg.piano_sfz_path = "/absolute/path/to/default.sfz";  // explicit for sampled playback
    cfg.piano_reverb_wet = 0.08f;
    cfg.piano_mic_mix = 0.28f;
    cfg.piano_presence = 0.56f;
    cfg.piano_render_backend = VB_PIANO_RENDER_BACKEND_AUTO;
    cfg.piano_fem_mix = 0.26f;
    cfg.piano_fem_brightness = 0.54f;
    vb_engine_create(&cfg, &g_engine);
}

void process_audio(float** outputs, unsigned frames) {
    vb_engine_process(g_engine, outputs, 2, frames);
}

void shutdown_engine(void) {
    vb_engine_destroy(g_engine);
    g_engine = 0;
}
```

## 4. Realtime Integration Rules
Call from non-audio threads:
- `vb_engine_note_on`
- `vb_engine_note_off`
- `vb_engine_control_change`

Call only from audio callback:
- `vb_engine_process`

Host-side rules:
- Keep callback block size `<= cfg.max_block_size`.
- Use stable sample rate after initialization (or recreate on rate change).
- Avoid blocking calls in your own callback path.

## 5. SFZ / Sample Library Notes
- Core engine does **not** auto-discover a sample path.
- Provide `cfg.piano_sfz_path` explicitly.
- Generator/tester tools may still fallback to `Samples/Piano-Library/default.sfz`.
- Supported sample formats in SFZ regions:
  - WAV PCM/float
  - AIFF/AIFC PCM
  - FLAC

## 6. Pedal Support
Use CC64:
- `0..127` raw pedal values.
- Modes:
  - `auto`
  - `binary`
  - `continuous`

For binary pedals, keep threshold at default 64 or set `piano_pedal_binary_threshold`.

## 7. Diagnostics
`vb_engine_get_diagnostics(...)` returns:
- voice steals,
- worst stolen activity,
- max output delta,
- hard jump count,
- non-finite output sample count,
- active render backend,
- GPU fallback block count.

Use `vb_engine_reset_diagnostics(...)` between profiling runs.

## 8. Backend Selection
`cfg.piano_render_backend`:
- `VB_PIANO_RENDER_BACKEND_AUTO`: choose best available backend (recommended).
- `VB_PIANO_RENDER_BACKEND_CPU_HYBRID`: force existing CPU hybrid path.
- `VB_PIANO_RENDER_BACKEND_GPU_FEM`: request FEM path with automatic fallback if unavailable.

`cfg.piano_fem_mix` and `cfg.piano_fem_brightness` control FEM body contribution when that backend is active.

## 9. Pre-Ship Checklist for Host Apps
1. Run unit tests in VB-Engine repo.
2. Run soak tests with your target block size/rate.
3. Validate no clipping/jump artifacts on your MIDI workloads.
4. Pin tested library commit/tag in your app.
5. Keep your own audio callback lock-free.
