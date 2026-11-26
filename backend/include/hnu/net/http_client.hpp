#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hnu::net {

struct CookieItem {
  std::string name;
  std::string value;
  std::string domain;       // 小写域名；host_only=false 时允许子域匹配
  std::string path = "/";
  bool host_only = true;    // Set-Cookie 未指定 Domain 时仅匹配精确主机（保障超星 passport2 等子域隔离）
  int64_t expires = 0;      // 0 = 会话级有效
};

// 线程安全多域 RFC6265 CookieJar（确保不同子域如 passport2.chaoxing.com 与 mooc1.chaoxing.com 绝不串 Cookie）
class CookieJar {
 public:
  void ingest_set_cookie(std::string_view request_url, std::string_view set_cookie_line);
  std::string build_cookie_header(std::string_view request_url) const;
  std::string get_cookie(std::string_view name, std::string_view domain_suffix = "") const;
  bool has_any_cookie(std::string_view domain_suffix = "") const;
  void set_cookie(CookieItem item);
  void remove_cookie(std::string_view name);
  void clear();

  nlohmann::json dump_json() const;
  void load_json(const nlohmann::json& arr);
  void merge_json(const nlohmann::json& arr, std::string_view domain_suffix_filter = "");

 private:
  mutable std::mutex mu_;
  std::vector<CookieItem> cookies_;
};

struct HttpResponse {
  int status = 0;
  std::string body;                           // 原始字节（若为文本可由 ensure_utf8 转换）
  std::string final_url;
  std::map<std::string, std::string> headers; // 小写 header 名 -> 值
  std::vector<std::string> set_cookies;
  std::string error;

  std::string text_utf8() const;
};

struct HttpRequestOptions {
  std::string method = "GET";
  std::string url;
  std::string body;
  std::string content_type;
  std::map<std::string, std::string> headers;
  int timeout_ms = 20000;
  bool follow_redirects = true;
  int max_redirects = 8;
  bool verify_ssl = false;  // 校内部分老旧子站证书链偶发不全，默认兼容放行校内证书
  std::vector<std::string> allowed_redirect_domains;  // 非空时强制校验初始及每跳重定向域名白名单
};

class HttpSession {
 public:
  explicit HttpSession(int polite_delay_ms = 0);
  ~HttpSession();
  HttpSession(const HttpSession&) = delete;
  HttpSession& operator=(const HttpSession&) = delete;

  CookieJar& jar() { return jar_; }
  const CookieJar& jar() const { return jar_; }

  void set_user_agent(std::string ua) { user_agent_ = std::move(ua); }
  void set_polite_delay_ms(int ms) { polite_delay_ms_ = ms; }

  HttpResponse request(const HttpRequestOptions& opts);
  HttpResponse get(const std::string& url,
                   const std::map<std::string, std::string>& headers = {},
                   bool follow_redirects = true,
                   int timeout_ms = 20000);
  HttpResponse post_form(const std::string& url,
                         const std::vector<std::pair<std::string, std::string>>& form,
                         const std::map<std::string, std::string>& headers = {},
                         bool follow_redirects = true,
                         int timeout_ms = 25000);
  HttpResponse post_json(const std::string& url,
                         const nlohmann::json& payload,
                         const std::map<std::string, std::string>& headers = {},
                         int timeout_ms = 25000);

 private:
  HttpResponse single_hop(const HttpRequestOptions& opts);
  void apply_pace();

  CookieJar jar_;
  std::string user_agent_ =
      "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
      "(KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36";
  int polite_delay_ms_ = 0;
  std::mutex pace_mu_;
  int64_t last_req_ms_ = 0;
  std::mutex curl_mu_;
  void* curl_handle_ = nullptr;
};

}  // namespace hnu::net
