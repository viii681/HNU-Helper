# 安全政策 (Security Policy)

**HNU-Helper（海大通）** 涉及校园账号会话与个人课表数据，我们将用户隐私与系统安全视为最高优先级。

---

## 一、内置安全架构

本项目在设计上内置了多层纵深防御机制：

- **凭据保险箱加密**：所有托管凭据均采用 **AES-256-GCM**（独立随机 96-bit IV + `user_id:provider` AAD 附加认证数据）加密存储，密钥文件独立存放（推荐权限 `0600`）。
- **无明文会话落库**：登录会话令牌采用 **SHA-256** 单向哈希后写入 SQLite WAL 数据库。
- **SSRF 防护与 Cookie 隔离**：服务端强制执行目标域名白名单校验；多域 `CookieJar` 严格遵循 RFC 6265 `hostOnly` 规则，杜绝跨子域 Cookie 泄露。
- **双层 PII 强脱敏**：前后端均内置日志脱敏器，自动屏蔽学号、密码、Token、邮箱与部分 IP 信息。
- **生产加固模板**：提供 Systemd 沙盒加固配置（`deploy/systemd/hnutong.service`）与 Nginx HTTPS 反向代理安全头配置（`deploy/nginx/hnutong.conf`）。

---

## 二、生产部署必查安全项

如果你自行在云服务器进行公益部署，请务必核对以下事项：

1. 将 `backend/config/config.json` 中的 `enable_demo_account` 设为 `false`。
2. 修改默认的 `admin_stats_key` 为高强度随机密钥。
3. 严禁将 `hnutong_server` 直接裸奔暴露于公网 HTTP，必须通过 Nginx 配置 HTTPS（TLS 1.2/1.3）反向代理，并将后端监听绑定在 `127.0.0.1`。
4. 确保 `master.key` 与 SQLite 数据库目录权限为 `0600` / `0700`，且绝不纳入 Git 版本控制。

---

## 三、如何负责任地报告安全漏洞

如果你发现了潜在的安全漏洞（例如加密实现缺陷、SSRF 绕过、凭据泄露、XSS/CSRF 或鉴权问题）：

- **请勿在公开 Issue 或讨论区直接披露漏洞利用细节**。
- 请通过 GitHub 仓库的 **Security Advisories（Private vulnerability reporting）** 提交私密报告，或直接联系仓库维护者处理。
- 我们在确认问题后将第一时间修复并发布安全更新，同时在致谢名单中向报告者致谢（如你希望公开署名）。
