#!/bin/bash

cmake -S . -B build
cmake --build build --target nam_tests
