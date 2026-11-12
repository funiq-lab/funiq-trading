#!/usr/bin/env bash
set -euo pipefail

# 固定参数：脚本所在目录即应用目录
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
APP_PORT=8080
APP_DIR="${SCRIPT_DIR}"
APP_NAME="ft_trading_client"
APP_BIN="${APP_DIR}/${APP_NAME}"
API_BASE="http://127.0.0.1:${APP_PORT}"
LOG_DIR="${APP_DIR}/logs"
LOG_FILE="${LOG_DIR}/restart.log"

log() {
  echo "[$(date +'%F %T')] $*" >> "${LOG_FILE}"
}

mkdir -p "${LOG_DIR}" || true
: > "${LOG_FILE}"
log "restart_trading.sh begin (APP_BIN=${APP_BIN})"
log "env: OLD_PID=${OLD_PID:-} pid=$$ ppid=$PPID"



# 基础校验
if [[ ! -x "${APP_BIN}" ]]; then
  log "ERROR: app binary not found or not executable: ${APP_BIN}"
  exit 1
fi

# 1) 优雅停止（快速超时，忽略失败）
http_code=$(curl -s -m 2 -o /dev/null -w "%{http_code}" -X POST "${API_BASE}/system/stop" || true)
log "stop request to ${API_BASE}/system/stop -> http=${http_code}"
sleep 3

# 2) 尝试优雅关闭（SIGTERM），超时再 kill -9（模糊匹配，但限定二进制名边界/全路径）
# 收集目标进程（快照），避免新进程被误杀
collect_pids() {
  local pattern_full="(^|/)${APP_NAME}( |$)"
  local pattern_path="${APP_BIN}( |$)"
  mapfile -t p1 < <(pgrep -f "${pattern_full}|${pattern_path}" 2>/dev/null || true)
  printf '%s\n' "${p1[@]:-}" | awk 'NF{a[$0]=1}END{for(k in a)print k}' | sort -n
}

mapfile -t ft_pids < <(collect_pids)
if (( ${#ft_pids[@]} > 0 )); then
  log "SIGTERM ${APP_NAME} pids=${ft_pids[*]}"
  for p in "${ft_pids[@]}"; do kill -TERM "$p" 2>/dev/null || true; done
  # 等待最多 5s（0.5s 步进）
  for _ in {1..10}; do
    any=0
    for p in "${ft_pids[@]}"; do kill -0 "$p" 2>/dev/null && any=1 && break; done
    (( any==0 )) && break
    sleep 0.5
  done
  # 仍存活则强杀
  remain=()
  for p in "${ft_pids[@]}"; do kill -0 "$p" 2>/dev/null && remain+=("$p"); done
  if (( ${#remain[@]} > 0 )); then
    log "kill -9 ${APP_NAME} pids=${remain[*]}"
    for p in "${remain[@]}"; do kill -9 "$p" 2>/dev/null || true; done
  fi
else
  log "no ${APP_NAME} process found to kill"
fi
sleep 3

# 3) 启动（不使用 exec），然后校验是否存活
cd "${APP_DIR}"
chmod u+x "${APP_BIN}" || true
log "start ${APP_BIN} (app stdout/stderr -> restart.log)"
nohup "${APP_BIN}" >> "${LOG_FILE}" 2>&1 & disown
new_pid=$!
for i in {1..10}; do
if ! kill -0 "${new_pid}" 2>/dev/null; then break; fi
sleep 0.5
done
if kill -0 "${new_pid}" 2>/dev/null; then
log "started ok pid=${new_pid}"
else
log "ERROR: start failed (pid exited). See restart.log for app output"
fi



exit 0