cmake_minimum_required(VERSION 3.21)
project(vb_engine VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(VB_ENGINE_BUILD_TESTS "Build unit tests" ON)
option(VB_ENGINE_BUILD_BENCHMARKS "Build benchmark targets" ON)
option(VB_ENGINE_BUILD_PIANO_TESTER "Build SDL piano tester app" ON)

find_package(OpenCL QUIET)

add_library(vb_engine_core STATIC
    src/core/engine.cpp
    src/core/voice_pool.cpp
    src/instruments/piano/sample_library.cpp
    src/instruments/piano/mechanics.cpp
    src/instruments/piano/fem_string_body_model.cpp
    src/instruments/piano/sampled_piano_voice.cpp
    src/instruments/piano/resonance_matrix.cpp
    src/instruments/piano/post_processor.cpp
    src/instruments/piano/acoustic_grand_voice.cpp
    src/instruments/guitar/clean_electric_voice.cpp
)

target_include_directories(vb_engine_core
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/src
)

target_compile_options(vb_engine_core PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(vb_engine_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(OpenCL_FOUND)
    target_link_libraries(vb_engine_core PRIVATE OpenCL::OpenCL)
    target_compile_definitions(vb_engine_core PRIVATE VB_ENGINE_ENABLE_OPENCL=1)
endif()

add_library(vb_engine SHARED src/c_api/c_api.cpp)
target_link_libraries(vb_engine PRIVATE vb_engine_core)
target_include_directories(vb_engine PUBLIC ${PROJECT_SOURCE_DIR}/include)
set_target_properties(vb_engine PROPERTIES
    OUTPUT_NAME vb_engine
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
)

add_executable(vb_engine_standalone src/standalone/main.cpp)
target_link_libraries(vb_engine_standalone PRIVATE vb_engine_core)

if(VB_ENGINE_BUILD_TESTS)
    enable_testing()
    if(EXISTS "${PROJECT_SOURCE_DIR}/tests/test_engine.cpp")
        add_executable(vb_engine_tests tests/test_engine.cpp src/c_api/c_api.cpp)
        target_link_libraries(vb_engine_tests PRIVATE vb_engine_core)
        add_test(NAME vb_engine_tests COMMAND vb_engine_tests)
    else()
        message(STATUS "tests/test_engine.cpp not found: skipping vb_engine_tests")
    endif()

    if(EXISTS "${PROJECT_SOURCE_DIR}/tests/test_soak.cpp")
        add_executable(vb_engine_soak_tests tests/test_soak.cpp src/c_api/c_api.cpp)
        target_link_libraries(vb_engine_soak_tests PRIVATE vb_engine_core)
        add_test(NAME vb_engine_soak_tests COMMAND vb_engine_soak_tests)
        set_tests_properties(vb_engine_soak_tests PROPERTIES TIMEOUT 120)
    else()
        message(STATUS "tests/test_soak.cpp not found: skipping vb_engine_soak_tests")
    endif()
endif()

if(VB_ENGINE_BUILD_BENCHMARKS)
    if(EXISTS "${PROJECT_SOURCE_DIR}/benchmarks/render_benchmark.cpp")
        add_executable(vb_engine_benchmark benchmarks/render_benchmark.cpp src/c_api/c_api.cpp)
        target_link_libraries(vb_engine_benchmark PRIVATE vb_engine_core)
    else()
        message(STATUS "benchmarks/render_benchmark.cpp not found: skipping vb_engine_benchmark")
    endif()

    if(EXISTS "${PROJECT_SOURCE_DIR}/benchmarks/onset_stress_benchmark.cpp")
        add_executable(vb_engine_onset_stress_benchmark benchmarks/onset_stress_benchmark.cpp src/c_api/c_api.cpp)
        target_link_libraries(vb_engine_onset_stress_benchmark PRIVATE vb_engine_core)
    else()
        message(STATUS "benchmarks/onset_stress_benchmark.cpp not found: skipping vb_engine_onset_stress_benchmark")
    endif()
endif()

add_executable(vb_engine_sample_generator tools/generate_piano_samples.cpp src/c_api/c_api.cpp)
target_link_libraries(vb_engine_sample_generator PRIVATE vb_engine_core)

add_executable(vb_engine_compare_wavs tools/compare_wav_dirs.cpp)
add_executable(vb_engine_analyze_wavs tools/analyze_wav_quality.cpp)

if(VB_ENGINE_BUILD_PIANO_TESTER)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(SDL2 QUIET sdl2)
        pkg_check_modules(ALSA QUIET alsa)
    endif()

    if(SDL2_FOUND)
        add_executable(vb_engine_piano_tester tools/piano_tester/main.cpp src/c_api/c_api.cpp)
        target_link_libraries(vb_engine_piano_tester PRIVATE vb_engine_core ${SDL2_LIBRARIES})
        target_include_directories(vb_engine_piano_tester PRIVATE ${SDL2_INCLUDE_DIRS})
        target_compile_options(vb_engine_piano_tester PRIVATE ${SDL2_CFLAGS_OTHER})
        if(ALSA_FOUND)
            target_link_libraries(vb_engine_piano_tester PRIVATE ${ALSA_LIBRARIES})
            target_include_directories(vb_engine_piano_tester PRIVATE ${ALSA_INCLUDE_DIRS})
            target_compile_options(vb_engine_piano_tester PRIVATE ${ALSA_CFLAGS_OTHER})
            target_compile_definitions(vb_engine_piano_tester PRIVATE VB_TESTER_HAS_ALSA_MIDI=1)
        endif()
    else()
        message(STATUS "SDL2 not found: skipping vb_engine_piano_tester")
    endif()
endif()

include(GNUInstallDirs)
install(TARGETS vb_engine vb_engine_standalone
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(FILES include/vb_engine/c_api.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/vb_engine
)
