# HNU-Helper（海大通）`v1.0.0` 发布页文案

> 以下内容用于 GitHub 仓库的 **About（仓库简介）**、**Topics（主题标签）** 以及 **Releases (`v1.0.0`)** 发布页。

---

## 一、GitHub 仓库 About 设置

- **Description（仓库简介）**：
  > HNU-Helper（海大通）—— 海南大学校园一体化服务系统｜C++20 原生高性能后端 + Vue 3 / TypeScript 前后端分离 PWA｜完全开源·严禁任何商业用途与盈利
- **Topics（主题标签）**：
  `hainan-university`, `hainanu`, `campus`, `cpp20`, `vue3`, `typescript`, `sqlite3`, `pwa`, `open-source`, `non-commercial`

---

## 二、GitHub Release 发布页标题与正文

**Tag**：`v1.0.0`  
**Release Title**：`v1.0.0 — HNU-Helper（海大通）完全开源最新版发布`

---

### 发布页正文

```markdown
## 🎉 HNU-Helper（海大通）v1.0.0 正式开源发布

**HNU-Helper（海大通）** 是一套专为 **海南大学（Hainan University）** 学子深度定制的校园一体化服务系统，采用 **C++20 原生高性能后端 + Vue 3 / TypeScript 前后端分离 PWA 架构**，无任何 Python/Node 服务端运行时依赖，支持直接部署至 Linux 云服务器并通过 HTTPS 对外提供服务。

> ⚠️ **完全开源 · 严禁任何商业用途与盈利声明**  
> 本项目基于 **[PolyForm Noncommercial License 1.0.0](https://github.com/viii681/HNU-Helper/blob/main/LICENSE)** **完全开源**，仅供个人学习研究、技术交流与校园纯公益非营利场景免费使用。  
> **严禁用于任何形式的商业用途、收费分发、付费代部署、会员收费、广告变现或盈利行为！** 详见 [LICENSE](https://github.com/viii681/HNU-Helper/blob/main/LICENSE) 与 [中文许可声明 (LICENSE-ZH.md)](https://github.com/viii681/HNU-Helper/blob/main/LICENSE-ZH.md)。

---

### ✨ 核心亮点

- **⚡ 纯正 C++20 后端内核**：`cpp-httplib` 多线程服务 + `SQLite3 (WAL)` + `OpenSSL/libcurl`（Windows 自动切换 `WinHTTP + CNG BCrypt`），单二进制即可运行。
- **🔐 纵深安全与多用户强隔离**：
  - 全凭据 **AES-256-GCM**（独立随机 96-bit IV + `user_id:provider` AAD 隔离）加密存储，会话令牌 **SHA-256** 哈希落库
  - 多用户会话、凭据、快照、进度条与前端离线缓存严格隔离，切换/退出账号彻底清除旧用户状态
  - RFC 6265 多域 `CookieJar` 子域隔离 + SSRF 域名白名单 + UTF-8 安全双层 PII 日志强脱敏
- **🎓 深度适配海南大学五校区与十七书院生态**：
  - 原生对接海南大学强智教务网（`jxgl.hainanu.edu.cn`）：图形验证码、CAS/`CASTGC` 静默无感续期、11 节制课表、11 位新生学号与嵌套学籍解析、平时/期中/期末成绩明细（`pscj_list.do`）、`0.25` 学分精度审计与五校区空教室查询
  - 内置海甸 / 儋州 / 观澜湖 / 城西（早 07:40 起）与三亚崖州湾作息预设、5 大校区与 17 个书院身份展示及手动校准
  - 实时聚合本科生院与主站 6 大通知源，自动识别**台风预警、停课调休、停水停电**置顶高亮
  - 校园页新增最近考试倒计时、今日课程速览与公网/校外免插件 WebVPN 分区导航
- **🛡️ 独立智慧门户 + 多学习平台聚合 + 日历订阅**：
  - 独立智慧门户二次算术验证码认证（算错绝不误删已存密码），支持一卡通账单与 19 项网办单点换票
  - 内置 **超星学习通** 与 **中国大学 MOOC** 异步扫描，预留 **雨课堂、智慧树、知到** 标准扩展槽位
  - 标准 RFC 5545 `.ics` 日历订阅流（含 `Asia/Shanghai` 时区），支持 iOS / Android / 鸿蒙桌面小组件同步
- **🚀 开箱即用的生产部署与自动化验证**：
  - 完整 Linux 部署脚本与手册（Nginx HTTPS 安全响应头、Systemd 沙盒加固、SQLite WAL 属主保护热备）及 Windows 一键启动脚本 `start_local.bat`
  - 内置 **13 大 C++20 核心单元测试套件**（`hnutong_tests`）与 GitHub Actions CI 自动构建验证

---

### 📦 快速体验（5 分钟本地启动）

```bash
# 1. 编译 C++20 后端并运行单元测试
cd backend
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 8
./build/hnutong_tests

# 2. 构建 Vue 3 前端静态资源
cd ../frontend
npm install
npm run build

# 3. 启动一体化服务（访问 http://127.0.0.1:8788）
cd ../backend
cp config/config.example.json config/config.json
./build/hnutong_server -c config/config.json
```

> 线上生产部署请务必阅读 [Linux 云服务器部署手册](https://github.com/viii681/HNU-Helper/blob/main/deploy/DEPLOY_LINUX.md)，关闭体验账号（`enable_demo_account: false`）并启用 HTTPS。

---

### 📚 文档与协议索引

- [开源与非商业许可中文声明 (LICENSE-ZH.md)](https://github.com/viii681/HNU-Helper/blob/main/LICENSE-ZH.md)
- [Linux 云服务器部署手册](https://github.com/viii681/HNU-Helper/blob/main/deploy/DEPLOY_LINUX.md)
- [前后端 RESTful API 文档](https://github.com/viii681/HNU-Helper/blob/main/docs/API.md)
- [系统使用说明](https://github.com/viii681/HNU-Helper/blob/main/docs/USER_GUIDE.md)
- [编译测试与验证说明](https://github.com/viii681/HNU-Helper/blob/main/docs/TEST_VERIFICATION.md)
- [更新日志 (CHANGELOG.md)](https://github.com/viii681/HNU-Helper/blob/main/CHANGELOG.md)
```
