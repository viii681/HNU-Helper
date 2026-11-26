#include "hnu/utils/logger.hpp"
#include "hnu/utils/strings_and_time.hpp"

#include <filesystem>
#include <iostream>

namespace hnu::utils {

Logger& Logger::instance() {
  static Logger inst;
  return inst;
}

LogLevel Logger::parse_level(std::string_view str) {
  std::string s = to_lower(trim(str));
  if (s == "debug") return LogLevel::Debug;
  if (s == "warn" || s == "warning") return LogLevel::Warn;
  if (s == "error") return LogLevel::Error;
  return LogLevel::Info;
}

void Logger::init(const std::string& file_path,
                  size_t max_bytes,
                  size_t max_files,
                  LogLevel min_level,
                  bool also_stderr) {
  std::lock_guard<std::mutex> lk(mu_);
  if (ofs_.is_open()) {
    ofs_.close();
  }
  ofs_.clear();
  file_path_ = file_path;
  max_bytes_ = max_bytes > 65536 ? max_bytes : 65536;
  max_files_ = max_files > 0 ? max_files : 1;
  min_level_.store(min_level, std::memory_order_release);
  also_stderr_ = also_stderr;
  current_bytes_ = 0;

  if (!file_path_.empty()) {
    try {
      std::filesystem::path p(file_path_);
      if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
      }
      if (std::filesystem::exists(p)) {
        current_bytes_ = static_cast<size_t>(std::filesystem::file_size(p));
      } else {
        current_bytes_ = 0;
      }
      ofs_.open(file_path_, std::ios::app | std::ios::out);
    } catch (...) {
      // 日志打开失败绝不影响业务进程存活
    }
  }
}

void Logger::rotate_locked() {
  if (file_path_.empty()) return;
  try {
    if (ofs_.is_open()) ofs_.close();
    for (size_t i = max_files_; i >= 1; --i) {
      std::filesystem::path src = (i == 1)
                                      ? std::filesystem::path(file_path_)
                                      : std::filesystem::path(file_path_ + "." + std::to_string(i - 1));
      std::filesystem::path dst(file_path_ + "." + std::to_string(i));
      if (std::filesystem::exists(src)) {
        std::error_code ec;
        std::filesystem::remove(dst, ec);
        std::filesystem::rename(src, dst, ec);
      }
    }
    current_bytes_ = 0;
    ofs_.open(file_path_, std::ios::trunc | std::ios::out);
  } catch (...) {
  }
}

void Logger::log(LogLevel level, std::string_view module, std::string_view message) {
  if (level < min_level_.load(std::memory_order_acquire)) return;
  const char* lv = "INFO";
  switch (level) {
    case LogLevel::Debug: lv = "DEBUG"; break;
    case LogLevel::Info:  lv = "INFO "; break;
    case LogLevel::Warn:  lv = "WARN "; break;
    case LogLevel::Error: lv = "ERROR"; break;
  }

  // 所有写入磁盘和标准错误的日志强制经过 PII 脱敏过滤
  std::string safe_msg = scrub_pii(message, 600);
  std::string line = "[" + bj_datetime_str() + "] [" + lv + "] [" +
                     std::string(module) + "] " + safe_msg + "\n";

  std::lock_guard<std::mutex> lk(mu_);
  if (also_stderr_) {
    std::cerr << line;
  }
  if (ofs_.is_open()) {
    if (current_bytes_ + line.size() > max_bytes_) {
      rotate_locked();
    }
    if (ofs_.is_open()) {
      ofs_ << line;
      ofs_.flush();
      current_bytes_ += line.size();
    }
  }
}

}  // namespace hnu::utils
