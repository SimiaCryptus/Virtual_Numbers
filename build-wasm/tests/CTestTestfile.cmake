# CMake generated Testfile for 
# Source directory: /home/andrew/code/NAM/tests
# Build directory: /home/andrew/code/NAM/build-wasm/tests
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[nam_tests]=] "/home/andrew/emsdk/node/22.16.0_64bit/bin/node" "/home/andrew/code/NAM/build-wasm/tests/nam_tests.js")
set_tests_properties([=[nam_tests]=] PROPERTIES _BACKTRACE_TRIPLES "/home/andrew/code/NAM/tests/CMakeLists.txt;14;add_test;/home/andrew/code/NAM/tests/CMakeLists.txt;0;")
