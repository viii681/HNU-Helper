#include "hnu/net/http_client.hpp"
#include "hnu/utils/json_safe.hpp"
#include "hnu/utils/logger.hpp"
#include "hnu/utils/strings_and_time.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <unordered_map>

#if defined(HNU_USE_LIBCURL)
#include <curl/curl.h>
#elif defined(HNU_USE_WINHTTP)
#include <windows.h>
#include <winhttp.h>
#endif

namespace hnu::net {

namespace {

std::string url_path_of(std::string_view url) {
  size_t scheme_end = url.find("://");
  size_t start = (scheme_end == std::string_view::npos) ? 0 : scheme_end + 3;
  size_t slash = url.find('/', start);
  if (slash == std::string_view::npos) return "/";
  size_t q = url.find_first_of("?#", slash);
  std::string p = (q == std::string_view::npos)
                      ? std::string(url.substr(slash))
                      : std::string(url.substr(slash, q - slash));
  return p.empty() ? "/" : p;
}

bool domain_matches(std::string_view req_host, const CookieItem& c) {
  if (c.domain.empty()) return false;
  if (c.host_only) {
    return req_host == c.domain;
  }
  if (req_host == c.domain) return true;
  return req_host.size() > c.domain.size() + 1 &&
         req_host[req_host.size() - c.domain.size() - 1] == '.' &&
         utils::ends_with(req_host, c.domain);
}

bool path_matches(std::string_view req_path, std::string_view cookie_path) {
  if (cookie_path.empty() || cookie_path == "/") return true;
  if (req_path == cookie_path) return true;
  if (utils::starts_with(req_path, cookie_path)) {
    if (cookie_path.back() == '/' || req_path[cookie_path.size()] == '/') {
      return true;
    }
  }
  return false;
}

}  // namespace

// ---------------------------------------------------------------------------
// CookieJar
// ---------------------------------------------------------------------------
void CookieJar::ingest_set_cookie(std::string_view request_url, std::string_view set_cookie_line) {
  auto parts = utils::split(set_cookie_line, ';');
  if (parts.empty()) return;
  size_t eq = parts[0].find('=');
  if (eq == std::string::npos) return;

  CookieItem item;
  item.name = utils::trim(std::string_view(parts[0]).substr(0, eq));
  item.value = utils::trim(std::string_view(parts[0]).substr(eq + 1));
  if (item.name.empty()) return;

  std::string req_host = utils::extract_host(request_url);
  item.domain = req_host;
  item.host_only = true;
  item.path = "/";

  bool expired_immediately = false;
  for (size_t i = 1; i < parts.size(); ++i) {
    std::string attr = utils::trim(parts[i]);
    if (attr.empty()) continue;
    size_t aeq = attr.find('=');
    std::string k = utils::to_lower(aeq == std::string::npos ? attr : attr.substr(0, aeq));
    std::string v = (aeq == std::string::npos) ? "" : utils::trim(attr.substr(aeq + 1));
    if (k == "domain" && !v.empty()) {
      if (v.front() == '.') v.erase(v.begin());
      v = utils::to_lower(v);
      // 防跨域伪造 Cookie：domain 必须是 req_host 或其父域
      if (req_host == v ||
          (req_host.size() > v.size() + 1 &&
           req_host[req_host.size() - v.size() - 1] == '.' &&
           utils::ends_with(req_host, v))) {
        item.domain = v;
        item.host_only = false;
      }
    } else if (k == "path" && !v.empty()) {
      item.path = v;
    } else if (k == "max-age") {
      try {
        int64_t ma = std::stoll(v);
        if (ma <= 0) {
          expired_immediately = true;
        } else {
          item.expires = utils::now_sec() + ma;
        }
      } catch (...) {
      }
    }
  }

  std::lock_guard<std::mutex> lk(mu_);
  cookies_.erase(
      std::remove_if(cookies_.begin(), cookies_.end(),
                     [&](const CookieItem& existing) {
                       return existing.name == item.name &&
                              existing.domain == item.domain &&
                              existing.path == item.path;
                     }),
      cookies_.end());
  if (!expired_immediately && !item.value.empty() && item.value != "deleteMe") {
    cookies_.push_back(std::move(item));
  }
}

std::string CookieJar::build_cookie_header(std::string_view request_url) const {
  std::string req_host = utils::extract_host(request_url);
  std::string req_path = url_path_of(request_url);
  int64_t now = utils::now_sec();

  std::lock_guard<std::mutex> lk(mu_);
  std::vector<const CookieItem*> matched;
  for (const auto& c : cookies_) {
    if (c.expires > 0 && c.expires < now) continue;
    if (domain_matches(req_host, c) && path_matches(req_path, c.path)) {
      matched.push_back(&c);
    }
  }
  // 长路径优先
  std::sort(matched.begin(), matched.end(),
            [](const CookieItem* a, const CookieItem* b) {
              return a->path.size() > b->path.size();
            });
  std::string out;
  for (size_t i = 0; i < matched.size(); ++i) {
    if (i > 0) out.append("; ");
    out.append(matched[i]->name).append("=").append(matched[i]->value);
  }
  return out;
}

std::string CookieJar::get_cookie(std::string_view name, std::string_view domain_suffix) const {
  std::lock_guard<std::mutex> lk(mu_);
  int64_t now = utils::now_sec();
  std::string suf = utils::to_lower(domain_suffix);
  if (!suf.empty() && suf.front() == '.') suf.erase(suf.begin());
  for (const auto& c : cookies_) {
    if (c.expires > 0 && c.expires < now) continue;
    if (c.name != name) continue;
    if (!suf.empty() && c.domain != suf && !utils::ends_with(c.domain, "." + suf)) continue;
    return c.value;
  }
  return "";
}

bool CookieJar::has_any_cookie(std::string_view domain_suffix) const {
  std::lock_guard<std::mutex> lk(mu_);
  int64_t now = utils::now_sec();
  std::string suf = utils::to_lower(domain_suffix);
  if (!suf.empty() && suf.front() == '.') suf.erase(suf.begin());
  for (const auto& c : cookies_) {
    if (c.expires > 0 && c.expires < now) continue;
    if (!suf.empty() && c.domain != suf && !utils::ends_with(c.domain, "." + suf)) continue;
    if (!c.value.empty()) return true;
  }
  return false;
}

void CookieJar::set_cookie(CookieItem item) {
  if (!item.domain.empty() && item.domain.front() == '.') {
    item.domain.erase(item.domain.begin());
    item.host_only = false;
  }
  item.domain = utils::to_lower(item.domain);
  if (item.path.empty()) item.path = "/";
  std::lock_guard<std::mutex> lk(mu_);
  cookies_.erase(
      std::remove_if(cookies_.begin(), cookies_.end(),
                     [&](const CookieItem& e) {
                       return e.name == item.name && e.domain == item.domain && e.path == item.path;
                     }),
      cookies_.end());
  cookies_.push_back(std::move(item));
}

void CookieJar::remove_cookie(std::string_view name) {
  std::lock_guard<std::mutex> lk(mu_);
  cookies_.erase(
      std::remove_if(cookies_.begin(), cookies_.end(),
                     [&](const CookieItem& e) { return e.name == name; }),
      cookies_.end());
}

void CookieJar::clear() {
  std::lock_guard<std::mutex> lk(mu_);
  cookies_.clear();
}

nlohmann::json CookieJar::dump_json() const {
  std::lock_guard<std::mutex> lk(mu_);
  nlohmann::json arr = nlohmann::json::array();
  int64_t now = utils::now_sec();
  for (const auto& c : cookies_) {
    if (c.expires > 0 && c.expires < now) continue;
    arr.push_back({
        {"name", c.name},
        {"value", c.value},
        {"domain", c.domain},
        {"path", c.path},
        {"hostOnly", c.host_only},
        {"expires", c.expires},
    });
  }
  return arr;
}

void CookieJar::load_json(const nlohmann::json& arr) {
  if (!arr.is_array()) return;
  std::lock_guard<std::mutex> lk(mu_);
  cookies_.clear();
  for (const auto& it : arr) {
    if (!it.is_object()) continue;
    CookieItem c;
    c.name = utils::json_str(it, "name", "");
    c.value = utils::json_str(it, "value", "");
    c.domain = utils::to_lower(utils::json_str(it, "domain", ""));
    bool had_dot = (!c.domain.empty() && c.domain.front() == '.');
    if (had_dot) c.domain.erase(c.domain.begin());
    c.path = utils::json_str(it, "path", "/");
    if (c.path.empty()) c.path = "/";
    c.host_only = it.contains("hostOnly") ? utils::json_bool(it, "hostOnly", true) : !had_dot;
    int64_t exp = utils::json_i64(it, "expires", 0);
    c.expires = exp > 0 ? exp : 0;
    if (!c.name.empty() && !c.domain.empty()) {
      cookies_.push_back(std::move(c));
    }
  }
}

void CookieJar::merge_json(const nlohmann::json& arr, std::string_view domain_suffix_filter) {
  if (!arr.is_array()) return;
  std::string filt = utils::to_lower(domain_suffix_filter);
  if (!filt.empty() && filt.front() == '.') filt.erase(filt.begin());
  std::lock_guard<std::mutex> lk(mu_);
  int64_t now = utils::now_sec();
  for (const auto& it : arr) {
    if (!it.is_object()) continue;
    CookieItem c;
    c.name = utils::json_str(it, "name", "");
    c.value = utils::json_str(it, "value", "");
    c.domain = utils::to_lower(utils::json_str(it, "domain", ""));
    bool had_dot = (!c.domain.empty() && c.domain.front() == '.');
    if (had_dot) c.domain.erase(c.domain.begin());
    c.path = utils::json_str(it, "path", "/");
    if (c.path.empty()) c.path = "/";
    c.host_only = it.contains("hostOnly") ? utils::json_bool(it, "hostOnly", true) : !had_dot;
    int64_t exp = utils::json_i64(it, "expires", 0);
    c.expires = exp > 0 ? exp : 0;
    if (c.name.empty() || c.domain.empty() || c.value.empty()) continue;
    if (c.expires > 0 && c.expires < now) continue;
    if (!filt.empty() && c.domain != filt && !utils::ends_with(c.domain, "." + filt)) continue;
    cookies_.erase(
        std::remove_if(cookies_.begin(), cookies_.end(),
                       [&](const CookieItem& e) {
                         return e.name == c.name && e.domain == c.domain && e.path == c.path;
                       }),
        cookies_.end());
    cookies_.push_back(std::move(c));
  }
}

std::string HttpResponse::text_utf8() const {
  return utils::ensure_utf8(body);
}

// ---------------------------------------------------------------------------
// HttpSession
// ---------------------------------------------------------------------------
HttpSession::HttpSession(int polite_delay_ms) : polite_delay_ms_(polite_delay_ms) {
#if defined(HNU_USE_LIBCURL)
  static std::once_flag curl_once;
  std::call_once(curl_once, []() { curl_global_init(CURL_GLOBAL_DEFAULT); });
  curl_handle_ = curl_easy_init();
#endif
}

HttpSession::~HttpSession() {
#if defined(HNU_USE_LIBCURL)
  std::lock_guard<std::mutex> curl_lk(curl_mu_);
  if (curl_handle_) {
    curl_easy_cleanup(static_cast<CURL*>(curl_handle_));
    curl_handle_ = nullptr;
  }
#endif
}

void HttpSession::apply_pace() {
  if (polite_delay_ms_ <= 0) return;
  std::lock_guard<std::mutex> lk(pace_mu_);
  int64_t now = utils::now_ms();
  int64_t elapsed = now - last_req_ms_;
  if (last_req_ms_ > 0 && elapsed < polite_delay_ms_) {
    std::this_thread::sleep_for(std::chrono::milliseconds(polite_delay_ms_ - elapsed));
  }
  last_req_ms_ = utils::now_ms();
}

#if defined(HNU_USE_LIBCURL)

static size_t curl_write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
  size_t total = size * nmemb;
  auto* buf = static_cast<std::string*>(userdata);
  buf->append(ptr, total);
  return total;
}

static size_t curl_header_cb(char* buffer, size_t size, size_t nitems, void* userdata) {
  size_t total = size * nitems;
  auto* resp = static_cast<HttpResponse*>(userdata);
  std::string line(buffer, total);
  line = utils::trim(line);
  size_t colon = line.find(':');
  if (colon != std::string::npos) {
    std::string k = utils::to_lower(utils::trim(std::string_view(line).substr(0, colon)));
    std::string v = utils::trim(std::string_view(line).substr(colon + 1));
    if (k == "set-cookie") {
      resp->set_cookies.push_back(v);
    } else {
      resp->headers[k] = v;
    }
  }
  return total;
}

HttpResponse HttpSession::single_hop(const HttpRequestOptions& opts) {
  HttpResponse resp;
  resp.final_url = opts.url;

  std::lock_guard<std::mutex> curl_lk(curl_mu_);
  if (!curl_handle_) {
    curl_handle_ = curl_easy_init();
  }
  CURL* curl = static_cast<CURL*>(curl_handle_);
  if (!curl) {
    resp.error = "curl_easy_init failed";
    return resp;
  }
  // 复用同一个 CURL handle 保持底层 TCP + TLS Keep-Alive 连接池，仅重置单次请求参数
  curl_easy_reset(curl);

  curl_easy_setopt(curl, CURLOPT_URL, opts.url.c_str());
#if defined(CURLOPT_PROTOCOLS_STR)
  curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
  curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
#else
  curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
  curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
#endif
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);  // 由我们自己的 CookieJar 逐跳处理重定向与域隔离
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(opts.timeout_ms));
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 10000L);
  curl_easy_setopt(curl, CURLOPT_NOPROXY, "*");        // 直连学校与学习平台，绕过环境变量残余代理
  curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, ""); // 自动解压 gzip/deflate
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, opts.verify_ssl ? 1L : 0L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, opts.verify_ssl ? 2L : 0L);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

  if (opts.method == "POST") {
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, opts.body.data());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(opts.body.size()));
  } else if (opts.method == "HEAD") {
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
  } else if (opts.method != "GET") {
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, opts.method.c_str());
    if (!opts.body.empty()) {
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS, opts.body.data());
      curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(opts.body.size()));
    }
  }

  struct curl_slist* hdr_list = nullptr;
  bool has_ua = false;
  bool has_cookie = false;
  for (const auto& [k, v] : opts.headers) {
    std::string low = utils::to_lower(k);
    if (low == "user-agent") has_ua = true;
    if (low == "cookie") has_cookie = true;
    std::string h = k + ": " + v;
    hdr_list = curl_slist_append(hdr_list, h.c_str());
  }
  if (!has_ua && !user_agent_.empty()) {
    std::string h = "User-Agent: " + user_agent_;
    hdr_list = curl_slist_append(hdr_list, h.c_str());
  }
  if (!opts.content_type.empty()) {
    std::string h = "Content-Type: " + opts.content_type;
    hdr_list = curl_slist_append(hdr_list, h.c_str());
  }
  if (!has_cookie) {
    std::string ck = jar_.build_cookie_header(opts.url);
    if (!ck.empty()) {
      std::string h = "Cookie: " + ck;
      hdr_list = curl_slist_append(hdr_list, h.c_str());
    }
  }
  if (hdr_list) {
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdr_list);
  }

  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);
  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, curl_header_cb);
  curl_easy_setopt(curl, CURLOPT_HEADERDATA, &resp);

  CURLcode rc = curl_easy_perform(curl);
  if (rc != CURLE_OK) {
    resp.error = curl_easy_strerror(rc);
  } else {
    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    resp.status = static_cast<int>(code);
  }

  if (hdr_list) curl_slist_free_all(hdr_list);

  for (const auto& sc : resp.set_cookies) {
    jar_.ingest_set_cookie(opts.url, sc);
  }
  return resp;
}

#elif defined(HNU_USE_WINHTTP)

static std::wstring utf8_to_wide(std::string_view s) {
  if (s.empty()) return L"";
  int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  if (len <= 0) return L"";
  std::wstring out(static_cast<size_t>(len), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), len);
  return out;
}

static std::string wide_to_utf8(std::wstring_view ws) {
  if (ws.empty()) return "";
  int len = WideCharToMultiByte(CP_UTF8, 0, ws.data(), static_cast<int>(ws.size()),
                                nullptr, 0, nullptr, nullptr);
  if (len <= 0) return "";
  std::string out(static_cast<size_t>(len), '\0');
  WideCharToMultiByte(CP_UTF8, 0, ws.data(), static_cast<int>(ws.size()),
                      out.data(), len, nullptr, nullptr);
  return out;
}

// 按主机名独立记录直连 vs 系统代理偏好，避免内网域名与外网域名之间全局乒乓抖动
static std::mutex g_proxy_pref_mu;
static std::unordered_map<std::string, bool> g_host_proxy_pref;

static bool get_host_proxy_pref(const std::string& host) {
  std::lock_guard<std::mutex> lk(g_proxy_pref_mu);
  auto it = g_host_proxy_pref.find(host);
  return it != g_host_proxy_pref.end() ? it->second : false;
}

static void set_host_proxy_pref(const std::string& host, bool prefer_proxy) {
  if (host.empty()) return;
  std::lock_guard<std::mutex> lk(g_proxy_pref_mu);
  if (g_host_proxy_pref.size() > 256) g_host_proxy_pref.clear();
  g_host_proxy_pref[host] = prefer_proxy;
}

static bool windows_has_system_proxy() {
  WINHTTP_CURRENT_USER_IE_PROXY_CONFIG ie_cfg{};
  if (!WinHttpGetIEProxyConfigForCurrentUser(&ie_cfg)) {
    return false;
  }
  bool has = (ie_cfg.lpszProxy != nullptr && ie_cfg.lpszProxy[0] != L'\0') ||
             (ie_cfg.lpszAutoConfigUrl != nullptr && ie_cfg.lpszAutoConfigUrl[0] != L'\0') ||
             (ie_cfg.fAutoDetect == TRUE);
  if (ie_cfg.lpszProxy) GlobalFree(ie_cfg.lpszProxy);
  if (ie_cfg.lpszProxyBypass) GlobalFree(ie_cfg.lpszProxyBypass);
  if (ie_cfg.lpszAutoConfigUrl) GlobalFree(ie_cfg.lpszAutoConfigUrl);
  return has;
}

static HINTERNET get_winhttp_session(std::string_view default_ua, bool use_system_proxy) {
  static HINTERNET s_direct_session = nullptr;
  static std::once_flag s_direct_once;
  static HINTERNET s_proxy_session = nullptr;
  static std::once_flag s_proxy_once;

  if (!use_system_proxy) {
    std::call_once(s_direct_once, [&]() {
      std::wstring wua = utf8_to_wide(default_ua);
      s_direct_session = WinHttpOpen(wua.c_str(), WINHTTP_ACCESS_TYPE_NO_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
      if (s_direct_session) {
        DWORD max_conns = 16;
        WinHttpSetOption(s_direct_session, WINHTTP_OPTION_MAX_CONNS_PER_SERVER, &max_conns, sizeof(max_conns));
        WinHttpSetTimeouts(s_direct_session, 8000, 10000, 20000, 20000);
      }
    });
    return s_direct_session;
  }

  std::call_once(s_proxy_once, [&]() {
    std::wstring wua = utf8_to_wide(default_ua);
#ifdef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
    s_proxy_session = WinHttpOpen(wua.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
#endif
    if (!s_proxy_session) {
      s_proxy_session = WinHttpOpen(wua.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    }
    if (s_proxy_session) {
      DWORD max_conns = 16;
      WinHttpSetOption(s_proxy_session, WINHTTP_OPTION_MAX_CONNS_PER_SERVER, &max_conns, sizeof(max_conns));
      WinHttpSetTimeouts(s_proxy_session, 8000, 10000, 20000, 20000);
    }
  });
  return s_proxy_session;
}

static HttpResponse single_hop_winhttp_impl(HINTERNET hSession,
                                            const HttpRequestOptions& opts,
                                            const CookieJar& jar,
                                            std::string_view default_ua,
                                            bool disable_keepalive,
                                            int effective_timeout_ms,
                                            DWORD* out_win_err) {
  HttpResponse resp;
  resp.final_url = opts.url;
  if (out_win_err) *out_win_err = 0;

  std::wstring wurl = utf8_to_wide(opts.url);
  URL_COMPONENTS uc{};
  uc.dwStructSize = sizeof(uc);
  std::vector<wchar_t> host_buf(std::max<size_t>(512, wurl.size() + 16), L'\0');
  std::vector<wchar_t> path_buf(std::max<size_t>(4096, wurl.size() + 16), L'\0');
  uc.lpszHostName = host_buf.data();
  uc.dwHostNameLength = static_cast<DWORD>(host_buf.size());
  uc.lpszUrlPath = path_buf.data();
  uc.dwUrlPathLength = static_cast<DWORD>(path_buf.size());

  if (!WinHttpCrackUrl(wurl.c_str(), static_cast<DWORD>(wurl.size()), 0, &uc) ||
      (uc.nScheme != INTERNET_SCHEME_HTTP && uc.nScheme != INTERNET_SCHEME_HTTPS)) {
    resp.error = "Invalid URL";
    return resp;
  }

  if (!hSession) {
    resp.error = "WinHttpOpen failed";
    return resp;
  }

  std::wstring whost(uc.lpszHostName, uc.dwHostNameLength);
  HINTERNET hConnect = WinHttpConnect(hSession, whost.c_str(), uc.nPort, 0);
  if (!hConnect) {
    if (out_win_err) *out_win_err = GetLastError();
    resp.error = "WinHttpConnect failed";
    return resp;
  }

  std::wstring wpath(uc.lpszUrlPath, uc.dwUrlPathLength);
  if (wpath.empty()) wpath = L"/";
  std::wstring wmethod = utf8_to_wide(opts.method);
  DWORD req_flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;

  HINTERNET hRequest = WinHttpOpenRequest(hConnect, wmethod.c_str(), wpath.c_str(),
                                          nullptr, WINHTTP_NO_REFERER,
                                          WINHTTP_DEFAULT_ACCEPT_TYPES, req_flags);
  if (!hRequest) {
    if (out_win_err) *out_win_err = GetLastError();
    WinHttpCloseHandle(hConnect);
    resp.error = "WinHttpOpenRequest failed";
    return resp;
  }
  int resolve_ms = std::clamp(effective_timeout_ms, 2500, 8000);
  int connect_ms = std::clamp(effective_timeout_ms, 3000, 10000);
  WinHttpSetTimeouts(hRequest, resolve_ms, connect_ms, effective_timeout_ms, effective_timeout_ms);

  // 禁用 WinHTTP 内置重定向与自动 Cookie，统一交由我们的 CookieJar 管控；重连重试时同时禁用 Keep-Alive 避开失效长连接
  DWORD disable_flags = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES;
  if (disable_keepalive) {
    disable_flags |= WINHTTP_DISABLE_KEEP_ALIVE;
  }
  WinHttpSetOption(hRequest, WINHTTP_OPTION_DISABLE_FEATURE, &disable_flags, sizeof(disable_flags));

  if ((uc.nScheme == INTERNET_SCHEME_HTTPS) && !opts.verify_ssl) {
    DWORD sec_flags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                      SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                      SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                      SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &sec_flags, sizeof(sec_flags));
  }

  std::string all_hdrs;
  bool has_ua = false;
  bool has_cookie = false;
  for (const auto& [k, v] : opts.headers) {
    std::string low = utils::to_lower(k);
    if (low == "user-agent") has_ua = true;
    if (low == "cookie") has_cookie = true;
    all_hdrs.append(k).append(": ").append(v).append("\r\n");
  }
  if (!has_ua && !default_ua.empty()) {
    all_hdrs.append("User-Agent: ").append(default_ua).append("\r\n");
  }
  if (!opts.content_type.empty()) {
    all_hdrs.append("Content-Type: ").append(opts.content_type).append("\r\n");
  }
  if (!has_cookie) {
    std::string ck = jar.build_cookie_header(opts.url);
    if (!ck.empty()) {
      all_hdrs.append("Cookie: ").append(ck).append("\r\n");
    }
  }
  std::wstring whdrs = utf8_to_wide(all_hdrs);

  BOOL sent = WinHttpSendRequest(
      hRequest,
      whdrs.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : whdrs.c_str(),
      whdrs.empty() ? 0 : static_cast<DWORD>(whdrs.size()),
      opts.body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(opts.body.data()),
      static_cast<DWORD>(opts.body.size()),
      static_cast<DWORD>(opts.body.size()),
      0);

  if (!sent || !WinHttpReceiveResponse(hRequest, nullptr)) {
    DWORD err = GetLastError();
    if (out_win_err) *out_win_err = err;
    resp.error = "WinHTTP network error (" + std::to_string(err) + ")";
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    return resp;
  }

  DWORD status_code = 0;
  DWORD sz = sizeof(status_code);
  WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                      WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &sz, WINHTTP_NO_HEADER_INDEX);
  resp.status = static_cast<int>(status_code);

  DWORD raw_hdr_bytes = 0;
  WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX,
                      nullptr, &raw_hdr_bytes, WINHTTP_NO_HEADER_INDEX);
  if (raw_hdr_bytes > 0) {
    std::vector<wchar_t> hbuf(raw_hdr_bytes / sizeof(wchar_t) + 1, L'\0');
    if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX,
                            hbuf.data(), &raw_hdr_bytes, WINHTTP_NO_HEADER_INDEX)) {
      std::string raw_utf8 = wide_to_utf8(hbuf.data());
      for (const auto& line_raw : utils::split(raw_utf8, '\n')) {
        std::string line = utils::trim(line_raw);
        size_t colon = line.find(':');
        if (colon != std::string::npos) {
          std::string k = utils::to_lower(utils::trim(std::string_view(line).substr(0, colon)));
          std::string v = utils::trim(std::string_view(line).substr(colon + 1));
          if (k == "set-cookie") {
            resp.set_cookies.push_back(v);
          } else {
            resp.headers[k] = v;
          }
        }
      }
    }
  }

  DWORD avail = 0;
  bool read_ok = true;
  do {
    avail = 0;
    if (!WinHttpQueryDataAvailable(hRequest, &avail)) {
      DWORD err = GetLastError();
      if (out_win_err) *out_win_err = err;
      resp.error = "WinHTTP read error (" + std::to_string(err) + ")";
      read_ok = false;
      break;
    }
    if (avail == 0) break;
    std::vector<char> chunk(avail);
    DWORD read_n = 0;
    if (!WinHttpReadData(hRequest, chunk.data(), avail, &read_n)) {
      DWORD err = GetLastError();
      if (out_win_err) *out_win_err = err;
      resp.error = "WinHTTP read error (" + std::to_string(err) + ")";
      read_ok = false;
      break;
    }
    if (read_n > 0) {
      resp.body.append(chunk.data(), read_n);
    } else {
      break;
    }
  } while (avail > 0);
  if (!read_ok) {
    resp.status = 0;
    resp.body.clear();
  }

  WinHttpCloseHandle(hRequest);
  WinHttpCloseHandle(hConnect);
  return resp;
}

HttpResponse HttpSession::single_hop(const HttpRequestOptions& opts) {
  std::string host = utils::extract_host(opts.url);
  bool prefer_proxy = get_host_proxy_pref(host);
  DWORD err1 = 0;
  HINTERNET primary = get_winhttp_session(user_agent_, prefer_proxy);
  HttpResponse resp = single_hop_winhttp_impl(primary, opts, jar_, user_agent_, false, opts.timeout_ms, &err1);

  if (resp.status == 0) {
    std::string path = url_path_of(opts.url);
    HNU_LOG_WARN("net", "WinHTTP " + std::string(prefer_proxy ? "proxy" : "direct") +
                            " failed host=" + host + " path=" + path +
                            " err=" + std::to_string(err1));

    // 自动容灾 1：若本机配置了系统代理（如校园 VPN / Clash / 代理端口），自动切换直连 <-> 系统代理重试一次
    bool has_sys_proxy = windows_has_system_proxy();
    if (has_sys_proxy) {
      bool alt_proxy = !prefer_proxy;
      HINTERNET alt_sess = get_winhttp_session(user_agent_, alt_proxy);
      DWORD err2 = 0;
      int retry_ms = std::min(opts.timeout_ms, 10000);
      HttpResponse resp2 = single_hop_winhttp_impl(alt_sess, opts, jar_, user_agent_, true, retry_ms, &err2);
      if (resp2.status > 0) {
        set_host_proxy_pref(host, alt_proxy);
        HNU_LOG_INFO("net", "WinHTTP auto-switched to " +
                                std::string(alt_proxy ? "system-proxy" : "direct") +
                                " mode for host=" + host);
        resp = std::move(resp2);
      } else {
        HNU_LOG_WARN("net", "WinHTTP fallback " + std::string(alt_proxy ? "proxy" : "direct") +
                                " also failed host=" + host + " err=" + std::to_string(err2));
      }
    } else {
      // 自动容灾 2：无代理环境下遇到 Keep-Alive 连接被网关重置或瞬时断开，禁用 Keep-Alive 立即新建连接重试一次
      bool is_reset = (err1 == 12030 || err1 == 12031 || err1 == 12152 ||
                       (err1 == 12002 && opts.method == "GET"));
      if (is_reset) {
        DWORD err2 = 0;
        int retry_ms = std::min(opts.timeout_ms, 10000);
        HttpResponse resp2 = single_hop_winhttp_impl(primary, opts, jar_, user_agent_, true, retry_ms, &err2);
        if (resp2.status > 0) {
          resp = std::move(resp2);
        }
      }
    }
  }

  for (const auto& sc : resp.set_cookies) {
    jar_.ingest_set_cookie(opts.url, sc);
  }
  return resp;
}

#endif

HttpResponse HttpSession::request(const HttpRequestOptions& opts) {
  apply_pace();
  HttpRequestOptions cur = opts;
  HttpResponse resp;
  resp.final_url = cur.url;

  // 强制仅允许合法 http/https URL，若设置了白名单则在首跳即校验
  if (!utils::is_http_url_allowed(cur.url, cur.allowed_redirect_domains)) {
    resp.error = "Blocked invalid or disallowed URL";
    return resp;
  }

  int hops = 0;
  while (true) {
    resp = single_hop(cur);
    if (!cur.follow_redirects || hops >= cur.max_redirects) break;
    if (resp.status != 301 && resp.status != 302 && resp.status != 303 &&
        resp.status != 307 && resp.status != 308) {
      break;
    }
    auto it = resp.headers.find("location");
    if (it == resp.headers.end() || it->second.empty()) break;

    std::string next_url = utils::resolve_url(cur.url, it->second);
    if (!utils::is_http_url_allowed(next_url, cur.allowed_redirect_domains)) {
      resp.status = 0;
      resp.body.clear();
      resp.error = "Redirect target blocked by scheme or domain allowlist";
      break;
    }
    if (utils::extract_host(next_url) != utils::extract_host(cur.url)) {
      // 跨域跳转时剥离敏感鉴权头，防止凭证泄漏到第三方域
      for (auto hit = cur.headers.begin(); hit != cur.headers.end();) {
        std::string low = utils::to_lower(hit->first);
        if (low == "authorization" || low == "cookie" || low == "proxy-authorization") {
          hit = cur.headers.erase(hit);
        } else {
          ++hit;
        }
      }
    }
    if (resp.status == 301 || resp.status == 302 || resp.status == 303) {
      cur.method = "GET";
      cur.body.clear();
      cur.content_type.clear();
    }
    cur.url = next_url;
    ++hops;
  }
  resp.final_url = cur.url;
  return resp;
}

HttpResponse HttpSession::get(const std::string& url,
                              const std::map<std::string, std::string>& headers,
                              bool follow_redirects,
                              int timeout_ms) {
  HttpRequestOptions o;
  o.method = "GET";
  o.url = url;
  o.headers = headers;
  o.follow_redirects = follow_redirects;
  o.timeout_ms = timeout_ms;
  return request(o);
}

HttpResponse HttpSession::post_form(
    const std::string& url,
    const std::vector<std::pair<std::string, std::string>>& form,
    const std::map<std::string, std::string>& headers,
    bool follow_redirects,
    int timeout_ms) {
  HttpRequestOptions o;
  o.method = "POST";
  o.url = url;
  o.body = utils::build_form_urlencoded(form);
  o.content_type = "application/x-www-form-urlencoded; charset=UTF-8";
  o.headers = headers;
  o.follow_redirects = follow_redirects;
  o.timeout_ms = timeout_ms;
  return request(o);
}

HttpResponse HttpSession::post_json(
    const std::string& url,
    const nlohmann::json& payload,
    const std::map<std::string, std::string>& headers,
    int timeout_ms) {
  HttpRequestOptions o;
  o.method = "POST";
  o.url = url;
  o.body = payload.is_null() ? "" : payload.dump();
  o.content_type = "application/json; charset=UTF-8";
  o.headers = headers;
  o.follow_redirects = true;
  o.timeout_ms = timeout_ms;
  return request(o);
}

}  // namespace hnu::net
