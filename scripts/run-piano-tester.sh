#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BIN="${REPO_ROOT}/build/release/vb_engine_piano_tester"

if [[ ! -x "${BIN}" \
  || "${REPO_ROOT}/tools/piano_tester/main.cpp" -nt "${BIN}" \
  || "${REPO_ROOT}/src/c_api/c_api.cpp" -nt "${BIN}" \
  || "${REPO_ROOT}/include/vb_engine/c_api.h" -nt "${BIN}" \
  || "${REPO_ROOT}/src/core/engine.cpp" -nt "${BIN}" \
  || "${REPO_ROOT}/src/core/voice_pool.cpp" -nt "${BIN}" \
  || "${REPO_ROOT}/src/instruments/piano/fem_string_body_model.cpp" -nt "${BIN}" \
  || "${REPO_ROOT}/src/instruments/piano/fem_string_body_model.hpp" -nt "${BIN}" \
  || "${REPO_ROOT}/src/instruments/piano/resonance_matrix.cpp" -nt "${BIN}" \
  || "${REPO_ROOT}/src/instruments/piano/post_processor.cpp" -nt "${BIN}" \
  || "${REPO_ROOT}/src/instruments/piano/sampled_piano_voice.cpp" -nt "${BIN}" \
  || "${REPO_ROOT}/src/instruments/piano/sample_library.cpp" -nt "${BIN}" \
  || "${REPO_ROOT}/CMakeLists.txt" -nt "${BIN}" ]]; then
  cmake --preset release
  cmake --build --preset release --target vb_engine_piano_tester
fi

: "${VB_TESTER_SAMPLE_RATE:=48000}"
: "${VB_TESTER_BUFFER:=256}"
: "${VB_TESTER_MAX_VOICES:=96}"
: "${VB_TESTER_PEDAL_NOISE:=0}"
: "${VB_TESTER_MIC_MIX:=0.20}"
: "${VB_TESTER_REVERB_WET:=0.04}"
: "${VB_TESTER_PRESENCE:=0.72}"
: "${VB_TESTER_STEREO_WIDTH:=1.60}"
: "${VB_TESTER_OUTPUT_GAIN:=1.45}"
: "${VB_TESTER_SOFT_PEDAL:=0.50}"
: "${VB_TESTER_IMAGE_ENGINE:=1.25}"
: "${VB_TESTER_GAIN_ENGINE:=1.00}"
: "${VB_TESTER_HUMANIZE_TIME_MS:=1.20}"
: "${VB_TESTER_HUMANIZE_VELOCITY:=2.00}"
: "${VB_TESTER_STRETCH:=0.90}"
: "${VB_TESTER_PEDAL_THRESHOLD:=64}"
: "${VB_TESTER_PEDAL_MODE:=0}"
: "${VB_TESTER_RENDER_BACKEND:=0}"
: "${VB_TESTER_FEM_MIX:=0.26}"
: "${VB_TESTER_FEM_BRIGHTNESS:=0.54}"
: "${VB_TESTER_STREAMING:=1}"
: "${VB_TESTER_STREAM_THRESHOLD:=65536}"
: "${VB_TESTER_LOG:=1}"
: "${VB_ENGINE_FEM_GPU:=0}"
export VB_TESTER_SAMPLE_RATE VB_TESTER_BUFFER VB_TESTER_MAX_VOICES VB_TESTER_PEDAL_NOISE VB_TESTER_MIC_MIX VB_TESTER_REVERB_WET
export VB_TESTER_PRESENCE VB_TESTER_STEREO_WIDTH VB_TESTER_OUTPUT_GAIN
export VB_TESTER_SOFT_PEDAL VB_TESTER_IMAGE_ENGINE VB_TESTER_GAIN_ENGINE
export VB_TESTER_HUMANIZE_TIME_MS VB_TESTER_HUMANIZE_VELOCITY VB_TESTER_STRETCH VB_TESTER_PEDAL_THRESHOLD VB_TESTER_PEDAL_MODE
export VB_TESTER_RENDER_BACKEND VB_TESTER_FEM_MIX VB_TESTER_FEM_BRIGHTNESS
export VB_TESTER_STREAMING VB_TESTER_STREAM_THRESHOLD VB_TESTER_LOG
export VB_ENGINE_FEM_GPU

exec "${BIN}"
