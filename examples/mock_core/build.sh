#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$repo_dir"
cmake -S . -B build/mock -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_VENDOR_ADAPTERS=OFF -DBUILD_MOCK_EXAMPLES=ON -DBUILD_MOCK_TESTS=ON
cmake --build build/mock --parallel "${BUILD_JOBS:-2}"
ctest --test-dir build/mock --output-on-failure
