#pragma once

#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <string_view>

namespace hnu::adapters {

class MoocError : public std::runtime_error {
 public:
  MoocError(const std::string& msg, std::string kind = "net")
      : std::runtime_error(msg), kind_(std::move(kind)) {}
  const std::string& kind() const noexcept { return kind_; }

 private:
  std::string kind_;
};

// 中国大学 MOOC（爱课程 icourse163.org）只读客户端
// 突破原网页版 webOnly 限制：支持用户在 B/S 浏览器端导入 Cookie 字符串或 JSON 绑定并由 C++ 服务端加密托管扫描！
class MoocClient {
 public:
  explicit MoocClient(bool enable_demo = true) : enable_demo_(enable_demo) {}

  // 解析用户粘贴的原始 Cookie 串（如 "STUDY_SESS=...; NTESSTUDYSI=...; STUDY_INFO=..."）并验证登录态
  nlohmann::json verify_and_bind_cookie(std::string_view raw_cookie_input,
                                        nlohmann::json* out_cookies_json = nullptr);

  // 扫描全部在修课程章节作业、测验与期末考试
  nlohmann::json scan(const std::string& account_hint,
                      const nlohmann::json& saved_cookies);

 private:
  bool enable_demo_ = true;
};

}  // namespace hnu::adapters
