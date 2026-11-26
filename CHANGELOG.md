# 更新日志 (Changelog)

本项目遵循 [语义化版本 (Semantic Versioning)](https://semver.org/lang/zh-CN/) 规范。

---

## [1.0.0] - 2026-11-26

### 完整开源版本发布（完全开源 · 严禁任何商业用途与盈利）

#### ✨ 核心后端（C++20 原生高性能内核）
- **零脚本运行时依赖**：基于 C++20 + `cpp-httplib` 多线程服务 + `SQLite3 (WAL)` + `OpenSSL/libcurl`（Windows 环境自动回退 `WinHTTP + CNG BCrypt`），单二进制即可运行。
- **纵深安全与多用户强隔离体系**：
  - 全凭据 **AES-256-GCM**（独立随机 96-bit IV + `user_id:provider` AAD 隔离）加密存储，会话令牌 **SHA-256** 哈希落库。
  - 严格多用户隔离：切换或退出账号时自动吊销旧会话、禁止跨账号复用 `CASTGC`、校验同步进度归属（`owner_user_id` / `owner_ip`），并清空内存与本地离线缓存。
  - RFC 6265 多域 `CookieJar` `hostOnly` 子域隔离、SSRF 域名与 URL 绕过防护、速率限制以及 UTF-8 安全双层 PII 日志强脱敏。
- **海南大学教务深度适配**：
  - 原生对接海南大学强智教务系统（`jxgl.hainanu.edu.cn`），支持图形验证码、CAS/`CASTGC` 静默无感续期、11 节制课表 `#kbtable` 与空单元格星期对齐解析、11 位新生学号与嵌套 `#xjkpTable` 学籍解析、期末考场、五校区空教室查询。
  - 支持平时/期中/期末成绩明细（`pscj_list.do`）与大一阶段执行计划学分审计（精准保留 `0.25` 劳动学分精度、剔除重复子项与留学生汉语替代课程），内置 `json_safe.hpp` 免疫异常 JSON 类型错误。
- **十七书院与校园实时聚合**：
  - 完整覆盖海甸 / 儋州 / 观澜湖 / 城西 / 三亚崖州湾 5 大校区与 17 个书院字典，支持手动校准书院信息（`/api/v1/account/profile`）。
  - 实时聚合本科生院与主站 6 大通知源，自动识别**台风预警、停课调休、停水停电**置顶高亮，自动过滤招标噪音并持久化缓存正文与红头附件。
- **多学习平台异步扫描与 `.ics` 日历流**：
  - 内置超星学习通（课表白名单异步扫描 + 否定词优先防漏判红线）与中国大学 MOOC 扫描，预留雨课堂、智慧树、知到标准接口槽位。
  - 生成标准 RFC 5545 令牌化 `.ics` 日历订阅流（含 `VTIMEZONE`、`Asia/Shanghai` 早 07:40 首节课与期末考场提醒）。

#### 🎨 前端界面（Vue 3 + TypeScript + Pinia + PWA）
- 严守 Apple HIG 浅/深双色视觉规范，提供五大主模块：**课表（Schedule）**、**学业（Study）**、**海大指南（Wiki）**、**校园服务（Campus）**、**我的（Me）**。
- 校园页新增**最近考试倒计时**、**今日课程速览**以及公网直连 / 校外免插件 **WebVPN** 分区导航。
- 新增 `SyncProgressBar.vue` 实时同步进度条、离线秒开缓存与退出/切号状态彻底重置机制。

#### 🚀 部署运维、社区治理与测试保障
- 提供完整 Linux 生产部署套件（`deploy/`）：Ubuntu/Debian 依赖脚本、一键构建脚本、Nginx HTTPS 安全响应头配置、Systemd 沙盒加固、SQLite WAL 属主保护热备脚本及 Windows 一键启动脚本 `start_local.bat`。
- 内置 **13 大 C++20 核心单元测试套件**（`hnutong_tests`）与 GitHub Actions CI 自动构建、TypeScript 类型校验与单测验证。
- 采用 **PolyForm Noncommercial License 1.0.0** 非商业开源协议，配套中文许可声明（`LICENSE-ZH.md`）、贡献指南、安全政策及 Issue/PR 模板。
