#pragma once

#include <cstdint>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;

namespace hnu::db {

struct UserRecord {
  int64_t id = 0;
  std::string student_id;
  std::string name;
  std::string gender;
  std::string college;
  std::string academy;
  std::string major;
  std::string direction;
  std::string class_name;
  std::string campus = "海甸校区";
  std::string grade;
  std::string edu_level = "本科";
  std::string edu_duration = "4";
  std::string status = "在籍";
  std::string enrolled_at;
  std::string graduate_at;
  std::string ics_token;
  std::string role = "student";
  int64_t last_login_at = 0;

  nlohmann::json to_profile_json() const;
};

struct VaultEntry {
  bool exists = false;
  std::string provider;
  std::string account_masked;
  std::string state = "none";  // none / ok / expired / broken
  std::string last_error;
  int64_t updated_at = 0;
  nlohmann::json secret;       // 解密后的 JSON 对象 {account, password, cookies, extra...}
};

struct SnapshotRecord {
  bool found = false;
  int64_t synced_at = 0;
  nlohmann::json payload;
};

class Database {
 public:
  Database(const std::string& db_path,
           const std::vector<uint8_t>& master_key,
           const std::string& schema_sql_path = "",
           const std::string& seed_sql_path = "");
  ~Database();

  Database(const Database&) = delete;
  Database& operator=(const Database&) = delete;

  // 用户与会话管理
  UserRecord upsert_user(const std::string& student_id, const nlohmann::json& profile = {});
  std::optional<UserRecord> update_user_profile_manual(int64_t user_id,
                                                       const std::string& academy,
                                                       const std::string& campus = "");
  std::optional<UserRecord> get_user_by_id(int64_t user_id);
  std::optional<UserRecord> get_user_by_student_id(const std::string& student_id);
  std::optional<UserRecord> get_user_by_ics_token(const std::string& ics_token);
  std::vector<int64_t> list_active_jwxt_user_ids(int max_age_days = 30);
  std::string rotate_ics_token(int64_t user_id);

  std::string create_session(int64_t user_id,
                             const std::string& device_label,
                             const std::string& ip_masked,
                             int ttl_days = 365);
  std::optional<UserRecord> authenticate_session(const std::string& raw_token, int slide_ttl_days = 365);
  void revoke_session(const std::string& raw_token);
  void purge_expired_sessions();

  // AES-256-GCM 凭据保险箱
  bool save_credential(int64_t user_id,
                       const std::string& provider,
                       const std::string& raw_account,
                       const nlohmann::json& secret_obj,
                       const std::string& state = "ok",
                       const std::string& last_error = "");
  VaultEntry load_credential(int64_t user_id, const std::string& provider);
  void update_credential_state(int64_t user_id,
                               const std::string& provider,
                               const std::string& state,
                               const std::string& last_error = "");
  void delete_credential(int64_t user_id, const std::string& provider);
  nlohmann::json list_credential_briefs(int64_t user_id);

  // 数据快照存储（教务课表/成绩/考试/培养方案/学习通/MOOC/门户）
  void save_snapshot(int64_t user_id,
                     const std::string& kind,
                     const std::string& semester_id,
                     const nlohmann::json& payload);
  SnapshotRecord get_snapshot(int64_t user_id,
                              const std::string& kind,
                              const std::string& semester_id = "current");
  void delete_snapshots(int64_t user_id, const std::string& kind);
  void clear_all_user_cache(int64_t user_id);

  // 用户跨端偏好设置（作息、隐藏课程/单节、自定义加课、调色、已交标记、阈值）
  nlohmann::json get_preferences(int64_t user_id);
  void save_preferences(int64_t user_id, const nlohmann::json& patch);

  // 海南大学宿舍电量管理与台账
  nlohmann::json get_dorm_electricity(int64_t user_id);
  nlohmann::json update_dorm_electricity(int64_t user_id, const nlohmann::json& input);
  nlohmann::json add_dorm_elec_log(int64_t user_id,
                                   const std::string& kind,
                                   double amount_val,
                                   std::optional<double> balance_after,
                                   const std::string& note);
  nlohmann::json list_dorm_elec_logs(int64_t user_id, int limit = 20);

  // 海南大学公开通知公告缓存
  void upsert_notices(const std::vector<nlohmann::json>& items);
  nlohmann::json list_notices(const std::string& tier = "", int limit = 60);
  int64_t count_notices();
  std::optional<nlohmann::json> get_notice_detail_cached(const std::string& url);
  void save_notice_detail(const std::string& url,
                          const std::string& publish_date,
                          const std::string& dept,
                          const std::string& when_text,
                          const std::string& where_text,
                          const std::string& summary,
                          const nlohmann::json& attachments = nlohmann::json::array());
  int64_t latest_notice_fetch_at();

  // 学校动态配置元数据
  nlohmann::json get_school_meta(const std::string& key);
  void set_school_meta(const std::string& key, const nlohmann::json& value);

  // 问题反馈与匿名遥测统计
  void insert_feedback(std::optional<int64_t> user_id,
                       const std::string& app_version,
                       const std::string& client_os,
                       const std::string& description,
                       const std::string& scrubbed_log,
                       const std::string& ip_masked);
  void record_telemetry(const std::string& event_kind,
                        const std::string& detail_code,
                        const std::string& message,
                        const std::string& page_name,
                        const std::string& anon_day_id = "");
  nlohmann::json get_admin_stats(int days = 7);

 private:
  void exec_sql_script(const std::string& sql);
  void ensure_builtin_defaults();
  UserRecord read_user_row(void* stmt_ptr);

  sqlite3* db_ = nullptr;
  mutable std::recursive_mutex mu_;
  std::vector<uint8_t> vault_key_;
};

}  // namespace hnu::db
