-- ============================================================================
-- 海大通 (HnuTong) · 海南大学校园一体化服务系统
-- 数据库建表与索引脚本 (SQLite 3 / WAL 高并发模式兼容)
-- ============================================================================

PRAGMA foreign_keys = ON;
PRAGMA journal_mode = WAL;
PRAGMA synchronous = NORMAL;

-- 1. 平台用户主表（多租户核心隔离主键）
CREATE TABLE IF NOT EXISTS users (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    student_id      TEXT    NOT NULL UNIQUE,               -- 海南大学学号 / 工号
    name            TEXT    NOT NULL DEFAULT '',           -- 姓名
    gender          TEXT    NOT NULL DEFAULT '',           -- 性别
    college         TEXT    NOT NULL DEFAULT '',           -- 所属学院（如：计算机科学与技术学院）
    academy         TEXT    NOT NULL DEFAULT '',           -- 所属书院（海大特色：如南海书院、椰风书院、中和书院）
    major           TEXT    NOT NULL DEFAULT '',           -- 专业名称
    direction       TEXT    NOT NULL DEFAULT '',           -- 专业方向
    class_name      TEXT    NOT NULL DEFAULT '',           -- 行政班级
    campus          TEXT    NOT NULL DEFAULT '海甸校区',    -- 所在校区（海甸校区/儋州校区/观澜湖校区/崖州湾科教园/城西教学点）
    grade           TEXT    NOT NULL DEFAULT '',           -- 年级（如 2024）
    edu_level       TEXT    NOT NULL DEFAULT '本科',        -- 学历层次
    edu_duration    TEXT    NOT NULL DEFAULT '4',          -- 学制
    status          TEXT    NOT NULL DEFAULT '在籍',        -- 学籍状态
    enrolled_at     TEXT    NOT NULL DEFAULT '',           -- 入校日期
    graduate_at     TEXT    NOT NULL DEFAULT '',           -- 预计毕业日期
    ics_token       TEXT    NOT NULL UNIQUE,               -- 动态 .ics 日历免登订阅令牌（支持重置吊销）
    role            TEXT    NOT NULL DEFAULT 'student',    -- 角色：student / admin
    created_at      INTEGER NOT NULL,
    updated_at      INTEGER NOT NULL,
    last_login_at   INTEGER NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_users_ics_token ON users(ics_token);

-- 2. 浏览器会话表（支持多设备同时在线与单点注销，仅存 Token 的 SHA-256 摘要）
CREATE TABLE IF NOT EXISTS sessions (
    token_hash      TEXT    PRIMARY KEY,                   -- SHA-256(Bearer Token)
    user_id         INTEGER NOT NULL,
    device_label    TEXT    NOT NULL DEFAULT '',           -- 脱敏 UA 设备摘要
    ip_masked       TEXT    NOT NULL DEFAULT '',           -- 脱敏 IP（如 10.12.*.*）
    expires_at      INTEGER NOT NULL,
    created_at      INTEGER NOT NULL,
    last_seen_at    INTEGER NOT NULL,
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_sessions_user_id ON sessions(user_id);
CREATE INDEX IF NOT EXISTS idx_sessions_expires_at ON sessions(expires_at);

-- 3. 上游系统凭据保险箱（OpenSSL / CNG AES-256-GCM 认证加密，绝不明文落盘）
-- provider 枚举：
--   jwxt       : 海南大学强智教务 / 统一身份认证
--   portal     : 海南大学独立门户 / 网上办事大厅 (ehall + 验证码会话)
--   chaoxing   : 超星学习通
--   mooc       : 中国大学 MOOC (icourse163 Cookie 凭据)
--   yuketang   : 雨课堂（预留接入槽）
--   zhihuishu  : 智慧树（预留接入槽）
--   zhidao     : 知到（预留接入槽）
CREATE TABLE IF NOT EXISTS credential_vault (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id         INTEGER NOT NULL,
    provider        TEXT    NOT NULL,
    account_masked  TEXT    NOT NULL DEFAULT '',           -- 界面展示用脱敏账号（如 2024****28）
    cipher_b64      TEXT    NOT NULL,                      -- AES-256-GCM 密文 Base64（内含账号、密码、CookieJar JSON）
    iv_b64          TEXT    NOT NULL,                      -- 96-bit 随机 IV Base64
    tag_b64         TEXT    NOT NULL,                      -- 128-bit GCM Auth Tag Base64
    state           TEXT    NOT NULL DEFAULT 'ok',         -- ok / expired / broken
    last_error      TEXT    NOT NULL DEFAULT '',
    updated_at      INTEGER NOT NULL,
    UNIQUE(user_id, provider),
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_vault_user_provider ON credential_vault(user_id, provider);

-- 4. 教务与学习平台数据快照表（实现打开秒显，且严格遵守「没读到 ≠ 覆盖清空」红线）
-- kind 枚举：
--   timetable  : 课表（按 semester_id 区分，'current' 为本学期，其余如 '2025-2026-2'）
--   grades     : 历年成绩与 GPA 汇总
--   exams      : 考试安排（按 semester_id 区分）
--   semesters  : 可选学年学期列表
--   plan       : 培养方案与毕业学分要求
--   jw_todo    : 教务选课/评教/缓考重修开放状态
--   cx_todo    : 超星学习通扫描结果（含作业、考试、随堂练习、签到、结课记忆 rest）
--   cx_notices : 超星通知中心收件箱缓存
--   mooc_todo  : 中国大学 MOOC 扫描结果
--   portal_card: 门户一卡通/海大e卡快照与消费流水
--   portal_apps: 门户可用办事应用列表快照
CREATE TABLE IF NOT EXISTS academic_snapshots (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id         INTEGER NOT NULL,
    kind            TEXT    NOT NULL,
    semester_id     TEXT    NOT NULL DEFAULT 'current',
    payload_json    TEXT    NOT NULL,
    synced_at       INTEGER NOT NULL,
    UNIQUE(user_id, kind, semester_id),
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_snapshots_lookup ON academic_snapshots(user_id, kind, semester_id);

-- 5. 用户跨端漫游偏好设置与课表个性化表（换手机/换电脑登录自动同步）
CREATE TABLE IF NOT EXISTS user_preferences (
    user_id             INTEGER PRIMARY KEY,
    campus_preset       TEXT    NOT NULL DEFAULT 'haidian',    -- haidian(海甸/儋州/观澜湖) / yazhou(崖州湾) / custom
    term_start_date     TEXT    NOT NULL DEFAULT '2026-08-31', -- 当前学期第 1 周周一
    total_weeks         INTEGER NOT NULL DEFAULT 20,           -- 学期总周数
    bell_schedule_json  TEXT    NOT NULL DEFAULT '[]',         -- 自定义 11 节作息时间表 JSON（空数组表示走校区默认）
    hidden_courses_json TEXT    NOT NULL DEFAULT '[]',         -- 隐藏整门课代码列表 ["B101", ...]
    hidden_slots_json   TEXT    NOT NULL DEFAULT '[]',         -- 隐藏单节课列表 [{"code","day","period","week"}, ...]
    custom_courses_json TEXT    NOT NULL DEFAULT '[]',         -- 用户自定义添加的课程列表
    course_colors_json  TEXT    NOT NULL DEFAULT '{}',         -- 课程自定义色相映射 {"高等数学": "#3b82f6"}
    done_marks_json     TEXT    NOT NULL DEFAULT '{}',         -- 手动标记「我交过了」的作业 {"课程|标题": deadlineSec}
    seen_items_json     TEXT    NOT NULL DEFAULT '{}',         -- 首次发现时间戳记录
    card_low_threshold  REAL    NOT NULL DEFAULT 20.0,         -- 一卡通低余额提醒阈值（元）
    elec_low_threshold  REAL    NOT NULL DEFAULT 15.0,         -- 宿舍低电量提醒阈值（度 kWh）
    remind_cfg_json     TEXT    NOT NULL DEFAULT '{"courseBeforeMin":15,"workBeforeHours":6,"enabled":false}',
    telemetry_opt_in    INTEGER NOT NULL DEFAULT 1,            -- 是否允许匿名错误统计（1=开，0=关）
    updated_at          INTEGER NOT NULL,
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
);

-- 6. 海大宿舍电量速记与购电/用电台账表（支持书院公寓房间号管理）
CREATE TABLE IF NOT EXISTS dorm_electricity (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id         INTEGER NOT NULL UNIQUE,
    campus          TEXT    NOT NULL DEFAULT '海甸校区',
    academy_or_zone TEXT    NOT NULL DEFAULT '南海书院',
    building_name   TEXT    NOT NULL DEFAULT '',           -- 如：12号学生公寓
    room_name       TEXT    NOT NULL DEFAULT '',           -- 如：408室
    meter_account   TEXT    NOT NULL DEFAULT '',           -- 电控户号/房间编号（可选）
    balance_kwh     REAL,                                  -- 当前剩余电量（度 kWh），NULL 表示尚未录入/未读到
    daily_burn_kwh  REAL    NOT NULL DEFAULT 4.5,          -- 估算日均耗电量（海南空调高频场景默认 4.5 度/天）
    recorded_at     INTEGER NOT NULL DEFAULT 0,
    updated_at      INTEGER NOT NULL,
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS dorm_elec_logs (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id         INTEGER NOT NULL,
    kind            TEXT    NOT NULL,                      -- 'pay' (购电充值, 元/度) / 'check' (电量抄表快照, 度)
    amount_val      REAL    NOT NULL,                      -- 度数或充值金额
    balance_after   REAL,                                  -- 变动后剩余度数
    note            TEXT    NOT NULL DEFAULT '',
    logged_at       INTEGER NOT NULL,
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_elec_logs_user ON dorm_elec_logs(user_id, logged_at DESC);

-- 7. 海南大学公开通知公告缓存表（多院系聚合 + 台风/出行/办事分级 + 正文摘要懒加载）
CREATE TABLE IF NOT EXISTS campus_notices (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    url_hash        TEXT    NOT NULL UNIQUE,               -- SHA-256(url)
    title           TEXT    NOT NULL,
    url             TEXT    NOT NULL,
    source_name     TEXT    NOT NULL,                      -- 如：海大主站 / 本科生院 / 信息化中心 / 后勤保障部
    dept            TEXT    NOT NULL,
    publish_date    TEXT    NOT NULL,                      -- YYYY-MM-DD
    tier            TEXT    NOT NULL,                      -- impact(台风/停水电/断网) / action(要办事) / activity(活动) / other
    tags_json       TEXT    NOT NULL DEFAULT '[]',         -- 如 ["台风预警", "影响出行"]
    when_text       TEXT    NOT NULL DEFAULT '',           -- 正文提取的活动/截止时间
    where_text      TEXT    NOT NULL DEFAULT '',           -- 正文提取的地点
    summary         TEXT    NOT NULL DEFAULT '',           -- 正文前 500 字纯文本摘要
    attachments_json TEXT   NOT NULL DEFAULT '[]',         -- 附件与红头 PDF 链接 JSON
    detail_fetched  INTEGER NOT NULL DEFAULT 0,
    fetched_at      INTEGER NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_notices_date ON campus_notices(publish_date DESC, id DESC);
CREATE INDEX IF NOT EXISTS idx_notices_tier ON campus_notices(tier, publish_date DESC);

-- 8. 学校动态元数据配置表（校历、海大指南 WIKI、校园服务导航、关于我们，热更新免重编译）
CREATE TABLE IF NOT EXISTS school_meta (
    meta_key        TEXT    PRIMARY KEY,
    value_json      TEXT    NOT NULL,
    updated_at      INTEGER NOT NULL
);

-- 9. 用户问题反馈与脱敏日志上报记录表（私有化收归本机云端，不发任何第三方）
CREATE TABLE IF NOT EXISTS feedback_reports (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id         INTEGER,                               -- 可为空（未登录用户也可反馈）
    app_version     TEXT    NOT NULL DEFAULT '',
    client_os       TEXT    NOT NULL DEFAULT '',
    description     TEXT    NOT NULL,                      -- 服务端二次脱敏后的问题描述
    scrubbed_log    TEXT    NOT NULL DEFAULT '',           -- 服务端二次脱敏后的运行日志
    ip_masked       TEXT    NOT NULL DEFAULT '',
    created_at      INTEGER NOT NULL
);

-- 10. 匿名系统运行与错误遥测表（支撑管理员数据台）
CREATE TABLE IF NOT EXISTS telemetry_events (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    day_bj          TEXT    NOT NULL,                      -- 北京时间 YYYY-MM-DD
    event_kind      TEXT    NOT NULL,                      -- dau_ping / jw_sync_err / cx_scan_err / portal_err / js_err
    detail_code     TEXT    NOT NULL DEFAULT '',
    message_scrub   TEXT    NOT NULL DEFAULT '',
    page_name       TEXT    NOT NULL DEFAULT '',
    anon_day_id     TEXT    NOT NULL DEFAULT '',           -- 每日轮换匿名随机串（不跨天追踪）
    created_at      INTEGER NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_telemetry_day ON telemetry_events(day_bj, event_kind);
