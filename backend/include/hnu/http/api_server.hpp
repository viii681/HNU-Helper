#pragma once

#include "hnu/adapters/chaoxing_client.hpp"
#include "hnu/adapters/mooc_client.hpp"
#include "hnu/adapters/notice_crawler.hpp"
#include "hnu/adapters/portal_client.hpp"
#include "hnu/adapters/qiangzhi_client.hpp"
#include "hnu/app/config.hpp"
#include "hnu/db/database.hpp"
#include "hnu/services/scan_manager.hpp"

#include <httplib.h>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>

namespace hnu::http {

class ApiServer {
 public:
  ApiServer(const app::AppConfig& cfg, db::Database& db);
  ~ApiServer();

  bool listen_blocking();
  void stop();

 private:
  // 安全与中间件辅助
  std::string client_ip_of(const httplib::Request& req) const;
  bool check_rate_limit(const std::string& bucket_key, int max_per_minute);
  std::string extract_session_token(const httplib::Request& req) const;
  std::optional<db::UserRecord> require_user(const httplib::Request& req, httplib::Response& res);
  void send_json(httplib::Response& res, const nlohmann::json& body, int status = 200) const;
  nlohmann::json parse_body(const httplib::Request& req) const;

  void register_routes();
  nlohmann::json build_user_bundle(const db::UserRecord& user);
  std::string save_academic_snapshots_safely(int64_t user_id, const nlohmann::json& sync_data);
  void update_sync_progress(const std::string& sync_id,
                            int percent,
                            int step,
                            int total_steps,
                            const std::string& stage,
                            const std::string& detail,
                            const std::string& status = "running",
                            int64_t owner_user_id = 0,
                            const std::string& owner_ip = "");
  void start_keepalive_worker();
  void stop_keepalive_worker();
  void trigger_background_notice_refresh();
  bool load_jwxt_cred_for_user(int64_t user_id,
                               const std::string& fallback_sid,
                               httplib::Response& res,
                               std::string& out_acc,
                               std::string& out_pwd,
                               nlohmann::json& out_cookies,
                               std::string& out_state);
  void handle_jw_error_response(int64_t user_id,
                                const adapters::JwError& e,
                                httplib::Response& res);
  static void merge_portal_castgc_unique(nlohmann::json& target_cookies,
                                         const nlohmann::json& portal_cookies);

  app::AppConfig cfg_;
  db::Database& db_;
  adapters::QiangzhiClient qz_client_;
  adapters::PortalClient portal_client_;
  adapters::HnuNoticeCrawler notice_crawler_;
  adapters::ChaoxingClient cx_client_;
  adapters::MoocClient mooc_client_;
  services::ScanManager scan_mgr_;

  httplib::Server svr_;

  struct RateWindow {
    int64_t minute_slot = 0;
    int count = 0;
  };
  std::mutex rate_mu_;
  std::unordered_map<std::string, RateWindow> rate_windows_;

  struct SyncProgressEntry {
    int64_t owner_user_id = 0;  // >0 表示绑定登录用户；0 表示登录前/公开通知，按 owner_ip + 高熵 syncId 隔离
    std::string owner_ip;
    int percent = 0;
    int step = 0;
    int total_steps = 1;
    std::string stage;
    std::string detail;
    std::string status = "running";  // running / done / error
    int64_t started_at_ms = 0;
    int64_t updated_at_ms = 0;
  };
  std::mutex sync_progress_mu_;
  std::unordered_map<std::string, SyncProgressEntry> sync_progress_;

  std::mutex notice_refresh_mu_;
  std::atomic<bool> notice_refresh_inflight_{false};
  std::atomic<int64_t> last_notice_attempt_at_{0};
  std::mutex notice_worker_mu_;
  std::condition_variable notice_worker_cv_;
  int active_notice_workers_ = 0;

  std::mutex stop_mu_;
  std::atomic<bool> stopping_{false};
  std::atomic<bool> keepalive_stop_{false};
  std::mutex keepalive_mu_;
  std::condition_variable keepalive_cv_;
  std::thread keepalive_thread_;
};

}  // namespace hnu::http
