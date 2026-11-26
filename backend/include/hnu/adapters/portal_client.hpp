#pragma once

#include "hnu/net/http_client.hpp"

#include <mutex>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace hnu::adapters {

class PortalError : public std::runtime_error {
 public:
  // kind: "expired" / "auth" / "code" / "net"
  // code: "PASSERROR" / "CODEFALSE" / "USERLOCK" / ""
  PortalError(const std::string& msg, std::string kind = "net", std::string code = "")
      : std::runtime_error(msg), kind_(std::move(kind)), code_(std::move(code)) {}

  const std::string& kind() const noexcept { return kind_; }
  const std::string& code() const noexcept { return code_; }

  bool should_purge_saved_password() const noexcept {
    return code_ == "PASSERROR" || code_ == "NOUSER" || code_ == "USERLOCK";
  }

 private:
  std::string kind_;
  std::string code_;
};

// 海南大学智慧门户（网上办事大厅 ehall.hainanu.edu.cn + 统一认证 authserver.hainanu.edu.cn）客户端
// 安全机制说明：
// 1. 算术验证码一次性有效（防重放、防暴力撞号触发 5 次锁号）
// 2. 支持已存门户密码免重输（过期重连只需算一道算术验证码）
// 3. 验证码算错（CODEFALSE）绝不误删已保存密码；仅明确密码错误（PASSERROR）才自动清除失效密码
// 4. CAS 免登票据换取严格校验 *.hainanu.edu.cn 域名白名单
class PortalClient {
 public:
  PortalClient(std::string cas_login_url = "https://authserver.hainanu.edu.cn/authserver/login",
               std::string ehall_base_url = "https://ehall.hainanu.edu.cn",
               std::vector<std::string> allowed_domains = {"hainanu.edu.cn"},
               bool enable_demo = true);

  // 生成一次性的门户二次算术验证码挑战：{captchaId, img, timeout}
  nlohmann::json create_captcha_challenge();

  // 执行带算术验证码校验的独立门户登录
  nlohmann::json login(const std::string& username,
                       const std::string& password,
                       const std::string& captcha_id,
                       const std::string& arithmetic_answer,
                       nlohmann::json* out_cookies_to_save = nullptr);

  // 读取门户一卡通（海大 e 卡）实时余额与消费记录
  nlohmann::json fetch_card(const std::string& username, const nlohmann::json& saved_cookies);

  // 读取门户 19 个高频办事应用入口
  nlohmann::json fetch_apps(const std::string& username, const nlohmann::json& saved_cookies);

  // 为校内白名单地址换取一次性 CAS Service Ticket (ST-xxx)
  nlohmann::json exchange_ticket(const std::string& username,
                                 const nlohmann::json& saved_cookies,
                                 const std::string& target_service_url);

 private:
  struct ArithmeticChallenge {
    std::string answer;
    int64_t expires_at = 0;
  };

  std::string cas_login_url_;
  std::string ehall_base_url_;
  std::vector<std::string> allowed_domains_;
  bool enable_demo_ = true;

  std::mutex mu_;
  std::unordered_map<std::string, ArithmeticChallenge> challenges_;
};

}  // namespace hnu::adapters
