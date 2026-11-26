# 参与贡献指南 (Contributing Guide)

感谢你对 **HNU-Helper（海大通）** 开源项目的关注与支持！本项目坚持**完全开源、纯公益非商业**原则，欢迎所有海南大学同学及开源开发者共同完善。

---

## 一、开源与非商业约定

在提交任何 Issue、代码或文档贡献（Pull Request）前，请确认你已阅读并同意本项目的 [LICENSE](./LICENSE)（PolyForm Noncommercial License 1.0.0）与 [LICENSE-ZH.md](./LICENSE-ZH.md)：

- 你的贡献将以相同的非商业开源协议随本项目公开发布。
- 本项目及其所有衍生贡献均**严禁用于任何商业用途或盈利行为**。

---

## 二、如何参与贡献

### 1. 反馈问题与提出建议
- 在提交新 Issue 前，请先搜索是否已有相同问题。
- 请使用仓库提供的 **Bug 反馈** 或 **功能建议** 模板填写清晰的环境信息、复现步骤与脱敏后的日志（**切勿在 Issue 中粘贴真实学号、密码、Cookie 或 Session Token**）。

### 2. 提交代码（Pull Request 流程）
1. Fork 本仓库并创建你的特性分支：
   ```bash
   git checkout -b feat/your-feature-name
   # 或修复 Bug：
   git checkout -b fix/your-bug-description
   ```
2. 完成修改后，请务必在本地完成编译与测试验证：
   ```bash
   # 1) C++20 后端编译与单元测试
   cd backend
   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
   cmake --build build -j 8
   ./build/hnutong_tests

   # 2) Vue 3 + TypeScript 前端类型检查与构建
   cd ../frontend
   npm install
   npx vue-tsc --noEmit
   npm run build
   ```
3. 提交清晰规范的 Commit 信息（推荐 Conventional Commits，例如 `feat: ...`、`fix: ...`、`docs: ...`、`refactor: ...`）。
4. 推送分支并提交 Pull Request，按 PR 模板填写改动说明与验证结果。

---

## 三、代码与安全红线

请确保所有提交严格遵守以下工程红线：

1. **绝不提交敏感文件**：禁止提交 `backend/config/config.json`、真实数据库文件（`*.db`）、主密钥 `master.key`、真实学生 Cookie/Token 或抓包隐私数据。
2. **保护凭据隔离语义**：
   - 凭据加密必须使用 AES-256-GCM + `user_id:provider` AAD 防串号校验。
   - 智慧门户算术验证码输错时，绝不可误删用户已保存的门户凭据。
   - 日志与反馈输出必须经过脱敏（PII Scrubber），不得明文打印密码、Token、完整学号或完整 IP。
3. **SSRF 与主机隔离**：所有外部抓取/单点换票 URL 必须通过域名白名单校验；多域 `CookieJar` 必须遵守 RFC 6265 `hostOnly` 规则。
4. **C++20 与 TypeScript 规范**：
   - 后端遵循现代 C++20（无裸内存泄漏、无跨平台不可编译的私有扩展，需兼顾 Linux GCC/OpenSSL/libcurl 与 Windows MinGW/CNG/WinHTTP）。
   - 前端遵循 Vue 3 `<script setup lang="ts">` 与 Apple HIG 浅/深双色视觉规范。
