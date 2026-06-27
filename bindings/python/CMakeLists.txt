# bindings/python/CMakeLists.txt
#
# Phase 3: pybind11 Python bindings for the user-facing Number API.
# The binding exposes ONLY the combinator/user layer (Number), never the
# raw NumVMFn ABI, per THEORY.md "Most users never see the primitive layer".
#
# Honesty commitments preserved across the language boundary:
#   - Comparison maps to Python tri-state: True | False | None (pending).
#   - Fork cost is annotated by tier (fork_cost()).
#   - Memoization is an explicit mode (.streaming() / .cached(N)).

find_package(pybind11 CONFIG QUIET)

if (NOT pybind11_FOUND)
    message(STATUS "pybind11 not found via find_package; trying FetchContent")
    include(FetchContent)
    FetchContent_Declare(
            pybind11
            GIT_REPOSITORY https://github.com/pybind/pybind11.git
            GIT_TAG v2.11.1
    )
    FetchContent_MakeAvailable(pybind11)
endif ()

pybind11_add_module(nam_py MODULE nam_py.cpp)
target_link_libraries(nam_py PRIVATE nam)
target_compile_features(nam_py PRIVATE cxx_std_20)
set_target_properties(nam_py PROPERTIES OUTPUT_NAME "nam")