#!/usr/bin/env bash
# Granjon build. Deps (Fedora): gtkmm4.0-devel spdlog-devel libxml2-devel cmake gcc-c++.
# Deps (Ubuntu/Debian): libgtkmm-4.0-dev libspdlog-dev libxml2-dev cmake g++.
set -e
cd "$(dirname "${BASH_SOURCE[0]}")"
# Optimised with debug info unless BUILD_TYPE says otherwise (an old Debug
# cache is overridden here -- that was the "a little slow" of s03).
cmake -B build -DCMAKE_BUILD_TYPE="${BUILD_TYPE:-RelWithDebInfo}"
cmake --build build -- -j"$(nproc)"
echo ""
echo "Built: ./build/granjon  and  ./build/granjon_selftest"
echo "Smoke: ./build/granjon_selftest"
