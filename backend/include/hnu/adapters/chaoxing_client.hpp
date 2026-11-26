#pragma once

#include "hnu/net/http_client.hpp"

#include <functional>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace hnu::adapters {

class CxError : public std::runtime_error {
 public:
  CxError(const std::string& msg, std::string kind = "net", bool busy = false)
      : std::runtime_error(msg), kind_(std::move(kind)), busy_(busy) {}

  const std::string& kind() const noexcept { return kind_; }
  bool busy() const noexcept { return busy_; }

 private:
  std::string kind_;
  bool busy_ = false;
};

// 纯函数供单元测试直接验证状态归类、课程列表容错解析与加密一致性
std::string chaoxing_aes_encrypt(std::string_view text);
std::string chaoxing_work_bucket(std::string_view status_text);
nlohmann::json parse_chaoxing_courses_json(std::string_view json_text,
                                           std::string_view cookie_fid = "");

// 超星学习通只读客户端（严守学术诚信红线：只读待办与课堂活动提醒，不自动签到/刷课/答题）
class ChaoxingClient {
 public:
  using ProgressCallback = std::function<void(int cur, int total, const std::string& course, const std::string& phase)>;

  explicit ChaoxingClient(bool enable_demo = true) : enable_demo_(enable_demo) {}

  // 登录（AES-128-CBC 加密，失败绝不自动重试以防触发滑块验证）
  nlohmann::json login(const std::string& phone_or_account,
                       const std::string& password,
                       nlohmann::json* out_cookies_to_save = nullptr);

  // 扫描课程作业、考试与课堂活动（支持本学期海大课表白名单降频 + HTTP 202 风控熔断）
  nlohmann::json scan(const std::string& account,
                      const nlohmann::json& saved_cookies,
                      const std::vector<std::string>& active_course_names,
                      bool force_all,
                      ProgressCallback on_progress = nullptr,
                      nlohmann::json* out_updated_cookies = nullptr);

  // 今日有课的随堂练习/限时签到秒级快查
  nlohmann::json live_check(const std::string& account,
                            const nlohmann::json& saved_cookies,
                            const std::vector<std::string>& today_course_names);

  // 通知中心收件箱拉取
  nlohmann::json fetch_notices(const std::string& account,
                               const nlohmann::json& saved_cookies);

 private:
  nlohmann::json fetch_courses_on_session(net::HttpSession& sess);

  bool enable_demo_ = true;
};

}  // namespace hnu::adapters
