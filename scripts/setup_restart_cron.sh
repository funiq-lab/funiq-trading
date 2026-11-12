#!/usr/bin/env bash
set -euo pipefail

# 说明：
# - 将每天 08:35 运行的重启任务写入当前用户的 crontab
# - 使用与本脚本同目录的 restart_trading.sh 的绝对路径，避免工作目录影响
# - 支持卸载：scripts/setup_restart_cron.sh remove

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
RESTART_SCRIPT="${SCRIPT_DIR}/restart_trading.sh"
CRON_MARKER="# FT_RESTART_CRON"
LOG_DIR="${SCRIPT_DIR}/logs"
LOG_FILE="${LOG_DIR}/cron_restart.log"
BASH_BIN="$(command -v bash 2>/dev/null || echo /bin/bash)"

if [[ ${1:-} == "remove" || ${1:-} == "uninstall" ]]; then
  TMP_FILE="$(mktemp)"
  crontab -l 2>/dev/null | grep -v "${CRON_MARKER}" >"${TMP_FILE}" || true
  crontab "${TMP_FILE}" || true
  rm -f "${TMP_FILE}"
  echo "[OK] 已卸载每日 08:35 重启定时任务"
  exit 0
fi

# 确保目标脚本具有执行权限
if [[ -f "${RESTART_SCRIPT}" ]]; then
  chmod u+x "${RESTART_SCRIPT}" 2>/dev/null || true
fi

if [[ ! -x "${RESTART_SCRIPT}" ]]; then
  echo "[ERR] 未找到或不可执行的脚本: ${RESTART_SCRIPT}" >&2
  echo "[TIP] 可尝试: chmod +x ${RESTART_SCRIPT}" >&2
  exit 1
fi

mkdir -p "${LOG_DIR}" || true
# 覆写日志文件
: > "${LOG_FILE}"

# 每天 08:35 触发。用 bash 显式执行脚本，避免执行位/挂载 noexec 导致的问题
CRON_LINE="35 8 * * * : > ${LOG_FILE} && ${BASH_BIN} ${RESTART_SCRIPT} >> ${LOG_FILE} 2>&1 ${CRON_MARKER}"

TMP_FILE="$(mktemp)"
# 去掉旧的相同标记，保持幂等
crontab -l 2>/dev/null | grep -v "${CRON_MARKER}" >"${TMP_FILE}" || true
echo "${CRON_LINE}" >>"${TMP_FILE}"
crontab "${TMP_FILE}"
rm -f "${TMP_FILE}"

echo "[OK] 已安装每日 08:35 重启定时任务"
echo "     任务：${CRON_LINE}"
echo "     日志：${LOG_FILE}"


