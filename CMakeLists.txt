cmake_minimum_required(VERSION 3.20)
project(numbers_as_machines CXX)

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

add_library(nam INTERFACE)
target_include_directories(nam INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_features(nam INTERFACE cxx_std_20)

if(NAM_USE_LLVM_APINT)
    find_package(LLVM CONFIG REQUIRED)
    target_include_directories(nam INTERFACE ${LLVM_INCLUDE_DIRS})
    target_compile_definitions(nam INTERFACE NAM_USE_LLVM_APINT=1)
endif()

enable_testing()
add_subdirectory(tests)