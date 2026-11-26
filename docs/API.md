# 海大通 (HnuTong) · 前后端 RESTful API 接口文档

- **协议规范**：HTTP/1.1 或 HTTP/2，请求体与响应体统一采用 `application/json; charset=utf-8`（日历订阅接口返回 `text/calendar`）。
- **版本前缀**：`/api/v1`
- **认证机制**：
  - 登录成功后，服务端同时下发 `HttpOnly; SameSite=Lax` Cookie (`hnu_session=<token>`) 与响应字段 `sessionToken`。
  - 前端后续请求支持自动携带 Cookie，或显式携带请求头 `Authorization: Bearer <sessionToken>`（亦兼容 `X-Session-Token`）。
- **统一错误包裹**：
  ```json
  {
    "ok": false,
    "error": "面向用户的清晰中文原因说明",
    "kind": "auth | captcha | expired | net | server",
    "needBind": false,
    "needCaptcha": false
  }
  ```

---

## 1. 公共基础与元数据接口（免登录）

### 1.1 健康探测 `GET /api/v1/health`
- **响应示例**：
  ```json
  { "ok": true, "service": "HnuTong", "school": "海南大学", "version": "1.0.0", "serverTime": 1795532400 }
  ```

### 1.2 冷启动公共元数据 `GET /api/v1/meta/bootstrap`
一次性下发无需登录即可浏览的海南大学静态与配置字典：
- `demoEnabled`: 是否开启沙箱体验账号
- `forgotPwdUrls`: 经过真实验证的找回密码官方地址（`jwxt` / `portal` 指向 `https://authserver.hainanu.edu.cn/retrieve-password/retrievePassword/index.html`，`chaoxing` 指向 `https://passport2.chaoxing.com/pwd/getpwd`）
- `bellPresets`: 海南大学 11 节作息预设（`haidian` 07:40 起、`yazhou` 08:00 起）
- `campusesAndAcademies`: 海大五校区（海甸、儋州、观澜湖、崖州湾、城西）与全部 17 个书院分组列表
- `calendarTerms`: 海南大学 2026–2027 学年两学期校历节点
- `wiki`: 海大指南分组词条
- `quickLinks`: 校园四宫格高频服务
- `externalPlatforms`: 外部学习平台目录（`chaoxing`、`mooc` 以及预留槽位 `yuketang`、`zhihuishu`、`zhidao`）

### 1.3 海南大学公开通知列表 `GET /api/v1/notice`
- **Query 参数**：
  - `limit`: 条数上限（默认 `60`，最大 `200`）
  - `tier`: 可选筛选分级 `impact`（台风停课/停水电）| `action`（要办事）| `activity`（讲座活动）| `other`
  - `force`: `1` 表示忽略 TTL 立即回源抓取海大本科生院与主站 6 大栏目
- **响应字段**：`{ ok, at, cached, sources, failed: [], items: [{ title, url, source, dept, date, tier, tags }] }`

### 1.4 通知正文摘要提取 `GET /api/v1/notice/detail?u=<encoded_url>`
- **安全机制**：严格校验目标域名白名单（仅限 `*.hainanu.edu.cn`），阻断 SSRF。
- **响应字段**：`{ ok, title, publish, dept, when, where, summary, url }`

### 1.5 提交问题反馈 `POST /api/v1/feedback`
- **请求体**：`{ "desc": "问题描述", "log": "客户端预脱敏日志" }`（服务端落库前会执行二次正则强脱敏）。

### 1.6 匿名日活与故障计数 `POST /api/v1/telemetry`
- **请求体**：`{ "k": "dau_ping", "dayId": "2026-11-24_xxx" }`（不含学号与设备指纹）。

### 1.7 管理端运营统计 `GET /api/v1/admin/stats`
- **请求头**：`x-hnu-key: <config.security.admin_stats_key>`（常量时间比对防时序侧信道）。

---

## 2. 海南大学强智教务认证与学业接口

### 2.1 会话自检、全量快照恢复与登出
- `GET /api/v1/auth/session`：
  - 未登录返回 `{"ok": true, "authenticated": false}`。
  - 已登录返回完整用户业务包（`profile`, `icsToken`, `credState`, `credBriefs`, `preferences`, `dormElec`, `timetable`, `grades`, `exams`, `semesters`, `credits`, `cxTodo`, `moocTodo`, `portalCard`, `portalApps`）。
- `POST /api/v1/auth/logout`：仅吊销当前 `hnu_session`（保留已绑定的加密凭据与本地快照）。
- `GET /api/v1/sync/progress?token=<op_token>`：轮询登录/同步/通知抓取/成绩拉取等长耗时操作的实时进度 `{ ok, active, percent, step, phase, done, total, updatedAt }`。

### 2.2 拉取强智教务验证码 `GET /api/v1/jw/captcha`
- 自动访问 `https://jxgl.hainanu.edu.cn/jsxsd/verifycode.servlet` 建立前置 `JSESSIONID` 并返回 Base64 Data URI：
  `{ "ok": true, "captchaId": "...", "img": "data:image/jpeg;base64,...", "timeout": 900, "mode": "live" }`

### 2.3 登录绑定教务并同步全量数据 `POST /api/v1/jw/login`
- **请求体**：
  ```json
  {
    "username": "2024300128",
    "password": "统一身份认证密码（过期重连时可留空，服务端自动取用 AES-256-GCM 已存密文）",
    "captchaId": "...",
    "captchaCode": "..."
  }
  ```
- **协议细节**：自动执行海南大学强智 `LoginToXkLdap`（`encoded = Base64(xh) + "%%%" + Base64(pwd)`）或移动接口探测，同步 11 节课表、总评成绩、考场安排、学期列表与培养方案，且**遵循快照保护红线：若某子页面偶发超时报错，绝不用空结果覆盖上一次有效快照**。

### 2.4 一键增量同步教务 `POST /api/v1/jw/sync`
- 优先复用加密保存的教务 Cookie，若上游会话失效则自动尝试静默重连；若需人工输入图形验证码则返回 `{"ok": false, "needCaptcha": true, "kind": "expired"}`。

### 2.5 切换查询指定学期课表 / 成绩 / 考场
- `POST /api/v1/jw/timetable`：请求体 `{ "semesterId": "2025-2026-2" }`（严格校验 `^20\d{2}-20\d{2}-[123]$`）。
- `POST /api/v1/jw/grades`：请求体 `{ "semesterId": "" }`（空字符串表示查全部学期，否则按 `YYYY-YYYY-N` 格式校验；可选 `progressToken`）。
- `POST /api/v1/jw/exams`：请求体 `{ "semesterId": "2026-2027-1" }`。

### 2.6 探测选课 / 评教 / 重修通道状态 `GET /api/v1/jw/todo`
- **响应**：`{ "ok": true, "items": [{ "key": "elect|evaluate|retake", "name": "...", "state": "open|closed", "text": "...", "detail": "..." }] }`。

### 2.7 空教室筛选项与空闲教室查询
- `GET /api/v1/jw/room-options` -> 返回海大五校区、教学楼与教室类型列表。
- `POST /api/v1/jw/free-rooms` -> 请求体 `{ "date": "2026-11-25", "campusId": "1", "buildingId": "101", "periodBegin": 1, "periodEnd": 4 }`（节次范围 `1 ~ 11`）。

### 2.8 解除教务绑定并销毁会话 `POST /api/v1/jw/unbind`
- 删除教务凭据、清空快照缓存并吊销当前 `hnu_session`。

---

## 3. 独立智慧门户接口（保留二次算术验证码安全机制）

### 3.1 查询门户连接状态 `GET /api/v1/portal/status`
- **响应**：`{ "ok": true, "state": "none|ok|expired", "user": "2024****28", "saved": true, "savedUser": "2024****28", "card": {...}, "apps": [...] }`

### 3.2 生成一次性二次算术验证码 `GET /api/v1/portal/captcha`
- **响应**：`{ "ok": true, "captchaId": "...", "img": "data:image/svg+xml;base64,...", "timeout": 600 }`

### 3.3 智慧门户认证登录 `POST /api/v1/portal/login`
- **请求体**：`{ "user": "2024300128", "pass": "留空则使用已存密码", "captchaId": "...", "code": "21" }`
- **防误删与防锁号红线**：
  - 每次提交无论成功失败均当场销毁该 `captchaId` 防止重放；
  - 若使用已保存密码重连时算错算术题（`CODEFALSE`），**绝不清除**已保存的门户密码；仅当上游明确返回密码错误（`PASSERROR` / `USERLOCK`）时才清除旧密码并返回 `"pwdStale": true`。

### 3.4 校园卡余额流水与网办换票
- `GET /api/v1/portal/card` -> `{ ok, balance, cardStatus, cardNo, records: [{ time, place, amount, balance }] }`
- `GET /api/v1/portal/apps` -> 返回 19 项海大网上办事大厅应用入口
- `GET /api/v1/portal/ticket?service=<url>` -> 校验 `*.hainanu.edu.cn` 白名单后换取免登跳转 `{ ok, ticket, url }`
- `POST /api/v1/portal/logout` -> 断开智慧门户并清理一卡通缓存

---

## 4. 海南大学宿舍电量管理接口

- `GET /api/v1/elec/state` -> 获取当前绑定的校区、书院、楼栋房间、表显度数、按日均耗电折算的实时剩余度数与预计可用天数，以及最近 20 条购电/核对台账。
- `POST /api/v1/elec/update` -> 请求体 `{ "campus": "海甸校区", "academy": "南海书院", "building": "3号楼", "room": "408", "balanceKwh": 46.5, "dailyBurnKwh": 9.0 }`。
- `POST /api/v1/elec/log` -> 请求体 `{ "kind": "pay|check", "amount": 50, "balanceAfter": 96.5, "note": "4人平摊电费" }`。

---

## 5. 外部学习平台接口（学习通 + MOOC + 预留雨课堂/智慧树/知到）

### 5.1 超星学习通（多用户隔离后台异步扫描）
- `GET /api/v1/cx/status` -> 查询绑定状态与后台扫描进度 `scan: { state, at, i, n, course, phase, err, kind }`
- `POST /api/v1/cx/login` -> `{ "user": "...", "pass": "..." }`（AES-128-CBC 官方参数只读登录）
- `POST /api/v1/cx/scan` -> `{ "force": true }` 启动后台线程按本学期海大课表白名单扫描作业/测验/考试（内置 HTTP 202 反爬熔断保护）
- `GET /api/v1/cx/todo` -> 轮询扫描进度并获取最新任务清单
- `POST /api/v1/cx/live` -> `{ "names": ["操作系统原理", ...] }` 随堂练习与签到快查
- `GET /api/v1/cx/notices` -> 获取学习通通知收件箱
- `POST /api/v1/cx/logout` -> 断开学习通连接

### 5.2 中国大学 MOOC（Cookie 凭据安全托管）
- `GET /api/v1/mooc/status` -> 查询绑定状态与缓存结果
- `POST /api/v1/mooc/bind` -> `{ "cookie": "NTESSTUDYSI=...; ..." }` 验证并加密绑定，立即执行首次章节测验与互评作业扫描
- `POST /api/v1/mooc/scan` -> 重新扫描在修精品课任务
- `POST /api/v1/mooc/logout` -> 断开 MOOC 绑定

### 5.3 预留学习平台标准槽位（`yuketang` 雨课堂 / `zhihuishu` 智慧树 / `zhidao` 知到）
- `GET /api/v1/ext/:platform/status`
- `POST /api/v1/ext/:platform/bind` -> `{ "account": "...", "secret": "...", "memo": "..." }`（AES-256-GCM 加密托管）
- `POST /api/v1/ext/:platform/logout`

---

## 6. 用户个性化偏好、档案维护与 `.ics` 系统日历订阅

- `GET /api/v1/preferences` & `POST /api/v1/preferences` -> 读写开学首周日期、11节作息预设、自定义加课、隐藏课程、自定义配色、`doneMarks` 已交标记及低额预警阈值。
- `POST /api/v1/account/profile` -> 手动更新用户的书院与校区信息（`{ "academy": "海德书院", "campus": "海甸校区" }`），后续教务同步不会覆盖已设书院。
- `GET /api/v1/calendar/ics/:token.ics` -> 免 Cookie 令牌化输出符合 RFC 5545 标准的 iCalendar 日历流（含 `Asia/Shanghai` 时区、全部排课节次、期末考场与未交作业提醒），可直接添加至 iOS / Android / 鸿蒙 / Outlook 系统日历。
- `POST /api/v1/calendar/rotate-token` -> 一键轮换 `.ics` 订阅令牌使旧链接立即失效。
- `POST /api/v1/account/clear-cache` -> 清空当前用户所有云端业务快照缓存（保留账号绑定）。
