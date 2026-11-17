#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd "$(dirname "$0")/../.." && pwd)"
export LD_LIBRARY_PATH="$repo_dir/build/mock/build_x64/Debug/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec python3 "$repo_dir/examples/python_c_api/demo.py" "$@"
