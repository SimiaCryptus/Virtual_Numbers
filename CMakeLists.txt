cmake_minimum_required(VERSION 3.20)
project(numbers_as_machines C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# APInt is desired per the plan, but to keep Phase 1 buildable without a
# full LLVM install we provide a tiny vendored bounded-bit-width integer
# (nam::BoundedInt) that mirrors the APInt semantics we actually use:
# bounded bit-width with explicit O(log n) growth tracking.
option(NAM_USE_LLVM_APINT "Use llvm::APInt instead of vendored BoundedInt" OFF)
# GMP is preferred for the series-tier unbounded accumulators; to keep
# Phase 2 buildable with no external dependency we vendor a minimal
# arbitrary-precision integer (nam::BigInt). Enable this to use GMP instead
# (a thin RAII wrapper would replace the vendored BigInt).
option(NAM_USE_GMP "Use libgmp for series-tier accumulators" OFF)

add_library(nam INTERFACE)
target_include_directories(nam INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_features(nam INTERFACE cxx_std_20)

if(NAM_USE_LLVM_APINT)
    find_package(LLVM CONFIG REQUIRED)
    target_include_directories(nam INTERFACE ${LLVM_INCLUDE_DIRS})
    target_compile_definitions(nam INTERFACE NAM_USE_LLVM_APINT=1)
endif()
# Phase 4: the JIT compilation path. compile(expr_tree) -> NumVMFn. Off by
# default so the core library still builds with zero external dependencies
# (it falls back to a function-pointer interpreter that honors the same C
# ABI). Enable to emit a single specialized NumVMFn via LLVM ORC v2.
option(NAM_USE_LLVM_JIT "Use LLVM ORC v2 JIT for compile(expr_tree)" OFF)
if(NAM_USE_LLVM_JIT)
    find_package(LLVM CONFIG REQUIRED)
    message(STATUS "NAM JIT: using LLVM ${LLVM_PACKAGE_VERSION}")
    target_include_directories(nam INTERFACE ${LLVM_INCLUDE_DIRS})
    target_compile_definitions(nam INTERFACE ${LLVM_DEFINITIONS})
    target_compile_definitions(nam INTERFACE NAM_USE_LLVM_JIT=1)
    llvm_map_components_to_libnames(NAM_LLVM_LIBS
        core orcjit native support irreader)
    target_link_libraries(nam INTERFACE ${NAM_LLVM_LIBS})
endif()

if(NAM_USE_GMP)
     find_package(PkgConfig REQUIRED)
     pkg_check_modules(GMP REQUIRED gmp)
     target_include_directories(nam INTERFACE ${GMP_INCLUDE_DIRS})
     target_link_libraries(nam INTERFACE ${GMP_LIBRARIES})
     target_compile_definitions(nam INTERFACE NAM_USE_GMP=1)
endif()

# Phase 3: the ergonomic Python user layer via pybind11. Off by default so
# the core library still builds with zero external dependencies.
option(NAM_BUILD_PYTHON "Build the pybind11 Python bindings" OFF)
if(NAM_BUILD_PYTHON)
   add_subdirectory(bindings)
endif()


enable_testing()
add_subdirectory(tests)