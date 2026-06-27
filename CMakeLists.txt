cmake_minimum_required(VERSION 3.20)
project(numbers_as_machines)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif ()

add_library(nam INTERFACE)
target_include_directories(nam INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_features(nam INTERFACE cxx_std_20)

# GMP is preferred for the series-tier unbounded accumulators; to keep
# Phase 2 buildable with no external dependency we vendor a minimal
# arbitrary-precision integer (nam::BigInt). Enable this to use GMP instead
# (a thin RAII wrapper would replace the vendored BigInt).
option(NAM_USE_GMP "Use libgmp for series-tier accumulators" OFF)

if (NAM_USE_GMP)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GMP REQUIRED gmp)
    target_include_directories(nam INTERFACE ${GMP_INCLUDE_DIRS})
    target_link_libraries(nam INTERFACE ${GMP_LIBRARIES})
    target_compile_definitions(nam INTERFACE NAM_USE_GMP=1)
endif ()

# Phase 3: the ergonomic Python user layer via pybind11. Off by default so
# the core library still builds with zero external dependencies.
option(NAM_BUILD_PYTHON "Build the pybind11 Python bindings" OFF)
if (NAM_BUILD_PYTHON)
    add_subdirectory(bindings/python)
endif ()

# Phase 3 (WASM target): the ergonomic user layer compiled to WebAssembly via Emscripten + embind
# Enable with the Emscripten toolchain:
#   emcmake cmake -S . -B build-wasm -DNAM_BUILD_WASM=ON && cmake --build build-wasm
#   node build-wasm/bindings/wasm/test_nam_wasm.js
option(NAM_BUILD_WASM "Build the Emscripten/embind WebAssembly bindings" OFF)
if (NAM_BUILD_WASM)
    add_subdirectory(bindings/wasm)
endif ()


enable_testing()
add_subdirectory(tests)