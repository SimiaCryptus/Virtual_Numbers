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

enable_testing()
add_subdirectory(tests)