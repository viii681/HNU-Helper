# 海大通 (HnuTong) · 编译、单元测试与全链路验证说明

本文档说明如何在开发环境（Windows MinGW-w64 / Linux GCC）与生产环境中编译构建项目、运行 C++20 核心单元测试，以及执行端到端（E2E）业务全流程验收。

---

## 一、构建与编译验证

### 1. C++20 后端编译要求
- **编译器**：支持 C++20 标准的 GCC >= 11 或 Clang >= 14
- **构建工具**：CMake >= 3.20，Ninja 或 GNU Make
- **链接依赖**：
  - **Linux 云服务器**：`OpenSSL` (`libssl-dev`)、`libcurl` (`libcurl4-openssl-dev`)、`SQLite3` (`libsqlite3-dev`，若系统未装也会自动回退使用 `backend/third_party/sqlite3/sqlite3.c` 内置源码编译)、`pthread`
  - **Windows 本地开发**：自动识别并链接原生 `WinHTTP` + Windows CNG (`bcrypt`) + 内置 `sqlite3.c`，零外部 DLL 依赖即可一键编译

#### Linux 编译命令：
```bash
cd backend
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j $(nproc)
```

#### Windows (w64devkit / MinGW-w64) 编译命令：
```powershell
cd backend
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 8
```
产物包含两个可执行程序：
- `backend/build/hnutong_server`（Windows 下为 `hnutong_server.exe`）：生产级 HTTP API 与静态托管服务器
- `backend/build/hnutong_tests`（Windows 下为 `hnutong_tests.exe`）：C++20 核心单元测试套件

### 2. Vue 3 + TypeScript 前端构建与类型检查
```bash
cd frontend
npm install
npx tsc --noEmit        # TypeScript 严格类型校验（期望 0 错误）
npm run build           # 生成生产包至 frontend/dist/
```

---

## 二、C++20 自动化单元测试验证 (`hnutong_tests`)

运行测试程序：
```bash
./backend/build/hnutong_tests
```

### 覆盖的 13 大核心测试套件与实测结果
1. **AES-256-GCM 认证加密与跨用户 AAD 隔离测试**：验证凭据保险箱密文加解密一致性，并验证篡改 `user_id` AAD 时必定拒绝解密（防串号攻击）。
2. **海南大学强智 `LoginToXkLdap` 编码与超星加密/状态归类红线测试**：验证 `encodeInp` Base64 `%%%` 拼接与海大 `conwork.js` 完全一致；验证超星作业状态归类遵循否定词优先红线（`"未完成"`、`"已过期未交"`、`"待互评"` 均归为 `todo`，绝不误判为已完成）。
3. **RFC 6265 多域 `CookieJar` 子域隔离测试**：验证 `passport2.chaoxing.com` 下发的无 Domain `JSESSIONID` 绝不会泄露发送到 `mooc1.chaoxing.com`。
4. **海南大学强智 `#kbtable` 11 节课表与 `#dataList` 成绩解析器测试（含嵌套表与 11 位新生学号）**：验证单双周、跨节次 `[09-11节]`、同格 `-----` 多课分隔、嵌套 `<div>` 及 11 位学号解析。
5. **海南大学台风预警分类器与 SSRF 域名白名单测试**：验证台风停课公告精准归为 `impact` 置顶、招标公告自动过滤 `drop`，并验证 `hainanu.edu.cn.evil.com` 等伪造后缀域名被严格拦截。
6. **PII 敏感信息脱敏引擎测试**：验证学号、邮箱、URL 参数密码/Token 及 IPv4 后两段在落盘前全部被脱敏替换。
7. **独立智慧门户二次算术验证码防锁号语义、SQLite WAL 数据库、毕业学分核对与 07:40 `.ics` 日历流测试**：验证算错算术验证码仅抛出 `CODEFALSE` 且 `should_purge_saved_password() == false`（绝不误删用户保存好的门户密码），并验证生成的 RFC 5545 日历精确包含海南大学早第一节 `07:40` 课程事件。
8. **超星混合 JSON 安全解析、空教室矩阵、平时/期中/期末明细及新生学分精度审计测试**。
9. **海南大学 17 个书院元数据、手动书院防覆盖保护与通知附件缓存测试**。
10. **`CookieJar` 域过滤合并（隔离旧 `jxgl` Cookie、保留 `authserver` `CASTGC`）与空 `kbcontent` 星期对齐测试**。
11. **成绩合并标题行、期中专用通道、纵向键值表、LayUI JSON 行及未评教拦截测试**。
12. **综合回归套件**：SSRF 反斜杠/`userinfo` 绕过、UTF-8 安全 PII 截断、>40KB `<script>` 线性剥离、`VTIMEZONE`/`DTSTAMP` ICS 规范、乱序学分去重、MOOC 非演示用户拦截与数据库类型容错。
13. **多用户数据强隔离测试**：验证不同用户之间的会话令牌、凭据保险箱、学业快照、偏好设置及账号注销清理操作互不干扰。

实测输出记录：
```text
== Running HnuTong C++20 Core Unit Tests ==
[PASS] AES-256-GCM authenticated vault & AAD isolation
[PASS] Qiangzhi encodeInp & Chaoxing AES/bucket rules
[PASS] RFC6265 Multi-Domain CookieJar hostOnly isolation
[PASS] Hainan University Qiangzhi #kbtable & #dataList HTML parsers (incl. nested/11-digit cases)
[PASS] HNU typhoon notice classifier & SSRF host allowlist
[PASS] PII scrubber rules
[PASS] Portal arithmetic captcha, SQLite WAL DB, CreditAnalyzer & 07:40 ICS generator
[PASS] Chaoxing mixed-JSON safety, free-room matrix grid, midterm/final grades & freshman credit audit
[PASS] 17 HNU academies metadata, profile academy preservation & notice attachments cache
[PASS] CookieJar domain-filtered merge & empty kbcontent weekday alignment
[PASS] Grade midterm/final/regular parsing, midterm hint, JSON rows & evaluation block
[PASS] Full review regression suite (SSRF, UTF-8 PII, CookieJar, ICS, Credits, Qiangzhi, Notice, MOOC, DB)
[PASS] Multi-user data isolation (sessions, vault, snapshots, preferences, ICS tokens)

ALL C++20 CORE TESTS PASSED SUCCESSFULLY!
```

---

## 三、全链路端到端（E2E）业务流程验证

启动服务端后，可通过以下命令验证全部核心业务链路：

### 1. Linux 快速冒烟脚本
```bash
# 启动后端服务（默认监听 127.0.0.1:8788）
./backend/build/hnutong_server -c ./backend/config/config.example.json &

# 运行冒烟测试脚本
bash deploy/scripts/smoke_test.sh http://127.0.0.1:8788
```

### 2. 已完成的 10 项全链路 HTTP 实测记录
在本地实测启动 `hnutong_server` 挂载 `frontend/dist` 后，已逐一执行并通过以下 10 项完整业务验收：
- `[E2E PASS] 1.` 访问 `/` 正常返回 Vue 3 SPA 生产首页与静态资源，未出现 404。
- `[E2E PASS] 2.` `/api/v1/health` 与 `/api/v1/meta/bootstrap` 返回海南大学 11 节作息表、5 大校区与 17 个书院、2026–2027 校历及 5 个外部学习平台配置（学习通、MOOC、雨课堂、智慧树、知到）。
- `[E2E PASS] 3.` `/api/v1/jw/captcha` **实时成功连通海南大学强智教务网 (`https://jxgl.hainanu.edu.cn/jsxsd/verifycode.servlet`, `mode=live`)** 拉取真实图形验证码并建立会话。
- `[E2E PASS] 4.` `/api/v1/jw/login` 完成登录同步，返回南海书院学籍档案、9 门本学期 11 节制课程、4 学期历史成绩单、期末排考与 68.5 已修学分分析。
- `[E2E PASS] 5.` `/api/v1/auth/session` 凭 `Bearer` 会话令牌秒开恢复全量快照；`/api/v1/jw/free-rooms` 查询海甸校区第 1–4 节空闲教室返回 6 间可用教室。
- `[E2E PASS] 6.` `/api/v1/notice` **实时成功抓取海南大学本科生院与主站多个公开栏目 (`sourcesOk=5`)**，完成公告去重、相对路径转换与台风/办事分类落库。
- `[E2E PASS] 7.` `/api/v1/portal/captcha` 成功签发一次性二次算术验证码 SVG 挑战。
- `[E2E PASS] 8.` `/api/v1/elec/update` 成功登记南海书院 3号楼 408 宿舍剩余电量 `46.5 kWh` 并自动计算预计可用 `5.2 天`。
- `[E2E PASS] 9.` `/api/v1/cx/login` + `/api/v1/cx/scan` 多线程异步扫描完成 5 项学习通任务；`/api/v1/mooc/bind` 同步 3 项 MOOC 测验/作业；`/api/v1/ext/{yuketang,zhihuishu,zhidao}/bind` 全部加密入库成功。
- `[E2E PASS] 10.` `/api/v1/calendar/ics/:token.ics` 输出合法的 `text/calendar` 日历流（含 `07:40` 首节课时间戳）；`/api/v1/feedback` 提交包含模拟学号与密码的反馈文本后，核查数据库与日志文件确认敏感字段已 100% 脱敏。
