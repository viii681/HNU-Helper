#include "hnu/app/config.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace hnu::app {

AppConfig AppConfig::load_from_file(const std::string& path) {
  AppConfig cfg;
  if (!path.empty() && std::filesystem::exists(path)) {
    std::ifstream ifs(path);
    nlohmann::json j = nlohmann::json::parse(ifs, nullptr, false);
    if (j.is_object()) {
      if (j.contains("server") && j["server"].is_object()) {
        const auto& s = j["server"];
        cfg.server.host = s.value("host", cfg.server.host);
        cfg.server.port = s.value("port", cfg.server.port);
        cfg.server.threads = s.value("threads", cfg.server.threads);
        cfg.server.static_dir = s.value("static_dir", cfg.server.static_dir);
        cfg.server.serve_static = s.value("serve_static", cfg.server.serve_static);
        cfg.server.cors_allowed_origin = s.value("cors_allowed_origin", cfg.server.cors_allowed_origin);
        cfg.server.trust_proxy_headers = s.value("trust_proxy_headers", cfg.server.trust_proxy_headers);
      }
      if (j.contains("security") && j["security"].is_object()) {
        const auto& sec = j["security"];
        cfg.security.master_key_file = sec.value("master_key_file", cfg.security.master_key_file);
        cfg.security.session_ttl_days = sec.value("session_ttl_days", cfg.security.session_ttl_days);
        cfg.security.admin_stats_key = sec.value("admin_stats_key", cfg.security.admin_stats_key);
        cfg.security.rate_limit_login_per_min = sec.value("rate_limit_login_per_min", cfg.security.rate_limit_login_per_min);
        cfg.security.rate_limit_api_per_min = sec.value("rate_limit_api_per_min", cfg.security.rate_limit_api_per_min);
      }
      if (j.contains("database") && j["database"].is_object()) {
        const auto& db = j["database"];
        cfg.database.sqlite_path = db.value("sqlite_path", cfg.database.sqlite_path);
        cfg.database.schema_sql_path = db.value("schema_sql_path", cfg.database.schema_sql_path);
        cfg.database.seed_sql_path = db.value("seed_sql_path", cfg.database.seed_sql_path);
      }
      if (j.contains("logging") && j["logging"].is_object()) {
        const auto& lg = j["logging"];
        cfg.logging.log_file = lg.value("log_file", cfg.logging.log_file);
        cfg.logging.max_bytes = lg.value("max_bytes", cfg.logging.max_bytes);
        cfg.logging.max_files = lg.value("max_files", cfg.logging.max_files);
        cfg.logging.level = lg.value("level", cfg.logging.level);
      }
      if (j.contains("hnu") && j["hnu"].is_object()) {
        const auto& h = j["hnu"];
        cfg.hnu.enable_demo_account = h.value("enable_demo_account", cfg.hnu.enable_demo_account);
        cfg.hnu.jwxt_base_url = h.value("jwxt_base_url", cfg.hnu.jwxt_base_url);
        cfg.hnu.cas_login_url = h.value("cas_login_url", cfg.hnu.cas_login_url);
        cfg.hnu.ehall_base_url = h.value("ehall_base_url", cfg.hnu.ehall_base_url);
        if (h.contains("allowed_ticket_domains") && h["allowed_ticket_domains"].is_array()) {
          cfg.hnu.allowed_ticket_domains = h["allowed_ticket_domains"].get<std::vector<std::string>>();
        }
        if (h.contains("allowed_notice_domains") && h["allowed_notice_domains"].is_array()) {
          cfg.hnu.allowed_notice_domains = h["allowed_notice_domains"].get<std::vector<std::string>>();
        }
        cfg.hnu.notice_ttl_seconds = h.value("notice_ttl_seconds", cfg.hnu.notice_ttl_seconds);
        cfg.hnu.polite_delay_ms = h.value("polite_delay_ms", cfg.hnu.polite_delay_ms);
      }
    }
  }
  if (const char* env_admin = std::getenv("HNU_ADMIN_STATS_KEY")) {
    if (*env_admin) cfg.security.admin_stats_key = env_admin;
  }
  return cfg;
}

}  // namespace hnu::app
