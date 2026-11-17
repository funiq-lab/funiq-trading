#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd "$(dirname "$0")/../.." && pwd)"
runtime_dir="$repo_dir/build/cpp-native-run"
mkdir -p "$runtime_dir"
cd "$runtime_dir"
export LD_LIBRARY_PATH="$repo_dir/build/mock/build_x64/Debug/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$repo_dir/build/mock/build_x64/Debug/bin/cpp_native_example" "$repo_dir/examples/mock_core"
