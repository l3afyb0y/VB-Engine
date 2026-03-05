# VB-Engine Bootstrap Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Bootstrap a minimal realtime-safe C++20 VB-Engine with C ABI, tests, benchmark, and Arch-first packaging artifacts.

**Architecture:** Build a host-agnostic core renderer with preallocated resources, expose stable C ABI for embedding, and keep plugin wrappers out-of-scope for this slice. Validate with tests and sanitizer-ready build presets.

**Tech Stack:** C++20, CMake, CTest, POSIX shell scripts, Arch PKGBUILD.

---

### Task 1: Build System and Layout
**Files:**
- Create: `CMakeLists.txt`
- Create: `CMakePresets.json`

### Task 2: Core Engine and C ABI
**Files:**
- Create: `include/vb_engine/c_api.h`
- Create: `src/core/*`
- Create: `src/c_api/*`

### Task 3: Instruments
**Files:**
- Create: `src/instruments/piano/*`
- Create: `src/instruments/guitar/*`

### Task 4: Tests and Benchmark
**Files:**
- Create: `tests/test_engine.cpp`
- Create: `benchmarks/render_benchmark.cpp`

### Task 5: Packaging and Install
**Files:**
- Create: `PKGBUILD`
- Create: `scripts/install-arch.sh`
- Create: `scripts/install-linux.sh`

### Task 6: Validate
- Configure and build.
- Run tests.
- Run benchmark smoke check.
