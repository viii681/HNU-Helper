#!/usr/bin/env bash
# ============================================================================
# 海大通 (HnuTong) · Ubuntu 22.04 / 24.04 / Debian 12 依赖一键安装脚本
# 用法：sudo bash deploy/scripts/install_deps_ubuntu.sh
# ============================================================================
set -euo pipefail

if [[ "${EUID}" -ne 0 ]]; then
  echo "请使用 root 或 sudo 运行此脚本：sudo bash $0"
  exit 1
fi

echo "[1/4] 更新 APT 软件源索引..."
apt-get update -y

echo "[2/4] 安装 C++20 编译工具链与核心原生库 (GCC, CMake, Ninja, OpenSSL, libcurl, SQLite3)..."
apt-get install -y \
  build-essential \
  g++ \
  cmake \
  ninja-build \
  pkg-config \
  libssl-dev \
  libcurl4-openssl-dev \
  libsqlite3-dev \
  sqlite3 \
  ca-certificates \
  curl \
  git

echo "[3/4] 安装 Nginx 与 Let's Encrypt Certbot HTTPS 工具..."
apt-get install -y nginx certbot python3-certbot-nginx

echo "[4/4] 检查 Node.js 环境（仅前端构建打包阶段需要，服务端运行时为纯 C++）..."
if ! command -v node >/dev/null 2>&1; then
  curl -fsSL https://deb.nodesource.com/setup_20.x | bash -
  apt-get install -y nodejs
fi

echo "========================================================"
echo "依赖环境安装完成！"
echo "  g++    : $(g++ --version | head -n 1)"
echo "  cmake  : $(cmake --version | head -n 1)"
echo "  sqlite3: $(sqlite3 --version)"
echo "  node   : $(node -v)"
echo "  nginx  : $(nginx -v 2>&1)"
echo "========================================================"
