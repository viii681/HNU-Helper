#include "hnu/http/api_server.hpp"
#include "hnu/adapters/sandbox_provider.hpp"
#include "hnu/crypto/crypto.hpp"
#include "hnu/services/calendar_ics.hpp"
#include "hnu/services/credit_analyzer.hpp"
#include "hnu/utils/json_safe.hpp"
#include "hnu/utils/logger.hpp"
#include "hnu/utils/strings_and_time.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <regex>

namespace hnu::http {

namespace {

bool is_loopback_addr(std::string_view addr) {
  std::string a = utils::to_lower(utils::trim(addr));
  return a == "127.0.0.1" || a == "::1" || a == "::ffff:127.0.0.1" || a == "localhost";
}

}  // namespace

ApiServer::ApiServer(const app::AppConfig& cfg, db::Database& db)
    : cfg_(cfg),
      db_(db),
      qz_client_(cfg.hnu.jwxt_base_url, cfg.hnu.polite_delay_ms, cfg.hnu.enable_demo_account, cfg.hnu.cas_login_url),
      portal_client_(cfg.hnu.cas_login_url, cfg.hnu.ehall_base_url, cfg.hnu.allowed_ticket_domains, cfg.hnu.enable_demo_account),
      notice_crawler_(cfg.hnu.allowed_notice_domains),
      cx_client_(cfg.hnu.enable_demo_account),
      mooc_client_(cfg.hnu.enable_demo_account),
      scan_mgr_(db, cx_client_) {
  size_t pool_n = static_cast<size_t>(std::clamp(cfg_.server.threads, 2, 64));
  svr_.new_task_queue = [pool_n] { return new httplib::ThreadPool(pool_n); };
  svr_.set_payload_max_length(2 * 1024 * 1024);  // 限制请求体最大 2MB 防 DoS
  svr_.set_read_timeout(60, 0);
  svr_.set_write_timeout(60, 0);
  register_routes();
  start_keepalive_worker();
}

ApiServer::~ApiServer() {
  stop();
}

void ApiServer::update_sync_progress(const std::string& sync_id,
                                     int percent,
                                     int step,
                                     int total_steps,
                                     const std::string& stage,
                                     const std::string& detail,
                                     const std::string& status,
                                     int64_t owner_user_id,
                                     const std::string& owner_ip) {
  if (sync_id.size() < 8 || sync_id.size() > 96) return;
  int64_t now_ms = utils::now_ms();
  std::lock_guard<std::mutex> lk(sync_progress_mu_);
  if (sync_progress_.size() > 256) {
    for (auto it = sync_progress_.begin(); it != sync_progress_.end();) {
      if (now_ms - it->second.updated_at_ms > 300000) it = sync_progress_.erase(it);
      else ++it;
    }
  }
  auto it = sync_progress_.find(sync_id);
  if (it != sync_progress_.end()) {
    // 防止不同用户或来源意外覆写已存在的 sync_id 进度条目
    if (it->second.owner_user_id > 0 && it->second.owner_user_id != owner_user_id) return;
    if (it->second.owner_user_id == 0 && !it->second.owner_ip.empty() && !owner_ip.empty() &&
        it->second.owner_ip != owner_ip) {
      return;
    }
  }
  auto& e = sync_progress_[sync_id];
  if (e.started_at_ms == 0) {
    e.started_at_ms = now_ms;
    e.owner_user_id = owner_user_id;
    e.owner_ip = owner_ip;
  }
  e.percent = std::clamp(percent, 0, 100);
  e.step = step;
  e.total_steps = std::max(1, total_steps);
  e.stage = stage;
  e.detail = detail;
  e.status = status;
  e.updated_at_ms = now_ms;
}

void ApiServer::start_keepalive_worker() {
  keepalive_stop_.store(false);
  keepalive_thread_ = std::thread([this]() {
    // 后台保活线程：每 8 分钟对近期活跃用户的教务会话执行一次轻量保活/无感续期，大幅延长登录保留时间
    while (!keepalive_stop_.load()) {
      {
        std::unique_lock<std::mutex> lk(keepalive_mu_);
        keepalive_cv_.wait_for(lk, std::chrono::minutes(8), [this]() { return keepalive_stop_.load(); });
      }
      if (keepalive_stop_.load()) break;

      try {
        auto uids = db_.list_active_jwxt_user_ids(30);
        for (int64_t uid : uids) {
          if (keepalive_stop_.load()) break;
          auto cred = db_.load_credential(uid, "jwxt");
          if (!cred.exists || cred.state == "broken") continue;
          std::string acc = utils::json_str(cred.secret, "account", "");
          std::string pwd = utils::json_str(cred.secret, "password", "");
          nlohmann::json cks = (cred.secret.is_object() && cred.secret.contains("cookies") &&
                                cred.secret["cookies"].is_array())
                                   ? cred.secret["cookies"]
                                   : nlohmann::json::array();
          if (acc.empty()) continue;

          // 若门户凭据里保存有同账号长效 CASTGC，自动去重合并辅助无感续期
          auto pcred = db_.load_credential(uid, "portal");
          if (pcred.exists && pcred.state != "broken" && pcred.secret.is_object() &&
              pcred.secret.contains("cookies")) {
            std::string p_acc = utils::json_str(pcred.secret, "account", "");
            if (p_acc.empty() || p_acc == acc) {
              merge_portal_castgc_unique(cks, pcred.secret["cookies"]);
            }
          }

          nlohmann::json updated_cks;
          if (qz_client_.keepalive_session(acc, pwd, cks, &updated_cks)) {
            if (!updated_cks.empty()) {
              db_.save_credential(uid, "jwxt", acc,
                                  {{"account", acc}, {"password", pwd}, {"cookies", updated_cks}},
                                  "ok", "");
            } else if (cred.state == "expired") {
              db_.update_credential_state(uid, "jwxt", "ok", "");
            }
          }
        }
      } catch (...) {
      }
    }
  });
}

void ApiServer::stop_keepalive_worker() {
  keepalive_stop_.store(true);
  keepalive_cv_.notify_all();
  if (keepalive_thread_.joinable()) {
    keepalive_thread_.join();
  }
}

void ApiServer::merge_portal_castgc_unique(nlohmann::json& target_cookies,
                                           const nlohmann::json& portal_cookies) {
  if (!portal_cookies.is_array()) return;
  if (!target_cookies.is_array()) target_cookies = nlohmann::json::array();
  nlohmann::json castgc_item;
  for (const auto& pc : portal_cookies) {
    if (pc.is_object() && utils::json_str(pc, "name", "") == "CASTGC" &&
        !utils::json_str(pc, "value", "").empty()) {
      castgc_item = pc;
    }
  }
  if (!castgc_item.is_object()) return;
  nlohmann::json dedup = nlohmann::json::array();
  for (const auto& c : target_cookies) {
    if (c.is_object() && utils::json_str(c, "name", "") == "CASTGC") continue;
    dedup.push_back(c);
  }
  dedup.push_back(std::move(castgc_item));
  target_cookies = std::move(dedup);
}

bool ApiServer::load_jwxt_cred_for_user(int64_t user_id,
                                        const std::string& fallback_sid,
                                        httplib::Response& res,
                                        std::string& out_acc,
                                        std::string& out_pwd,
                                        nlohmann::json& out_cookies,
                                        std::string& out_state) {
  auto cred = db_.load_credential(user_id, "jwxt");
  if (!cred.exists || cred.state == "broken") {
    send_json(res, {{"ok", false},
                    {"error", cred.last_error.empty() ? "尚未保存教务凭据，请重新绑定" : cred.last_error},
                    {"needBind", true},
                    {"credState", cred.state}});
    return false;
  }
  out_acc = utils::json_str(cred.secret, "account", fallback_sid);
  out_pwd = utils::json_str(cred.secret, "password", "");
  out_cookies = (cred.secret.is_object() && cred.secret.contains("cookies") &&
                 cred.secret["cookies"].is_array())
                    ? cred.secret["cookies"]
                    : nlohmann::json::array();
  out_state = cred.state;

  auto p_cred = db_.load_credential(user_id, "portal");
  if (p_cred.exists && p_cred.state != "broken" && p_cred.secret.is_object() &&
      p_cred.secret.contains("cookies")) {
    std::string p_acc = utils::json_str(p_cred.secret, "account", "");
    if (p_acc.empty() || p_acc == out_acc) {
      merge_portal_castgc_unique(out_cookies, p_cred.secret["cookies"]);
    }
  }
  return true;
}

void ApiServer::handle_jw_error_response(int64_t user_id,
                                         const adapters::JwError& e,
                                         httplib::Response& res) {
  bool need_cap = (e.kind() == "expired" || e.kind() == "captcha");
  if (need_cap) {
    db_.update_credential_state(user_id, "jwxt", "expired", e.what());
  }
  nlohmann::json err_res = {
      {"ok", false},
      {"error", e.what()},
      {"kind", e.kind()},
      {"needCaptcha", need_cap},
      {"authFail", e.kind() == "auth"},
  };
  if (need_cap) {
    err_res["captcha"] = qz_client_.fetch_login_captcha();
  }
  send_json(res, err_res);
}

void ApiServer::trigger_background_notice_refresh() {
  if (stopping_.load(std::memory_order_acquire)) return;
  bool expected = false;
  if (!notice_refresh_inflight_.compare_exchange_strong(expected, true)) return;
  last_notice_attempt_at_.store(utils::now_sec());

  {
    std::lock_guard<std::mutex> lk(notice_worker_mu_);
    if (stopping_.load(std::memory_order_acquire)) {
      notice_refresh_inflight_.store(false);
      return;
    }
    ++active_notice_workers_;
  }

  std::thread([this]() {
    struct Guard {
      ApiServer* self;
      ~Guard() {
        self->notice_refresh_inflight_.store(false);
        std::lock_guard<std::mutex> lk(self->notice_worker_mu_);
        --self->active_notice_workers_;
        self->notice_worker_cv_.notify_all();
      }
    } guard{this};

    try {
      if (stopping_.load(std::memory_order_acquire)) return;
      std::lock_guard<std::mutex> lk(notice_refresh_mu_);
      if (stopping_.load(std::memory_order_acquire)) return;
      auto live = notice_crawler_.fetch_all_sources();
      if (!stopping_.load(std::memory_order_acquire) &&
          live.contains("items") && live["items"].is_array() && !live["items"].empty()) {
        std::vector<nlohmann::json> vec = live["items"].get<std::vector<nlohmann::json>>();
        db_.upsert_notices(vec);
      }
    } catch (...) {
    }
  }).detach();
}

std::string ApiServer::client_ip_of(const httplib::Request& req) const {
  if (cfg_.server.trust_proxy_headers && is_loopback_addr(req.remote_addr)) {
    if (req.has_header("X-Real-IP")) {
      std::string ip = utils::trim(req.get_header_value("X-Real-IP"));
      if (!ip.empty()) return ip;
    }
    if (req.has_header("X-Forwarded-For")) {
      auto parts = utils::split(req.get_header_value("X-Forwarded-For"), ',');
      if (!parts.empty()) {
        std::string ip = utils::trim(parts[0]);
        if (!ip.empty()) return ip;
      }
    }
  }
  return req.remote_addr;
}

bool ApiServer::check_rate_limit(const std::string& bucket_key, int max_per_minute) {
  if (max_per_minute <= 0) return true;
  int64_t slot = utils::now_sec() / 60;
  std::lock_guard<std::mutex> lk(rate_mu_);
  if (rate_windows_.size() > 4096) {
    for (auto it = rate_windows_.begin(); it != rate_windows_.end();) {
      if (it->second.minute_slot < slot) it = rate_windows_.erase(it);
      else ++it;
    }
  }
  auto& w = rate_windows_[bucket_key];
  if (w.minute_slot != slot) {
    w.minute_slot = slot;
    w.count = 0;
  }
  ++w.count;
  return w.count <= max_per_minute;
}

std::string ApiServer::extract_session_token(const httplib::Request& req) const {
  if (req.has_header("Authorization")) {
    std::string auth = utils::trim(req.get_header_value("Authorization"));
    if (utils::starts_with(auth, "Bearer ") || utils::starts_with(auth, "bearer ")) {
      return utils::trim(auth.substr(7));
    }
  }
  if (req.has_header("X-Session-Token")) {
    return utils::trim(req.get_header_value("X-Session-Token"));
  }
  if (req.has_header("Cookie")) {
    std::string ck = req.get_header_value("Cookie");
    for (const auto& part : utils::split(ck, ';')) {
      std::string kv = utils::trim(part);
      if (utils::starts_with(kv, "hnu_session=")) {
        return utils::trim(kv.substr(12));
      }
    }
  }
  return "";
}

std::optional<db::UserRecord> ApiServer::require_user(const httplib::Request& req,
                                                      httplib::Response& res) {
  std::string tok = extract_session_token(req);
  if (tok.empty()) {
    send_json(res, {{"ok", false}, {"error", "尚未登录或绑定教务账号"}, {"needBind", true}}, 401);
    return std::nullopt;
  }
  auto user = db_.authenticate_session(tok, cfg_.security.session_ttl_days);
  if (!user) {
    send_json(res, {{"ok", false}, {"error", "会话已过期，请重新登录"}, {"needBind", true}}, 401);
    return std::nullopt;
  }
  return user;
}

void ApiServer::send_json(httplib::Response& res, const nlohmann::json& body, int status) const {
  res.status = status;
  res.set_header("Cache-Control", "no-store");
  res.set_header("X-Content-Type-Options", "nosniff");
  if (!cfg_.server.cors_allowed_origin.empty()) {
    res.set_header("Access-Control-Allow-Origin", cfg_.server.cors_allowed_origin);
    res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization, X-Session-Token, x-hnu-key");
    res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    if (cfg_.server.cors_allowed_origin != "*") {
      res.set_header("Access-Control-Allow-Credentials", "true");
    }
  }
  res.set_content(body.dump(), "application/json; charset=utf-8");
}

nlohmann::json ApiServer::parse_body(const httplib::Request& req) const {
  if (req.body.empty()) return nlohmann::json::object();
  auto j = nlohmann::json::parse(req.body, nullptr, false);
  return j.is_object() ? j : nlohmann::json::object();
}

nlohmann::json ApiServer::build_user_bundle(const db::UserRecord& user) {
  auto tt_snap = db_.get_snapshot(user.id, "timetable", "current");
  auto gr_snap = db_.get_snapshot(user.id, "grades", "current");
  auto ex_snap = db_.get_snapshot(user.id, "exams", "current");
  auto pl_snap = db_.get_snapshot(user.id, "plan", "current");
  auto sem_snap = db_.get_snapshot(user.id, "semesters", "current");
  auto cx_snap = db_.get_snapshot(user.id, "cx_todo", "current");
  auto mooc_snap = db_.get_snapshot(user.id, "mooc_todo", "current");
  auto pcard_snap = db_.get_snapshot(user.id, "portal_card", "current");
  auto papps_snap = db_.get_snapshot(user.id, "portal_apps", "current");

  auto jw_cred = db_.load_credential(user.id, "jwxt");
  auto prof_json = user.to_profile_json();
  auto prefs_json = db_.get_preferences(user.id);
  nlohmann::json credits = services::CreditAnalyzer::analyze(
      pl_snap.found ? pl_snap.payload : nlohmann::json::object(),
      gr_snap.found ? gr_snap.payload : nlohmann::json::object(),
      tt_snap.found ? tt_snap.payload : nlohmann::json::object(),
      prof_json,
      prefs_json);

  return {
      {"profile", prof_json},
      {"icsToken", user.ics_token},
      {"credState", jw_cred.exists ? jw_cred.state : "none"},
      {"credBriefs", db_.list_credential_briefs(user.id)},
      {"preferences", prefs_json},
      {"dormElec", db_.get_dorm_electricity(user.id)},
      {"syncedAt", tt_snap.synced_at},
      {"timetable", tt_snap.found ? tt_snap.payload : nlohmann::json(nullptr)},
      {"grades", gr_snap.found ? gr_snap.payload : nlohmann::json(nullptr)},
      {"exams", ex_snap.found ? ex_snap.payload : nlohmann::json(nullptr)},
      {"semesters", sem_snap.found ? sem_snap.payload : nlohmann::json::array()},
      {"plan", pl_snap.found ? pl_snap.payload : nlohmann::json(nullptr)},
      {"credits", credits},
      {"cxTodo", cx_snap.found ? cx_snap.payload : nlohmann::json(nullptr)},
      {"moocTodo", mooc_snap.found ? mooc_snap.payload : nlohmann::json(nullptr)},
      {"portalCard", pcard_snap.found ? pcard_snap.payload : nlohmann::json(nullptr)},
      {"portalApps", papps_snap.found ? papps_snap.payload : nlohmann::json(nullptr)},
  };
}

std::string ApiServer::save_academic_snapshots_safely(int64_t user_id, const nlohmann::json& sync_data) {
  std::string tt_warn = sync_data.value("timetableError", "");

  // 1. 课表快照红线保护：绝不用空课表（0 门课）覆盖库里已有的非空有效课表！
  if (!sync_data.contains("timetableError") && sync_data.contains("courses") && sync_data["courses"].is_array()) {
    bool new_empty = sync_data["courses"].empty();
    auto prev_tt = db_.get_snapshot(user_id, "timetable", "current");
    bool prev_has_courses = prev_tt.found && prev_tt.payload.is_object() &&
                            prev_tt.payload.contains("courses") &&
                            prev_tt.payload["courses"].is_array() &&
                            !prev_tt.payload["courses"].empty();
    if (new_empty && prev_has_courses) {
      tt_warn = "教务系统本次返回空课表，已自动保留上次同步的有效课表缓存";
      HNU_LOG_WARN("jwxt", "Protected existing non-empty timetable snapshot from empty overwrite");
    } else {
      nlohmann::json tt_obj = {
          {"semesterId", sync_data.value("semesterId", "")},
          {"semester", sync_data.value("semester", "")},
          {"unitCount", sync_data.value("unitCount", 11)},
          {"totalWeeks", sync_data.value("totalWeeks", 20)},
          {"courses", sync_data["courses"]},
      };
      db_.save_snapshot(user_id, "timetable", "current", tt_obj);
    }
  }

  // 2. 成绩快照红线保护：绝不用空成绩覆盖已有成绩
  if (sync_data.contains("grades") && sync_data["grades"].is_object() && !sync_data["grades"].contains("error")) {
    const auto& gr = sync_data["grades"];
    bool new_empty = !gr.contains("courses") || !gr["courses"].is_array() || gr["courses"].empty();
    auto prev_gr = db_.get_snapshot(user_id, "grades", "current");
    bool prev_has = prev_gr.found && prev_gr.payload.is_object() &&
                    prev_gr.payload.contains("courses") &&
                    prev_gr.payload["courses"].is_array() &&
                    !prev_gr.payload["courses"].empty();
    if (!(new_empty && prev_has)) {
      db_.save_snapshot(user_id, "grades", "current", gr);
    }
  }

  // 3. 考场快照保护：绝不用空考场覆盖同学期已有考场
  if (sync_data.contains("exams") && sync_data["exams"].is_object() && !sync_data["exams"].contains("error")) {
    const auto& ex = sync_data["exams"];
    bool new_empty = !ex.contains("exams") || !ex["exams"].is_array() || ex["exams"].empty();
    auto prev_ex = db_.get_snapshot(user_id, "exams", "current");
    bool prev_has = prev_ex.found && prev_ex.payload.is_object() &&
                    prev_ex.payload.contains("exams") &&
                    prev_ex.payload["exams"].is_array() &&
                    !prev_ex.payload["exams"].empty();
    if (!(new_empty && prev_has)) {
      db_.save_snapshot(user_id, "exams", "current", ex);
    }
  }

  // 4. 可选学期列表
  if (sync_data.contains("semesters") && sync_data["semesters"].is_array() && !sync_data["semesters"].empty()) {
    db_.save_snapshot(user_id, "semesters", "current", sync_data["semesters"]);
  }

  // 5. 培养方案快照红线保护：绝不用 found=false 或空课程覆盖已有的完整培养方案！
  if (sync_data.contains("plan") && sync_data["plan"].is_object() && !sync_data["plan"].contains("error")) {
    const auto& pl = sync_data["plan"];
    bool new_empty = !pl.value("found", false) || !pl.contains("courses") ||
                     !pl["courses"].is_array() || pl["courses"].empty();
    auto prev_pl = db_.get_snapshot(user_id, "plan", "current");
    bool prev_has = prev_pl.found && prev_pl.payload.is_object() &&
                    prev_pl.payload.value("found", false) &&
                    prev_pl.payload.contains("courses") &&
                    prev_pl.payload["courses"].is_array() &&
                    !prev_pl.payload["courses"].empty();
    if (new_empty && prev_has) {
      HNU_LOG_WARN("jwxt", "Protected existing non-empty plan snapshot from empty overwrite");
    } else {
      db_.save_snapshot(user_id, "plan", "current", pl);
    }
  }

  return tt_warn;
}

void ApiServer::register_routes() {
  // 全局未捕获异常兜底：禁止向客户端泄漏 EXCEPTION_WHAT 内部细节，统一返回脱敏 JSON 500
  svr_.set_exception_handler([this](const httplib::Request& req, httplib::Response& res, std::exception_ptr ep) {
    std::string what_msg = "unknown exception";
    try {
      if (ep) std::rethrow_exception(ep);
    } catch (const std::exception& ex) {
      what_msg = ex.what();
    } catch (...) {
    }
    HNU_LOG_ERROR("http", "Unhandled exception on " + req.method + " " + req.path + ": " + what_msg);
    res.headers.erase("EXCEPTION_WHAT");
    send_json(res, {{"ok", false}, {"error", "请求参数格式无效或服务内部处理异常"}}, 500);
  });

  // 全接口速率限制（覆盖 /api/v1/notice、/api/v1/notice/detail 等公开接口，放行健康检查与高频进度条轮询）
  svr_.set_pre_routing_handler([this](const httplib::Request& req, httplib::Response& res) {
    if (req.method != "OPTIONS" && utils::starts_with(req.path, "/api/") &&
        req.path != "/api/v1/health" && req.path != "/api/v1/sync/progress" &&
        cfg_.security.rate_limit_api_per_min > 0) {
      std::string ip = client_ip_of(req);
      if (!check_rate_limit("api:" + ip, cfg_.security.rate_limit_api_per_min)) {
        send_json(res, {{"ok", false}, {"error", "请求过于频繁，请稍后再试"}}, 429);
        return httplib::Server::HandlerResponse::Handled;
      }
    }
    return httplib::Server::HandlerResponse::Unhandled;
  });

  // CORS 预检统一放行
  svr_.Options(R"(/api/.*)", [this](const httplib::Request&, httplib::Response& res) {
    if (!cfg_.server.cors_allowed_origin.empty()) {
      res.set_header("Access-Control-Allow-Origin", cfg_.server.cors_allowed_origin);
      res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization, X-Session-Token, x-hnu-key");
      res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
      if (cfg_.server.cors_allowed_origin != "*") {
        res.set_header("Access-Control-Allow-Credentials", "true");
      }
    }
    res.status = 204;
  });

  // 1. 健康探测
  svr_.Get("/api/v1/health", [this](const httplib::Request&, httplib::Response& res) {
    send_json(res, {
        {"ok", true},
        {"service", "HnuTong"},
        {"school", "海南大学"},
        {"version", "1.0.0"},
        {"serverTime", utils::now_sec()},
    });
  });

  // 1b. 实时数据同步 / 上传更新进度查询（严格校验用户归属或同源高熵 syncId，防止跨用户窥探）
  svr_.Get("/api/v1/sync/progress", [this](const httplib::Request& req, httplib::Response& res) {
    std::string sid = req.has_param("id") ? utils::trim(req.get_param_value("id")) : "";
    if (sid.size() < 8 || sid.size() > 96) {
      send_json(res, {{"ok", true}, {"found", false}});
      return;
    }
    int64_t req_uid = 0;
    std::string tok = extract_session_token(req);
    if (!tok.empty()) {
      if (auto u = db_.authenticate_session(tok, cfg_.security.session_ttl_days)) {
        req_uid = u->id;
      }
    }
    std::string req_ip = client_ip_of(req);

    SyncProgressEntry snap;
    bool allowed = false;
    {
      std::lock_guard<std::mutex> lk(sync_progress_mu_);
      auto it = sync_progress_.find(sid);
      if (it != sync_progress_.end()) {
        const auto& e = it->second;
        if (e.owner_user_id > 0) {
          allowed = (req_uid == e.owner_user_id);
        } else {
          allowed = e.owner_ip.empty() || e.owner_ip == req_ip;
        }
        if (allowed) snap = e;
      }
    }
    if (!allowed) {
      send_json(res, {{"ok", true}, {"found", false}});
      return;
    }
    send_json(res, {
        {"ok", true},
        {"found", true},
        {"percent", snap.percent},
        {"step", snap.step},
        {"totalSteps", snap.total_steps},
        {"stage", snap.stage},
        {"detail", snap.detail},
        {"status", snap.status},
        {"elapsedMs", std::max<int64_t>(0, utils::now_ms() - snap.started_at_ms)},
    });
  });

  // 2. 前端冷启动公共元数据（无需登录即可查看海大校历、海大指南、11节作息与预留平台目录）
  svr_.Get("/api/v1/meta/bootstrap", [this](const httplib::Request&, httplib::Response& res) {
    nlohmann::json ext_platforms = nlohmann::json::array({
        {{"key", "chaoxing"}, {"name", "超星学习通"}, {"status", "active"}, {"loginMode", "password"},
         {"homeUrl", "https://i.chaoxing.com"}, {"desc", "支持自动扫描本学期作业、在线测验、随堂练习快查与通知收件箱"}},
        {{"key", "mooc"}, {"name", "中国大学 MOOC"}, {"status", "active"}, {"loginMode", "cookie"},
         {"homeUrl", "https://www.icourse163.org"}, {"desc", "支持浏览器 Cookie 凭据安全导入，自动汇总章节测验、互评作业与期末考"}},
        {{"key", "yuketang"}, {"name", "雨课堂"}, {"status", "reserved"}, {"loginMode", "token_memo"},
         {"homeUrl", "https://changjiang.yuketang.cn"}, {"desc", "已预留插件槽位：可登记账号/身份凭证并一键直达长江雨课堂网页端"}},
        {{"key", "zhihuishu"}, {"name", "智慧树"}, {"status", "reserved"}, {"loginMode", "token_memo"},
         {"homeUrl", "https://onlineweb.zhihuishu.com"}, {"desc", "已预留插件槽位：可登记海大跨校共享课账号并一键直达智慧树在线学堂"}},
        {{"key", "zhidao"}, {"name", "知到"}, {"status", "reserved"}, {"loginMode", "token_memo"},
         {"homeUrl", "https://www.zhihuishu.com"}, {"desc", "已预留插件槽位：智慧树移动端「知到」同步账号备忘与快捷入口"}},
    });

    send_json(res, {
        {"ok", true},
        {"demoEnabled", cfg_.hnu.enable_demo_account},
        {"forgotPwdUrls", {
            {"jwxt", "https://authserver.hainanu.edu.cn/retrieve-password/retrievePassword/index.html"},
            {"portal", "https://authserver.hainanu.edu.cn/retrieve-password/retrievePassword/index.html"},
            {"chaoxing", "https://passport2.chaoxing.com/pwd/getpwd"}
        }},
        {"about", db_.get_school_meta("about_info")},
        {"bellPresets", db_.get_school_meta("bell_presets")},
        {"campusesAndAcademies", db_.get_school_meta("campuses_and_academies")},
        {"calendarTerms", db_.get_school_meta("calendar_terms")},
        {"wiki", db_.get_school_meta("wiki_groups")},
        {"quickLinks", db_.get_school_meta("campus_quick_links")},
        {"externalPlatforms", ext_platforms},
    });
  });

  // 3. 当前用户会话自检与全量快照秒开拉取
  svr_.Get("/api/v1/auth/session", [this](const httplib::Request& req, httplib::Response& res) {
    std::string tok = extract_session_token(req);
    if (tok.empty()) {
      send_json(res, {{"ok", true}, {"authenticated", false}});
      return;
    }
    auto user = db_.authenticate_session(tok, cfg_.security.session_ttl_days);
    if (!user) {
      send_json(res, {{"ok", true}, {"authenticated", false}});
      return;
    }
    auto bundle = build_user_bundle(*user);
    bundle["ok"] = true;
    bundle["authenticated"] = true;
    bundle["sessionToken"] = tok;
    bundle["sessionTtlDays"] = cfg_.security.session_ttl_days;
    // 每次打开应用自动滑动续期 HttpOnly 会话 Cookie（默认 365 天），覆盖整学年免重登
    int64_t max_age = static_cast<int64_t>(cfg_.security.session_ttl_days) * 86400LL;
    res.set_header("Set-Cookie",
                   "hnu_session=" + tok + "; Path=/; Max-Age=" +
                       std::to_string(max_age) + "; HttpOnly; SameSite=Lax");
    send_json(res, bundle);
  });

  svr_.Post("/api/v1/auth/logout", [this](const httplib::Request& req, httplib::Response& res) {
    std::string tok = extract_session_token(req);
    if (!tok.empty()) db_.revoke_session(tok);
    res.set_header("Set-Cookie", "hnu_session=; Path=/; Max-Age=0; HttpOnly; SameSite=Lax");
    send_json(res, {{"ok", true}});
  });

  // 4. 海南大学强智教务验证码、登录绑定与全量同步
  svr_.Get("/api/v1/jw/captcha", [this](const httplib::Request& req, httplib::Response& res) {
    std::string ip = client_ip_of(req);
    if (!check_rate_limit("jw_cap:" + ip, 60)) {
      send_json(res, {{"ok", false}, {"error", "刷新验证码太频繁，请稍等片刻"}}, 429);
      return;
    }
    std::string reuse_cid = req.has_param("cid") ? utils::trim(req.get_param_value("cid")) : "";
    send_json(res, qz_client_.fetch_login_captcha(reuse_cid));
  });

  svr_.Post("/api/v1/jw/login", [this](const httplib::Request& req, httplib::Response& res) {
    std::string ip = client_ip_of(req);
    if (!check_rate_limit("jw_login:" + ip, cfg_.security.rate_limit_login_per_min)) {
      send_json(res, {{"ok", false}, {"error", "登录尝试过于频繁，请 1 分钟后再试"}}, 429);
      return;
    }
    auto body = parse_body(req);
    std::string username = utils::trim(utils::json_str(body, "username", ""));
    std::string password = utils::json_str(body, "password", "");
    std::string cap_id = utils::trim(utils::json_str(body, "captchaId", ""));
    std::string cap_code = utils::trim(utils::json_str(body, "captchaCode", ""));
    std::string sync_id = utils::trim(utils::json_str(body, "syncId", ""));

    nlohmann::json seed_cookies = nlohmann::json::array();
    // 仅在确认「同一学号」重连时才复用本机已存密文密码与 CASTGC；切换到新学号时严禁复用旧账号任何 Cookie！
    std::string cur_tok = extract_session_token(req);
    std::optional<db::UserRecord> cur_u;
    if (!cur_tok.empty()) {
      cur_u = db_.authenticate_session(cur_tok, cfg_.security.session_ttl_days);
      if (cur_u) {
        auto saved = db_.load_credential(cur_u->id, "jwxt");
        std::string saved_acc = (saved.exists && saved.state != "broken")
                                    ? utils::json_str(saved.secret, "account", cur_u->student_id)
                                    : cur_u->student_id;
        bool same_account = username.empty() || username == cur_u->student_id || username == saved_acc;
        if (same_account) {
          if (saved.exists && saved.state != "broken") {
            if (password.empty()) {
              username = saved_acc;
              password = utils::json_str(saved.secret, "password", "");
            }
            if (saved.secret.is_object() && saved.secret.contains("cookies") && saved.secret["cookies"].is_array()) {
              seed_cookies = saved.secret["cookies"];
            }
          }
          auto p_saved = db_.load_credential(cur_u->id, "portal");
          if (p_saved.exists && p_saved.state != "broken" && p_saved.secret.is_object() &&
              p_saved.secret.contains("cookies")) {
            std::string p_acc = utils::json_str(p_saved.secret, "account", "");
            if (p_acc.empty() || p_acc == username) {
              merge_portal_castgc_unique(seed_cookies, p_saved.secret["cookies"]);
            }
          }
        }
      }
    }

    if (username.empty() || password.empty()) {
      send_json(res, {{"ok", false}, {"error", "请填写海南大学学号与统一身份认证密码"}});
      return;
    }

    auto prog_cb = [this, &sync_id, &ip](int pct, int step, int total, const std::string& stage, const std::string& detail) {
      update_sync_progress(sync_id, pct, step, total, stage, detail, "running", 0, ip);
    };
    update_sync_progress(sync_id, 5, 1, 6, "准备连接海南大学教务系统…", "正在加密封装认证请求", "running", 0, ip);

    try {
      nlohmann::json cookies;
      auto sync_data = qz_client_.login_and_sync(
          username, password, cap_id, cap_code, &cookies, prog_cb, seed_cookies);

      auto user = db_.upsert_user(username, sync_data.value("profile", nlohmann::json::object()));
      // 若同一浏览器未退出直接切换登录了另一学号，立即吊销上一个用户的旧会话令牌
      if (!cur_tok.empty() && cur_u && cur_u->id != user.id) {
        db_.revoke_session(cur_tok);
      }
      bool saved_ok = db_.save_credential(
          user.id, "jwxt", username,
          {{"account", username}, {"password", password}, {"cookies", cookies}},
          "ok", "");

      // 红线：绝不用空课表/空培养方案/空成绩覆盖已有的有效快照！
      std::string tt_warn = save_academic_snapshots_safely(user.id, sync_data);

      std::string ua = req.get_header_value("User-Agent").substr(0, 64);
      std::string sess_tok = db_.create_session(
          user.id, ua, utils::mask_ip(ip), cfg_.security.session_ttl_days);
      int64_t max_age = static_cast<int64_t>(cfg_.security.session_ttl_days) * 86400LL;
      res.set_header("Set-Cookie",
                     "hnu_session=" + sess_tok + "; Path=/; Max-Age=" +
                         std::to_string(max_age) + "; HttpOnly; SameSite=Lax");

      update_sync_progress(sync_id, 100, 6, 6, "教务同步全部完成", "本地长效会话已建立（365天免重登）", "done", 0, ip);

      auto out = build_user_bundle(user);
      out["ok"] = true;
      out["authenticated"] = true;
      out["sessionToken"] = sess_tok;
      out["sessionTtlDays"] = cfg_.security.session_ttl_days;
      out["saved"] = saved_ok ? "" : "fail: 凭据写入数据库失败";
      if (!tt_warn.empty()) out["timetableError"] = tt_warn;
      if (sync_data.contains("profileError")) out["profileError"] = sync_data["profileError"];
      send_json(res, out);
    } catch (const adapters::JwError& e) {
      update_sync_progress(sync_id, 100, 1, 6, "同步未完成", e.what(), "error", 0, ip);
      db_.record_telemetry("jw_sync_err", e.diag().empty() ? e.kind() : e.diag(), e.what(), "me");
      nlohmann::json err_res = {
          {"ok", false},
          {"error", e.what()},
          {"kind", e.kind()},
          {"needCaptcha", e.kind() == "captcha"},
          {"authFail", e.kind() == "auth"},
      };
      if (e.kind() != "net") {
        err_res["captcha"] = qz_client_.fetch_login_captcha();
      }
      send_json(res, err_res);
    } catch (const std::exception& e) {
      update_sync_progress(sync_id, 100, 1, 6, "连接异常", e.what(), "error", 0, ip);
      send_json(res, {
          {"ok", false},
          {"error", std::string("连接教务系统异常：") + e.what()},
          {"kind", "net"},
      });
    }
  });

  svr_.Post("/api/v1/jw/sync", [this](const httplib::Request& req, httplib::Response& res) {
    auto user = require_user(req, res);
    if (!user) return;
    auto body = parse_body(req);
    std::string sync_id = utils::trim(utils::json_str(body, "syncId", ""));

    std::string acc, pwd, cred_state;
    nlohmann::json cookies;
    if (!load_jwxt_cred_for_user(user->id, user->student_id, res, acc, pwd, cookies, cred_state)) {
      return;
    }

    int64_t uid = user->id;
    auto prog_cb = [this, &sync_id, uid](int pct, int step, int total, const std::string& stage, const std::string& detail) {
      update_sync_progress(sync_id, pct, step, total, stage, detail, "running", uid);
    };
    update_sync_progress(sync_id, 5, 1, 6, "准备同步海南大学教务数据…", "正在检查本地加密会话", "running", uid);

    try {
      nlohmann::json updated_cookies;
      auto sync_data = qz_client_.sync_with_saved(acc, pwd, cookies, &updated_cookies, prog_cb);
      auto updated_user = db_.upsert_user(acc, sync_data.value("profile", nlohmann::json::object()));

      if (!updated_cookies.empty()) {
        db_.save_credential(user->id, "jwxt", acc,
                            {{"account", acc}, {"password", pwd}, {"cookies", updated_cookies}}, "ok", "");
      } else if (cred_state != "ok") {
        db_.update_credential_state(user->id, "jwxt", "ok", "");
      }
      std::string tt_warn = save_academic_snapshots_safely(user->id, sync_data);

      std::string tok = extract_session_token(req);
      if (!tok.empty()) {
        int64_t max_age = static_cast<int64_t>(cfg_.security.session_ttl_days) * 86400LL;
        res.set_header("Set-Cookie",
                       "hnu_session=" + tok + "; Path=/; Max-Age=" +
                           std::to_string(max_age) + "; HttpOnly; SameSite=Lax");
      }

      update_sync_progress(sync_id, 100, 6, 6, "教务数据同步完成", "已更新课表、成绩、考场与学分快照", "done", uid);

      auto out = build_user_bundle(updated_user);
      out["ok"] = true;
      if (!tt_warn.empty()) out["timetableError"] = tt_warn;
      send_json(res, out);
    } catch (const adapters::JwError& e) {
      update_sync_progress(sync_id, 100, 1, 6, "同步未能完成", e.what(), "error", uid);
      handle_jw_error_response(user->id, e, res);
    } catch (const std::exception& e) {
      update_sync_progress(sync_id, 100, 1, 6, "同步异常", e.what(), "error", uid);
      send_json(res, {{"ok", false}, {"error", std::string("同步教务数据异常：") + e.what()}, {"kind", "net"}});
    }
  });

  svr_.Post("/api/v1/jw/unbind", [this](const httplib::Request& req, httplib::Response& res) {
    auto user = require_user(req, res);
    if (!user) return;
    db_.delete_credential(user->id, "jwxt");
    db_.clear_all_user_cache(user->id);
    scan_mgr_.reset_user(user->id);
    std::string tok = extract_session_token(req);
    if (!tok.empty()) db_.revoke_session(tok);
    res.set_header("Set-Cookie", "hnu_session=; Path=/; Max-Age=0; HttpOnly; SameSite=Lax");
    send_json(res, {{"ok", true}});
  });

  svr_.Post("/api/v1/jw/timetable", [this](const httplib::Request& req, httplib::Response& res) {
    auto user = require_user(req, res);
    if (!user) return;
    auto body = parse_body(req);
    std::string sid = utils::trim(utils::json_str(body, "semesterId", ""));
    static const std::regex re_sem(R"(^20\d{2}-20\d{2}-[123]$)");
    if (!std::regex_match(sid, re_sem)) {
      send_json(res, {{"ok", false}, {"error", "学年学期编号格式不正确（如 2026-2027-1）"}});
      return;
    }

    std::string acc, pwd, cred_state;
    nlohmann::json cookies;
    if (!load_jwxt_cred_for_user(user->id, user->student_id, res, acc, pwd, cookies, cred_state)) {
      return;
    }
    try {
      nlohmann::json updated_cookies;
      auto out = qz_client_.fetch_timetable_for(acc, pwd, cookies, sid, &updated_cookies);
      if (!updated_cookies.empty()) {
        db_.save_credential(user->id, "jwxt", acc,
                            {{"account", acc}, {"password", pwd}, {"cookies", updated_cookies}}, "ok", "");
      } else if (cred_state != "ok") {
        db_.update_credential_state(user->id, "jwxt", "ok", "");
      }
      if (out.contains("courses") && out["courses"].is_array() && !out["courses"].empty()) {
        db_.save_snapshot(user->id, "timetable", sid, out);
      } else {
        auto prev_sid = db_.get_snapshot(user->id, "timetable", sid);
        if (prev_sid.found && prev_sid.payload.is_object() && prev_sid.payload.contains("courses") &&
            prev_sid.payload["courses"].is_array() && !prev_sid.payload["courses"].empty()) {
          out = prev_sid.payload;
          out["fromCache"] = true;
          out["staleWarn"] = "教务系统本次返回该学期空课表，已保留历史有效缓存";
        } else {
          db_.save_snapshot(user->id, "timetable", sid, out);
        }
      }
      out["ok"] = true;
      send_json(res, out);
    } catch (const adapters::JwError& e) {
      if (e.kind() == "expired" || e.kind() == "captcha") {
        db_.update_credential_state(user->id, "jwxt", "expired", e.what());
      }
      auto cached = db_.get_snapshot(user->id, "timetable", sid);
      if (cached.found && e.kind() != "expired" && e.kind() != "captcha") {
        auto out = cached.payload;
        out["ok"] = true;
        out["fromCache"] = true;
        out["staleWarn"] = e.what();
        send_json(res, out);
        return;
      }
      handle_jw_error_response(user->id, e, res);
    }
  });

  svr_.Post("/api/v1/jw/exams", [this](const httplib::Request& req, httplib::Response& res) {
    auto user = require_user(req, res);
    if (!user) return;
    auto body = parse_body(req);
    std::string sid = utils::trim(utils::json_str(body, "semesterId", ""));
    static const std::regex re_sem(R"(^20\d{2}-20\d{2}-[123]$)");
    if (!std::regex_match(sid, re_sem)) {
      send_json(res, {{"ok", false}, {"error", "学年学期编号格式不正确"}});
      return;
    }

    std::string acc, pwd, cred_state;
    nlohmann::json cookies;
    if (!load_jwxt_cred_for_user(user->id, user->student_id, res, acc, pwd, cookies, cred_state)) {
      return;
    }
    try {
      nlohmann::json updated_cookies;
      auto out = qz_client_.fetch_exams_for(acc, pwd, cookies, sid, &updated_cookies);
      if (!updated_cookies.empty()) {
        db_.save_credential(user->id, "jwxt", acc,
                            {{"account", acc}, {"password", pwd}, {"cookies", updated_cookies}}, "ok", "");
      } else if (cred_state != "ok") {
        db_.update_credential_state(user->id, "jwxt", "ok", "");
      }
      bool has_exams = out.contains("exams") && out["exams"].is_array() && !out["exams"].empty();
      if (has_exams) {
        db_.save_snapshot(user->id, "exams", sid, out);
      } else {
        auto prev_sid = db_.get_snapshot(user->id, "exams", sid);
        bool prev_has = prev_sid.found && prev_sid.payload.is_object() &&
                        prev_sid.payload.contains("exams") &&
                        prev_sid.payload["exams"].is_array() &&
                        !prev_sid.payload["exams"].empty();
        if (prev_has) {
          out = prev_sid.payload;
          out["fromCache"] = true;
          out["staleWarn"] = "教务系统本次返回该学期空考场，已保留历史有效缓存";
        } else {
          db_.save_snapshot(user->id, "exams", sid, out);
        }
      }
      out["ok"] = true;
      send_json(res, out);
    } catch (const adapters::JwError& e) {
      if (e.kind() == "expired" || e.kind() == "captcha") {
        db_.update_credential_state(user->id, "jwxt", "expired", e.what());
      }
      auto cached = db_.get_snapshot(user->id, "exams", sid);
      if (cached.found && e.kind() != "expired" && e.kind() != "captcha") {
        auto out = cached.payload;
        out["ok"] = true;
        out["fromCache"] = true;
        out["staleWarn"] = e.what();
        send_json(res, out);
        return;
      }
      handle_jw_error_response(user->id, e, res);
    }
  });

  svr_.Post("/api/v1/jw/grades", [this](const httplib::Request& req, httplib::Response& res) {
    auto user = require_user(req, res);
    if (!user) return;
    auto body = parse_body(req);
    std::string sid = utils::trim(utils::json_str(body, "semesterId", ""));
    std::string sync_id = utils::trim(utils::json_str(body, "syncId", ""));
    static const std::regex re_sem(R"(^20\d{2}-20\d{2}-[123]$)");
    if (!sid.empty() && !std::regex_match(sid, re_sem)) {
      send_json(res, {{"ok", false}, {"error", "学年学期编号格式不正确（如 2026-2027-1）"}});
      return;
    }

    std::string acc, pwd, cred_state;
    nlohmann::json cookies;
    if (!load_jwxt_cred_for_user(user->id, user->student_id, res, acc, pwd, cookies, cred_state)) {
      return;
    }

    int64_t uid = user->id;
    auto prog_cb = [this, &sync_id, uid](int pct, int step, int total, const std::string& stage, const std::string& detail) {
      update_sync_progress(sync_id, pct, step, total, stage, detail, "running", uid);
    };
    update_sync_progress(sync_id, 5, 4, 6, "准备查询成绩…", "正在检查本地加密会话", "running", uid);

    auto count_courses = [](const nlohmann::json& g) -> size_t {
      return (g.is_object() && g.contains("courses") && g["courses"].is_array()) ? g["courses"].size() : 0;
    };

    try {
      nlohmann::json updated_cookies;
      auto out = qz_client_.fetch_grades_for(acc, pwd, cookies, sid, &updated_cookies, prog_cb);
      if (!updated_cookies.empty()) {
        db_.save_credential(user->id, "jwxt", acc,
                            {{"account", acc}, {"password", pwd}, {"cookies", updated_cookies}}, "ok", "");
      }
      bool has_courses = count_courses(out) > 0;
      auto prev = db_.get_snapshot(user->id, "grades", "current");
      bool prev_has = prev.found && count_courses(prev.payload) > 0;

      if (sid.empty()) {
        // 红线：绝不用空成绩覆盖已有非空成绩快照
        if (has_courses || !prev_has) db_.save_snapshot(user->id, "grades", "current", out);
      } else if (has_courses) {
        db_.save_snapshot(user->id, "grades", sid, out);
        // 合并进「全部学期」快照：替换该学期课程，保留其他学期
        nlohmann::json cur = prev_has ? prev.payload : nlohmann::json{{"summary", nlohmann::json::array()}, {"overall", nullptr}, {"courses", nlohmann::json::array()}};
        nlohmann::json nc = nlohmann::json::array();
        for (const auto& c : cur["courses"]) {
          if (c.value("term", "") != sid) nc.push_back(c);
        }
        for (const auto& c : out["courses"]) nc.push_back(c);
        nlohmann::json ns = nlohmann::json::array();
        if (cur.contains("summary") && cur["summary"].is_array()) {
          for (const auto& sm : cur["summary"]) {
            if (sm.value("semester", "") != sid) ns.push_back(sm);
          }
        }
        if (out.contains("summary") && out["summary"].is_array()) {
          for (const auto& sm : out["summary"]) ns.push_back(sm);
        }
        auto num = [](const nlohmann::json& o, const char* k) {
          try { return std::stod(utils::json_str(o, k, "0")); } catch (...) { return 0.0; }
        };
        double cr_sum = 0.0, w_sum = 0.0;
        int cnt = 0;
        for (const auto& sm : ns) {
          double cr = num(sm, "credits");
          cr_sum += cr;
          w_sum += cr * num(sm, "gpa");
          cnt += static_cast<int>(num(sm, "count"));
        }
        nlohmann::json overall = nullptr;
        if (cnt > 0) {
          char b1[32], b2[32];
          std::snprintf(b1, sizeof(b1), "%.2f", cr_sum);
          std::snprintf(b2, sizeof(b2), "%.2f", cr_sum > 0 ? w_sum / cr_sum : 0.0);
          overall = {{"count", std::to_string(cnt)}, {"credits", b1}, {"gpa", b2}};
        }
        int mid_n = 0, fin_n = 0, reg_n = 0;
        for (const auto& c : nc) {
          if (!utils::json_str(c, "midtermScore", "").empty()) ++mid_n;
          if (!utils::json_str(c, "regularScore", "").empty()) ++reg_n;
          if (!utils::json_str(c, "score", "").empty() || !utils::json_str(c, "finalScore", "").empty()) ++fin_n;
        }
        db_.save_snapshot(user->id, "grades", "current", {
            {"summary", ns}, {"overall", overall}, {"courses", nc},
            {"midtermCount", mid_n}, {"finalCount", fin_n}, {"regularCount", reg_n},
            {"evalBlocked", out.value("evalBlocked", false)},
        });
      }

      update_sync_progress(sync_id, 100, 6, 6, "成绩查询完成",
                           "共 " + std::to_string(count_courses(out)) + " 门课程", "done", uid);
      out["ok"] = true;
      send_json(res, out);
    } catch (const adapters::JwError& e) {
      update_sync_progress(sync_id, 100, 4, 6, "成绩查询未完成", e.what(), "error", uid);
      if (e.kind() == "expired" || e.kind() == "captcha") {
        db_.update_credential_state(user->id, "jwxt", "expired", e.what());
        send_json(res, {{"ok", false}, {"error", e.what()}, {"kind", e.kind()}, {"needCaptcha", true},
                        {"captcha", qz_client_.fetch_login_captcha()}});
        return;
      }
      auto cached = db_.get_snapshot(user->id, "grades", sid.empty() ? "current" : sid);
      if (cached.found && count_courses(cached.payload) > 0) {
        auto out = cached.payload;
        out["ok"] = true;
        out["fromCache"] = true;
        out["staleWarn"] = e.what();
        send_json(res, out);
        return;
      }
      if (!sid.empty()) {
        auto cur = db_.get_snapshot(user->id, "grades", "current");
        if (cur.found && count_courses(cur.payload) > 0) {
          nlohmann::json filtered = nlohmann::json::array();
          for (const auto& c : cur.payload["courses"]) {
            if (c.value("term", "") == sid) filtered.push_back(c);
          }
          if (!filtered.empty()) {
            send_json(res, {{"ok", true}, {"fromCache", true}, {"staleWarn", e.what()}, {"queriedSemester", sid},
                            {"summary", nlohmann::json::array()}, {"overall", nullptr}, {"courses", filtered}});
            return;
          }
        }
      }
      send_json(res, {{"ok", false}, {"error", e.what()}, {"kind", e.kind()}});
    } catch (const std::exception& e) {
      update_sync_progress(sync_id, 100, 4, 6, "成绩查询异常", e.what(), "error", uid);
      send_json(res, {{"ok", false}, {"error", std::string("查询成绩异常：") + e.what()}, {"kind", "net"}});
    }
  });

  svr_.Get("/api/v1/jw/todo", [this](const httplib::Request& req, httplib::Response& res) {
    auto user = require_user(req, res);
    if (!user) return;
    std::string acc, pwd, cred_state;
    nlohmann::json cookies;
    if (!load_jwxt_cred_for_user(user->id, user->student_id, res, acc, pwd, cookies, cred_state)) {
      return;
    }
    try {
      nlohmann::json updated_cookies;
      auto out = qz_client_.fetch_jw_todo(acc, pwd, cookies, &updated_cookies);
      if (!updated_cookies.empty()) {
        db_.save_credential(user->id, "jwxt", acc,
                            {{"account", acc}, {"password", pwd}, {"cookies", updated_cookies}}, "ok", "");
      } else if (cred_state != "ok") {
        db_.update_credential_state(user->id, "jwxt", "ok", "");
      }
      out["ok"] = true;
      out["at"] = utils::now_sec();
      send_json(res, out);
    } catch (const adapters::JwError& e) {
      handle_jw_error_response(user->id, e, res);
    }
  });

  // 5. 跨端用户设置与 .ics 系统日历订阅
  svr_.Get("/api/v1/preferences", [this](const httplib::Request& req, httplib::Response& res) {
    auto user = require_user(req, res);
    if (!user) return;
    send_json(res, {{"ok", true}, {"preferences", db_.get_preferences(user->id)}});
  });

  svr_.Post("/api/v1/preferences", [this](const httplib::Request& req, httplib::Response& res) {
    auto user = require_user(req, res);
    if (!user) return;
    db_.save_preferences(user->id, parse_body(req));
    send_json(res, {{"ok", true}, {"preferences", db_.get_preferences(user->id)}});
  });

  svr_.Post("/api/v1/account/profile", [this](const httplib::Request& req, httplib::Response& res) {
    auto user = require_user(req, res);
    if (!user) return;
    auto body = parse_body(req);
    std::string academy = utils::trim(utils::json_str(body, "academy", ""));
    std::string campus = utils::trim(utils::json_str(body, "campus", ""));
    if (academy.size() > 64 || campus.size() > 64) {
      send_json(res, {{"ok", false}, {"error", "书院或校区名称过长"}}, 400);
      return;
    }
    auto updated = db_.update_user_profile_manual(user->id, academy, campus);
    if (!updated) {
      send_json(res, {{"ok", false}, {"error", "未找到用户档案"}}, 404);
      return;
    }
    send_json(res, {{"ok", true}, {"profile", updated->to_profile_json()}});
  });

  svr_.Post("/api/v1/account/clear-cache", [this](const httplib::Request& req, httplib::Response& res) {
    auto user = require_user(req, res);
    if (!user) return;
    db_.clear_all_user_cache(user->id);
    scan_mgr_.reset_user(user->id);
    send_json(res, {{"ok", true}});
  });

  svr_.Post("/api/v1/calendar/rotate-token", [this](const httplib::Request& req, httplib::Response& res) {
    auto user = require_user(req, res);
    if (!user) return;
    std::string new_tok = db_.rotate_ics_token(user->id);
    send_json(res, {{"ok", true}, {"icsToken", new_tok}});
  });

  svr_.Get(R"(/api/v1/calendar/ics/([A-Za-z0-9_-]{16,64})(?:\.ics)?)",
           [this](const httplib::Request& req, httplib::Response& res) {
             std::string tok = req.matches[1].str();
             auto user = db_.get_user_by_ics_token(tok);
             if (!user) {
               res.status = 404;
               res.set_content("Invalid calendar token", "text/plain; charset=utf-8");
               return;
             }
             auto tt = db_.get_snapshot(user->id, "timetable", "current");
             auto ex = db_.get_snapshot(user->id, "exams", "current");
             auto cx = db_.get_snapshot(user->id, "cx_todo", "current");
             auto mo = db_.get_snapshot(user->id, "mooc_todo", "current");
             auto prefs = db_.get_preferences(user->id);
             auto presets = db_.get_school_meta("bell_presets");

             std::string ics_text = services::CalendarIcsGenerator::build_user_ics(
                 tt.found ? tt.payload : nlohmann::json::object(),
                 ex.found ? ex.payload : nlohmann::json::object(),
                 cx.found ? cx.payload : nlohmann::json::object(),
                 mo.found ? mo.payload : nlohmann::json::object(),
                 prefs, presets);

             res.status = 200;
             res.set_header("Content-Disposition", "attachment; filename=\"hnutong_schedule.ics\"");
             res.set_content(ics_text, "text/calendar; charset=utf-8");
           });

  // 6. 海南大学公开通知公告（无需登录即可查看；采用 SWR 缓存即显 + 6源并发抓取，永不超时白屏）
  svr_.Get("/api/v1/notice", [this](const httplib::Request& req, httplib::Response& res) {
    int limit = 60;
    if (req.has_param("limit")) {
      try { limit = std::clamp(std::stoi(req.get_param_value("limit")), 1, 200); } catch (...) {}
    }
    std::string tier = req.has_param("tier") ? req.get_param_value("tier") : "";
    bool force = req.has_param("force") && req.get_param_value("force") == "1";
    std::string sync_id = req.has_param("syncId") ? utils::trim(req.get_param_value("syncId")) : "";
    std::string req_ip = client_ip_of(req);

    int64_t cached_count = db_.count_notices();
    int64_t last_at = db_.latest_notice_fetch_at();
    int64_t now = utils::now_sec();
    nlohmann::json failed = nlohmann::json::array();
    int sources_ok = 6;
    bool did_fetch = false;
    bool stale = false;

    if (!force && cached_count > 0) {
      // Stale-While-Revalidate：库里已有缓存公告时毫秒级直接返回，TTL 过期则在后台异步静默刷新，绝不阻塞页面导致公告变空！
      if (now - last_at > cfg_.hnu.notice_ttl_seconds &&
          now - last_notice_attempt_at_.load() > 300) {
        trigger_background_notice_refresh();
      }
    } else {
      std::lock_guard<std::mutex> lk(notice_refresh_mu_);
      int64_t recheck_count = db_.count_notices();
      int64_t recheck_at = db_.latest_notice_fetch_at();
      int64_t now_locked = utils::now_sec();
      if (recheck_count > 0 && (now_locked - recheck_at) < 10) {
        // 刚刚已有另一路并发/后台抓取完成入库，直接复用最新结果避免重复等待
        last_at = recheck_at;
        update_sync_progress(sync_id, 100, 6, 6, "通知公告已是最新", "已复用刚刚完成的官网同步结果", "done", 0, req_ip);
      } else if (force || recheck_count == 0 || (now_locked - recheck_at > cfg_.hnu.notice_ttl_seconds)) {
        last_notice_attempt_at_.store(now_locked);
        update_sync_progress(sync_id, 8, 0, 6, "正在连接海南大学官方公告源…", "并发拉取本科生院与海大主站 6 大栏目", "running", 0, req_ip);
        auto prog_cb = [this, &sync_id, &req_ip](int pct, int done, int total, const std::string& label) {
          update_sync_progress(sync_id, pct, done, total, label, "海南大学公开公告聚合", "running", 0, req_ip);
        };
        auto live = notice_crawler_.fetch_all_sources(prog_cb);
        did_fetch = true;
        failed = live.value("failed", nlohmann::json::array());
        sources_ok = live.value("sourcesOk", 0);
        if (live.contains("items") && live["items"].is_array() && !live["items"].empty()) {
          std::vector<nlohmann::json> vec = live["items"].get<std::vector<nlohmann::json>>();
          db_.upsert_notices(vec);
          last_at = utils::now_sec();
          update_sync_progress(sync_id, 100, 6, 6, "通知公告已更新", "共同步 " + std::to_string(vec.size()) + " 条最新公告", "done", 0, req_ip);
        } else {
          stale = true;
          if (recheck_count == 0) {
            // 首次离线冷启动兜底种入海大示例通知，绝不白屏
            db_.upsert_notices(adapters::SandboxProvider::fallback_hnu_notices());
            last_at = utils::now_sec();
          }
          update_sync_progress(sync_id, 100, 6, 6, "官网源暂不可达，已展示本地缓存公告", "请检查网络或代理设置后重试", "done", 0, req_ip);
        }
      }
    }

    auto items = db_.list_notices(tier, limit);
    send_json(res, {
        {"ok", true},
        {"at", last_at},
        {"cached", !did_fetch},
        {"stale", stale},
        {"sources", sources_ok},
        {"failed", failed},
        {"items", items},
    });
  });

  svr_.Get("/api/v1/notice/detail", [this](const httplib::Request& req, httplib::Response& res) {
    std::string u = req.has_param("u") ? utils::trim(req.get_param_value("u")) : "";
    if (u.empty()) {
      send_json(res, {{"ok", false}, {"error", "缺少通知地址参数 u"}}, 400);
      return;
    }
    auto cached = db_.get_notice_detail_cached(u);
    if (cached) {
      std::string sum = cached->value("summary", "");
      bool has_atts = cached->contains("attachments") && cached->at("attachments").is_array() &&
                      !cached->at("attachments").empty();
      if (!sum.empty() && (sum.find("红头 PDF") == std::string::npos || has_atts)) {
        send_json(res, *cached);
        return;
      }
    }
    auto detail = notice_crawler_.fetch_article_detail(u);
    if (detail.value("ok", false)) {
      db_.save_notice_detail(u,
                             detail.value("publish", ""),
                             detail.value("dept", ""),
                             detail.value("when", ""),
                             detail.value("where", ""),
                             detail.value("summary", ""),
                             detail.value("attachments", nlohmann::json::array()));
      send_json(res, detail);
      return;
    }
    if (cached && !cached->value("summary", "").empty()) {
      (*cached)["cachedFallback"] = true;
      send_json(res, *cached);
      return;
    }
    send_json(res, detail);
  });

  // 7. 海南大学独立智慧门户（保留二次算术验证码登录页 + 免重输密码重连机制）
  svr_.Get("/api/v1/portal/status", [this](const httplib::Request& req, httplib::Response& res) {
    auto user = require_user(req, res);
    if (!user) return;
    auto cred = db_.load_credential(user->id, "portal");
    if (!cred.exists) {
      send_json(res, {{"ok", true}, {"state", "none"}, {"saved", false}, {"savedUser", ""}});
      return;
    }
    bool has_pwd = !cred.secret.value("password", "").empty();
    std::string raw_acc = cred.secret.value("account", "");
    auto card_snap = db_.get_snapshot(user->id, "portal_card", "current");
    auto apps_snap = db_.get_snapshot(user->id, "portal_apps", "current");

    send_json(res, {
        {"ok", true},
        {"state", cred.state},
        {"user", cred.account_masked},
        {"saved", has_pwd},
        {"savedUser", utils::mask_account(raw_acc)},
        {"card", card_snap.found ? card_snap.payload : nlohmann::json(nullptr)},
        {"apps", apps_snap.found ? apps_snap.payload : nlohmann::json(nullptr)},
    });
  });

  svr_.Get("/api/v1/portal/captcha", [this](const httplib::Request& req, httplib::Response& res) {
    auto user = require_user(req, res);
    if (!user) return;
    send_json(res, portal_client_.create_captcha_challenge());
  });

  svr_.Post("/api/v1/portal/login", [this](const httplib::Request& req, httplib::Response& res) {
    auto user = require_user(req, res);
    if (!user) return;
    std::string ip = client_ip_of(req);
    if (!check_rate_limit("portal_login:" + ip, cfg_.security.rate_limit_login_per_min)) {
      send_json(res, {{"ok", false}, {"error", "门户登录尝试过于频繁，请稍后再试"}, {"needCaptcha", true}}, 429);
      return;
    }

    auto body = parse_body(req);
    std::string acc = utils::trim(utils::json_str(body, "user", ""));
    std::string pwd = utils::json_str(body, "pass", "");
    std::string cid = utils::trim(utils::json_str(body, "captchaId", ""));
    std::string code = utils::trim(utils::json_str(body, "code", ""));

    // 支持密码留空时自动取用已保存的门户密码（过期重登只需算一道算术题！）
    bool used_saved = false;
    if (pwd.empty()) {
      auto saved = db_.load_credential(user->id, "portal");
      std::string sp = utils::json_str(saved.secret, "password", "");
      std::string su = utils::json_str(saved.secret, "account", "");
      if (sp.empty()) {
        send_json(res, {{"ok", false}, {"error", "请填写门户账号与密码"}, {"needCaptcha", true}});
        return;
      }
      if (!acc.empty() && !su.empty() && acc != su && acc != utils::mask_account(su)) {
        send_json(res, {{"ok", false}, {"needCaptcha", true},
                        {"error", "本机存的是 " + utils::mask_account(su) + " 的门户密码，与所填学号不一致，请手动输入密码"}});
        return;
      }
      pwd = sp;
      acc = su;
      used_saved = true;
    }
    if (acc.empty() || pwd.empty()) {
      send_json(res, {{"ok", false}, {"error", "学号和密码都要填"}, {"needCaptcha", true}});
      return;
    }

    try {
      nlohmann::json cookies;
      auto login_res = portal_client_.login(acc, pwd, cid, code, &cookies);
      bool ok = db_.save_credential(
          user->id, "portal", acc,
          {{"account", acc}, {"password", pwd}, {"cookies", cookies}},
          "ok", "");

      // 登录成功后顺手预取一次卡务与办事入口快照
      try {
        auto card = portal_client_.fetch_card(acc, cookies);
        db_.save_snapshot(user->id, "portal_card", "current", card);
        auto apps = portal_client_.fetch_apps(acc, cookies);
        db_.save_snapshot(user->id, "portal_apps", "current", apps);
      } catch (...) {
      }

      send_json(res, {
          {"ok", true},
          {"user", utils::mask_account(login_res.value("user", acc))},
          {"saved", ok ? "" : "fail: 门户凭据保存失败"},
      });
    } catch (const adapters::PortalError& e) {
      nlohmann::json err_body = {
          {"ok", false},
          {"error", e.what()},
          {"kind", e.kind()},
          {"needCaptcha", true},
      };
      // 红线：仅当使用已保存密码且服务器明确返回 PASSERROR/USERLOCK 时才删除旧密码（CODEFALSE 算错验证码绝不删！）
      if (used_saved && e.should_purge_saved_password()) {
        db_.delete_credential(user->id, "portal");
        err_body["pwdStale"] = true;
        err_body["error"] = std::string(e.what()) + " —— 已清除失效的保存密码，请手动输入新密码";
      }
      send_json(res, err_body);
    }
  });

  svr_.Post("/api/v1/portal/logout", [this](const httplib::Request& req, httplib::Response& res) {
    auto user = require_user(req, res);
    if (!user) return;
    db_.delete_credential(user->id, "portal");
    db_.delete_snapshots(user->id, "portal_card");
    db_.delete_snapshots(user->id, "portal_apps");
    send_json(res, {{"ok", true}});
  });

  svr_.Get("/api/v1/portal/card", [this](const httplib::Request& req, httplib::Response& res) {
    auto user = require_user(req, res);
    if (!user) return;
    auto cred = db_.load_credential(user->id, "portal");
    if (!cred.exists) {
      send_json(res, {{"ok", false}, {"error", "还没连接智慧门户"}, {"needLogin", true}});
      return;
    }
    try {
      auto card = portal_client_.fetch_card(
          cred.secret.value("account", ""),
          cred.secret.value("cookies", nlohmann::json::array()));
      db_.save_snapshot(user->id, "portal_card", "current", card);
      card["ok"] = true;
      send_json(res, card);
    } catch (const adapters::PortalError& e) {
      if (e.kind() == "expired") db_.update_credential_state(user->id, "portal", "expired", e.what());
      send_json(res, {{"ok", false}, {"error", e.what()}, {"needLogin", e.kind() == "expired"}});
    }
  });

  svr_.Get("/api/v1/portal/apps", [this](const httplib::Request& req, httplib::Response& res) {
    auto user = require_user(req, res);
    if (!user) return;
    auto cred = db_.load_credential(user->id, "portal");
    if (!cred.exists) {
      send_json(res, {{"ok", false}, {"error", "还没连接智慧门户"}, {"needLogin", true}});
      return;
    }
    try {
      auto apps = portal_client_.fetch_apps(
          cred.secret.value("account", ""),
          cred.secret.value("cookies", nlohmann::json::array()));
      db_.save_snapshot(user->id, "portal_apps", "current", apps);
      send_json(res, {{"ok", true}, {"apps", apps}});
    } catch (const adapters::PortalError& e) {
      if (e.kind() == "expired") db_.update_credential_state(user->id, "portal", "expired", e.what());
      send_json(res, {{"ok", false}, {"error", e.what()}, {"needLogin", e.kind() == "expired"}});
    }
  });

  svr_.Get("/api/v1/portal/ticket", [this](const httplib::Request& req, httplib::Response& res) {
    auto user = require_user(req, res);
    if (!user) return;
    std::string svc = req.has_param("service") ? utils::trim(req.get_param_value("service")) : "";
    auto cred = db_.load_credential(user->id, "portal");
    if (!cred.exists) {
      send_json(res, {{"ok", false}, {"error", "还没连接智慧门户"}, {"needLogin", true}});
      return;
    }
    try {
      auto tk = portal_client_.exchange_ticket(
          cred.secret.value("account", ""),
          cred.secret.value("cookies", nlohmann::json::array()),
          svc);
      tk["ok"] = true;
      send_json(res, tk);
    } catch (const adapters::PortalError& e) {
      send_json(res, {{"ok", false}, {"error", e.what()}, {"needLogin", e.kind() == "expired"}});
    }
  });

  // 8. 海南大学宿舍电量管理与购电/抄表台账
  svr_.Get("/api/v1/elec/state", [this](const httplib::Request& req, httplib::Response& res) {
    auto user = require_user(req, res);
    if (!user) return;
    send_json(res, {
        {"ok", true},
        {"elec", db_.get_dorm_electricity(user->id)},
        {"logs", db_.list_dorm_elec_logs(user->id, 20)},
    });
  });

  svr_.Post("/api/v1/elec/update", [this](const httplib::Request& req, httplib::Response& res) {
    auto user = require_user(req, res);
    if (!user) return;
    auto updated = db_.update_dorm_electricity(user->id, parse_body(req));
    send_json(res, {{"ok", true}, {"elec", updated}});
  });

  svr_.Post("/api/v1/elec/log", [this](const httplib::Request& req, httplib::Response& res) {
    auto user = require_user(req, res);
    if (!user) return;
    auto body = parse_body(req);
    std::string kind = utils::json_str(body, "kind", "check") == "pay" ? "pay" : "check";
    double amount = utils::json_double(body, "amount", 0.0);
    if (!std::isfinite(amount)) amount = 0.0;
    std::optional<double> bal_after;
    if (body.contains("balanceAfter") && !body["balanceAfter"].is_null()) {
      double b = utils::json_double(body, "balanceAfter", std::numeric_limits<double>::quiet_NaN());
      if (std::isfinite(b)) bal_after = std::clamp(b, 0.0, 100000.0);
    }
    std::string note = utils::trim(utils::json_str(body, "note", ""));
    auto logs = db_.add_dorm_elec_log(user->id, kind, amount, bal_after, note);
    send_json(res, {
        {"ok", true},
        {"elec", db_.get_dorm_electricity(user->id)},
        {"logs", logs},
    });
  });

  // 9. 超星学习通（多用户异步后台扫描 + 随堂练习快查 + 收件箱）
  svr_.Get("/api/v1/cx/status", [this](const httplib::Request& req, httplib::Response& res) {
    auto user = require_user(req, res);
    if (!user) return;
    auto cred = db_.load_credential(user->id, "chaoxing");
    auto snap = db_.get_snapshot(user->id, "cx_todo", "current");
    bool has_pwd = cred.exists && !utils::json_str(cred.secret, "password", "").empty();
    int64_t age = snap.found ? std::max<int64_t>(0, utils::now_sec() - snap.synced_at) : -1;
    send_json(res, {
        {"ok", true},
        {"linked", cred.exists},
        {"user", cred.account_masked},
        {"state", cred.state},
        {"canRelink", has_pwd},
        {"scan", scan_mgr_.get_chaoxing_scan_state(user->id)},
        {"age", age},
    });
  });

  svr_.Post("/api/v1/cx/login", [this](const httplib::Request& req, httplib::Response& res) {
    auto user = require_user(req, res);
    if (!user) return;
    auto body = parse_body(req);
    std::string acc = utils::trim(utils::json_str(body, "user", ""));
    std::string pwd = utils::json_str(body, "pass", "");
    bool used_saved = false;

    if (pwd.empty()) {
      auto saved = db_.load_credential(user->id, "chaoxing");
      std::string su = utils::json_str(saved.secret, "account", "");
      std::string sp = utils::json_str(saved.secret, "password", "");
      if (!saved.exists || sp.empty()) {
        send_json(res, {{"ok", false}, {"error", "请填写超星学习通手机号/账号与密码"}});
        return;
      }
      if (!acc.empty() && !su.empty() && acc != su && acc != utils::mask_account(su)) {
        send_json(res, {{"ok", false},
                        {"error", "本机存的是 " + utils::mask_account(su) + " 的学习通密码，与所填账号不一致，请手动输入密码"}});
        return;
      }
      acc = su;
      pwd = sp;
      used_saved = true;
    }
    if (acc.empty() || pwd.empty()) {
      send_json(res, {{"ok", false}, {"error", "账号和密码都要填"}});
      return;
    }

    try {
      nlohmann::json cookies;
      cx_client_.login(acc, pwd, &cookies);
      db_.save_credential(user->id, "chaoxing", acc,
                          {{"account", acc}, {"password", pwd}, {"cookies", cookies}}, "ok", "");
      send_json(res, {{"ok", true}, {"user", utils::mask_account(acc)}});
    } catch (const adapters::CxError& e) {
      if (used_saved && e.kind() == "auth") {
        auto saved = db_.load_credential(user->id, "chaoxing");
        nlohmann::json sec = saved.secret;
        sec.erase("password");
        db_.save_credential(user->id, "chaoxing", acc, sec, "expired", e.what());
        send_json(res, {{"ok", false}, {"kind", e.kind()}, {"pwdStale", true},
                        {"error", std::string(e.what()) + " —— 已清除失效密码，请重新输入新密码"}});
        return;
      }
      send_json(res, {{"ok", false}, {"error", e.what()}, {"kind", e.kind()}});
    }
  });

  svr_.Post("/api/v1/cx/scan", [this](const httplib::Request& req, httplib::Response& res) {
    auto user = require_user(req, res);
    if (!user) return;
    auto cred = db_.load_credential(user->id, "chaoxing");
    if (!cred.exists) {
      send_json(res, {{"ok", false}, {"error", "尚未连接超星学习通"}, {"needLogin", true}});
      return;
    }
    auto body = parse_body(req);
    bool force = utils::json_bool(body, "force", false);

    // 自动加载本学期海大教务课表课程名作为活跃白名单
    std::vector<std::string> active_names;
    if (body.contains("activeNames") && body["activeNames"].is_array()) {
      for (const auto& x : body["activeNames"]) {
        if (x.is_string()) active_names.push_back(x.get<std::string>());
        else if (x.is_object()) active_names.push_back(utils::json_str(x, "name", ""));
      }
    } else {
      auto tt = db_.get_snapshot(user->id, "timetable", "current");
      if (tt.found && tt.payload.is_object() && tt.payload.contains("courses") && tt.payload["courses"].is_array()) {
        for (const auto& c : tt.payload["courses"]) {
          active_names.push_back(utils::json_str(c, "name", ""));
        }
      }
    }

    bool started = scan_mgr_.start_chaoxing_scan(user->id, active_names, force);
    send_json(res, {
        {"ok", true},
        {"started", started},
        {"scan", scan_mgr_.get_chaoxing_scan_state(user->id)},
    });
  });

  svr_.Get("/api/v1/cx/todo", [this](const httplib::Request& req, httplib::Response& res) {
    auto user = require_user(req, res);
    if (!user) return;
    auto snap = db_.get_snapshot(user->id, "cx_todo", "current");
    int64_t age = snap.found ? std::max<int64_t>(0, utils::now_sec() - snap.synced_at) : -1;
    send_json(res, {
        {"ok", true},
        {"scan", scan_mgr_.get_chaoxing_scan_state(user->id)},
        {"age", age},
        {"data", snap.found ? snap.payload : nlohmann::json(nullptr)},
    });
  });

  svr_.Post("/api/v1/cx/live", [this](const httplib::Request& req, httplib::Response& res) {
    auto user = require_user(req, res);
    if (!user) return;
    auto cred = db_.load_credential(user->id, "chaoxing");
    if (!cred.exists) {
      send_json(res, {{"ok", false}, {"error", "尚未连接学习通"}, {"kind", "auth"}});
      return;
    }
    auto body = parse_body(req);
    std::vector<std::string> names;
    if (body.contains("names") && body["names"].is_array()) {
      for (const auto& n : body["names"]) {
        if (n.is_string()) names.push_back(n.get<std::string>());
      }
    }
    try {
      auto lv = cx_client_.live_check(
          utils::json_str(cred.secret, "account", ""),
          (cred.secret.is_object() && cred.secret.contains("cookies") && cred.secret["cookies"].is_array())
              ? cred.secret["cookies"]
              : nlohmann::json::array(),
          names);
      lv["ok"] = true;
      send_json(res, lv);
    } catch (const adapters::CxError& e) {
      send_json(res, {{"ok", false}, {"error", e.what()}, {"kind", e.kind()}});
    }
  });

  svr_.Get("/api/v1/cx/notices", [this](const httplib::Request& req, httplib::Response& res) {
    auto user = require_user(req, res);
    if (!user) return;
    bool force = req.has_param("force") && req.get_param_value("force") == "1";
    auto snap = db_.get_snapshot(user->id, "cx_notices", "current");
    int64_t now = utils::now_sec();
    if (!force && snap.found && (now - snap.synced_at < 300)) {
      send_json(res, {{"ok", true}, {"list", snap.payload}, {"age", now - snap.synced_at}});
      return;
    }
    auto cred = db_.load_credential(user->id, "chaoxing");
    if (!cred.exists) {
      send_json(res, {{"ok", false}, {"error", "尚未连接学习通"}, {"needLogin", true}});
      return;
    }
    try {
      auto rows = cx_client_.fetch_notices(
          cred.secret.value("account", ""),
          cred.secret.value("cookies", nlohmann::json::array()));
      db_.save_snapshot(user->id, "cx_notices", "current", rows);
      send_json(res, {{"ok", true}, {"list", rows}, {"age", 0}});
    } catch (const adapters::CxError& e) {
      send_json(res, {{"ok", false}, {"error", e.what()}, {"kind", e.kind()}, {"needLogin", e.kind() == "auth"}});
    }
  });

  svr_.Post("/api/v1/cx/logout", [this](const httplib::Request& req, httplib::Response& res) {
    auto user = require_user(req, res);
    if (!user) return;
    db_.delete_credential(user->id, "chaoxing");
    db_.delete_snapshots(user->id, "cx_todo");
    db_.delete_snapshots(user->id, "cx_notices");
    scan_mgr_.reset_user(user->id);
    send_json(res, {{"ok", true}});
  });

  // 10. 中国大学 MOOC（突破浏览器同源限制：支持粘贴 Cookie 加密绑定与扫描）
  svr_.Get("/api/v1/mooc/status", [this](const httplib::Request& req, httplib::Response& res) {
    auto user = require_user(req, res);
    if (!user) return;
    auto cred = db_.load_credential(user->id, "mooc");
    auto snap = db_.get_snapshot(user->id, "mooc_todo", "current");
    int64_t age = snap.found ? std::max<int64_t>(0, utils::now_sec() - snap.synced_at) : -1;
    send_json(res, {
        {"ok", true},
        {"linked", cred.exists},
        {"user", cred.account_masked},
        {"state", cred.state},
        {"age", age},
        {"data", snap.found ? snap.payload : nlohmann::json(nullptr)},
    });
  });

  svr_.Post("/api/v1/mooc/bind", [this](const httplib::Request& req, httplib::Response& res) {
    auto user = require_user(req, res);
    if (!user) return;
    auto body = parse_body(req);
    std::string raw_cookie = utils::json_str(body, "cookie", "");
    try {
      nlohmann::json cookies;
      auto info = mooc_client_.verify_and_bind_cookie(raw_cookie, &cookies);
      std::string nick = utils::json_str(info, "nickName", "MOOC学员");
      db_.save_credential(user->id, "mooc", nick,
                          {{"account", nick}, {"cookies", cookies}}, "ok", "");
      // 绑定成功后立即扫一次
      auto scan_res = mooc_client_.scan(utils::json_bool(info, "demo", false) ? "demo" : nick, cookies);
      db_.save_snapshot(user->id, "mooc_todo", "current", scan_res);
      send_json(res, {{"ok", true}, {"user", nick}, {"data", scan_res}});
    } catch (const adapters::MoocError& e) {
      send_json(res, {{"ok", false}, {"error", e.what()}, {"kind", e.kind()}});
    }
  });

  svr_.Post("/api/v1/mooc/scan", [this](const httplib::Request& req, httplib::Response& res) {
    auto user = require_user(req, res);
    if (!user) return;
    auto cred = db_.load_credential(user->id, "mooc");
    if (!cred.exists) {
      send_json(res, {{"ok", false}, {"error", "尚未绑定中国大学 MOOC"}, {"needLogin", true}});
      return;
    }
    try {
      auto scan_res = mooc_client_.scan(
          utils::json_str(cred.secret, "account", ""),
          (cred.secret.is_object() && cred.secret.contains("cookies") && cred.secret["cookies"].is_array())
              ? cred.secret["cookies"]
              : nlohmann::json::array());
      db_.save_snapshot(user->id, "mooc_todo", "current", scan_res);
      send_json(res, {{"ok", true}, {"data", scan_res}});
    } catch (const adapters::MoocError& e) {
      if (e.kind() == "auth") db_.update_credential_state(user->id, "mooc", "expired", e.what());
      send_json(res, {{"ok", false}, {"error", e.what()}, {"kind", e.kind()}, {"needLogin", e.kind() == "auth"}});
    }
  });

  svr_.Post("/api/v1/mooc/logout", [this](const httplib::Request& req, httplib::Response& res) {
    auto user = require_user(req, res);
    if (!user) return;
    db_.delete_credential(user->id, "mooc");
    db_.delete_snapshots(user->id, "mooc_todo");
    send_json(res, {{"ok", true}});
  });

  // 11. 预留扩展学习平台槽位接口（雨课堂 yuketang / 智慧树 zhihuishu / 知到 zhidao）
  svr_.Get(R"(/api/v1/ext/(yuketang|zhihuishu|zhidao)/status)",
           [this](const httplib::Request& req, httplib::Response& res) {
             auto user = require_user(req, res);
             if (!user) return;
             std::string plat = req.matches[1].str();
             auto cred = db_.load_credential(user->id, plat);
             std::string portal_url = (plat == "yuketang")
                                          ? "https://changjiang.yuketang.cn"
                                          : (plat == "zhihuishu" ? "https://onlineweb.zhihuishu.com"
                                                                 : "https://www.zhihuishu.com");
             send_json(res, {
                 {"ok", true},
                 {"platform", plat},
                 {"reserved", true},
                 {"linked", cred.exists},
                 {"account", cred.account_masked},
                 {"portalUrl", portal_url},
                 {"note", cred.exists ? utils::json_str(cred.secret, "memo", "") : ""},
             });
           });

  svr_.Post(R"(/api/v1/ext/(yuketang|zhihuishu|zhidao)/bind)",
            [this](const httplib::Request& req, httplib::Response& res) {
              auto user = require_user(req, res);
              if (!user) return;
              std::string plat = req.matches[1].str();
              auto body = parse_body(req);
              std::string acc = utils::trim(utils::json_str(body, "account", ""));
              std::string token_or_pwd = utils::json_str(body, "secret", "");
              std::string memo = utils::trim(utils::json_str(body, "memo", ""));
              auto existing = db_.load_credential(user->id, plat);
              if (acc.empty() && existing.exists) {
                acc = utils::json_str(existing.secret, "account", "");
                if (token_or_pwd.empty()) {
                  token_or_pwd = utils::json_str(existing.secret, "secret", "");
                }
              }
              if (acc.empty()) {
                send_json(res, {{"ok", false}, {"error", "请填写该平台的绑定手机号或学号"}});
                return;
              }
              db_.save_credential(user->id, plat, acc,
                                  {{"account", acc}, {"secret", token_or_pwd}, {"memo", memo}},
                                  "ok", "");
              send_json(res, {{"ok", true}, {"platform", plat}, {"account", utils::mask_account(acc)}});
            });

  svr_.Post(R"(/api/v1/ext/(yuketang|zhihuishu|zhidao)/logout)",
            [this](const httplib::Request& req, httplib::Response& res) {
              auto user = require_user(req, res);
              if (!user) return;
              std::string plat = req.matches[1].str();
              db_.delete_credential(user->id, plat);
              send_json(res, {{"ok", true}});
            });

  // 12. 用户问题反馈、匿名错误遥测与管理员数据台
  svr_.Post("/api/v1/feedback", [this](const httplib::Request& req, httplib::Response& res) {
    std::string ip = client_ip_of(req);
    if (!check_rate_limit("feedback:" + ip, 5)) {
      send_json(res, {{"ok", false}, {"error", "提交过于频繁，请稍后再试"}}, 429);
      return;
    }
    auto body = parse_body(req);
    std::string desc = utils::trim(utils::json_str(body, "desc", ""));
    std::string log_txt = utils::json_str(body, "log", "");
    if (desc.empty() && log_txt.empty()) {
      send_json(res, {{"ok", false}, {"error", "请填写问题描述或勾选附带脱敏运行日志"}}, 400);
      return;
    }
    std::optional<int64_t> uid;
    std::string tok = extract_session_token(req);
    if (!tok.empty()) {
      if (auto u = db_.authenticate_session(tok, cfg_.security.session_ttl_days)) uid = u->id;
    }
    db_.insert_feedback(uid,
                        utils::json_str(body, "v", "1.0.0"),
                        utils::json_str(body, "os", ""),
                        desc,
                        log_txt,
                        utils::mask_ip(ip));
    send_json(res, {{"ok", true}, {"message", "反馈已安全送达（敏感信息已完成双重脱敏），感谢支持！"}});
  });

  svr_.Post("/api/v1/telemetry", [this](const httplib::Request& req, httplib::Response& res) {
    auto body = parse_body(req);
    std::string kind = utils::trim(utils::json_str(body, "k", "dau_ping"));
    db_.record_telemetry(kind,
                         utils::json_str(body, "d", ""),
                         utils::json_str(body, "m", ""),
                         utils::json_str(body, "p", ""),
                         utils::json_str(body, "dayId", ""));
    send_json(res, {{"ok", true}});
  });

  svr_.Get("/api/v1/admin/stats", [this](const httplib::Request& req, httplib::Response& res) {
    if (cfg_.security.admin_stats_key.empty()) {
      send_json(res, {{"ok", false}, {"error", "服务端未启用管理员统计密钥"}}, 403);
      return;
    }
    std::string got = req.get_header_value("x-hnu-key");
    if (!crypto::constant_time_equals(got, cfg_.security.admin_stats_key)) {
      send_json(res, {{"ok", false}, {"error", "管理员统计密钥无效"}}, 403);
      return;
    }
    int days = 7;
    if (req.has_param("d")) {
      try { days = std::stoi(req.get_param_value("d")); } catch (...) {}
    }
    send_json(res, db_.get_admin_stats(days));
  });

  // 所有未匹配的 /api/* 统一返回 JSON 404（绝不掉进静态文件返回 HTML 导致前端 JSON 解析异常！）
  svr_.Get(R"(/api/.*)", [this](const httplib::Request&, httplib::Response& res) {
    send_json(res, {{"ok", false}, {"error", "API 接口不存在或请求方法需要使用 POST"}}, 404);
  });
  svr_.Post(R"(/api/.*)", [this](const httplib::Request&, httplib::Response& res) {
    send_json(res, {{"ok", false}, {"error", "API 接口不存在或请求方法需要使用 GET"}}, 404);
  });

  // 统一错误处理器（无论是否开启 serve_static，均确保 /api/* 错误返回 JSON + CORS）
  bool static_enabled = cfg_.server.serve_static && std::filesystem::is_directory(cfg_.server.static_dir);
  if (static_enabled) {
    svr_.set_mount_point("/", cfg_.server.static_dir);
  }
  svr_.set_error_handler([this, static_enabled](const httplib::Request& req, httplib::Response& res) {
    if (utils::starts_with(req.path, "/api/")) {
      if (res.body.empty()) {
        int st = res.status > 0 ? res.status : 404;
        std::string msg = (st >= 500) ? "服务内部错误" : "API 路由未命中或请求方法不支持";
        send_json(res, {{"ok", false}, {"error", msg}}, st);
      }
      return;
    }
    if (static_enabled && req.method == "GET" && res.status == 404) {
      std::filesystem::path idx = std::filesystem::path(cfg_.server.static_dir) / "index.html";
      if (std::filesystem::exists(idx)) {
        std::ifstream ifs(idx, std::ios::binary);
        std::string html((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        res.status = 200;
        res.set_content(html, "text/html; charset=utf-8");
      }
    }
  });
}

bool ApiServer::listen_blocking() {
  HNU_LOG_INFO("http", "Starting HnuTong C++20 Server on " + cfg_.server.host + ":" +
                           std::to_string(cfg_.server.port));
  return svr_.listen(cfg_.server.host, cfg_.server.port);
}

void ApiServer::stop() {
  std::lock_guard<std::mutex> lk(stop_mu_);
  if (stopping_.exchange(true, std::memory_order_acq_rel)) {
    return;
  }
  svr_.stop();
  stop_keepalive_worker();
  {
    std::unique_lock<std::mutex> wlk(notice_worker_mu_);
    notice_worker_cv_.wait(wlk, [this]() { return active_notice_workers_ == 0; });
  }
  scan_mgr_.shutdown();
}

}  // namespace hnu::http
