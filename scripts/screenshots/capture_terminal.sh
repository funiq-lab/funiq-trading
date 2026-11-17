#!/usr/bin/env bash
# Capture real xterm windows in the Linux development container.
set -euo pipefail
cd /app
export DISPLAY=:97
export XDG_RUNTIME_DIR=/tmp/funiq-xdg
mkdir -p "$XDG_RUNTIME_DIR" /app/build/terminal-capture
chmod 700 "$XDG_RUNTIME_DIR"
Xvfb "$DISPLAY" -screen 0 1800x1100x24 -nolisten tcp >/tmp/funiq-xvfb.log 2>&1 &
x_pid=$!
openbox >/tmp/funiq-openbox.log 2>&1 &
wm_pid=$!
terminal_pid=''
cleanup() {
    if [[ -f /app/build/terminal-core/core.pid ]]; then
        kill -TERM "$(cat /app/build/terminal-core/core.pid)" 2>/dev/null || true
    fi
    [[ -z "$terminal_pid" ]] || kill "$terminal_pid" 2>/dev/null || true
    kill "$wm_pid" "$x_pid" 2>/dev/null || true
}
trap cleanup EXIT
for _ in $(seq 1 40); do
    [[ -S /tmp/.X11-unix/X97 ]] && break
    sleep 0.1
done
# Relaunch the window manager if its initial attempt preceded the X server.
if ! kill -0 "$wm_pid" 2>/dev/null; then
    openbox >/tmp/funiq-openbox.log 2>&1 & wm_pid=$!
fi
assets=/app/docs/assets/mock-demo
rm -f /app/build/terminal-capture/core-console.txt \
      /app/build/terminal-capture/cpp-native.txt \
      /app/build/terminal-capture/python-c-api.txt \
      /app/build/terminal-core/core.pid
open_terminal() {
    xterm -T "$1" -fa 'DejaVu Sans Mono' -fs 11 -geometry "180x${2}+0+0" -hold \
        -e bash -c "$3" >/tmp/funiq-xterm.log 2>&1 &
    terminal_pid=$!
}
wait_for() {
    for _ in $(seq 1 300); do
        [[ -f "$1" ]] && grep -q "$2" "$1" && return 0
        sleep 0.1
    done
    cat /tmp/funiq-xterm.log >&2
    return 1
}
# The foreground C++ server produces these logs live in the terminal.
export MOCK_RUNTIME_DIR=/app/build/terminal-core
open_terminal 'mock_core - running C++ backend' 52 \
    'set -x; bash examples/mock_core/start.sh 2>&1 | tee /app/build/terminal-capture/core-console.txt'
wait_for /app/build/terminal-capture/core-console.txt 'Press Ctrl+C to shutdown'
sleep 0.3
rm -f "$assets/core-startup.png"
scrot --focused --border "$assets/core-startup.png"
cp /app/build/terminal-capture/core-console.txt "$assets/core-startup.txt"
python3 examples/http_web/exercise.py --url http://127.0.0.1:8080 \
    --output /app/build/terminal-capture/http-results >/app/build/terminal-capture/http-exercise.txt
sleep 3.2
rm -f "$assets/core-activity.png"
scrot --focused --border "$assets/core-activity.png"
cp /app/build/terminal-capture/core-console.txt "$assets/core-activity.txt"
kill -TERM "$(cat /app/build/terminal-core/core.pid)"
wait_for /app/build/terminal-capture/core-console.txt 'shut down successfully'
sleep 0.5
cp /app/build/terminal-capture/core-console.txt "$assets/core-full.txt"
kill "$terminal_pid" 2>/dev/null || true

open_terminal 'cpp_native - direct C++ service calls' 29 \
    'set -x; bash examples/cpp_native/run.sh 2>&1 | tee /app/build/terminal-capture/cpp-native.txt'
wait_for /app/build/terminal-capture/cpp-native.txt 'PASS: direct C++'
sleep 0.3
rm -f "$assets/cpp-native.png"
scrot --focused --border "$assets/cpp-native.png"
cp /app/build/terminal-capture/cpp-native.txt "$assets/cpp-native.txt"
kill "$terminal_pid" 2>/dev/null || true

open_terminal 'python_c_api - Python C-interface calls' 49 \
    'set -x; bash examples/python_c_api/run.sh 2>&1 | tee /app/build/terminal-capture/python-c-api.txt'
wait_for /app/build/terminal-capture/python-c-api.txt 'PASS: C-interface'
sleep 0.3
rm -f "$assets/python-c-api.png"
scrot --focused --border "$assets/python-c-api.png"
cp /app/build/terminal-capture/python-c-api.txt "$assets/python-c-api.txt"
cp /app/build/python-c-api-capture/report.json "$assets/python-c-api-report.json"
python3 - <<'PY'
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path

assets = Path('/app/docs/assets/mock-demo')
for name in ('core-startup', 'core-activity', 'cpp-native', 'python-c-api'):
    screenshot = assets / f'{name}.png'
    output = assets / f'{name}.txt'
    metadata = {
        'type': 'native-terminal-window-screenshot',
        'application': 'xterm',
        'environment': 'Linux development container with Xvfb and Openbox',
        'capture_method': 'scrot --focused --border',
        'capture_script': 'scripts/screenshots/capture_terminal.sh',
        'captured_at': datetime.fromtimestamp(screenshot.stat().st_mtime, timezone.utc).isoformat(),
        'image_sha256': hashlib.sha256(screenshot.read_bytes()).hexdigest(),
        'output_log': f'docs/assets/mock-demo/{output.name}',
        'output_sha256': hashlib.sha256(output.read_bytes()).hexdigest(),
    }
    screenshot.with_suffix('.png.json').write_text(json.dumps(metadata, indent=2) + '\n')
PY
