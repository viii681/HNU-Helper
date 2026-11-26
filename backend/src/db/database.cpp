#include "hnu/db/database.hpp"
#include "hnu/crypto/crypto.hpp"
#include "hnu/utils/json_safe.hpp"
#include "hnu/utils/logger.hpp"
#include "hnu/utils/strings_and_time.hpp"

#include <sqlite3.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace hnu::db {

nlohmann::json UserRecord::to_profile_json() const {
  return {
      {"studentId", student_id},
      {"name", name},
      {"gender", gender},
      {"dept", college},
      {"college", college},
      {"academy", academy},
      {"major", major},
      {"direction", direction},
      {"className", class_name},
      {"campus", campus},
      {"grade", grade},
      {"level", edu_level},
      {"duration", edu_duration},
      {"status", status},
      {"enrolledAt", enrolled_at},
      {"graduateAt", graduate_at},
  };
}

namespace {

struct StmtGuard {
  sqlite3_stmt* st = nullptr;
  ~StmtGuard() {
    if (st) sqlite3_finalize(st);
  }
};

std::string col_text(sqlite3_stmt* st, int idx) {
  const unsigned char* p = sqlite3_column_text(st, idx);
  return p ? reinterpret_cast<const char*>(p) : "";
}

nlohmann::json parse_json_or(const std::string& s, nlohmann::json fallback) {
  if (s.empty()) return fallback;
  auto j = nlohmann::json::parse(s, nullptr, false);
  return j.is_discarded() ? fallback : j;
}

std::string read_file_text(const std::string& path) {
  if (path.empty() || !std::filesystem::exists(path)) return "";
  std::ifstream ifs(path, std::ios::binary);
  std::ostringstream oss;
  oss << ifs.rdbuf();
  return oss.str();
}

// 内联最小建表保障：即便运行目录未带外部 sql/schema.sql 也能完整自举建库
const char* kFallbackSchemaSql = R"SQL(
PRAGMA foreign_keys = ON;
PRAGMA journal_mode = WAL;
PRAGMA synchronous = NORMAL;

CREATE TABLE IF NOT EXISTS users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    student_id TEXT NOT NULL UNIQUE,
    name TEXT NOT NULL DEFAULT '',
    gender TEXT NOT NULL DEFAULT '',
    college TEXT NOT NULL DEFAULT '',
    academy TEXT NOT NULL DEFAULT '',
    major TEXT NOT NULL DEFAULT '',
    direction TEXT NOT NULL DEFAULT '',
    class_name TEXT NOT NULL DEFAULT '',
    campus TEXT NOT NULL DEFAULT '海甸校区',
    grade TEXT NOT NULL DEFAULT '',
    edu_level TEXT NOT NULL DEFAULT '本科',
    edu_duration TEXT NOT NULL DEFAULT '4',
    status TEXT NOT NULL DEFAULT '在籍',
    enrolled_at TEXT NOT NULL DEFAULT '',
    graduate_at TEXT NOT NULL DEFAULT '',
    ics_token TEXT NOT NULL UNIQUE,
    role TEXT NOT NULL DEFAULT 'student',
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL,
    last_login_at INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS sessions (
    token_hash TEXT PRIMARY KEY,
    user_id INTEGER NOT NULL,
    device_label TEXT NOT NULL DEFAULT '',
    ip_masked TEXT NOT NULL DEFAULT '',
    expires_at INTEGER NOT NULL,
    created_at INTEGER NOT NULL,
    last_seen_at INTEGER NOT NULL,
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
);
CREATE TABLE IF NOT EXISTS credential_vault (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id INTEGER NOT NULL,
    provider TEXT NOT NULL,
    account_masked TEXT NOT NULL DEFAULT '',
    cipher_b64 TEXT NOT NULL,
    iv_b64 TEXT NOT NULL,
    tag_b64 TEXT NOT NULL,
    state TEXT NOT NULL DEFAULT 'ok',
    last_error TEXT NOT NULL DEFAULT '',
    updated_at INTEGER NOT NULL,
    UNIQUE(user_id, provider),
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
);
CREATE TABLE IF NOT EXISTS academic_snapshots (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id INTEGER NOT NULL,
    kind TEXT NOT NULL,
    semester_id TEXT NOT NULL DEFAULT 'current',
    payload_json TEXT NOT NULL,
    synced_at INTEGER NOT NULL,
    UNIQUE(user_id, kind, semester_id),
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
);
CREATE TABLE IF NOT EXISTS user_preferences (
    user_id INTEGER PRIMARY KEY,
    campus_preset TEXT NOT NULL DEFAULT 'haidian',
    term_start_date TEXT NOT NULL DEFAULT '2026-08-31',
    total_weeks INTEGER NOT NULL DEFAULT 20,
    bell_schedule_json TEXT NOT NULL DEFAULT '[]',
    hidden_courses_json TEXT NOT NULL DEFAULT '[]',
    hidden_slots_json TEXT NOT NULL DEFAULT '[]',
    custom_courses_json TEXT NOT NULL DEFAULT '[]',
    course_colors_json TEXT NOT NULL DEFAULT '{}',
    done_marks_json TEXT NOT NULL DEFAULT '{}',
    seen_items_json TEXT NOT NULL DEFAULT '{}',
    card_low_threshold REAL NOT NULL DEFAULT 20.0,
    elec_low_threshold REAL NOT NULL DEFAULT 15.0,
    remind_cfg_json TEXT NOT NULL DEFAULT '{"courseBeforeMin":15,"workBeforeHours":6,"enabled":false}',
    telemetry_opt_in INTEGER NOT NULL DEFAULT 1,
    updated_at INTEGER NOT NULL,
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
);
CREATE TABLE IF NOT EXISTS dorm_electricity (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id INTEGER NOT NULL UNIQUE,
    campus TEXT NOT NULL DEFAULT '海甸校区',
    academy_or_zone TEXT NOT NULL DEFAULT '南海书院',
    building_name TEXT NOT NULL DEFAULT '',
    room_name TEXT NOT NULL DEFAULT '',
    meter_account TEXT NOT NULL DEFAULT '',
    balance_kwh REAL,
    daily_burn_kwh REAL NOT NULL DEFAULT 4.5,
    recorded_at INTEGER NOT NULL DEFAULT 0,
    updated_at INTEGER NOT NULL,
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
);
CREATE TABLE IF NOT EXISTS dorm_elec_logs (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id INTEGER NOT NULL,
    kind TEXT NOT NULL,
    amount_val REAL NOT NULL,
    balance_after REAL,
    note TEXT NOT NULL DEFAULT '',
    logged_at INTEGER NOT NULL,
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
);
CREATE TABLE IF NOT EXISTS campus_notices (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    url_hash TEXT NOT NULL UNIQUE,
    title TEXT NOT NULL,
    url TEXT NOT NULL,
    source_name TEXT NOT NULL,
    dept TEXT NOT NULL,
    publish_date TEXT NOT NULL,
    tier TEXT NOT NULL,
    tags_json TEXT NOT NULL DEFAULT '[]',
    when_text TEXT NOT NULL DEFAULT '',
    where_text TEXT NOT NULL DEFAULT '',
    summary TEXT NOT NULL DEFAULT '',
    attachments_json TEXT NOT NULL DEFAULT '[]',
    detail_fetched INTEGER NOT NULL DEFAULT 0,
    fetched_at INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS school_meta (
    meta_key TEXT PRIMARY KEY,
    value_json TEXT NOT NULL,
    updated_at INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS feedback_reports (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id INTEGER,
    app_version TEXT NOT NULL DEFAULT '',
    client_os TEXT NOT NULL DEFAULT '',
    description TEXT NOT NULL,
    scrubbed_log TEXT NOT NULL DEFAULT '',
    ip_masked TEXT NOT NULL DEFAULT '',
    created_at INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS telemetry_events (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    day_bj TEXT NOT NULL,
    event_kind TEXT NOT NULL,
    detail_code TEXT NOT NULL DEFAULT '',
    message_scrub TEXT NOT NULL DEFAULT '',
    page_name TEXT NOT NULL DEFAULT '',
    anon_day_id TEXT NOT NULL DEFAULT '',
    created_at INTEGER NOT NULL
);
)SQL";

}  // namespace

Database::Database(const std::string& db_path,
                   const std::vector<uint8_t>& master_key,
                   const std::string& schema_sql_path,
                   const std::string& seed_sql_path) {
  vault_key_ = crypto::derive_subkey_sha256(master_key, "hnutong.credential_vault.v1");

  if (db_path != ":memory:") {
    std::filesystem::path p(db_path);
    if (p.has_parent_path()) {
      std::filesystem::create_directories(p.parent_path());
    }
  }

  int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
  if (sqlite3_open_v2(db_path.c_str(), &db_, flags, nullptr) != SQLITE_OK) {
    std::string err = db_ ? sqlite3_errmsg(db_) : "sqlite3_open_v2 failed";
    if (db_) sqlite3_close(db_);
    throw std::runtime_error("Database open error: " + err);
  }
  sqlite3_busy_timeout(db_, 5000);

  std::string schema_sql = read_file_text(schema_sql_path);
  exec_sql_script(schema_sql.empty() ? kFallbackSchemaSql : schema_sql);

  // 兼容旧版数据库平滑迁移：补充通知附件缓存列（列已存在时忽略错误）
  {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    sqlite3_exec(db_, "ALTER TABLE campus_notices ADD COLUMN attachments_json TEXT NOT NULL DEFAULT '[]';",
                 nullptr, nullptr, nullptr);
  }

  // 若首次运行尚未种入海大元数据，则执行 seed_sql 并兜底内置默认值
  if (get_school_meta("bell_presets").is_null()) {
    std::string seed_sql = read_file_text(seed_sql_path);
    if (!seed_sql.empty()) {
      exec_sql_script(seed_sql);
    }
  }
  ensure_builtin_defaults();
}

Database::~Database() {
  if (db_) {
    sqlite3_close(db_);
    db_ = nullptr;
  }
}

void Database::exec_sql_script(const std::string& sql) {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  char* errmsg = nullptr;
  if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errmsg) != SQLITE_OK) {
    std::string err = errmsg ? errmsg : "SQL execution failed";
    sqlite3_free(errmsg);
    HNU_LOG_ERROR("db", "SQL script error: " + err);
    throw std::runtime_error("SQL script error: " + err);
  }
}

void Database::ensure_builtin_defaults() {
  if (get_school_meta("bell_presets").is_null()) {
    set_school_meta("bell_presets", {
        {"haidian", {
            {"name", "海甸 / 儋州 / 观澜湖 / 城西标准作息（11节制）"},
            {"lightsOut", "22:30"},
            {"periods", nlohmann::json::array({
                nlohmann::json::array({"07:40", "08:25"}), nlohmann::json::array({"08:35", "09:20"}),
                nlohmann::json::array({"09:45", "10:30"}), nlohmann::json::array({"10:40", "11:25"}),
                nlohmann::json::array({"14:30", "15:15"}), nlohmann::json::array({"15:25", "16:10"}),
                nlohmann::json::array({"16:35", "17:20"}), nlohmann::json::array({"17:30", "18:15"}),
                nlohmann::json::array({"19:20", "20:05"}), nlohmann::json::array({"20:15", "21:00"}),
                nlohmann::json::array({"21:10", "21:55"})
            })}
        }},
        {"yazhou", {
            {"name", "三亚崖州湾科教园协同作息（11节制）"},
            {"lightsOut", "22:30"},
            {"periods", nlohmann::json::array({
                nlohmann::json::array({"08:00", "08:45"}), nlohmann::json::array({"08:55", "09:40"}),
                nlohmann::json::array({"10:00", "10:45"}), nlohmann::json::array({"10:55", "11:40"}),
                nlohmann::json::array({"14:30", "15:15"}), nlohmann::json::array({"15:25", "16:10"}),
                nlohmann::json::array({"16:30", "17:15"}), nlohmann::json::array({"17:25", "18:10"}),
                nlohmann::json::array({"19:20", "20:05"}), nlohmann::json::array({"20:15", "21:00"}),
                nlohmann::json::array({"21:10", "21:55"})
            })}
        }}
    });
  }
  if (get_school_meta("calendar_terms").is_null()) {
    set_school_meta("calendar_terms", {
        {"year", "2026–2027"},
        {"sourceNote", "依据海南大学本科生院《2026–2027学年校历》整理。"},
        {"terms", nlohmann::json::array({
            {
                {"id", "2026-2027-1"},
                {"name", "第一学期（秋季）"},
                {"start", "2026-08-31"},
                {"weeks", 20},
                {"holiday", {{"d", "2027-01-18"}, {"label", "寒假"}}},
                {"marks", nlohmann::json::array({
                    {{"d", "2026-08-31"}, {"t", "全日制老生正式上课"}},
                    {{"d", "2026-12-12"}, {"t", "大学英语四六级笔试"}},
                    {{"d", "2027-01-04"}, {"e", "2027-01-17"}, {"w", "19–20 周"}, {"t", "期末集中考试周"}},
                    {{"d", "2027-01-18"}, {"t", "寒假开始"}}
                })}
            },
            {
                {"id", "2026-2027-2"},
                {"name", "第二学期（春季）"},
                {"start", "2027-02-22"},
                {"weeks", 20},
                {"holiday", {{"d", "2027-07-12"}, {"label", "暑假"}}},
                {"marks", nlohmann::json::array({
                    {{"d", "2027-02-22"}, {"t", "春季学期正式上课"}},
                    {{"d", "2027-06-28"}, {"e", "2027-07-11"}, {"w", "19–20 周"}, {"t", "期末集中考试周"}},
                    {{"d", "2027-07-12"}, {"t", "暑假开始"}}
                })}
            }
        })}
    });
  }

  // 自动升级/补全海南大学五校区与全部 17 个书院完整元数据（兼容已有旧库）
  auto cur_ca = get_school_meta("campuses_and_academies");
  bool need_ca_upgrade = cur_ca.is_null() || !cur_ca.is_object() ||
                         !cur_ca.contains("campusAcademies") ||
                         !cur_ca.contains("academies") ||
                         !cur_ca["academies"].is_array() ||
                         cur_ca["academies"].size() < 17;
  if (need_ca_upgrade) {
    set_school_meta("campuses_and_academies", {
        {"campuses", nlohmann::json::array({
            {{"id", "1"}, {"name", "海甸校区"}},
            {{"id", "2"}, {"name", "儋州校区"}},
            {{"id", "3"}, {"name", "观澜湖校区"}},
            {{"id", "4"}, {"name", "崖州湾科教园"}},
            {{"id", "5"}, {"name", "城西教学点"}},
        })},
        {"academies", nlohmann::json::array({
            "椰风书院", "乘风书院", "凤翔书院", "子衿书院", "青云书院",
            "南海书院", "淳明书院", "海德书院", "天工书院", "朴诚书院",
            "中和书院", "今朝书院", "日新书院", "丹心书院", "檀雅书院",
            "晨曦书院", "崖州书院"
        })},
        {"campusAcademies", nlohmann::json::array({
            {
                {"campusId", "1"},
                {"campusName", "海甸校区"},
                {"academies", nlohmann::json::array({
                    "椰风书院", "乘风书院", "凤翔书院", "子衿书院", "青云书院",
                    "南海书院", "淳明书院", "海德书院", "天工书院", "朴诚书院", "中和书院"
                })}
            },
            {
                {"campusId", "3"},
                {"campusName", "观澜湖校区"},
                {"academies", nlohmann::json::array({"今朝书院", "日新书院"})}
            },
            {
                {"campusId", "2"},
                {"campusName", "儋州校区"},
                {"academies", nlohmann::json::array({"丹心书院", "檀雅书院"})}
            },
            {
                {"campusId", "5"},
                {"campusName", "城西教学点"},
                {"academies", nlohmann::json::array({"晨曦书院"})}
            },
            {
                {"campusId", "4"},
                {"campusName", "崖州湾科教园"},
                {"academies", nlohmann::json::array({"崖州书院"})}
            }
        })}
    });
  }

  // 同步更新海大指南（兼容旧库）：补全十七书院描述，并仅保留「学业进阶 · 选课与共享攻略」栏目下的三个网页跳转
  auto cur_wiki = get_school_meta("wiki_groups");
  if (cur_wiki.is_object() && cur_wiki.contains("groups") && cur_wiki["groups"].is_array()) {
    bool wiki_changed = false;
    if (!cur_wiki.value("homeUrl", "").empty()) {
      cur_wiki["homeUrl"] = "";
      wiki_changed = true;
    }
    for (auto& g : cur_wiki["groups"]) {
      if (!g.is_object() || !g.contains("items") || !g["items"].is_array()) continue;
      bool allow_url = (g.value("group", "") == "学业进阶 · 选课与共享攻略");
      for (auto& it : g["items"]) {
        if (!it.is_object()) continue;
        if (it.value("title", "") == "五校区概览与书院制指南") {
          it["title"] = "五校区概览与十七书院制指南";
          it["summary"] = "海南大学实行「学院（专业教学）+ 书院（社区育人）」双院协同管理，现设五校区与 17 个书院：海甸校区（椰风、乘风、凤翔、子衿、青云、南海、淳明、海德、天工、朴诚、中和）、观澜湖校区（今朝、日新）、儋州校区（丹心、檀雅）、城西教学点（晨曦）与崖州湾科教园（崖州书院）。专业选课找所在学院教务办，宿舍调宿、请假销假、综测奖助找所在书院辅导员。";
          wiki_changed = true;
        }
        if (!allow_url && !it.value("url", "").empty()) {
          it["url"] = "";
          wiki_changed = true;
        }
      }
    }
    if (wiki_changed) {
      set_school_meta("wiki_groups", cur_wiki);
    }
  }

  // 同步清理旧库 campus_quick_links 中已下线的海大 e 卡购电缴费入口，并修正邮箱跳转地址
  auto cur_ql = get_school_meta("campus_quick_links");
  if (cur_ql.is_object() && cur_ql.contains("squares") && cur_ql["squares"].is_array()) {
    bool ql_has_legacy = false;
    for (const auto& sq : cur_ql["squares"]) {
      if (!sq.is_object()) continue;
      std::string sid = sq.value("id", "");
      if (sid == "pay" || sid == "ecard") {
        ql_has_legacy = true;
        break;
      }
    }
    if (ql_has_legacy) {
      set_school_meta("campus_quick_links", {
          {"squares", nlohmann::json::array({
              {
                  {"id", "webvpn"},
                  {"title", "校外免插件 WebVPN"},
                  {"sub", "校外进办事大厅 · 查内网"},
                  {"url", "https://webvpn.hainanu.edu.cn/##login?cas_login=true"},
                  {"guide", "在校外使用流量或宽带时，点击下方按钮登录海南大学免插件 WebVPN（账号密码与教务一致），即可直接访问网上服务大厅 (ehall)、正版软件平台及校内电子图书资源。"}
              },
              {
                  {"id", "lib"},
                  {"title", "图书馆与 CARSI 文献"},
                  {"sub", "座位研讨间 · 知网直连"},
                  {"url", "https://lib.hainanu.edu.cn/"},
                  {"guide", "海甸北馆/南馆、儋州馆、观澜湖馆研修间支持在线预约；校外下载知网 CNKI / IEEE 论文可使用学校 CARSI 联邦认证免 VPN 直连。"}
              },
              {
                  {"id", "edu_mail"},
                  {"title", "海大教育邮箱"},
                  {"sub", "@hainanu.edu.cn 邮箱登录"},
                  {"url", "https://mail.hainanu.edu.cn/"},
                  {"guide", "直接登录海南大学官方教育邮箱（@hainanu.edu.cn），用于学术投稿及认证 GitHub Student Pack、JetBrains 等学生免费授权。"}
              }
          })}
      });
    }
  }
}

UserRecord Database::read_user_row(void* stmt_ptr) {
  auto* st = static_cast<sqlite3_stmt*>(stmt_ptr);
  UserRecord u;
  u.id = sqlite3_column_int64(st, 0);
  u.student_id = col_text(st, 1);
  u.name = col_text(st, 2);
  u.gender = col_text(st, 3);
  u.college = col_text(st, 4);
  u.academy = col_text(st, 5);
  u.major = col_text(st, 6);
  u.direction = col_text(st, 7);
  u.class_name = col_text(st, 8);
  u.campus = col_text(st, 9);
  u.grade = col_text(st, 10);
  u.edu_level = col_text(st, 11);
  u.edu_duration = col_text(st, 12);
  u.status = col_text(st, 13);
  u.enrolled_at = col_text(st, 14);
  u.graduate_at = col_text(st, 15);
  u.ics_token = col_text(st, 16);
  u.role = col_text(st, 17);
  u.last_login_at = sqlite3_column_int64(st, 18);
  return u;
}

std::optional<UserRecord> Database::get_user_by_id(int64_t user_id) {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  const char* sql =
      "SELECT id,student_id,name,gender,college,academy,major,direction,class_name,"
      "campus,grade,edu_level,edu_duration,status,enrolled_at,graduate_at,ics_token,role,last_login_at "
      "FROM users WHERE id = ?1;";
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK) return std::nullopt;
  sqlite3_bind_int64(st, 1, user_id);
  std::optional<UserRecord> res;
  if (sqlite3_step(st) == SQLITE_ROW) {
    res = read_user_row(st);
  }
  sqlite3_finalize(st);
  return res;
}

std::optional<UserRecord> Database::get_user_by_student_id(const std::string& student_id) {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  const char* sql =
      "SELECT id,student_id,name,gender,college,academy,major,direction,class_name,"
      "campus,grade,edu_level,edu_duration,status,enrolled_at,graduate_at,ics_token,role,last_login_at "
      "FROM users WHERE student_id = ?1;";
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK) return std::nullopt;
  sqlite3_bind_text(st, 1, student_id.c_str(), -1, SQLITE_TRANSIENT);
  std::optional<UserRecord> res;
  if (sqlite3_step(st) == SQLITE_ROW) {
    res = read_user_row(st);
  }
  sqlite3_finalize(st);
  return res;
}

std::optional<UserRecord> Database::get_user_by_ics_token(const std::string& ics_token) {
  if (ics_token.size() < 16) return std::nullopt;
  std::lock_guard<std::recursive_mutex> lk(mu_);
  const char* sql =
      "SELECT id,student_id,name,gender,college,academy,major,direction,class_name,"
      "campus,grade,edu_level,edu_duration,status,enrolled_at,graduate_at,ics_token,role,last_login_at "
      "FROM users WHERE ics_token = ?1;";
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK) return std::nullopt;
  sqlite3_bind_text(st, 1, ics_token.c_str(), -1, SQLITE_TRANSIENT);
  std::optional<UserRecord> res;
  if (sqlite3_step(st) == SQLITE_ROW) {
    res = read_user_row(st);
  }
  sqlite3_finalize(st);
  return res;
}

UserRecord Database::upsert_user(const std::string& student_id, const nlohmann::json& profile) {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  int64_t now = utils::now_sec();
  auto existing = get_user_by_student_id(student_id);

  auto pick = [&](const char* key1, const char* key2, const std::string& cur) -> std::string {
    if (profile.is_object()) {
      if (profile.contains(key1) && profile[key1].is_string()) {
        std::string v = utils::trim(profile[key1].get<std::string>());
        if (!v.empty()) return v;
      }
      if (key2 && profile.contains(key2) && profile[key2].is_string()) {
        std::string v = utils::trim(profile[key2].get<std::string>());
        if (!v.empty()) return v;
      }
    }
    return cur;
  };

  if (!existing) {
    std::string ics_tok = crypto::random_urlsafe_token(24);
    const char* ins_sql =
        "INSERT INTO users (student_id,name,gender,college,academy,major,direction,class_name,"
        "campus,grade,edu_level,edu_duration,status,enrolled_at,graduate_at,ics_token,role,"
        "created_at,updated_at,last_login_at) "
        "VALUES (?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14,?15,?16,'student',?17,?17,?17);";
    StmtGuard sg;
    if (sqlite3_prepare_v2(db_, ins_sql, -1, &sg.st, nullptr) != SQLITE_OK) {
      throw std::runtime_error(std::string("SQLite prepare failed in upsert_user(insert): ") +
                               sqlite3_errmsg(db_));
    }
    sqlite3_stmt* st = sg.st;
    std::string name = pick("name", nullptr, "");
    std::string gender = pick("gender", nullptr, "");
    std::string college = pick("college", "dept", "");
    std::string academy = pick("academy", nullptr, "");
    std::string major = pick("major", nullptr, "");
    std::string direction = pick("direction", nullptr, "");
    std::string cls = pick("className", nullptr, "");
    std::string campus = pick("campus", nullptr, "海甸校区");
    std::string grade = pick("grade", nullptr, student_id.size() >= 4 ? student_id.substr(0, 4) : "");
    std::string level = pick("level", nullptr, "本科");
    std::string dur = pick("duration", nullptr, "4");
    std::string status = pick("status", nullptr, "在籍");
    std::string enrolled = pick("enrolledAt", nullptr, "");
    std::string grad = pick("graduateAt", nullptr, "");

    sqlite3_bind_text(st, 1, student_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, gender.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, college.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 5, academy.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 6, major.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 7, direction.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 8, cls.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 9, campus.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 10, grade.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 11, level.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 12, dur.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 13, status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 14, enrolled.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 15, grad.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 16, ics_tok.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 17, now);
    if (sqlite3_step(st) != SQLITE_DONE) {
      throw std::runtime_error(std::string("SQLite insert failed in upsert_user: ") +
                               sqlite3_errmsg(db_));
    }
    sqlite3_finalize(sg.st);
    sg.st = nullptr;

    auto created = get_user_by_student_id(student_id);
    if (!created) {
      throw std::runtime_error("Failed to reload inserted user in upsert_user");
    }
    // 初始化默认偏好设置
    save_preferences(created->id, {});
    return *created;
  } else {
    UserRecord u = *existing;
    // 红线：若本次学籍没读到（空字段），保留上一次的有效学籍字段，绝不覆盖为空
    std::string name = pick("name", nullptr, u.name);
    std::string gender = pick("gender", nullptr, u.gender);
    std::string college = pick("college", "dept", u.college);
    std::string academy = pick("academy", nullptr, u.academy);
    std::string major = pick("major", nullptr, u.major);
    std::string direction = pick("direction", nullptr, u.direction);
    std::string cls = pick("className", nullptr, u.class_name);
    std::string campus = pick("campus", nullptr, u.campus);
    std::string grade = pick("grade", nullptr, u.grade);
    std::string level = pick("level", nullptr, u.edu_level);
    std::string dur = pick("duration", nullptr, u.edu_duration);
    std::string status = pick("status", nullptr, u.status);
    std::string enrolled = pick("enrolledAt", nullptr, u.enrolled_at);
    std::string grad = pick("graduateAt", nullptr, u.graduate_at);

    const char* upd_sql =
        "UPDATE users SET name=?1,gender=?2,college=?3,academy=?4,major=?5,direction=?6,"
        "class_name=?7,campus=?8,grade=?9,edu_level=?10,edu_duration=?11,status=?12,"
        "enrolled_at=?13,graduate_at=?14,updated_at=?15,last_login_at=?15 WHERE id=?16;";
    StmtGuard sg;
    if (sqlite3_prepare_v2(db_, upd_sql, -1, &sg.st, nullptr) != SQLITE_OK) {
      throw std::runtime_error(std::string("SQLite prepare failed in upsert_user(update): ") +
                               sqlite3_errmsg(db_));
    }
    sqlite3_stmt* st = sg.st;
    sqlite3_bind_text(st, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, gender.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, college.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, academy.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 5, major.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 6, direction.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 7, cls.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 8, campus.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 9, grade.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 10, level.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 11, dur.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 12, status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 13, enrolled.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 14, grad.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 15, now);
    sqlite3_bind_int64(st, 16, u.id);
    if (sqlite3_step(st) != SQLITE_DONE) {
      throw std::runtime_error(std::string("SQLite update failed in upsert_user: ") +
                               sqlite3_errmsg(db_));
    }
    sqlite3_finalize(sg.st);
    sg.st = nullptr;
    auto updated = get_user_by_id(u.id);
    if (!updated) {
      throw std::runtime_error("Failed to reload updated user in upsert_user");
    }
    return *updated;
  }
}

std::optional<UserRecord> Database::update_user_profile_manual(int64_t user_id,
                                                               const std::string& academy,
                                                               const std::string& campus) {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  auto existing = get_user_by_id(user_id);
  if (!existing) return std::nullopt;
  std::string next_academy = utils::trim(academy);
  std::string next_campus = utils::trim(campus);
  if (next_campus.empty()) next_campus = existing->campus;

  const char* sql = "UPDATE users SET academy=?1, campus=?2, updated_at=?3 WHERE id=?4;";
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) == SQLITE_OK) {
    sqlite3_bind_text(st, 1, next_academy.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, next_campus.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 3, utils::now_sec());
    sqlite3_bind_int64(st, 4, user_id);
    sqlite3_step(st);
    sqlite3_finalize(st);
  }
  return get_user_by_id(user_id);
}

std::vector<int64_t> Database::list_active_jwxt_user_ids(int max_age_days) {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  std::vector<int64_t> ids;
  int64_t cutoff = utils::now_sec() - static_cast<int64_t>(std::max(1, max_age_days)) * 86400;
  const char* sql =
      "SELECT DISTINCT u.id FROM users u "
      "JOIN credential_vault v ON v.user_id = u.id AND v.provider = 'jwxt' "
      "WHERE v.state IN ('ok', 'expired') AND u.last_login_at >= ?1 "
      "ORDER BY u.last_login_at DESC LIMIT 32;";
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) == SQLITE_OK) {
    sqlite3_bind_int64(st, 1, cutoff);
    while (sqlite3_step(st) == SQLITE_ROW) {
      ids.push_back(sqlite3_column_int64(st, 0));
    }
    sqlite3_finalize(st);
  }
  return ids;
}

std::string Database::rotate_ics_token(int64_t user_id) {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  std::string tok = crypto::random_urlsafe_token(24);
  const char* sql = "UPDATE users SET ics_token=?1, updated_at=?2 WHERE id=?3;";
  StmtGuard sg;
  if (sqlite3_prepare_v2(db_, sql, -1, &sg.st, nullptr) != SQLITE_OK) {
    throw std::runtime_error(std::string("SQLite prepare failed in rotate_ics_token: ") +
                             sqlite3_errmsg(db_));
  }
  sqlite3_bind_text(sg.st, 1, tok.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(sg.st, 2, utils::now_sec());
  sqlite3_bind_int64(sg.st, 3, user_id);
  if (sqlite3_step(sg.st) != SQLITE_DONE) {
    throw std::runtime_error(std::string("SQLite update failed in rotate_ics_token: ") +
                             sqlite3_errmsg(db_));
  }
  return tok;
}

std::string Database::create_session(int64_t user_id,
                                     const std::string& device_label,
                                     const std::string& ip_masked,
                                     int ttl_days) {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  std::string raw_token = crypto::random_urlsafe_token(32);
  std::string token_hash = crypto::sha256_hex(raw_token);
  int64_t now = utils::now_sec();
  int64_t exp = now + static_cast<int64_t>(ttl_days > 0 ? ttl_days : 365) * 86400;

  const char* sql =
      "INSERT OR REPLACE INTO sessions (token_hash,user_id,device_label,ip_masked,"
      "expires_at,created_at,last_seen_at) VALUES (?1,?2,?3,?4,?5,?6,?6);";
  StmtGuard sg;
  if (sqlite3_prepare_v2(db_, sql, -1, &sg.st, nullptr) != SQLITE_OK) {
    throw std::runtime_error(std::string("SQLite prepare failed in create_session: ") +
                             sqlite3_errmsg(db_));
  }
  sqlite3_bind_text(sg.st, 1, token_hash.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(sg.st, 2, user_id);
  sqlite3_bind_text(sg.st, 3, device_label.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(sg.st, 4, ip_masked.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(sg.st, 5, exp);
  sqlite3_bind_int64(sg.st, 6, now);
  if (sqlite3_step(sg.st) != SQLITE_DONE) {
    throw std::runtime_error(std::string("SQLite insert failed in create_session: ") +
                             sqlite3_errmsg(db_));
  }
  return raw_token;
}

std::optional<UserRecord> Database::authenticate_session(const std::string& raw_token, int slide_ttl_days) {
  if (raw_token.size() < 16) return std::nullopt;
  std::string token_hash = crypto::sha256_hex(raw_token);
  int64_t now = utils::now_sec();

  std::lock_guard<std::recursive_mutex> lk(mu_);
  const char* sql = "SELECT user_id, expires_at FROM sessions WHERE token_hash = ?1;";
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK) return std::nullopt;
  sqlite3_bind_text(st, 1, token_hash.c_str(), -1, SQLITE_TRANSIENT);

  int64_t uid = 0;
  int64_t exp = 0;
  bool hit = (sqlite3_step(st) == SQLITE_ROW);
  if (hit) {
    uid = sqlite3_column_int64(st, 0);
    exp = sqlite3_column_int64(st, 1);
  }
  sqlite3_finalize(st);

  if (!hit || exp < now) {
    if (hit) revoke_session(raw_token);
    return std::nullopt;
  }

  // 滑动续期（默认 365 天，覆盖整学年，避免学生频繁掉登录）
  int64_t days = slide_ttl_days > 0 ? slide_ttl_days : 365;
  int64_t new_exp = std::max<int64_t>(exp, now + days * int64_t{86400});
  sqlite3_stmt* up = nullptr;
  if (sqlite3_prepare_v2(db_, "UPDATE sessions SET last_seen_at=?1, expires_at=?2 WHERE token_hash=?3;", -1, &up, nullptr) == SQLITE_OK) {
    sqlite3_bind_int64(up, 1, now);
    sqlite3_bind_int64(up, 2, new_exp);
    sqlite3_bind_text(up, 3, token_hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(up);
    sqlite3_finalize(up);
  }
  return get_user_by_id(uid);
}

void Database::revoke_session(const std::string& raw_token) {
  if (raw_token.empty()) return;
  std::string token_hash = crypto::sha256_hex(raw_token);
  std::lock_guard<std::recursive_mutex> lk(mu_);
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db_, "DELETE FROM sessions WHERE token_hash=?1;", -1, &st, nullptr) == SQLITE_OK) {
    sqlite3_bind_text(st, 1, token_hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(st);
    sqlite3_finalize(st);
  }
}

void Database::purge_expired_sessions() {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db_, "DELETE FROM sessions WHERE expires_at < ?1;", -1, &st, nullptr) == SQLITE_OK) {
    sqlite3_bind_int64(st, 1, utils::now_sec());
    sqlite3_step(st);
    sqlite3_finalize(st);
  }
}

// ---------------------------------------------------------------------------
// 凭据保险箱 (AES-256-GCM + AAD 绑定 user_id 与 provider)
// ---------------------------------------------------------------------------
bool Database::save_credential(int64_t user_id,
                               const std::string& provider,
                               const std::string& raw_account,
                               const nlohmann::json& secret_obj,
                               const std::string& state,
                               const std::string& last_error) {
  std::string aad = "hnutong:" + std::to_string(user_id) + ":" + provider;
  std::string plain = secret_obj.dump();
  auto enc = crypto::aes256_gcm_encrypt(vault_key_, plain, aad);
  if (!enc) return false;

  std::string masked = utils::mask_account(raw_account);
  int64_t now = utils::now_sec();

  std::lock_guard<std::recursive_mutex> lk(mu_);
  const char* sql =
      "INSERT INTO credential_vault (user_id,provider,account_masked,cipher_b64,iv_b64,tag_b64,state,last_error,updated_at) "
      "VALUES (?1,?2,?3,?4,?5,?6,?7,?8,?9) "
      "ON CONFLICT(user_id,provider) DO UPDATE SET "
      "account_masked=excluded.account_masked, cipher_b64=excluded.cipher_b64, "
      "iv_b64=excluded.iv_b64, tag_b64=excluded.tag_b64, state=excluded.state, "
      "last_error=excluded.last_error, updated_at=excluded.updated_at;";
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK) return false;
  sqlite3_bind_int64(st, 1, user_id);
  sqlite3_bind_text(st, 2, provider.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 3, masked.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 4, enc->cipher_b64.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 5, enc->iv_b64.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 6, enc->tag_b64.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 7, state.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 8, last_error.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(st, 9, now);
  bool ok = (sqlite3_step(st) == SQLITE_DONE);
  sqlite3_finalize(st);
  return ok;
}

VaultEntry Database::load_credential(int64_t user_id, const std::string& provider) {
  VaultEntry out;
  out.provider = provider;

  std::lock_guard<std::recursive_mutex> lk(mu_);
  const char* sql =
      "SELECT account_masked,cipher_b64,iv_b64,tag_b64,state,last_error,updated_at "
      "FROM credential_vault WHERE user_id=?1 AND provider=?2;";
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK) return out;
  sqlite3_bind_int64(st, 1, user_id);
  sqlite3_bind_text(st, 2, provider.c_str(), -1, SQLITE_TRANSIENT);

  crypto::AesGcmCiphertext ct;
  if (sqlite3_step(st) == SQLITE_ROW) {
    out.exists = true;
    out.account_masked = col_text(st, 0);
    ct.cipher_b64 = col_text(st, 1);
    ct.iv_b64 = col_text(st, 2);
    ct.tag_b64 = col_text(st, 3);
    out.state = col_text(st, 4);
    out.last_error = col_text(st, 5);
    out.updated_at = sqlite3_column_int64(st, 6);
  }
  sqlite3_finalize(st);

  if (!out.exists) return out;

  std::string aad = "hnutong:" + std::to_string(user_id) + ":" + provider;
  auto plain = crypto::aes256_gcm_decrypt(vault_key_, ct, aad);
  if (!plain) {
    // 精确区分「从没保存」与「密文存在但解密失败 (broken)」三态凭据状态
    out.state = "broken";
    out.last_error = "凭据密文校验未通过（可能服务端主密钥已轮换），请重新输入一次密码";
    out.secret = nlohmann::json::object();
    return out;
  }
  out.secret = parse_json_or(*plain, nlohmann::json::object());
  return out;
}

void Database::update_credential_state(int64_t user_id,
                                       const std::string& provider,
                                       const std::string& state,
                                       const std::string& last_error) {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  const char* sql =
      "UPDATE credential_vault SET state=?1, last_error=?2, updated_at=?3 "
      "WHERE user_id=?4 AND provider=?5;";
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) == SQLITE_OK) {
    sqlite3_bind_text(st, 1, state.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, last_error.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 3, utils::now_sec());
    sqlite3_bind_int64(st, 4, user_id);
    sqlite3_bind_text(st, 5, provider.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(st);
    sqlite3_finalize(st);
  }
}

void Database::delete_credential(int64_t user_id, const std::string& provider) {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db_, "DELETE FROM credential_vault WHERE user_id=?1 AND provider=?2;", -1, &st, nullptr) == SQLITE_OK) {
    sqlite3_bind_int64(st, 1, user_id);
    sqlite3_bind_text(st, 2, provider.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(st);
    sqlite3_finalize(st);
  }
}

nlohmann::json Database::list_credential_briefs(int64_t user_id) {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  nlohmann::json out = nlohmann::json::object();
  const char* sql =
      "SELECT provider, account_masked, state, last_error, updated_at "
      "FROM credential_vault WHERE user_id=?1;";
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) == SQLITE_OK) {
    sqlite3_bind_int64(st, 1, user_id);
    while (sqlite3_step(st) == SQLITE_ROW) {
      std::string prov = col_text(st, 0);
      out[prov] = {
          {"linked", true},
          {"account", col_text(st, 1)},
          {"state", col_text(st, 2)},
          {"error", col_text(st, 3)},
          {"updatedAt", sqlite3_column_int64(st, 4)},
      };
    }
    sqlite3_finalize(st);
  }
  return out;
}

// ---------------------------------------------------------------------------
// 数据快照
// ---------------------------------------------------------------------------
void Database::save_snapshot(int64_t user_id,
                             const std::string& kind,
                             const std::string& semester_id,
                             const nlohmann::json& payload) {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  std::string sem = semester_id.empty() ? "current" : semester_id;
  std::string body = payload.dump();
  int64_t now = utils::now_sec();

  const char* sql =
      "INSERT INTO academic_snapshots (user_id,kind,semester_id,payload_json,synced_at) "
      "VALUES (?1,?2,?3,?4,?5) "
      "ON CONFLICT(user_id,kind,semester_id) DO UPDATE SET "
      "payload_json=excluded.payload_json, synced_at=excluded.synced_at;";
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) == SQLITE_OK) {
    sqlite3_bind_int64(st, 1, user_id);
    sqlite3_bind_text(st, 2, kind.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, sem.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, body.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 5, now);
    sqlite3_step(st);
    sqlite3_finalize(st);
  }
}

SnapshotRecord Database::get_snapshot(int64_t user_id,
                                      const std::string& kind,
                                      const std::string& semester_id) {
  SnapshotRecord rec;
  std::string sem = semester_id.empty() ? "current" : semester_id;
  std::lock_guard<std::recursive_mutex> lk(mu_);
  const char* sql =
      "SELECT payload_json, synced_at FROM academic_snapshots "
      "WHERE user_id=?1 AND kind=?2 AND semester_id=?3;";
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK) return rec;
  sqlite3_bind_int64(st, 1, user_id);
  sqlite3_bind_text(st, 2, kind.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 3, sem.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(st) == SQLITE_ROW) {
    rec.found = true;
    rec.payload = parse_json_or(col_text(st, 0), nlohmann::json::object());
    rec.synced_at = sqlite3_column_int64(st, 1);
  }
  sqlite3_finalize(st);
  return rec;
}

void Database::delete_snapshots(int64_t user_id, const std::string& kind) {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db_, "DELETE FROM academic_snapshots WHERE user_id=?1 AND kind=?2;", -1, &st, nullptr) == SQLITE_OK) {
    sqlite3_bind_int64(st, 1, user_id);
    sqlite3_bind_text(st, 2, kind.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(st);
    sqlite3_finalize(st);
  }
}

void Database::clear_all_user_cache(int64_t user_id) {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db_, "DELETE FROM academic_snapshots WHERE user_id=?1;", -1, &st, nullptr) == SQLITE_OK) {
    sqlite3_bind_int64(st, 1, user_id);
    sqlite3_step(st);
    sqlite3_finalize(st);
  }
}

// ---------------------------------------------------------------------------
// 用户偏好设置
// ---------------------------------------------------------------------------
nlohmann::json Database::get_preferences(int64_t user_id) {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  const char* sql =
      "SELECT campus_preset,term_start_date,total_weeks,bell_schedule_json,"
      "hidden_courses_json,hidden_slots_json,custom_courses_json,course_colors_json,"
      "done_marks_json,seen_items_json,card_low_threshold,elec_low_threshold,"
      "remind_cfg_json,telemetry_opt_in,updated_at "
      "FROM user_preferences WHERE user_id=?1;";
  sqlite3_stmt* st = nullptr;
  nlohmann::json out = {
      {"campusPreset", "haidian"},
      {"termStartDate", "2026-08-31"},
      {"totalWeeks", 20},
      {"bellSchedule", nlohmann::json::array()},
      {"hiddenCourses", nlohmann::json::array()},
      {"hiddenSlots", nlohmann::json::array()},
      {"customCourses", nlohmann::json::array()},
      {"courseColors", nlohmann::json::object()},
      {"doneMarks", nlohmann::json::object()},
      {"seenItems", nlohmann::json::object()},
      {"cardLowThreshold", 20.0},
      {"elecLowThreshold", 15.0},
      {"remindCfg", {{"courseBeforeMin", 15}, {"workBeforeHours", 6}, {"enabled", false}}},
      {"telemetryOptIn", true},
      {"updatedAt", 0},
  };
  if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) == SQLITE_OK) {
    sqlite3_bind_int64(st, 1, user_id);
    if (sqlite3_step(st) == SQLITE_ROW) {
      out["campusPreset"] = col_text(st, 0);
      out["termStartDate"] = col_text(st, 1);
      out["totalWeeks"] = sqlite3_column_int(st, 2);
      out["bellSchedule"] = parse_json_or(col_text(st, 3), nlohmann::json::array());
      out["hiddenCourses"] = parse_json_or(col_text(st, 4), nlohmann::json::array());
      out["hiddenSlots"] = parse_json_or(col_text(st, 5), nlohmann::json::array());
      out["customCourses"] = parse_json_or(col_text(st, 6), nlohmann::json::array());
      out["courseColors"] = parse_json_or(col_text(st, 7), nlohmann::json::object());
      out["doneMarks"] = parse_json_or(col_text(st, 8), nlohmann::json::object());
      out["seenItems"] = parse_json_or(col_text(st, 9), nlohmann::json::object());
      out["cardLowThreshold"] = sqlite3_column_double(st, 10);
      out["elecLowThreshold"] = sqlite3_column_double(st, 11);
      out["remindCfg"] = parse_json_or(col_text(st, 12), out["remindCfg"]);
      out["telemetryOptIn"] = sqlite3_column_int(st, 13) != 0;
      out["updatedAt"] = sqlite3_column_int64(st, 14);
    }
    sqlite3_finalize(st);
  }
  return out;
}

void Database::save_preferences(int64_t user_id, const nlohmann::json& patch) {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  nlohmann::json prev = get_preferences(user_id);
  nlohmann::json cur = prev;
  if (patch.is_object()) {
    for (auto it = patch.begin(); it != patch.end(); ++it) {
      cur[it.key()] = it.value();
    }
  }
  int64_t now = utils::now_sec();
  std::string preset = utils::trim(utils::json_str(cur, "campusPreset", utils::json_str(prev, "campusPreset", "haidian")));
  if (preset.empty()) preset = "haidian";
  std::string start_d = utils::trim(utils::json_str(cur, "termStartDate", utils::json_str(prev, "termStartDate", "2026-08-31")));
  if (start_d.empty()) start_d = utils::json_str(prev, "termStartDate", "2026-08-31");
  int prev_weeks = std::clamp(utils::json_int(prev, "totalWeeks", 20), 10, 30);
  int weeks = std::clamp(utils::json_int(cur, "totalWeeks", prev_weeks), 10, 30);

  auto dump_arr = [&](const char* k) -> std::string {
    if (cur.contains(k) && cur[k].is_array()) return cur[k].dump();
    if (prev.contains(k) && prev[k].is_array()) return prev[k].dump();
    return "[]";
  };
  auto dump_obj = [&](const char* k) -> std::string {
    if (cur.contains(k) && cur[k].is_object()) return cur[k].dump();
    if (prev.contains(k) && prev[k].is_object()) return prev[k].dump();
    return "{}";
  };

  std::string bell = dump_arr("bellSchedule");
  std::string hc = dump_arr("hiddenCourses");
  std::string hs = dump_arr("hiddenSlots");
  std::string cc = dump_arr("customCourses");
  std::string colors = dump_obj("courseColors");
  std::string dm = dump_obj("doneMarks");
  std::string si = dump_obj("seenItems");
  double prev_card = utils::json_double(prev, "cardLowThreshold", 20.0);
  double prev_elec = utils::json_double(prev, "elecLowThreshold", 15.0);
  double card_low = utils::json_double(cur, "cardLowThreshold", prev_card);
  double elec_low = utils::json_double(cur, "elecLowThreshold", prev_elec);
  if (!std::isfinite(card_low)) card_low = prev_card;
  if (!std::isfinite(elec_low)) elec_low = prev_elec;
  card_low = std::clamp(card_low, 0.0, 10000.0);
  elec_low = std::clamp(elec_low, 0.0, 10000.0);
  std::string rem = dump_obj("remindCfg");
  int tele = utils::json_bool(cur, "telemetryOptIn", utils::json_bool(prev, "telemetryOptIn", true)) ? 1 : 0;

  const char* sql =
      "INSERT INTO user_preferences (user_id,campus_preset,term_start_date,total_weeks,"
      "bell_schedule_json,hidden_courses_json,hidden_slots_json,custom_courses_json,"
      "course_colors_json,done_marks_json,seen_items_json,card_low_threshold,elec_low_threshold,"
      "remind_cfg_json,telemetry_opt_in,updated_at) "
      "VALUES (?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14,?15,?16) "
      "ON CONFLICT(user_id) DO UPDATE SET "
      "campus_preset=excluded.campus_preset, term_start_date=excluded.term_start_date, "
      "total_weeks=excluded.total_weeks, bell_schedule_json=excluded.bell_schedule_json, "
      "hidden_courses_json=excluded.hidden_courses_json, hidden_slots_json=excluded.hidden_slots_json, "
      "custom_courses_json=excluded.custom_courses_json, course_colors_json=excluded.course_colors_json, "
      "done_marks_json=excluded.done_marks_json, seen_items_json=excluded.seen_items_json, "
      "card_low_threshold=excluded.card_low_threshold, elec_low_threshold=excluded.elec_low_threshold, "
      "remind_cfg_json=excluded.remind_cfg_json, telemetry_opt_in=excluded.telemetry_opt_in, "
      "updated_at=excluded.updated_at;";
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) == SQLITE_OK) {
    sqlite3_bind_int64(st, 1, user_id);
    sqlite3_bind_text(st, 2, preset.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, start_d.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 4, weeks);
    sqlite3_bind_text(st, 5, bell.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 6, hc.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 7, hs.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 8, cc.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 9, colors.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 10, dm.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 11, si.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(st, 12, card_low);
    sqlite3_bind_double(st, 13, elec_low);
    sqlite3_bind_text(st, 14, rem.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 15, tele);
    sqlite3_bind_int64(st, 16, now);
    sqlite3_step(st);
    sqlite3_finalize(st);
  }
}

// ---------------------------------------------------------------------------
// 海大宿舍电量管理
// ---------------------------------------------------------------------------
nlohmann::json Database::get_dorm_electricity(int64_t user_id) {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  const char* sql =
      "SELECT campus,academy_or_zone,building_name,room_name,meter_account,"
      "balance_kwh,daily_burn_kwh,recorded_at,updated_at "
      "FROM dorm_electricity WHERE user_id=?1;";
  sqlite3_stmt* st = nullptr;
  nlohmann::json out = {
      {"configured", false},
      {"campus", "海甸校区"},
      {"academy", "南海书院"},
      {"building", ""},
      {"room", ""},
      {"meterAccount", ""},
      {"balanceKwh", nullptr},
      {"estimatedNowKwh", nullptr},
      {"estimatedDaysLeft", nullptr},
      {"dailyBurnKwh", 4.5},
      {"recordedAt", 0},
      {"payUrl", "https://ehall.hainanu.edu.cn"},
  };
  if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) == SQLITE_OK) {
    sqlite3_bind_int64(st, 1, user_id);
    if (sqlite3_step(st) == SQLITE_ROW) {
      out["configured"] = true;
      out["campus"] = col_text(st, 0);
      out["academy"] = col_text(st, 1);
      out["building"] = col_text(st, 2);
      out["room"] = col_text(st, 3);
      out["meterAccount"] = col_text(st, 4);
      bool has_bal = (sqlite3_column_type(st, 5) != SQLITE_NULL);
      double daily = sqlite3_column_double(st, 6);
      int64_t rec_at = sqlite3_column_int64(st, 7);
      out["dailyBurnKwh"] = daily;
      out["recordedAt"] = rec_at;
      out["updatedAt"] = sqlite3_column_int64(st, 8);
      if (has_bal) {
        double bal = sqlite3_column_double(st, 5);
        out["balanceKwh"] = std::round(bal * 100.0) / 100.0;
        if (rec_at > 0 && daily > 0.05) {
          double elapsed_days = std::max(0.0, static_cast<double>(utils::now_sec() - rec_at) / 86400.0);
          double est_now = std::max(0.0, bal - elapsed_days * daily);
          out["estimatedNowKwh"] = std::round(est_now * 100.0) / 100.0;
          out["estimatedDaysLeft"] = std::round((est_now / daily) * 10.0) / 10.0;
        }
      }
    }
    sqlite3_finalize(st);
  }
  return out;
}

nlohmann::json Database::update_dorm_electricity(int64_t user_id, const nlohmann::json& input) {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  nlohmann::json cur = get_dorm_electricity(user_id);
  std::string campus = utils::json_str(input, "campus", utils::json_str(cur, "campus", "海甸校区"));
  std::string academy = utils::json_str(input, "academy", utils::json_str(cur, "academy", "南海书院"));
  std::string building = utils::trim(utils::json_str(input, "building", utils::json_str(cur, "building", "")));
  std::string room = utils::trim(utils::json_str(input, "room", utils::json_str(cur, "room", "")));
  std::string meter = utils::trim(utils::json_str(input, "meterAccount", utils::json_str(cur, "meterAccount", "")));
  double cur_daily = utils::json_double(cur, "dailyBurnKwh", 4.5);
  double daily = utils::json_double(input, "dailyBurnKwh", cur_daily);
  if (!std::isfinite(daily)) daily = std::isfinite(cur_daily) ? cur_daily : 4.5;
  daily = std::clamp(daily, 0.1, 50.0);

  int64_t now = utils::now_sec();
  bool has_bal = false;
  double bal = 0.0;
  int64_t rec_at = utils::json_i64(cur, "recordedAt", 0);

  if (input.is_object() && input.contains("balanceKwh") && !input["balanceKwh"].is_null()) {
    double parsed = utils::json_double(input, "balanceKwh", std::numeric_limits<double>::quiet_NaN());
    if (std::isfinite(parsed)) {
      bal = std::clamp(parsed, 0.0, 100000.0);
      has_bal = true;
      rec_at = now;
    }
  }
  if (!has_bal && cur.contains("balanceKwh") && !cur["balanceKwh"].is_null()) {
    double prev_bal = utils::json_double(cur, "balanceKwh", std::numeric_limits<double>::quiet_NaN());
    if (std::isfinite(prev_bal)) {
      bal = prev_bal;
      has_bal = true;
    }
  }

  const char* sql =
      "INSERT INTO dorm_electricity (user_id,campus,academy_or_zone,building_name,room_name,"
      "meter_account,balance_kwh,daily_burn_kwh,recorded_at,updated_at) "
      "VALUES (?1,?2,?3,?4,?5,?6,?7,?8,?9,?10) "
      "ON CONFLICT(user_id) DO UPDATE SET "
      "campus=excluded.campus, academy_or_zone=excluded.academy_or_zone, "
      "building_name=excluded.building_name, room_name=excluded.room_name, "
      "meter_account=excluded.meter_account, balance_kwh=excluded.balance_kwh, "
      "daily_burn_kwh=excluded.daily_burn_kwh, recorded_at=excluded.recorded_at, "
      "updated_at=excluded.updated_at;";
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) == SQLITE_OK) {
    sqlite3_bind_int64(st, 1, user_id);
    sqlite3_bind_text(st, 2, campus.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, academy.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, building.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 5, room.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 6, meter.c_str(), -1, SQLITE_TRANSIENT);
    if (has_bal) sqlite3_bind_double(st, 7, bal);
    else sqlite3_bind_null(st, 7);
    sqlite3_bind_double(st, 8, daily);
    sqlite3_bind_int64(st, 9, rec_at);
    sqlite3_bind_int64(st, 10, now);
    sqlite3_step(st);
    sqlite3_finalize(st);
  }
  return get_dorm_electricity(user_id);
}

nlohmann::json Database::add_dorm_elec_log(int64_t user_id,
                                           const std::string& kind,
                                           double amount_val,
                                           std::optional<double> balance_after,
                                           const std::string& note) {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  int64_t now = utils::now_sec();
  const char* sql =
      "INSERT INTO dorm_elec_logs (user_id,kind,amount_val,balance_after,note,logged_at) "
      "VALUES (?1,?2,?3,?4,?5,?6);";
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) == SQLITE_OK) {
    sqlite3_bind_int64(st, 1, user_id);
    sqlite3_bind_text(st, 2, kind.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(st, 3, amount_val);
    if (balance_after.has_value()) sqlite3_bind_double(st, 4, *balance_after);
    else sqlite3_bind_null(st, 4);
    sqlite3_bind_text(st, 5, note.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 6, now);
    sqlite3_step(st);
    sqlite3_finalize(st);
  }
  if (balance_after.has_value()) {
    update_dorm_electricity(user_id, {{"balanceKwh", *balance_after}});
  }
  return list_dorm_elec_logs(user_id, 20);
}

nlohmann::json Database::list_dorm_elec_logs(int64_t user_id, int limit) {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  nlohmann::json items = nlohmann::json::array();
  const char* sql =
      "SELECT id,kind,amount_val,balance_after,note,logged_at "
      "FROM dorm_elec_logs WHERE user_id=?1 ORDER BY logged_at DESC, id DESC LIMIT ?2;";
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) == SQLITE_OK) {
    sqlite3_bind_int64(st, 1, user_id);
    sqlite3_bind_int(st, 2, std::clamp(limit, 1, 100));
    while (sqlite3_step(st) == SQLITE_ROW) {
      nlohmann::json row = {
          {"id", sqlite3_column_int64(st, 0)},
          {"kind", col_text(st, 1)},
          {"amount", std::round(sqlite3_column_double(st, 2) * 100.0) / 100.0},
          {"balanceAfter", sqlite3_column_type(st, 3) == SQLITE_NULL
                               ? nlohmann::json(nullptr)
                               : nlohmann::json(std::round(sqlite3_column_double(st, 3) * 100.0) / 100.0)},
          {"note", col_text(st, 4)},
          {"at", sqlite3_column_int64(st, 5)},
          {"dateStr", utils::bj_datetime_str(sqlite3_column_int64(st, 5))},
      };
      items.push_back(std::move(row));
    }
    sqlite3_finalize(st);
  }
  return items;
}

// ---------------------------------------------------------------------------
// 海大公开通知缓存
// ---------------------------------------------------------------------------
void Database::upsert_notices(const std::vector<nlohmann::json>& items) {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  int64_t now = utils::now_sec();

  struct TxGuard {
    Database* self;
    bool active = false;
    ~TxGuard() {
      if (active) {
        try { self->exec_sql_script("ROLLBACK;"); } catch (...) {}
      }
    }
  };

  exec_sql_script("BEGIN TRANSACTION;");
  TxGuard tx{this, true};

  const char* sql =
      "INSERT INTO campus_notices (url_hash,title,url,source_name,dept,publish_date,tier,tags_json,fetched_at) "
      "VALUES (?1,?2,?3,?4,?5,?6,?7,?8,?9) "
      "ON CONFLICT(url_hash) DO UPDATE SET "
      "title=excluded.title, source_name=excluded.source_name, dept=excluded.dept, "
      "publish_date=excluded.publish_date, tier=excluded.tier, tags_json=excluded.tags_json, "
      "fetched_at=excluded.fetched_at;";
  StmtGuard sg;
  if (sqlite3_prepare_v2(db_, sql, -1, &sg.st, nullptr) != SQLITE_OK) {
    throw std::runtime_error(std::string("SQLite prepare failed in upsert_notices: ") +
                             sqlite3_errmsg(db_));
  }
  sqlite3_stmt* st = sg.st;
  for (const auto& it : items) {
    if (!it.is_object()) continue;
    std::string url = utils::ensure_utf8(utils::json_str(it, "url", ""));
    if (url.empty()) continue;
    std::string uhash = crypto::sha256_hex(url);
    std::string title = utils::ensure_utf8(utils::json_str(it, "title", ""));
    std::string src = utils::ensure_utf8(utils::json_str(it, "source", ""));
    std::string dept = utils::ensure_utf8(utils::json_str(it, "dept", ""));
    std::string date = utils::ensure_utf8(utils::json_str(it, "date", ""));
    std::string tier = utils::ensure_utf8(utils::json_str(it, "tier", "other"));
    std::string tags = "[]";
    if (it.contains("tags") && it["tags"].is_array()) {
      try {
        tags = it["tags"].dump();
      } catch (...) {
        tags = "[]";
      }
    }

    sqlite3_reset(st);
    sqlite3_clear_bindings(st);
    sqlite3_bind_text(st, 1, uhash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, url.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, src.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 5, dept.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 6, date.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 7, tier.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 8, tags.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 9, now);
    if (sqlite3_step(st) != SQLITE_DONE) {
      throw std::runtime_error(std::string("SQLite step failed in upsert_notices: ") +
                               sqlite3_errmsg(db_));
    }
  }
  sqlite3_finalize(sg.st);
  sg.st = nullptr;
  exec_sql_script("COMMIT;");
  tx.active = false;
}

nlohmann::json Database::list_notices(const std::string& tier, int limit) {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  nlohmann::json arr = nlohmann::json::array();
  std::string sql =
      "SELECT title,url,source_name,dept,publish_date,tier,tags_json "
      "FROM campus_notices ";
  if (!tier.empty() && tier != "all") {
    sql += "WHERE tier=?1 ";
  }
  // 台风/停水停电等出行安全通知在同一天内优先排前，其余按日期降序
  sql += "ORDER BY publish_date DESC, CASE WHEN tier='impact' THEN 0 WHEN tier='action' THEN 1 ELSE 2 END ASC, id DESC LIMIT ?2;";

  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &st, nullptr) == SQLITE_OK) {
    if (!tier.empty() && tier != "all") {
      sqlite3_bind_text(st, 1, tier.c_str(), -1, SQLITE_TRANSIENT);
    }
    sqlite3_bind_int(st, 2, std::clamp(limit, 1, 200));
    while (sqlite3_step(st) == SQLITE_ROW) {
      arr.push_back({
          {"title", col_text(st, 0)},
          {"url", col_text(st, 1)},
          {"source", col_text(st, 2)},
          {"dept", col_text(st, 3)},
          {"date", col_text(st, 4)},
          {"tier", col_text(st, 5)},
          {"tags", parse_json_or(col_text(st, 6), nlohmann::json::array())},
      });
    }
    sqlite3_finalize(st);
  }
  return arr;
}

int64_t Database::count_notices() {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  sqlite3_stmt* st = nullptr;
  int64_t cnt = 0;
  if (sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM campus_notices;", -1, &st, nullptr) == SQLITE_OK) {
    if (sqlite3_step(st) == SQLITE_ROW) cnt = sqlite3_column_int64(st, 0);
    sqlite3_finalize(st);
  }
  return cnt;
}

std::optional<nlohmann::json> Database::get_notice_detail_cached(const std::string& url) {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  std::string uhash = crypto::sha256_hex(url);
  const char* sql =
      "SELECT publish_date,dept,when_text,where_text,summary,detail_fetched,attachments_json "
      "FROM campus_notices WHERE url_hash=?1;";
  sqlite3_stmt* st = nullptr;
  std::optional<nlohmann::json> res;
  if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) == SQLITE_OK) {
    sqlite3_bind_text(st, 1, uhash.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(st) == SQLITE_ROW && sqlite3_column_int(st, 5) == 1) {
      res = nlohmann::json{
          {"ok", true},
          {"url", url},
          {"publish", col_text(st, 0)},
          {"dept", col_text(st, 1)},
          {"when", col_text(st, 2)},
          {"where", col_text(st, 3)},
          {"summary", col_text(st, 4)},
          {"attachments", parse_json_or(col_text(st, 6), nlohmann::json::array())},
      };
    }
    sqlite3_finalize(st);
  }
  return res;
}

void Database::save_notice_detail(const std::string& url,
                                  const std::string& publish_date,
                                  const std::string& dept,
                                  const std::string& when_text,
                                  const std::string& where_text,
                                  const std::string& summary,
                                  const nlohmann::json& attachments) {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  std::string uhash = crypto::sha256_hex(url);
  std::string att_str = attachments.is_array() ? attachments.dump() : "[]";
  const char* sql =
      "UPDATE campus_notices SET "
      "publish_date = CASE WHEN ?1 != '' THEN ?1 ELSE publish_date END, "
      "dept = CASE WHEN ?2 != '' THEN ?2 ELSE dept END, "
      "when_text=?3, where_text=?4, summary=?5, attachments_json=?6, detail_fetched=1 "
      "WHERE url_hash=?7;";
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) == SQLITE_OK) {
    sqlite3_bind_text(st, 1, publish_date.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, dept.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, when_text.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, where_text.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 5, summary.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 6, att_str.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 7, uhash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(st);
    sqlite3_finalize(st);
  }
}

int64_t Database::latest_notice_fetch_at() {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  sqlite3_stmt* st = nullptr;
  int64_t ts = 0;
  if (sqlite3_prepare_v2(db_, "SELECT COALESCE(MAX(fetched_at), 0) FROM campus_notices;", -1, &st, nullptr) == SQLITE_OK) {
    if (sqlite3_step(st) == SQLITE_ROW) ts = sqlite3_column_int64(st, 0);
    sqlite3_finalize(st);
  }
  return ts;
}

// ---------------------------------------------------------------------------
// 学校动态元数据与反馈/统计
// ---------------------------------------------------------------------------
nlohmann::json Database::get_school_meta(const std::string& key) {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  sqlite3_stmt* st = nullptr;
  nlohmann::json val = nullptr;
  if (sqlite3_prepare_v2(db_, "SELECT value_json FROM school_meta WHERE meta_key=?1;", -1, &st, nullptr) == SQLITE_OK) {
    sqlite3_bind_text(st, 1, key.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(st) == SQLITE_ROW) {
      val = parse_json_or(col_text(st, 0), nullptr);
    }
    sqlite3_finalize(st);
  }
  return val;
}

void Database::set_school_meta(const std::string& key, const nlohmann::json& value) {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  std::string dumped = value.dump();
  const char* sql =
      "INSERT INTO school_meta (meta_key,value_json,updated_at) VALUES (?1,?2,?3) "
      "ON CONFLICT(meta_key) DO UPDATE SET value_json=excluded.value_json, updated_at=excluded.updated_at;";
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) == SQLITE_OK) {
    sqlite3_bind_text(st, 1, key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, dumped.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 3, utils::now_sec());
    sqlite3_step(st);
    sqlite3_finalize(st);
  }
}

void Database::insert_feedback(std::optional<int64_t> user_id,
                               const std::string& app_version,
                               const std::string& client_os,
                               const std::string& description,
                               const std::string& scrubbed_log,
                               const std::string& ip_masked) {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  std::string safe_desc = utils::scrub_pii(description, 1000);
  // 逐行二次脱敏客户端附带的日志
  std::ostringstream safe_log;
  size_t lines_n = 0;
  for (const auto& line : utils::split(scrubbed_log, '\n')) {
    if (lines_n >= 200) break;
    safe_log << utils::scrub_pii(line, 320) << "\n";
    ++lines_n;
  }
  std::string final_log = safe_log.str();

  const char* sql =
      "INSERT INTO feedback_reports (user_id,app_version,client_os,description,scrubbed_log,ip_masked,created_at) "
      "VALUES (?1,?2,?3,?4,?5,?6,?7);";
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) == SQLITE_OK) {
    if (user_id.has_value()) sqlite3_bind_int64(st, 1, *user_id);
    else sqlite3_bind_null(st, 1);
    sqlite3_bind_text(st, 2, app_version.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, client_os.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, safe_desc.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 5, final_log.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 6, ip_masked.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 7, utils::now_sec());
    sqlite3_step(st);
    sqlite3_finalize(st);
  }
}

void Database::record_telemetry(const std::string& event_kind,
                                const std::string& detail_code,
                                const std::string& message,
                                const std::string& page_name,
                                const std::string& anon_day_id) {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  int64_t now = utils::now_sec();
  std::string day = utils::bj_date_ymd(now);
  std::string safe_msg = utils::scrub_pii(message, 180);

  const char* sql =
      "INSERT INTO telemetry_events (day_bj,event_kind,detail_code,message_scrub,page_name,anon_day_id,created_at) "
      "VALUES (?1,?2,?3,?4,?5,?6,?7);";
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) == SQLITE_OK) {
    sqlite3_bind_text(st, 1, day.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, event_kind.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, detail_code.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, safe_msg.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 5, page_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 6, anon_day_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 7, now);
    sqlite3_step(st);
    sqlite3_finalize(st);
  }
}

nlohmann::json Database::get_admin_stats(int days) {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  days = std::clamp(days, 1, 30);
  int64_t now = utils::now_sec();

  int64_t total_users = 0;
  int64_t active_sessions = 0;
  int64_t total_feedbacks = 0;

  auto scalar_i64 = [&](const char* q) -> int64_t {
    sqlite3_stmt* st = nullptr;
    int64_t v = 0;
    if (sqlite3_prepare_v2(db_, q, -1, &st, nullptr) == SQLITE_OK) {
      if (sqlite3_step(st) == SQLITE_ROW) v = sqlite3_column_int64(st, 0);
      sqlite3_finalize(st);
    }
    return v;
  };
  total_users = scalar_i64("SELECT COUNT(*) FROM users;");
  active_sessions = scalar_i64("SELECT COUNT(*) FROM sessions WHERE expires_at > strftime('%s','now');");
  total_feedbacks = scalar_i64("SELECT COUNT(*) FROM feedback_reports;");

  nlohmann::json daily = nlohmann::json::array();
  for (int i = 0; i < days; ++i) {
    std::string d = utils::bj_date_ymd(now - static_cast<int64_t>(i) * 86400);
    sqlite3_stmt* st = nullptr;
    int64_t dau = 0, errs = 0;
    if (sqlite3_prepare_v2(
            db_,
            "SELECT COUNT(DISTINCT CASE WHEN anon_day_id != '' THEN anon_day_id ELSE NULL END), "
            "SUM(CASE WHEN event_kind != 'dau_ping' THEN 1 ELSE 0 END) "
            "FROM telemetry_events WHERE day_bj=?1;",
            -1, &st, nullptr) == SQLITE_OK) {
      sqlite3_bind_text(st, 1, d.c_str(), -1, SQLITE_TRANSIENT);
      if (sqlite3_step(st) == SQLITE_ROW) {
        dau = sqlite3_column_int64(st, 0);
        errs = sqlite3_column_int64(st, 1);
      }
      sqlite3_finalize(st);
    }
    daily.push_back({{"date", d}, {"activeDevices", dau}, {"errors", errs}});
  }

  nlohmann::json recent_fb = nlohmann::json::array();
  sqlite3_stmt* fst = nullptr;
  if (sqlite3_prepare_v2(
          db_,
          "SELECT id, app_version, client_os, description, created_at "
          "FROM feedback_reports ORDER BY id DESC LIMIT 15;",
          -1, &fst, nullptr) == SQLITE_OK) {
    while (sqlite3_step(fst) == SQLITE_ROW) {
      recent_fb.push_back({
          {"id", sqlite3_column_int64(fst, 0)},
          {"version", col_text(fst, 1)},
          {"os", col_text(fst, 2)},
          {"desc", col_text(fst, 3)},
          {"at", utils::bj_datetime_str(sqlite3_column_int64(fst, 4))},
      });
    }
    sqlite3_finalize(fst);
  }

  return {
      {"ok", true},
      {"totalUsers", total_users},
      {"activeSessions", active_sessions},
      {"totalFeedbacks", total_feedbacks},
      {"daily", daily},
      {"recentFeedbacks", recent_fb},
  };
}

}  // namespace hnu::db
