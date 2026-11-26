#include "hnu/adapters/portal_client.hpp"
#include "hnu/adapters/sandbox_provider.hpp"
#include "hnu/crypto/crypto.hpp"
#include "hnu/utils/json_safe.hpp"
#include "hnu/utils/strings_and_time.hpp"

#include <regex>

namespace hnu::adapters {

namespace {

// 属性顺序无关地提取 <input ...> 的 value 值（支持 id/name 在 value 前或后）
std::string extract_input_attr_value(const std::string& html,
                                     const std::string& attr_key,
                                     const std::string& attr_val) {
  std::regex re1("<input\\b[^>]*\\b" + attr_key + "\\s*=\\s*[\"']" + attr_val +
                     "[\"'][^>]*\\bvalue\\s*=\\s*[\"']([^\"']*)[\"']",
                 std::regex::icase);
  std::smatch m;
  if (std::regex_search(html, m, re1)) return m[1].str();
  std::regex re2("<input\\b[^>]*\\bvalue\\s*=\\s*[\"']([^\"']*)[\"'][^>]*\\b" + attr_key +
                     "\\s*=\\s*[\"']" + attr_val + "[\"']",
                 std::regex::icase);
  if (std::regex_search(html, m, re2)) return m[1].str();
  return "";
}

bool looks_like_html_login_page(std::string_view text) {
  std::string low = utils::to_lower(text.substr(0, std::min<size_t>(text.size(), 1200)));
  return low.find("<html") != std::string::npos ||
         low.find("<!doctype") != std::string::npos ||
         low.find("authserver") != std::string::npos ||
         low.find("pwdencryptsalt") != std::string::npos;
}

net::HttpResponse ehall_get_with_sso_refresh(net::HttpSession& sess,
                                             const std::string& ehall_base_url,
                                             const std::string& api_url,
                                             const std::vector<std::string>& allowed_domains) {
  auto r = sess.get(api_url, {{"Referer", ehall_base_url + "/new/index.html"}}, false, 12000);
  if ((r.status == 302 || r.status == 401 || r.status == 403 ||
       (r.status == 200 && looks_like_html_login_page(r.text_utf8()))) &&
      !sess.jar().get_cookie("CASTGC").empty()) {
    // MOD_AUTH_CAS 失效但 CASTGC 仍有效时，尝试静默通过 CAS SSO 刷新一次 ehall 会话
    net::HttpRequestOptions sso_opts;
    sso_opts.method = "GET";
    sso_opts.url = ehall_base_url + "/login?service=" + utils::url_encode(ehall_base_url + "/new/index.html");
    sso_opts.follow_redirects = true;
    sso_opts.max_redirects = 6;
    sso_opts.timeout_ms = 12000;
    sso_opts.allowed_redirect_domains = allowed_domains;
    sess.request(sso_opts);
    r = sess.get(api_url, {{"Referer", ehall_base_url + "/new/index.html"}}, false, 12000);
  }
  return r;
}

}  // namespace

PortalClient::PortalClient(std::string cas_login_url,
                           std::string ehall_base_url,
                           std::vector<std::string> allowed_domains,
                           bool enable_demo)
    : cas_login_url_(std::move(cas_login_url)),
      ehall_base_url_(std::move(ehall_base_url)),
      allowed_domains_(std::move(allowed_domains)),
      enable_demo_(enable_demo) {
  while (!ehall_base_url_.empty() && ehall_base_url_.back() == '/') {
    ehall_base_url_.pop_back();
  }
}

nlohmann::json PortalClient::create_captcha_challenge() {
  auto [ans, svg_uri] = SandboxProvider::generate_arithmetic_captcha_svg();
  std::string cid = crypto::random_urlsafe_token(16);
  int64_t now = utils::now_sec();

  {
    std::lock_guard<std::mutex> lk(mu_);
    for (auto it = challenges_.begin(); it != challenges_.end();) {
      if (it->second.expires_at < now) it = challenges_.erase(it);
      else ++it;
    }
    challenges_[cid] = ArithmeticChallenge{ans, now + 600};
  }
  return {
      {"ok", true},
      {"captchaId", cid},
      {"img", svg_uri},
      {"timeout", 600},
  };
}

nlohmann::json PortalClient::login(const std::string& username,
                                   const std::string& password,
                                   const std::string& captcha_id,
                                   const std::string& arithmetic_answer,
                                   nlohmann::json* out_cookies_to_save) {
  std::string code = utils::trim(arithmetic_answer);
  if (code.empty()) {
    throw PortalError("请先计算并填写算术验证码", "code", "CODEFALSE");
  }

  // 1. 一次性消费算术验证码（不管后续哪一步成败，该验证码当场销毁，防止重放）
  std::string expected;
  bool found = false;
  int64_t exp = 0;
  {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = challenges_.find(captcha_id);
    if (it != challenges_.end()) {
      expected = it->second.answer;
      exp = it->second.expires_at;
      challenges_.erase(it);
      found = true;
    }
  }
  if (!found || exp < utils::now_sec()) {
    throw PortalError("算术验证码已过期，已为您刷新，请重算一次", "code", "CODEFALSE");
  }
  if (code != expected) {
    throw PortalError("算术验证码算错了，换一张重算", "code", "CODEFALSE");
  }

  // 2. 演示账号闭环校验
  if (enable_demo_ && SandboxProvider::is_demo_account(username)) {
    if (!SandboxProvider::verify_demo_password(username, password)) {
      throw PortalError("门户账号或密码不正确", "auth", "PASSERROR");
    }
    net::CookieJar jar;
    jar.set_cookie({"CASTGC", "TGT-demo-" + crypto::random_hex(12), "authserver.hainanu.edu.cn", "/", true, utils::now_sec() + 25920});
    jar.set_cookie({"MOD_AUTH_CAS", "ST-demo-" + crypto::random_hex(8), "ehall.hainanu.edu.cn", "/", true, utils::now_sec() + 7200});
    if (out_cookies_to_save) *out_cookies_to_save = jar.dump_json();
    return {{"user", username == "demo" ? "2024300128" : username}, {"demo", true}};
  }

  // 3. 真实海南大学统一身份认证 CAS (authserver.hainanu.edu.cn) + 强智 LDAP 双重鉴权兼容
  net::HttpSession sess(300);
  std::string service_url = ehall_base_url_ + "/login?service=" + utils::url_encode(ehall_base_url_ + "/new/index.html");
  auto r_get = sess.get(cas_login_url_ + "?service=" + utils::url_encode(service_url), {}, true, 12000);

  if (r_get.status == 200) {
    std::string html = r_get.text_utf8();
    // 精准截取 <form ... id="pwdFromId"> 表单块，防止被前面的 fidoLogin / dynamicLogin 隐藏字段干扰
    std::string pwd_form = html;
    size_t f_pos = html.find("id=\"pwdFromId\"");
    if (f_pos == std::string::npos) f_pos = html.find("id='pwdFromId'");
    if (f_pos != std::string::npos) {
      size_t f_end = html.find("</form>", f_pos);
      if (f_end != std::string::npos) pwd_form = html.substr(f_pos, f_end - f_pos + 7);
    }

    static const std::regex re_script_salt(R"(pwdDefaultEncryptSalt\s*=\s*["']([^"']+)["'])");
    std::smatch m_salt;
    std::string salt = extract_input_attr_value(pwd_form, "id", "pwdEncryptSalt");
    if (salt.empty() && std::regex_search(pwd_form, m_salt, re_script_salt)) {
      salt = m_salt[1].str();
    }
    if (salt.empty()) {
      salt = extract_input_attr_value(html, "id", "pwdEncryptSalt");
    }
    if (salt.empty() && std::regex_search(html, m_salt, re_script_salt)) {
      salt = m_salt[1].str();
    }
    std::string execution = extract_input_attr_value(pwd_form, "name", "execution");
    if (execution.empty()) execution = extract_input_attr_value(html, "name", "execution");
    if (execution.empty()) execution = "e1s1";
    std::string lt = extract_input_attr_value(pwd_form, "name", "lt");
    if (lt.empty()) lt = extract_input_attr_value(html, "name", "lt");

    std::string enc_pwd = (salt.size() == 16)
                              ? crypto::wisedu_cas_encrypt_password(password, salt)
                              : password;

    std::vector<std::pair<std::string, std::string>> form = {
        {"username", username},
        {"password", enc_pwd},
        {"captcha", ""},
        {"execution", execution},
        {"_eventId", "submit"},
        {"cllt", "userNameLogin"},
        {"dllt", "generalLogin"},
        {"lt", lt},
    };

    auto r_post = sess.post_form(r_get.final_url, form, {{"Referer", r_get.final_url}}, true, 18000);
    std::string post_html = r_post.text_utf8();
    bool has_tgt = !sess.jar().get_cookie("CASTGC").empty();
    bool landed_ehall = (r_post.final_url.find("ehall.hainanu.edu.cn") != std::string::npos &&
                         r_post.final_url.find("authserver") == std::string::npos);

    if (has_tgt || landed_ehall) {
      if (out_cookies_to_save) *out_cookies_to_save = sess.jar().dump_json();
      return {{"user", username}};
    }

    if (post_html.find("密码") != std::string::npos &&
        (post_html.find("有误") != std::string::npos || post_html.find("错误") != std::string::npos)) {
      throw PortalError("海南大学统一认证提示：学号或密码有误", "auth", "PASSERROR");
    }
    if (post_html.find("冻结") != std::string::npos || post_html.find("锁定") != std::string::npos) {
      throw PortalError("账号已被临时锁定，请稍后再试或前往统一身份认证平台重置", "auth", "USERLOCK");
    }
    if (post_html.find("验证码") != std::string::npos || post_html.find("滑块") != std::string::npos) {
      throw PortalError("学校统一认证触发了二次图形校验，请稍后再试", "code", "CODEFALSE");
    }
    if (r_post.status > 0) {
      throw PortalError("统一身份认证未通过（账号可能不存在、需修改初始密码或页面结构变更），请核对学号密码或先在官网登录确认",
                        "auth", "");
    }
  }

  throw PortalError("当前网络无法直连海南大学门户认证节点（authserver.hainanu.edu.cn 需校园网或 WebVPN 可达）；若需体验完整门户与一卡通功能可使用演示账号 2024300128（密码 hnu123456）",
                    "net", "");
}

nlohmann::json PortalClient::fetch_card(const std::string& username,
                                        const nlohmann::json& saved_cookies) {
  if (enable_demo_ && SandboxProvider::is_demo_account(username)) {
    return SandboxProvider::demo_portal_card();
  }
  net::HttpSession sess(300);
  sess.jar().load_json(saved_cookies);
  if (sess.jar().get_cookie("CASTGC").empty() && sess.jar().get_cookie("MOD_AUTH_CAS").empty()) {
    throw PortalError("门户登录凭证已过期，请算一道验证码重新连接", "expired");
  }

  // 请求海大 ehall 卡片接口探测登录存活与一卡通摘要（支持使用 CASTGC 静默刷新过期 MOD_AUTH_CAS）
  auto r = ehall_get_with_sso_refresh(sess, ehall_base_url_,
                                      ehall_base_url_ + "/jsonp/userDesktopInfo.json",
                                      allowed_domains_);
  if (r.status == 302 || r.status == 401 || r.status == 403) {
    throw PortalError("门户门票已过期，请算一道验证码重新连接", "expired");
  }
  if (r.status != 200) {
    throw PortalError("连接网上办事大厅失败（HTTP " + std::to_string(r.status) + "）", "net");
  }
  std::string body_txt = r.text_utf8();
  auto j = nlohmann::json::parse(body_txt, nullptr, false);
  if (!j.is_object() || looks_like_html_login_page(body_txt)) {
    throw PortalError("门户会话已失效（返回非 JSON 登录页），请算一道验证码重新连接", "expired");
  }
  // 若 ehall 未返回明文卡余额（海大一卡通明细部分迁移至企业微信「海大 e 卡」），如实返回在线状态与引导说明，绝不编造 0.00 元！
  std::string card_bal = utils::trim(utils::json_str(j, "cardBalance", ""));
  return {
      {"balance", card_bal.empty() ? nlohmann::json(nullptr) : nlohmann::json(card_bal)},
      {"cardStatus", "已连接海大门户"},
      {"cardNo", utils::mask_account(username)},
      {"updatedAt", utils::bj_datetime_str()},
      {"ecardNote", card_bal.empty() ? "海南大学实时刷卡流水与圈存已整合至微信「海大 e 卡」小程序，门户已就绪可免登进入下方办事服务。" : ""},
      {"records", nlohmann::json::array()},
  };
}

nlohmann::json PortalClient::fetch_apps(const std::string& username,
                                        const nlohmann::json& saved_cookies) {
  if (enable_demo_ && SandboxProvider::is_demo_account(username)) {
    return SandboxProvider::demo_portal_apps();
  }
  net::HttpSession sess(300);
  sess.jar().load_json(saved_cookies);
  if (sess.jar().get_cookie("CASTGC").empty() && sess.jar().get_cookie("MOD_AUTH_CAS").empty()) {
    throw PortalError("门户登录凭证已过期，请重新连接", "expired");
  }

  auto r = ehall_get_with_sso_refresh(
      sess, ehall_base_url_,
      ehall_base_url_ + "/jsonp/serviceSearchCustom.json?pageSize=30&pageNumber=1",
      allowed_domains_);
  if (r.status == 302 || r.status == 401 || r.status == 403) {
    throw PortalError("门户会话已过期，请算一道验证码重新连接", "expired");
  }
  if (r.status != 200) {
    throw PortalError("获取网上办事大厅应用列表失败（HTTP " + std::to_string(r.status) + "）", "net");
  }
  std::string body_txt = r.text_utf8();
  auto j = nlohmann::json::parse(body_txt, nullptr, false);
  if (!j.is_object() || looks_like_html_login_page(body_txt)) {
    throw PortalError("门户会话已过期，请算一道验证码重新连接", "expired");
  }
  if (j.contains("datas") && j["datas"].is_array() && !j["datas"].empty()) {
    nlohmann::json apps = nlohmann::json::array();
    for (const auto& item : j["datas"]) {
      if (!item.is_object()) continue;
      std::string name = utils::trim(utils::json_str(item, "appName", utils::json_str(item, "serviceName", "")));
      std::string url = utils::trim(utils::json_str(item, "appUrl", utils::json_str(item, "serviceUrl", "")));
      if (name.empty() || url.empty()) continue;
      std::string dept = utils::trim(utils::json_str(item, "departmentName", "网上办事大厅"));
      if (dept.empty()) dept = "网上办事大厅";
      apps.push_back({
          {"id", utils::json_str(item, "appId", "")},
          {"name", name},
          {"dept", dept},
          {"url", utils::resolve_url(ehall_base_url_ + "/", url)},
      });
    }
    if (!apps.empty()) return apps;
  }
  // 在已确认登录态有效但接口 datas 为空时，兜底提供海南大学 19 个高频官方办事入口矩阵
  return SandboxProvider::demo_portal_apps();
}

nlohmann::json PortalClient::exchange_ticket(const std::string& username,
                                             const nlohmann::json& saved_cookies,
                                             const std::string& target_service_url) {
  std::string svc_trim = utils::trim(target_service_url);
  if (!utils::starts_with(utils::to_lower(svc_trim), "https://")) {
    throw PortalError("安全限制：仅允许为 HTTPS 校内服务签发单点登录票据", "auth");
  }
  if (!utils::is_http_url_allowed(svc_trim, allowed_domains_)) {
    throw PortalError("安全拦截：仅允许向海南大学官方域名（*.hainanu.edu.cn）签发 CAS 票据", "auth");
  }

  if (enable_demo_ && SandboxProvider::is_demo_account(username)) {
    return {{"ticket", "ST-hnu-demo-" + crypto::random_hex(8)}, {"url", svc_trim}};
  }

  net::HttpSession sess(200);
  sess.jar().load_json(saved_cookies);
  std::string tgt = sess.jar().get_cookie("CASTGC");
  if (tgt.empty()) {
    throw PortalError("门户身份证（CASTGC）已失效，请重新连接门户", "expired");
  }

  auto r = sess.get(cas_login_url_ + "?service=" + utils::url_encode(svc_trim),
                    {}, false, 10000);
  if (r.status == 0 || r.status >= 500) {
    throw PortalError("连接统一身份认证节点超时或服务暂不可用（HTTP " + std::to_string(r.status) + "）", "net");
  }
  if (r.status == 302) {
    auto it = r.headers.find("location");
    if (it != r.headers.end() && it->second.find("ticket=ST-") != std::string::npos) {
      size_t p = it->second.find("ticket=");
      std::string st = it->second.substr(p + 7);
      size_t amp = st.find('&');
      if (amp != std::string::npos) st = st.substr(0, amp);
      return {{"ticket", st}, {"url", it->second}};
    }
  }
  throw PortalError("门户会话已过期，请算一道验证码重新连接", "expired");
}

}  // namespace hnu::adapters
