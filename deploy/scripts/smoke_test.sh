#!/usr/bin/env bash
# ============================================================================
# 海大通 (HnuTong) · Linux 部署后冒烟验收脚本
# 用法：bash deploy/scripts/smoke_test.sh http://127.0.0.1:8788
# ============================================================================
set -euo pipefail

BASE_URL="${1:-http://127.0.0.1:8788}"
echo "== 正在对 ${BASE_URL} 执行核心接口冒烟校验 =="

check_endpoint() {
  local label="$1"
  local url="$2"
  local needle="$3"
  echo -n "${label} ... "
  local resp
  resp="$(curl -fsS "${url}")"
  grep -qF "${needle}" <<<"${resp}"
  echo "PASS"
}

check_endpoint "[1/4] 检查 /api/v1/health" "${BASE_URL}/api/v1/health" '"ok":true'
check_endpoint "[2/4] 检查 /api/v1/meta/bootstrap 海大 11 节作息与五校区元数据" "${BASE_URL}/api/v1/meta/bootstrap" '"haidian"'
check_endpoint "[3/4] 检查 /api/v1/jw/captcha 强智教务验证码接口" "${BASE_URL}/api/v1/jw/captcha" '"ok":true'
check_endpoint "[4/4] 检查 /api/v1/notice 海南大学实时通知抓取与分类" "${BASE_URL}/api/v1/notice?limit=10" '"ok":true'

echo "== 全部冒烟检查通过！服务运行健康 =="
