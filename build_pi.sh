#!/usr/bin/env bash
set -euo pipefail

cmake -S . -B build-pi -DCMAKE_BUILD_TYPE=Release
cmake --build build-pi --parallel

echo "Built build-pi/bin/Hub75Simulator"
echo "Run with: sudo ./build-pi/bin/Hub75Simulator"
