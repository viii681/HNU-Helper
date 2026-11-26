#pragma once

#include "hnu/adapters/chaoxing_client.hpp"
#include "hnu/db/database.hpp"

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace hnu::services {

// 多用户隔离的后台异步学习通扫描调度器
// 按 user_id 独立维护扫描进度与 HTTP 202 风控冷却窗口，支持多学生并发安全运行
class ScanManager {
 public:
  ScanManager(db::Database& db, adapters::ChaoxingClient& cx_client);
  ~ScanManager();

  bool start_chaoxing_scan(int64_t user_id,
                           const std::vector<std::string>& active_course_names,
                           bool force_all);

  nlohmann::json get_chaoxing_scan_state(int64_t user_id);
  void reset_user(int64_t user_id);
  void shutdown();

 private:
  struct UserScanTask {
    std::string state = "idle";  // idle / scanning / done / error
    int64_t at = 0;
    int i = 0;
    int n = 0;
    std::string course;
    std::string phase;
    std::string err;
    std::string kind;
    int64_t busy_until = 0;      // 触发 202 风控后的 15 分钟冷静期截止秒
    uint64_t generation = 0;     // 代际号：用于识别重置/登出后的旧扫描线程，防止数据回魂
    bool running = false;        // 是否仍有后台线程在执行（含已取消但尚未返回的线程）
    bool cancelled = false;
    int64_t updated_at = 0;
  };

  db::Database& db_;
  adapters::ChaoxingClient& cx_client_;
  std::mutex mu_;
  std::condition_variable done_cv_;
  bool stop_ = false;
  int active_workers_ = 0;
  std::unordered_map<int64_t, UserScanTask> tasks_;
};

}  // namespace hnu::services
