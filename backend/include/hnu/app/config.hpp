#pragma once

#include <string>
#include <vector>

namespace hnu::app {

struct ServerConfig {
  std::string host = "127.0.0.1";
  int port = 8788;
  int threads = 8;
  std::string static_dir = "../frontend/dist";
  bool serve_static = true;
  std::string cors_allowed_origin = "";
  bool trust_proxy_headers = false;
};

struct SecurityConfig {
  std::string master_key_file = "./data/master.key";
  int session_ttl_days = 365;
  std::string admin_stats_key = "";
  int rate_limit_login_per_min = 20;
  int rate_limit_api_per_min = 240;
};

struct DatabaseConfig {
  std::string sqlite_path = "./data/hnutong.db";
  std::string schema_sql_path = "./sql/schema.sql";
  std::string seed_sql_path = "./config/hnu_seed.sql";
};

struct LoggingConfig {
  std::string log_file = "./data/hnutong.log";
  size_t max_bytes = 10 * 1024 * 1024;
  size_t max_files = 5;
  std::string level = "info";
};

struct HnuCampusConfig {
  bool enable_demo_account = true;
  std::string jwxt_base_url = "https://jxgl.hainanu.edu.cn";
  std::string cas_login_url = "https://authserver.hainanu.edu.cn/authserver/login";
  std::string ehall_base_url = "https://ehall.hainanu.edu.cn";
  std::vector<std::string> allowed_ticket_domains = {"hainanu.edu.cn"};
  std::vector<std::string> allowed_notice_domains = {"hainanu.edu.cn"};
  int notice_ttl_seconds = 1800;
  int polite_delay_ms = 120;
};

struct AppConfig {
  ServerConfig server;
  SecurityConfig security;
  DatabaseConfig database;
  LoggingConfig logging;
  HnuCampusConfig hnu;

  static AppConfig load_from_file(const std::string& path);
};

}  // namespace hnu::app
