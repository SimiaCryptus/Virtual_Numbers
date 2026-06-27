# bindings/wasm/CMakeLists.txt
#
# Phase 3 (WASM target): Emscripten/embind bindings for the user-facing
# Number API. Exposes ONLY the combinator/user layer (Number), never the
# raw NumVMFn ABI -- identical surface to the pybind11 bindings.
#
# Honesty commitments preserved across the JS/WASM boundary:
#   - Comparison maps to JS tri-state: -1 | 1 | null  and  true|false|null.
#   - Fork cost is annotated by tier (fork_cost()).
#   - Memoization is an explicit mode (.streaming() / .cached(N)).
#
# This subdirectory is only added when configuring with the Emscripten
# toolchain (EMSCRIPTEN is defined by Emscripten.cmake). Outside Emscripten
# we emit a clear diagnostic so misconfiguration fails fast.
if (NOT EMSCRIPTEN)
    message(FATAL_ERROR
        "NAM_BUILD_WASM=ON requires the Emscripten toolchain. Configure with "
        "-DCMAKE_TOOLCHAIN_FILE=<emsdk>/upstream/emscripten/cmake/Modules/"
        "Platform/Emscripten.cmake")
endif ()

add_executable(nam_wasm nam_wasm.cpp)
target_link_libraries(nam_wasm PRIVATE nam)
target_compile_features(nam_wasm PRIVATE cxx_std_20)

# Emscripten / embind link flags:
#   --bind            : enable embind.
#   MODULARIZE=1      : export a factory (await createNam()) -- matches nam.js.
#   EXPORT_ES6=0      : CommonJS factory so `require()` works in Node.
#   ALLOW_MEMORY_GROWTH : series-tier BigInt accumulators grow at runtime.
#   ENVIRONMENT=node,web : usable from both Node and the browser.
target_link_options(nam_wasm PRIVATE
        "--bind"
        "-sMODULARIZE=1"
        "-sEXPORT_NAME=createNam"
         "-sEXPORT_ES6=1"
        "-sALLOW_MEMORY_GROWTH=1"
         "-sENVIRONMENT=node,web")
set_target_properties(nam_wasm PROPERTIES OUTPUT_NAME "nam_wasm")

# Copy the ergonomic JS wrapper, example, and smoke test next to the built
# nam_wasm.js so `node example.js` / `node test_nam_wasm.js` work in-place.
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nam.js
        ${CMAKE_CURRENT_BINARY_DIR}/nam.js COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/example.js
        ${CMAKE_CURRENT_BINARY_DIR}/example.js COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/test_nam_wasm.js
        ${CMAKE_CURRENT_BINARY_DIR}/test_nam_wasm.js COPYONLY)

# Register the Node smoke test with ctest when a node interpreter is found.
find_program(NODE_EXECUTABLE NAMES node nodejs)
if (NODE_EXECUTABLE)
    add_test(NAME nam_wasm_tests
            COMMAND ${NODE_EXECUTABLE}
            ${CMAKE_CURRENT_BINARY_DIR}/test_nam_wasm.js)
    set_tests_properties(nam_wasm_tests PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
else ()
    message(STATUS "NAM WASM: node not found; skipping nam_wasm_tests")
endif ()