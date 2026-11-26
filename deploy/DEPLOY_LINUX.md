# 海大通 (HnuTong) · Linux 云服务器生产部署完整手册

本文档指导您将 **海大通（海南大学校园一体化服务系统）** 从零编译、配置并正式上线到您的 Linux 云服务器（推荐 **Ubuntu 22.04 LTS / 24.04 LTS** 或 **Debian 12**），实现 **自有域名绑定 + HTTPS 自动续期 + Nginx 动静分离反向代理 + Systemd 进程守护 + SQLite WAL 在线热备 + 脱敏滚动日志**。

---

## 一、生产环境部署拓扑

```text
学生手机/电脑浏览器 (HTTPS 443, HTTP/2)
        │
        ▼
  [ 自有域名 DNS A/AAAA 解析 ]
        │
        ▼
  [ Nginx 反向代理网关 (:80 -> :443 TLS 1.3) ]
   ├── 静态资源 /*          ──► 直接读取 /opt/hnutong/frontend/dist (Gzip + 指纹强缓存)
   └── 动态接口 /api/v1/*   ──► 反代至本机 http://127.0.0.1:8788
                                      │
                                      ▼
                    [ hnutong_server (C++20 原生编译守护进程) ]
                     ├── Systemd 沙盒守护 (User=hnutong, 自动拉起)
                     ├── AES-256-GCM 凭据保险箱 (/opt/hnutong/backend/data/master.key, 0600)
                     ├── SQLite3 WAL 数据库 (/opt/hnutong/backend/data/hnutong.db)
                     ├── 自动滚动脱敏日志 (/var/log/hnutong/hnutong.log)
                     └── 出站 HTTPS (libcurl + OpenSSL) ──► 海大强智教务 / 智慧门户 / 本科生院官网 / 超星 / MOOC
```

---

## 二、环境准备与依赖安装

### 1. 云服务器最低与推荐配置
- **CPU / 内存**：最低 1 核 1 GB（C++20 原生编译运行时常驻内存仅 `15MB ~ 35MB`！编译阶段若内存小于 2GB 建议开启 2GB Swap），推荐 2 核 2 GB 及以上。
- **操作系统**：Ubuntu 22.04 / 24.04 LTS（GCC >= 11，原生支持 C++20）或 Debian 12。
- **云厂商安全组 / 防火墙**：放行入站 `TCP 80`（HTTP 验证与跳转）与 `TCP 443`（HTTPS），**切勿**对公网暴露 `8788` 后端内部端口。
- **网络连通性说明**：请确认云主机出站可访问 `https://jxgl.hainanu.edu.cn/jsxsd/` 与 `https://ugs.hainanu.edu.cn/`（实测公网直连可达）。

### 2. 上传项目代码并安装系统依赖
在本地或通过 Git 将本项目上传至云服务器 `/opt/hnutong`：

```bash
# 在云服务器上创建目录（上传代码后执行）
sudo mkdir -p /opt/hnutong
# 假设代码已上传至 /opt/hnutong
cd /opt/hnutong

# 一键安装 GCC(C++20)、CMake、Ninja、OpenSSL、libcurl、SQLite3、Nginx、Certbot、Node.js
chmod +x deploy/scripts/*.sh
sudo bash deploy/scripts/install_deps_ubuntu.sh
```

> **注意（Linux 换行符兼容）**：若您从 Windows 直接通过 SCP 上传脚本，请先执行一次 `sed -i 's/\r$//' deploy/scripts/*.sh` 去除 CRLF 回车符。

---

## 三、编译安装与数据库初始化

### 1. 创建非特权专用运行账户
严禁使用 `root` 直接运行 Web 业务进程：

```bash
sudo useradd --system --no-create-home --shell /usr/sbin/nologin hnutong || true
sudo mkdir -p /var/log/hnutong
sudo chown -R hnutong:hnutong /var/log/hnutong
```

### 2. 一键编译 C++20 后端与 Vue 3 前端生产包
```bash
cd /opt/hnutong
bash deploy/scripts/build_release.sh
```
该脚本会自动依次完成：
1. 使用 `CMake + Ninja` 以 `-O3` (`Release`) 编译 `/opt/hnutong/backend/build/hnutong_server`；
2. 运行 `/opt/hnutong/backend/build/hnutong_tests`，现场校验当前服务器架构下的 AES-256-GCM、强智 `encodeInp`、多域 Cookie 隔离、海大 HTML 解析器与 `.ics` 生成器；
3. 构建前端静态包至 `/opt/hnutong/frontend/dist`。

### 3. 初始化数据库与海南大学专属种子数据
```bash
cd /opt/hnutong
bash deploy/scripts/init_and_backup_db.sh init
```
此命令会执行 `backend/sql/schema.sql` 建表脚本和 `backend/config/hnu_seed.sql`（写入海大 11 节作息预设、五校区与十七书院字典、2026–2027 校历、海大指南词条及服务矩阵）。

---

## 四、生产环境参数配置

编辑 `/opt/hnutong/backend/config/config.json`：

```json
{
  "server": {
    "host": "127.0.0.1",
    "port": 8788,
    "threads": 8,
    "static_dir": "../frontend/dist",
    "serve_static": false,
    "cors_allowed_origin": "",
    "trust_proxy_headers": true
  },
  "security": {
    "master_key_file": "/opt/hnutong/backend/data/master.key",
    "session_ttl_days": 30,
    "admin_stats_key": "请替换为一段32位以上高强度随机管理密钥",
    "rate_limit_login_per_min": 10,
    "rate_limit_api_per_min": 180
  },
  "database": {
    "sqlite_path": "/opt/hnutong/backend/data/hnutong.db",
    "schema_sql_path": "/opt/hnutong/backend/sql/schema.sql",
    "seed_sql_path": "/opt/hnutong/backend/config/hnu_seed.sql"
  },
  "logging": {
    "log_file": "/var/log/hnutong/hnutong.log",
    "max_bytes": 10485760,
    "max_files": 5,
    "level": "info"
  },
  "hnu": {
    "enable_demo_account": false,
    "jwxt_base_url": "https://jxgl.hainanu.edu.cn",
    "cas_login_url": "https://authserver.hainanu.edu.cn/authserver/login",
    "ehall_base_url": "https://ehall.hainanu.edu.cn",
    "allowed_ticket_domains": ["hainanu.edu.cn"],
    "allowed_notice_domains": ["hainanu.edu.cn"],
    "notice_ttl_seconds": 1800,
    "polite_delay_ms": 800
  }
}
```

设置严格的文件权限，确保主密钥 `master.key` 与 SQLite 数据库仅 `hnutong` 服务账户可读：
```bash
sudo chown -R hnutong:hnutong /opt/hnutong/backend/data /opt/hnutong/backend/config
sudo chmod 700 /opt/hnutong/backend/data
sudo chmod 600 /opt/hnutong/backend/config/config.json
```

---

## 五、配置 Systemd 进程守护与开机自启

```bash
sudo cp /opt/hnutong/deploy/systemd/hnutong.service /etc/systemd/system/hnutong.service
sudo systemctl daemon-reload
sudo systemctl enable --now hnutong

# 检查运行状态与本地健康探针
sudo systemctl status hnutong
curl -s http://127.0.0.1:8788/api/v1/health
```
期望输出：`{"ok":true,"school":"海南大学","serverTime":...,"service":"HnuTong","version":"1.0.0"}`。

---

## 六、自有域名绑定、Nginx 反向代理与 HTTPS 配置

### 1. 域名 DNS 解析
前往您的域名服务商控制台（阿里云 / 腾讯云 DNSPod / Cloudflare），添加一条 `A` 记录：
- **主机记录**：例如 `hnu`（对应 `hnu.yourdomain.com`）或 `@`（根域名）
- **记录值**：您的 Linux 云服务器公网 IPv4 地址
- **注意**：若使用中国大陆境内云服务器，域名须先完成 ICP 备案方可正常响应 80/443 端口。

### 2. 签发免费 Let's Encrypt HTTPS 证书并启用 Nginx 配置
假设您的自有域名为 `hnu.yourdomain.com`：

```bash
# 1. 创建 ACME 验证目录
sudo mkdir -p /var/www/certbot

# 2. 先使用 Certbot standalone 或 nginx 插件签发初始证书（首次可先用 certbot certonly --nginx）
sudo certbot certonly --nginx -d hnu.yourdomain.com --agree-tos -m admin@yourdomain.com --no-eff-email

# 3. 拷贝并替换 Nginx 站点配置中的示例域名
sudo cp /opt/hnutong/deploy/nginx/hnutong.conf /etc/nginx/sites-available/hnutong.conf
sudo sed -i "s/hnu.example.com/hnu.yourdomain.com/g" /etc/nginx/sites-available/hnutong.conf

# 4. 启用站点并校验 Nginx 语法
sudo ln -sf /etc/nginx/sites-available/hnutong.conf /etc/nginx/sites-enabled/hnutong.conf
sudo rm -f /etc/nginx/sites-enabled/default
sudo nginx -t
sudo systemctl reload nginx

# 5. 验证 Certbot 自动续期定时任务
sudo systemctl status certbot.timer
sudo certbot renew --dry-run
```

> **如果使用您自购/云平台签发的 SSL 证书**：只需将证书 `fullchain.pem` 与私钥 `privkey.pem` 放入 `/etc/nginx/ssl/`，并修改 `/etc/nginx/sites-available/hnutong.conf` 中的 `ssl_certificate` 与 `ssl_certificate_key` 路径后执行 `sudo nginx -t && sudo systemctl reload nginx`。

---

## 七、日志管理、数据备份与日常运维

### 1. 日志查看与双层自动轮转
C++20 服务端内置了按文件大小自动滚动（默认单文件 `10MB`，保留最近 `5` 个切片 `hnutong.log.1` ~ `hnutong.log.5`）及 **PII 敏感信息自动脱敏**（所有学号、密码、Token、Cookie、邮箱与完整 IP 在写入磁盘前已被正则擦除）。

```bash
# 查看应用脱敏业务日志
sudo tail -f /var/log/hnutong/hnutong.log

# 查看 Systemd 守护进程标准输出
sudo journalctl -u hnutong -f

# 查看管理端匿名统计面板（需传入 config.json 中配置的 admin_stats_key）
curl -H "x-hnu-key: 您的admin_stats_key" https://hnu.yourdomain.com/api/v1/admin/stats
```

### 2. 配置每日凌晨 SQLite WAL 在线无损备份
切勿直接 `cp` 正在高频写入的数据库文件，应使用内置 `init_and_backup_db.sh backup`（调用 `sqlite3 .backup` 在线一致性快照）：

```bash
# 使用非特权服务账户 hnutong 配置定时任务：每天凌晨 03:30 在线热备并保留最近 14 天
# （请勿以 root 直接操作 WAL 数据库，以免生成 root 属主的 -wal/-shm 导致业务写入失败）
(sudo crontab -u hnutong -l 2>/dev/null; echo "30 3 * * * /bin/bash /opt/hnutong/deploy/scripts/init_and_backup_db.sh backup >> /var/log/hnutong/backup.log 2>&1") | sudo crontab -u hnutong -
```

### 3. 后续代码平滑升级流程
```bash
cd /opt/hnutong
git pull                           # 或上传新版源码
bash deploy/scripts/build_release.sh
sudo systemctl restart hnutong     # C++ 进程毫秒级重启，用户会话与加密凭据无损保留
```
