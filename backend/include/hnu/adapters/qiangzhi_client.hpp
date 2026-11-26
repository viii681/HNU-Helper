#pragma once

#include "hnu/net/http_client.hpp"

#include <atomic>
#include <functional>
#include <mutex>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace hnu::adapters {

using SyncProgressCallback = std::function<void(
    int percent, int step, int total_steps, const std::string& stage, const std::string& detail)>;

class JwError : public std::runtime_error {
 public:
  // kind:
  //   "auth"     : 学号或密码不对 / 账号锁定
  //   "captcha"  : 需要图形验证码或验证码输入错误
  //   "expired"  : 登录会话已过期且静默续期未通过
  //   "nottable" : 教务尚未排课 / 暂无课表
  //   "readfail" : 单页读取失败（前端保留上一次有效缓存，绝不覆盖为空！）
  //   "net"      : 网络不可达或教务服务器 5xx
  JwError(const std::string& msg, std::string kind = "net", std::string diag = "")
      : std::runtime_error(msg), kind_(std::move(kind)), diag_(std::move(diag)) {}

  const std::string& kind() const noexcept { return kind_; }
  const std::string& diag() const noexcept { return diag_; }

 private:
  std::string kind_;
  std::string diag_;
};

// 纯解析函数（与网络解耦，供单元测试直接喂真实强智 HTML 样本检验）
nlohmann::json parse_qiangzhi_timetable_html(std::string_view html,
                                             std::string_view fallback_semester = "");
nlohmann::json parse_qiangzhi_grades_html(std::string_view html,
                                          std::string_view source_hint = "all",
                                          std::string_view fallback_term = "");
nlohmann::json parse_qiangzhi_pscj_html(std::string_view html);
nlohmann::json parse_qiangzhi_exams_html(std::string_view html);
nlohmann::json parse_qiangzhi_profile_html(std::string_view html);
nlohmann::json parse_qiangzhi_plan_html(std::string_view html);
nlohmann::json parse_qiangzhi_grad_summary_html(std::string_view html);
nlohmann::json parse_qiangzhi_semesters_html(std::string_view html);

// 海南大学强智科技（教学一体化服务平台 https://jxgl.hainanu.edu.cn/jsxsd/）客户端
class QiangzhiClient {
 public:
  explicit QiangzhiClient(std::string base_url = "https://jxgl.hainanu.edu.cn",
                          int polite_delay_ms = 120,
                          bool enable_demo = true,
                          std::string cas_login_url = "https://authserver.hainanu.edu.cn/authserver/login");
  ~QiangzhiClient();

  // 拉取登录图形验证码 (/jsxsd/verifycode.servlet)，支持后台预热秒开与原会话快速换图
  nlohmann::json fetch_login_captcha(const std::string& reuse_captcha_id = "");

  // 登录并执行全量同步（双通道：① LoginToXkLdap 验证码通道；② 海大统一身份认证 CAS SSO 免验证码通道；③ 演示沙盒通道）
  nlohmann::json login_and_sync(const std::string& username,
                                const std::string& password,
                                const std::string& captcha_id,
                                const std::string& captcha_code,
                                nlohmann::json* out_cookies_to_save = nullptr,
                                SyncProgressCallback progress = nullptr,
                                const nlohmann::json& seed_cookies = nlohmann::json::array());

  // 使用已存凭据与 Cookie 会话同步（当 bzb_jsxsd 过期时自动通过海大 CAS SSO / CASTGC 静默无感续期！）
  nlohmann::json sync_with_saved(const std::string& username,
                                 const std::string& password,
                                 const nlohmann::json& saved_cookies,
                                 nlohmann::json* out_updated_cookies = nullptr,
                                 SyncProgressCallback progress = nullptr);

  // 后台定时轻量保活：延长强智 Tomcat 会话生命周期，并在单次会话自然过期时用 CASTGC/CAS 静默续期
  bool keepalive_session(const std::string& username,
                         const std::string& password,
                         const nlohmann::json& saved_cookies,
                         nlohmann::json* out_updated_cookies = nullptr);

  nlohmann::json fetch_timetable_for(const std::string& username,
                                     const std::string& password,
                                     const nlohmann::json& saved_cookies,
                                     const std::string& semester_id,
                                     nlohmann::json* out_updated_cookies = nullptr);

  nlohmann::json fetch_exams_for(const std::string& username,
                                 const std::string& password,
                                 const nlohmann::json& saved_cookies,
                                 const std::string& semester_id,
                                 nlohmann::json* out_updated_cookies = nullptr);

  // 按学期（留空=全部学期）实时查询成绩，自动合并 期中 / 期末 / 平时 / 实验 / 总评 多通道数据
  nlohmann::json fetch_grades_for(const std::string& username,
                                  const std::string& password,
                                  const nlohmann::json& saved_cookies,
                                  const std::string& semester_id = "",
                                  nlohmann::json* out_updated_cookies = nullptr,
                                  SyncProgressCallback progress = nullptr);

  nlohmann::json fetch_semesters(const std::string& username,
                                 const std::string& password,
                                 const nlohmann::json& saved_cookies,
                                 nlohmann::json* out_updated_cookies = nullptr);

  nlohmann::json fetch_jw_todo(const std::string& username,
                               const std::string& password,
                               const nlohmann::json& saved_cookies,
                               nlohmann::json* out_updated_cookies = nullptr);

 private:
  struct PendingCaptcha {
    nlohmann::json cookies;
    std::string demo_answer;
    int64_t expires_at = 0;
    bool demo_only = false;
  };

  struct WarmCaptcha {
    bool ready = false;
    std::string img_data_uri;
    nlohmann::json cookies;
    int64_t fetched_at = 0;
  };

  bool fetch_live_captcha_raw(const nlohmann::json& seed_cookies,
                              std::string& out_data_uri,
                              nlohmann::json& out_cookies);
  void trigger_warm_replenish();

  bool try_cas_sso_login(net::HttpSession& sess,
                         const std::string& username,
                         const std::string& password,
                         bool* out_net_error = nullptr);
  bool try_isolated_cas_relogin(net::HttpSession& sess,
                                const std::string& username,
                                const std::string& password,
                                nlohmann::json* out_updated_cookies,
                                bool* out_net_error = nullptr);

  bool has_qz_session_cookie(const net::HttpSession& sess) const;
  bool ensure_session_or_relogin(net::HttpSession& sess,
                                 const std::string& username,
                                 const std::string& password,
                                 nlohmann::json* out_updated_cookies);

  nlohmann::json fetch_grades_on_session(net::HttpSession& sess,
                                         const std::string& cur_sem_id,
                                         const std::string& filter_semester,
                                         SyncProgressCallback progress = nullptr);

  nlohmann::json run_full_sync_on_session(net::HttpSession& sess,
                                          const std::string& username,
                                          SyncProgressCallback progress = nullptr);

  std::string base_url_;
  std::string cas_login_url_;
  int polite_delay_ms_ = 120;
  bool enable_demo_ = true;

  std::mutex cap_mu_;
  std::unordered_map<std::string, PendingCaptcha> pending_captchas_;
  WarmCaptcha warm_captcha_;
  std::atomic<bool> warm_inflight_{false};
  std::atomic<bool> shutting_down_{false};
  std::mutex warm_thread_mu_;
  std::thread warm_thread_;
};

}  // namespace hnu::adapters
