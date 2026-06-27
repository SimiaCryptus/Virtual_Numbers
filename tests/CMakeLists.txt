cmake_minimum_required(VERSION 3.20)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(nam_tests
        all_tests.cpp
)
target_link_libraries(nam_tests PRIVATE nam)
target_compile_features(nam_tests PRIVATE cxx_std_20)

add_test(NAME nam_tests COMMAND nam_tests)
