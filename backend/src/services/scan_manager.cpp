#include "hnu/services/scan_manager.hpp"
#include "hnu/utils/json_safe.hpp"
#include "hnu/utils/logger.hpp"
#include "hnu/utils/strings_and_time.hpp"

#include <thread>

namespace hnu::services {

ScanManager::ScanManager(db::Database& db, adapters::ChaoxingClient& cx_client)
    : db_(db), cx_client_(cx_client) {}

ScanManager::~ScanManager() {
  shutdown();
}

void ScanManager::shutdown() {
  std::unique_lock<std::mutex> lk(mu_);
  stop_ = true;
  for (auto& [uid, task] : tasks_) {
    task.cancelled = true;
    ++task.generation;
  }
  done_cv_.wait(lk, [this]() { return active_workers_ == 0; });
}

nlohmann::json ScanManager::get_chaoxing_scan_state(int64_t user_id) {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = tasks_.find(user_id);
  if (it == tasks_.end()) {
    return {{"state", "idle"}, {"at", 0}, {"i", 0}, {"n", 0},
            {"course", ""}, {"phase", ""}, {"err", ""}, {"kind", ""}};
  }
  const auto& t = it->second;
  return {
      {"state", t.state},
      {"at", t.at},
      {"i", t.i},
      {"n", t.n},
      {"course", t.course},
      {"phase", t.phase},
      {"err", t.err},
      {"kind", t.kind},
  };
}

void ScanManager::reset_user(int64_t user_id) {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = tasks_.find(user_id);
  if (it == tasks_.end()) return;
  auto& t = it->second;
  int64_t now = utils::now_sec();
  ++t.generation;
  t.cancelled = true;
  t.state = "idle";
  t.at = 0;
  t.i = 0;
  t.n = 0;
  t.course.clear();
  t.phase.clear();
  t.err.clear();
  t.kind.clear();
  t.updated_at = now;
  // 若无运行中线程且无未过期的风控冷却窗口，再安全移除条目
  if (!t.running && t.busy_until <= now) {
    tasks_.erase(it);
  }
}

bool ScanManager::start_chaoxing_scan(int64_t user_id,
                                      const std::vector<std::string>& active_course_names,
                                      bool force_all) {
  int64_t now = utils::now_sec();
  uint64_t my_gen = 0;
  {
    std::lock_guard<std::mutex> lk(mu_);
    if (stop_) return false;
    // 超过容量上限时自动清理已结束且冷却期已过的旧任务，避免长期运行内存无限增长
    if (tasks_.size() > 512) {
      for (auto it = tasks_.begin(); it != tasks_.end();) {
        const auto& tk = it->second;
        if (!tk.running && tk.busy_until <= now && (now - tk.updated_at > 1800)) {
          it = tasks_.erase(it);
        } else {
          ++it;
        }
      }
    }
    auto& t = tasks_[user_id];
    if (t.running || t.state == "scanning") {
      return false;  // 单用户不并发重入（含取消尚未收尾的旧线程），防风控与状态覆写
    }
    if (t.busy_until > now) {
      int left_min = static_cast<int>((t.busy_until - now + 59) / 60);
      t.state = "error";
      t.kind = "net";
      t.err = "超星风控冷却中，请再等约 " + std::to_string(left_min) + " 分钟后再试";
      t.updated_at = now;
      return false;
    }
    my_gen = ++t.generation;
    t.running = true;
    t.cancelled = false;
    t.state = "scanning";
    t.at = 0;
    t.i = 0;
    t.n = 0;
    t.course.clear();
    t.phase = "正在读取课程列表…";
    t.err.clear();
    t.kind.clear();
    t.updated_at = now;
    ++active_workers_;
  }

  std::thread([this, user_id, my_gen, active_course_names, force_all]() {
    struct WorkerGuard {
      ScanManager* self;
      int64_t uid;
      uint64_t gen;
      ~WorkerGuard() {
        std::lock_guard<std::mutex> lk(self->mu_);
        auto it = self->tasks_.find(uid);
        if (it != self->tasks_.end() && it->second.generation == gen) {
          it->second.running = false;
          it->second.updated_at = utils::now_sec();
        } else if (it != self->tasks_.end()) {
          // 已被 reset_user 递增 generation，但仍需清除 running 标记以允许后续重新扫描
          it->second.running = false;
          it->second.updated_at = utils::now_sec();
        }
        --self->active_workers_;
        self->done_cv_.notify_all();
      }
    } guard{this, user_id, my_gen};

    auto is_valid_locked = [this, user_id, my_gen]() -> bool {
      if (stop_) return false;
      auto it = tasks_.find(user_id);
      return it != tasks_.end() && it->second.generation == my_gen && !it->second.cancelled;
    };
    auto is_valid = [this, &is_valid_locked]() -> bool {
      std::lock_guard<std::mutex> lk(mu_);
      return is_valid_locked();
    };

    try {
      if (!is_valid()) return;
      auto cred = db_.load_credential(user_id, "chaoxing");
      if (!cred.exists || cred.state == "broken") {
        throw adapters::CxError("还没连接超星学习通", "auth");
      }
      std::string acc = utils::json_str(cred.secret, "account", "");
      std::string pwd = utils::json_str(cred.secret, "password", "");
      nlohmann::json cookies = (cred.secret.is_object() && cred.secret.contains("cookies") &&
                                cred.secret["cookies"].is_array())
                                   ? cred.secret["cookies"]
                                   : nlohmann::json::array();
      nlohmann::json updated_cookies;

      auto prog = [this, user_id, &is_valid_locked](int cur, int total, const std::string& cname, const std::string& phase) {
        std::lock_guard<std::mutex> lk(mu_);
        if (!is_valid_locked()) return;
        auto it = tasks_.find(user_id);
        if (it != tasks_.end()) {
          it->second.i = cur;
          it->second.n = total;
          it->second.course = cname;
          it->second.phase = phase;
          it->second.updated_at = utils::now_sec();
        }
      };

      nlohmann::json data;
      try {
        data = cx_client_.scan(acc, cookies, active_course_names, force_all, prog, &updated_cookies);
      } catch (const adapters::CxError& e) {
        if (!is_valid()) return;
        // 若超星 Cookie 过期且保险箱存有密码，自动静默重登一次后再扫
        if (e.kind() == "auth" && !pwd.empty()) {
          nlohmann::json relogin_cookies;
          cx_client_.login(acc, pwd, &relogin_cookies);
          if (!is_valid()) return;
          cookies = relogin_cookies;
          data = cx_client_.scan(acc, cookies, active_course_names, force_all, prog, &updated_cookies);
          if (updated_cookies.empty()) updated_cookies = relogin_cookies;
        } else {
          throw;
        }
      }

      if (!is_valid()) return;
      db_.save_snapshot(user_id, "cx_todo", "current", data);

      if (!updated_cookies.empty()) {
        nlohmann::json sec = cred.secret;
        sec["cookies"] = updated_cookies;
        db_.save_credential(user_id, "chaoxing", acc, sec, "ok", "");
      } else {
        db_.update_credential_state(user_id, "chaoxing", "ok", "");
      }

      std::lock_guard<std::mutex> lk(mu_);
      if (!is_valid_locked()) return;
      auto& t = tasks_[user_id];
      t.state = "done";
      t.at = utils::json_i64(data, "at", utils::now_sec());
      t.err.clear();
      t.kind.clear();
      t.updated_at = utils::now_sec();
    } catch (const adapters::CxError& e) {
      if (is_valid() && e.kind() == "auth") {
        db_.update_credential_state(user_id, "chaoxing", "expired", e.what());
      }
      std::lock_guard<std::mutex> lk(mu_);
      auto it = tasks_.find(user_id);
      if (it != tasks_.end() && e.busy()) {
        it->second.busy_until = utils::now_sec() + 15 * 60;
      }
      if (!is_valid_locked()) return;
      auto& t = tasks_[user_id];
      t.state = "error";
      t.err = e.what();
      t.kind = e.kind();
      t.updated_at = utils::now_sec();
    } catch (const std::exception& e) {
      std::lock_guard<std::mutex> lk(mu_);
      if (!is_valid_locked()) return;
      auto& t = tasks_[user_id];
      t.state = "error";
      t.err = std::string("扫描异常：") + e.what();
      t.kind = "net";
      t.updated_at = utils::now_sec();
    }
  }).detach();

  return true;
}

}  // namespace hnu::services
