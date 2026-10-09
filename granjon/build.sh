#!/usr/bin/env bash
# Granjon build. Deps (Fedora): gtkmm4.0-devel spdlog-devel libxml2-devel cmake gcc-c++.
# Deps (Ubuntu/Debian): libgtkmm-4.0-dev libspdlog-dev libxml2-dev cmake g++.
set -e
cd "$(dirname "${BASH_SOURCE[0]}")"
cmake -B build
cmake --build build -- -j"$(nproc)"
echo ""
echo "Built: ./build/granjon  and  ./build/granjon_selftest"
echo "Smoke: ./build/granjon_selftest"
