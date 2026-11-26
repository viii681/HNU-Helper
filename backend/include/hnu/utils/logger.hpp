#pragma once

#include <atomic>
#include <cstddef>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>

namespace hnu::utils {

enum class LogLevel {
  Debug = 0,
  Info = 1,
  Warn = 2,
  Error = 3,
};

class Logger {
 public:
  static Logger& instance();

  void init(const std::string& file_path,
            size_t max_bytes = 10 * 1024 * 1024,
            size_t max_files = 5,
            LogLevel min_level = LogLevel::Info,
            bool also_stderr = true);

  void log(LogLevel level, std::string_view module, std::string_view message);

  static LogLevel parse_level(std::string_view str);

 private:
  Logger() = default;
  void rotate_locked();

  std::mutex mu_;
  std::string file_path_;
  std::ofstream ofs_;
  size_t max_bytes_ = 10 * 1024 * 1024;
  size_t max_files_ = 5;
  size_t current_bytes_ = 0;
  std::atomic<LogLevel> min_level_{LogLevel::Info};
  bool also_stderr_ = true;
};

#define HNU_LOG_DEBUG(mod, msg) ::hnu::utils::Logger::instance().log(::hnu::utils::LogLevel::Debug, (mod), (msg))
#define HNU_LOG_INFO(mod, msg)  ::hnu::utils::Logger::instance().log(::hnu::utils::LogLevel::Info,  (mod), (msg))
#define HNU_LOG_WARN(mod, msg)  ::hnu::utils::Logger::instance().log(::hnu::utils::LogLevel::Warn,  (mod), (msg))
#define HNU_LOG_ERROR(mod, msg) ::hnu::utils::Logger::instance().log(::hnu::utils::LogLevel::Error, (mod), (msg))

}  // namespace hnu::utils
