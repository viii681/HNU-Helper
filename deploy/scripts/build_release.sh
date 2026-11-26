#!/usr/bin/env bash
# ============================================================================
# 海大通 (HnuTong) · Linux 生产环境前后端完整构建与自检脚本
# 用法：bash deploy/scripts/build_release.sh
# ============================================================================
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
echo "== 项目根目录: ${ROOT_DIR} =="

echo "[1/4] 编译 C++20 后端服务 (Release 模式)..."
cd "${ROOT_DIR}/backend"
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j "$(nproc)"

echo "[2/4] 执行 C++20 核心加密/解析/数据库单元测试..."
./build/hnutong_tests

echo "[3/4] 安装前端依赖并构建生产静态包 (frontend/dist)..."
cd "${ROOT_DIR}/frontend"
npm ci --prefer-offline || npm install
npm run build

echo "[4/4] 初始化生产配置模板（若 config.json 尚不存在）..."
cd "${ROOT_DIR}/backend"
mkdir -p data
if [[ ! -f config/config.json ]]; then
  cp config/config.example.json config/config.json
  # 生产环境默认关闭体验演示账号，仅允许真实海南大学学号登录
  sed -i 's/"enable_demo_account": true/"enable_demo_account": false/' config/config.json
  echo "已生成默认生产配置文件 backend/config/config.json（enable_demo_account=false）"
fi

echo "========================================================"
echo "构建成功！"
echo "  C++ 后端可执行文件 : ${ROOT_DIR}/backend/build/hnutong_server"
echo "  前端静态产物目录   : ${ROOT_DIR}/frontend/dist"
echo "========================================================"
