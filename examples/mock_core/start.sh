#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$repo_dir"
bash examples/mock_core/build.sh

# Keep all generated configuration, logs and database files in the build tree.
runtime_dir="${MOCK_RUNTIME_DIR:-$repo_dir/build/mock-run}"
mkdir -p "$runtime_dir/data/backup"
cat > "$runtime_dir/.env" <<ENV
QUOTE_CONFIG_FILE=$repo_dir/examples/mock_core/quote_config.json
TRADE_CONFIG_FILE=$repo_dir/examples/mock_core/trade_config.json
STRATEGY_CONFIG_FILE=$repo_dir/examples/mock_core/strategy_config.json
SYSTEM_CONFIG_FILE=$repo_dir/examples/mock_core/system_config.json
FT_DB_PATH=$runtime_dir/data/trading.db
FT_DB_BACKUP_PATH=$runtime_dir/data/backup
HTTP_SERVER_ADDRESS=0.0.0.0
HTTP_SERVER_PORT=8080
HTTP_SERVER_THREADS=4
HTTP_REQUEST_LOGGING=false
LOG_ENABLED=true
LOG_LEVEL=info
LOG_CONSOLE=true
LOG_FILE_ENABLED=true
LOG_FILE_DIR=$runtime_dir/logs
ENV
cd "$runtime_dir"
export LD_LIBRARY_PATH="$repo_dir/build/mock/build_x64/Debug/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
echo "$$" > "$runtime_dir/core.pid"
exec "$repo_dir/build/mock/build_x64/Debug/bin/ft_trading_client"
