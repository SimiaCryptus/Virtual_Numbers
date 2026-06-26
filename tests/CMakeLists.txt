add_executable(nam_tests
        all_tests.cpp
)
target_link_libraries(nam_tests PRIVATE nam)
target_compile_options(nam_tests PRIVATE -O2 -Wall -Wextra)

# Sanitizers per the plan (ASan + UBSan on test targets).
option(NAM_SANITIZE "Enable ASan+UBSan on tests" ON)
if (NAM_SANITIZE)
    target_compile_options(nam_tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(nam_tests PRIVATE -fsanitize=address,undefined)
endif ()

add_test(NAME nam_tests COMMAND nam_tests)