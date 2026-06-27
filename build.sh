#!/bin/bash

cmake -S . -B build
cmake --build build --target nam_tests

source ~/emsdk/emsdk_env.sh
emcmake cmake -S . -B build-wasm -DNAM_BUILD_WASM=ON
cmake --build build-wasm