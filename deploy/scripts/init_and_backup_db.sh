#!/usr/bin/env bash
# ============================================================================
# 海大通 (HnuTong) · 数据库建表、海大种子初始化与在线热备脚本
# 用法：
#   初始化数据库：bash deploy/scripts/init_and_backup_db.sh init
#   在线热备份  ：bash deploy/scripts/init_and_backup_db.sh backup
# ============================================================================
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DATA_DIR="${ROOT_DIR}/backend/data"
DB_FILE="${DATA_DIR}/hnutong.db"
BACKUP_DIR="${DATA_DIR}/backups"
ACTION="${1:-init}"

mkdir -p "${DATA_DIR}"
chmod 700 "${DATA_DIR}"

if [[ "${ACTION}" == "init" ]]; then
  echo "正在初始化 SQLite 数据库与海南大学元数据种子: ${DB_FILE}"
  sqlite3 "${DB_FILE}" < "${ROOT_DIR}/backend/sql/schema.sql"
  sqlite3 "${DB_FILE}" < "${ROOT_DIR}/backend/config/hnu_seed.sql"
  chmod 600 "${DB_FILE}"
  echo "数据库初始化完成！"
elif [[ "${ACTION}" == "backup" ]]; then
  if [[ ! -f "${DB_FILE}" ]]; then
    echo "错误：数据库文件不存在 (${DB_FILE})，请先执行 init 或启动服务" >&2
    exit 1
  fi
  # 若以 root 调用而数据库属主为 hnutong，则切至 hnutong 执行，避免创建 root 属主的 -wal/-shm 锁死业务进程
  if [[ "$(id -u)" -eq 0 ]] && id -u hnutong >/dev/null 2>&1; then
    DB_OWNER="$(stat -c '%U' "${DB_FILE}" 2>/dev/null || true)"
    if [[ "${DB_OWNER}" == "hnutong" ]]; then
      exec su -s /bin/bash hnutong -c "bash $(printf '%q' "$0") backup"
    fi
  fi
  mkdir -p "${BACKUP_DIR}"
  chmod 700 "${BACKUP_DIR}"
  STAMP="$(date +%Y%m%d_%H%M%S)"
  TARGET="${BACKUP_DIR}/hnutong_${STAMP}.db"
  if ! sqlite3 -cmd ".timeout 5000" "${DB_FILE}" ".backup '${TARGET}'"; then
    rm -f "${TARGET}"
    echo "SQLite WAL 在线热备失败" >&2
    exit 1
  fi
  gzip -f "${TARGET}"
  chmod 600 "${TARGET}.gz"
  # 自动清理超过 14 天的历史备份包
  find "${BACKUP_DIR}" -name "hnutong_*.db.gz" -mtime +14 -delete
  echo "SQLite WAL 在线热备完成: ${TARGET}.gz"
else
  echo "未知指令: ${ACTION}（可选 init | backup）"
  exit 1
fi
