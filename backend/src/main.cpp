#include "hnu/app/config.hpp"
#include "hnu/crypto/crypto.hpp"
#include "hnu/db/database.hpp"
#include "hnu/http/api_server.hpp"
#include "hnu/utils/logger.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

namespace {

volatile std::sig_atomic_t g_stop_signal = 0;

void handle_signal(int sig) {
  g_stop_signal = sig;
}

}  // namespace

int main(int argc, char* argv[]) {
  std::string config_path = "./config/config.json";
  int override_port = 0;
  std::string override_host;
  std::string override_static;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if ((arg == "--config" || arg == "-c") && i + 1 < argc) {
      config_path = argv[++i];
    } else if ((arg == "--port" || arg == "-p") && i + 1 < argc) {
      override_port = std::atoi(argv[++i]);
    } else if (arg == "--host" && i + 1 < argc) {
      override_host = argv[++i];
    } else if (arg == "--static-dir" && i + 1 < argc) {
      override_static = argv[++i];
    } else if (arg == "--help" || arg == "-h") {
      std::cout << "海大通 (HnuTong) · 海南大学校园一体化系统 C++20 服务端\n"
                << "用法: hnutong_server [选项]\n"
                << "  -c, --config <path>      指定配置文件路径 (默认 ./config/config.json)\n"
                << "  -p, --port <port>        覆盖监听端口 (默认 8788)\n"
                << "      --host <ip>          覆盖监听地址 (默认 127.0.0.1)\n"
                << "      --static-dir <dir>   覆盖前端静态构建目录 (默认 ../frontend/dist)\n";
      return 0;
    }
  }

  try {
    auto cfg = hnu::app::AppConfig::load_from_file(config_path);
    if (override_port > 0) cfg.server.port = override_port;
    if (!override_host.empty()) cfg.server.host = override_host;
    if (!override_static.empty()) cfg.server.static_dir = override_static;

    hnu::utils::Logger::instance().init(
        cfg.logging.log_file,
        cfg.logging.max_bytes,
        cfg.logging.max_files,
        hnu::utils::Logger::parse_level(cfg.logging.level),
        true);

    auto master_key = hnu::crypto::load_or_create_master_key(cfg.security.master_key_file);
    hnu::db::Database db(
        cfg.database.sqlite_path,
        master_key,
        cfg.database.schema_sql_path,
        cfg.database.seed_sql_path);
    db.purge_expired_sessions();

    hnu::http::ApiServer server(cfg, db);
    g_stop_signal = 0;

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    std::atomic<bool> server_exited{false};
    std::thread sig_watcher([&]() {
      while (!server_exited.load(std::memory_order_acquire)) {
        std::sig_atomic_t sig = g_stop_signal;
        if (sig != 0) {
          HNU_LOG_INFO("main", "Received termination signal (" + std::to_string(sig) + "), shutting down...");
          server.stop();
          break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }
    });

    std::cout << "\n"
              << "  ======================================================\n"
              << "  海大通 (HnuTong) · 海南大学校园一体化服务系统 C++ 后端\n"
              << "  监听地址: http://" << cfg.server.host << ":" << cfg.server.port << "\n"
              << "  数据库  : " << cfg.database.sqlite_path << " (SQLite WAL)\n"
              << "  演示账号: " << (cfg.hnu.enable_demo_account ? "已开启 (2024300128 / hnu123456)" : "已关闭") << "\n"
              << "  ======================================================\n\n";

    bool ok = server.listen_blocking();
    server_exited.store(true, std::memory_order_release);
    if (sig_watcher.joinable()) sig_watcher.join();
    if (!ok) {
      HNU_LOG_ERROR("main", "Failed to bind " + cfg.server.host + ":" + std::to_string(cfg.server.port));
      return 2;
    }
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "Fatal startup error: " << ex.what() << "\n";
    return 1;
  }
}
