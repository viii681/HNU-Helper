#include "hnu/adapters/chaoxing_client.hpp"
#include "hnu/adapters/sandbox_provider.hpp"
#include "hnu/crypto/crypto.hpp"
#include "hnu/utils/json_safe.hpp"
#include "hnu/utils/strings_and_time.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <regex>
#include <set>
#include <thread>

namespace hnu::adapters {

namespace {

constexpr std::string_view kCxAesKey = "u2oh6Vu^HWe4_AES";

bool is_numeric_id(std::string_view s) {
  if (s.empty()) return false;
  for (unsigned char c : s) {
    if (!std::isdigit(c)) return false;
  }
  return s != "0";
}

std::string pick_numeric_fid(std::initializer_list<std::string> candidates) {
  for (const auto& c : candidates) {
    std::string t = utils::trim(c);
    if (is_numeric_id(t)) return t;
  }
  return "0";
}

void check_cx_http_status(const net::HttpResponse& r, const std::string& what) {
  if (r.status == 401 || r.status == 302) {
    if (r.status == 401) {
      throw CxError(what + "打不开，学习通登录已过期，请点击「重新连接」", "auth", false);
    }
    auto loc_it = r.headers.find("location");
    std::string loc = (loc_it != r.headers.end()) ? utils::to_lower(loc_it->second) : "";
    if (loc.empty() || loc.find("passport2") != std::string::npos || loc.find("login") != std::string::npos) {
      throw CxError(what + "触发登录重定向，学习通登录已过期，请点击「重新连接」", "auth", false);
    }
    throw CxError(what + "返回重定向异常（HTTP 302）", "auth", false);
  }
  if (r.status == 202) {
    throw CxError("检查太频繁触发了学习通风控排队，已自动暂停，请隔 15 分钟再试", "net", true);
  }
  if (r.status == 0) {
    throw CxError("无法连接超星学习通服务器，请检查网络", "net", false);
  }
  if (r.status != 200) {
    throw CxError(what + "返回异常（HTTP " + std::to_string(r.status) + "）", "net", false);
  }
}

std::string norm_course_name(std::string_view name) {
  std::string s = utils::to_lower(utils::trim(name));
  // 统一罗马数字与常见全半角标点，便于学习通班级课名与海大强智课表模糊匹配
  static const std::pair<const char*, const char*> kReplaces[] = {
      {"ⅰ", "1"}, {"ⅱ", "2"}, {"ⅲ", "3"}, {"ⅳ", "4"},
      {"Ⅰ", "1"}, {"Ⅱ", "2"}, {"Ⅲ", "3"}, {"Ⅳ", "4"},
      {"（一）", "1"}, {"（二）", "2"}, {"（三）", "3"}, {"（四）", "4"},
      {"(一)", "1"}, {"(二)", "2"}, {"(三)", "3"}, {"(四)", "4"},
      {"（", ""}, {"）", ""}, {"(", ""}, {")", ""},
      {"【", ""}, {"】", ""}, {"[", ""}, {"]", ""},
      {"《", ""}, {"》", ""}, {"·", ""}, {"-", ""}, {"_", ""}, {" ", ""}, {"\t", ""},
  };
  for (const auto& [from, to] : kReplaces) {
    s = utils::replace_all(s, from, to);
  }
  return s;
}

bool course_matches_whitelist(const std::string& cx_name, const std::set<std::string>& white_set) {
  if (white_set.empty()) return true;
  std::string n = norm_course_name(cx_name);
  if (n.empty()) return false;
  for (const auto& w : white_set) {
    if (w == n) return true;
    if (w.size() >= 6 && n.size() >= 6 &&
        (n.find(w) != std::string::npos || w.find(n) != std::string::npos)) {
      return true;
    }
  }
  return false;
}

std::string format_remain_text(int64_t end_at, int64_t now_sec, const std::string& fallback_status) {
  if (end_at <= 0) return fallback_status;
  int64_t diff = end_at - now_sec;
  if (diff < 0) return fallback_status.empty() ? "已截止" : fallback_status;
  int64_t days = diff / 86400;
  int64_t hrs = (diff % 86400) / 3600;
  int64_t mins = (diff % 3600) / 60;
  if (days > 0) return "还剩 " + std::to_string(days) + " 天 " + std::to_string(hrs) + " 小时";
  if (hrs > 0) return "还剩 " + std::to_string(hrs) + " 小时 " + std::to_string(mins) + " 分";
  return "即将截止（剩 " + std::to_string(std::max<int64_t>(1, mins)) + " 分钟）";
}

int64_t parse_left_hours_to_end_sec(const std::string& text, int64_t now_sec) {
  // 解析 "剩余 26小时15分钟" / "剩余 3天4小时" / "2026-11-28 23:59"（若出现时间区间则优先取截止/最晚时间）
  static const std::regex re_dt(R"((20\d{2})-(\d{1,2})-(\d{1,2})\s+(\d{1,2}):(\d{2}))");
  int64_t last_ts = 0;
  int64_t deadline_ts = 0;
  for (std::sregex_iterator it(text.begin(), text.end(), re_dt), end; it != end; ++it) {
    const auto& m = *it;
    std::tm tm{};
    tm.tm_year = std::stoi(m[1].str()) - 1900;
    tm.tm_mon = std::stoi(m[2].str()) - 1;
    tm.tm_mday = std::stoi(m[3].str());
    tm.tm_hour = std::stoi(m[4].str());
    tm.tm_min = std::stoi(m[5].str());
#ifdef _WIN32
    time_t utc = _mkgmtime(&tm);
#else
    time_t utc = timegm(&tm);
#endif
    if (utc <= 0) continue;
    int64_t ts = static_cast<int64_t>(utc) - 8 * 3600;
    last_ts = ts;
    size_t pos = static_cast<size_t>(m.position());
    size_t pref_start = (pos > 24) ? (pos - 24) : 0;
    std::string pref = text.substr(pref_start, pos - pref_start);
    if (pref.find("截止") != std::string::npos || pref.find("结束") != std::string::npos ||
        pref.find("至") != std::string::npos || pref.find("~") != std::string::npos ||
        pref.find("～") != std::string::npos) {
      deadline_ts = ts;
    }
  }
  if (deadline_ts > 0) return deadline_ts;
  if (last_ts > 0) return last_ts;

  int64_t add_sec = 0;
  std::smatch m;
  static const std::regex re_day(R"((\d+)\s*天)");
  static const std::regex re_hr(R"((\d+)\s*小时)");
  static const std::regex re_min(R"((\d+)\s*分)");
  if (std::regex_search(text, m, re_day)) add_sec += std::stoll(m[1].str()) * 86400;
  if (std::regex_search(text, m, re_hr)) add_sec += std::stoll(m[1].str()) * 3600;
  if (std::regex_search(text, m, re_min)) add_sec += std::stoll(m[1].str()) * 60;
  return add_sec > 0 ? (now_sec + add_sec) : 0;
}

std::string extract_input_value_by_id(const std::string& html, const std::string& input_id) {
  std::regex re1("id\\s*=\\s*['\"]" + input_id + "['\"][^>]*value\\s*=\\s*['\"]([^'\"]+)['\"]",
                 std::regex::icase);
  std::smatch m;
  if (std::regex_search(html, m, re1)) return m[1].str();

  std::regex re2("value\\s*=\\s*['\"]([^'\"]+)['\"][^>]*id\\s*=\\s*['\"]" + input_id + "['\"]",
                 std::regex::icase);
  if (std::regex_search(html, m, re2)) return m[1].str();
  return "";
}

}  // namespace

std::string chaoxing_aes_encrypt(std::string_view text) {
  return crypto::aes128_cbc_encrypt_b64(text, kCxAesKey, kCxAesKey);
}

std::string chaoxing_work_bucket(std::string_view status_text) {
  // 状态词归类原则：先判否定词（"未完成" 包含 "完成"，顺序不可颠倒）
  static const char* kTodoWords[] = {
      "未交", "未完成", "未提交", "未开始", "待提交", "待完成", "待互评", "进行中", "未批阅"};
  static const char* kDoneWords[] = {
      "已完成", "已提交", "已交", "已批阅", "待批阅", "已通过", "已互评", "已作答", "已参加"};

  for (const char* w : kTodoWords) {
    if (status_text.find(w) != std::string_view::npos) return "todo";
  }
  for (const char* w : kDoneWords) {
    if (status_text.find(w) != std::string_view::npos) return "done";
  }
  return "other";
}

nlohmann::json parse_chaoxing_courses_json(std::string_view json_text,
                                           std::string_view cookie_fid) {
  auto j = nlohmann::json::parse(json_text, nullptr, false);
  if (!j.is_object() || utils::json_int(j, "result", 0) != 1) {
    throw CxError("学习通登录状态已过期，请点击「重新连接」", "auth");
  }

  nlohmann::json courses = nlohmann::json::array();
  std::set<std::string> seen;
  std::string fallback_fid(cookie_fid);

  if (j.contains("channelList") && j["channelList"].is_array()) {
    for (const auto& ch : j["channelList"]) {
      if (!ch.is_object()) continue;
      if (!ch.contains("content") || !ch["content"].is_object()) continue;
      const auto& content = ch["content"];
      if (!content.contains("course") || !content["course"].is_object()) continue;
      const auto& course_obj = content["course"];
      if (!course_obj.contains("data") || !course_obj["data"].is_array() || course_obj["data"].empty()) {
        continue;
      }
      const auto& c0 = course_obj["data"][0];
      if (!c0.is_object()) continue;

      // 注意：超星各版本接口中 id / cpi / belongSchoolId / state 可能为数字、数字字符串或 null，
      // 且 schools 字段通常为学校中文名（如 "海南大学"），必须使用类型安全转换避免 json::type_error.302！
      std::string course_id = utils::trim(utils::json_str(c0, "id", "0"));
      std::string class_id = utils::trim(utils::json_str(content, "id", "0"));
      std::string cpi = utils::trim(utils::json_str(ch, "cpi", utils::json_str(content, "cpi", "0")));
      if (cpi.empty()) cpi = "0";
      std::string cname = utils::trim(utils::json_str(c0, "name", ""));
      std::string teacher = utils::trim(utils::json_str(c0, "teacherfactor", ""));
      std::string fid = pick_numeric_fid({
          utils::json_str(c0, "belongSchoolId", ""),
          utils::json_str(c0, "fid", ""),
          utils::json_str(content, "fid", ""),
          utils::json_str(ch, "fid", ""),
          fallback_fid,
      });
      int state = utils::json_int(content, "state", 0);  // 1 = 已归档结课

      if (course_id.empty() || course_id == "0" ||
          class_id.empty() || class_id == "0" || cname.empty()) {
        continue;
      }
      std::string uk = course_id + "_" + class_id;
      if (!seen.insert(uk).second) continue;

      courses.push_back({
          {"courseId", course_id},
          {"classId", class_id},
          {"cpi", cpi},
          {"fid", fid},
          {"name", cname},
          {"teacher", teacher},
          {"archived", state == 1},
      });
    }
  }
  return courses;
}

nlohmann::json ChaoxingClient::login(const std::string& phone_or_account,
                                     const std::string& password,
                                     nlohmann::json* out_cookies_to_save) {
  if (enable_demo_ && SandboxProvider::is_demo_account(phone_or_account)) {
    if (!SandboxProvider::verify_demo_password(phone_or_account, password)) {
      throw CxError("演示学习通账号密码不对（演示密码为 hnu123456）", "auth");
    }
    if (out_cookies_to_save) *out_cookies_to_save = nlohmann::json::array();
    return {{"ok", true}, {"user", phone_or_account}, {"demo", true}};
  }

  net::HttpSession sess(450);
  // 先拿登录页初始会话（注意 passport2 的 JSESSIONID 为 hostOnly，不会泄露给 mooc1）
  sess.get("https://passport2.chaoxing.com/login?fid=&refer=", {}, true, 12000);

  auto r = sess.post_form(
      "https://passport2.chaoxing.com/fanyalogin",
      {
          {"fid", "-1"},
          {"uname", chaoxing_aes_encrypt(phone_or_account)},
          {"password", chaoxing_aes_encrypt(password)},
          {"refer", "https://i.chaoxing.com"},
          {"t", "true"},
          {"forbidotherlogin", "0"},
          {"validate", ""},
          {"doubleFactorLogin", "0"},
          {"independentId", "0"},
      },
      {
          {"Origin", "https://passport2.chaoxing.com"},
          {"Referer", "https://passport2.chaoxing.com/login?fid=&refer="},
          {"X-Requested-With", "XMLHttpRequest"},
      },
      false,
      15000);

  check_cx_http_status(r, "学习通登录接口");
  auto j = nlohmann::json::parse(r.text_utf8(), nullptr, false);
  if (!j.is_object()) {
    throw CxError("学习通登录返回格式无法识别", "parse");
  }
  if (!utils::json_bool(j, "status", false)) {
    std::string msg = utils::trim(utils::json_str(j, "msg2", ""));
    if (msg.empty()) msg = utils::trim(utils::json_str(j, "mes", ""));
    if (msg.empty()) msg = "账号或密码不正确";
    throw CxError(msg, "auth");
  }
  if (out_cookies_to_save) {
    *out_cookies_to_save = sess.jar().dump_json();
  }
  return {{"ok", true}, {"user", phone_or_account}};
}

nlohmann::json ChaoxingClient::fetch_courses_on_session(net::HttpSession& sess) {
  auto r = sess.get("https://mooc1-api.chaoxing.com/mycourse/backclazzdata?view=json&rss=1",
                    {{"Referer", "https://i.chaoxing.com/"}}, false, 15000);
  check_cx_http_status(r, "课程列表");
  return parse_chaoxing_courses_json(r.text_utf8(), sess.jar().get_cookie("fid"));
}

nlohmann::json ChaoxingClient::scan(const std::string& account,
                                    const nlohmann::json& saved_cookies,
                                    const std::vector<std::string>& active_course_names,
                                    bool force_all,
                                    ProgressCallback on_progress,
                                    nlohmann::json* out_updated_cookies) {
  if (enable_demo_ && SandboxProvider::is_demo_account(account)) {
    if (on_progress) on_progress(1, 1, "操作系统原理", "正在同步演示作业…");
    return SandboxProvider::demo_chaoxing_scan();
  }

  net::HttpSession sess(450);  // 严格 450ms 单请求节流
  sess.jar().load_json(saved_cookies);

  auto courses = fetch_courses_on_session(sess);
  std::set<std::string> white_set;
  for (const auto& nm : active_course_names) {
    std::string n = norm_course_name(nm);
    if (!n.empty()) white_set.insert(n);
  }

  int unarchived_total = 0;
  for (const auto& c : courses) {
    if (!utils::json_bool(c, "archived", false)) ++unarchived_total;
  }

  nlohmann::json items = nlohmann::json::array();
  nlohmann::json failed = nlohmann::json::array();
  int scanned_n = 0;
  int rested_n = 0;
  int total = static_cast<int>(courses.size());
  int64_t now = utils::now_sec();

  for (int i = 0; i < total; ++i) {
    const auto& c = courses[static_cast<size_t>(i)];
    std::string cname = utils::json_str(c, "name", "");
    std::string cid = utils::json_str(c, "courseId", "");
    std::string clzid = utils::json_str(c, "classId", "");
    std::string cpi = utils::json_str(c, "cpi", "0");
    std::string fid = utils::json_str(c, "fid", "0");
    bool archived = utils::json_bool(c, "archived", false);

    // 智能筛选：已归档结课的跳过；未归档课程若总数 ≤ 24 门全部扫描（防止大一新生班会/实验室/心理课名称与教务不同被漏扫！），超过 24 门时按模糊白名单降频
    bool in_white = course_matches_whitelist(cname, white_set);
    if (!force_all && (archived || (unarchived_total > 24 && !in_white))) {
      ++rested_n;
      continue;
    }

    if (on_progress) on_progress(i + 1, total, cname, "作业与活动");
    if (scanned_n > 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1400));
    }
    ++scanned_n;

    try {
      // 第 1 步：进课程中间页拿同一次加载的 stuenc + workEnc
      std::string mid_url = "https://mooc1.chaoxing.com/visit/stucoursemiddle?courseid=" + cid +
                            "&clazzid=" + clzid + "&cpi=" + cpi + "&ismooc2=1&v=2";
      auto r_mid = sess.get(mid_url, {}, true, 15000);
      check_cx_http_status(r_mid, cname);
      std::string mid_html = r_mid.text_utf8();

      static const std::regex re_stuenc(R"(stuenc=([A-Za-z0-9]+))");
      std::smatch m1;
      std::string stuenc = std::regex_search(r_mid.final_url, m1, re_stuenc) ? m1[1].str() : "";
      if (stuenc.empty() && std::regex_search(mid_html, m1, re_stuenc)) {
        stuenc = m1[1].str();
      }
      if (stuenc.empty()) {
        stuenc = extract_input_value_by_id(mid_html, "stuenc");
      }
      std::string workenc = extract_input_value_by_id(mid_html, "workEnc");

      // 第 2 步：抓取作业列表
      if (!stuenc.empty() && !workenc.empty()) {
        std::string w_url = "https://mooc1.chaoxing.com/mooc2/work/list?courseId=" + cid +
                            "&classId=" + clzid + "&cpi=" + cpi + "&ut=s&t=" +
                            std::to_string(utils::now_ms()) + "&stuenc=" + stuenc + "&enc=" + workenc;
        auto r_w = sess.get(w_url, {{"Referer", r_mid.final_url}}, true, 15000);
        check_cx_http_status(r_w, cname + "作业");
        std::string w_html = r_w.text_utf8();

        static const std::regex re_li(R"(<li\b[^>]*data\s*=\s*["']([^"']*)["'][^>]*>([\s\S]*?)</li>)",
                                      std::regex::icase);
        static const std::regex re_title(R"(class\s*=\s*["'][^"']*overHidden2[^"']*["'][^>]*>([\s\S]*?)</p>)",
                                         std::regex::icase);
        static const std::regex re_status(R"(class\s*=\s*["'][^"']*status[^"']*["'][^>]*>([\s\S]*?)</p>)",
                                          std::regex::icase);

        for (std::sregex_iterator lit(w_html.begin(), w_html.end(), re_li), lend; lit != lend; ++lit) {
          std::string link = utils::html_unescape((*lit)[1].str());
          std::string inner = (*lit)[2].str();
          std::smatch mt, ms;
          std::string title = std::regex_search(inner, mt, re_title)
                                  ? utils::strip_html_tags(mt[1].str(), false)
                                  : "";
          std::string status_s = std::regex_search(inner, ms, re_status)
                                     ? utils::strip_html_tags(ms[1].str(), false)
                                     : "";
          if (title.empty()) continue;
          std::string plain = utils::strip_html_tags(inner, false);
          int64_t end_at = parse_left_hours_to_end_sec(plain, now);
          std::string bkt = chaoxing_work_bucket(status_s);
          items.push_back({
              {"id", "cx_w_" + crypto::md5_hex(cname + "|" + title).substr(0, 10)},
              {"kind", "work"},
              {"kindName", "作业"},
              {"course", cname},
              {"title", title},
              {"status", status_s},
              {"bucket", bkt},
              {"endAt", end_at > 0 ? nlohmann::json(end_at) : nlohmann::json(nullptr)},
              {"remainText", format_remain_text(end_at, now, status_s)},
              {"url", link.empty() ? "https://mooc1.chaoxing.com" : utils::resolve_url(w_url, link)},
          });
        }
      }

      // 第 2.5 步：抓取课程在线考试列表 (/exam-ans/mooc2/exam/exam-list)
      if (!stuenc.empty()) {
        std::string ex_url = "https://mooc1.chaoxing.com/exam-ans/mooc2/exam/exam-list?courseid=" + cid +
                             "&clazzid=" + clzid + "&cpi=" + cpi + "&ut=s&t=" +
                             std::to_string(utils::now_ms()) + "&stuenc=" + stuenc;
        auto r_ex = sess.get(ex_url, {{"Referer", r_mid.final_url}}, true, 15000);
        if (r_ex.status == 200) {
          std::string ex_html = r_ex.text_utf8();
          static const std::regex re_ex_li(R"(<li\b[^>]*data\s*=\s*["']([^"']*)["'][^>]*>([\s\S]*?)</li>)",
                                           std::regex::icase);
          static const std::regex re_ex_title(R"(class\s*=\s*["'][^"']*overHidden2[^"']*["'][^>]*>([\s\S]*?)</p>)",
                                              std::regex::icase);
          static const std::regex re_ex_st(R"(class\s*=\s*["'][^"']*status[^"']*["'][^>]*>([\s\S]*?)</p>)",
                                           std::regex::icase);
          for (std::sregex_iterator eit(ex_html.begin(), ex_html.end(), re_ex_li), eend; eit != eend; ++eit) {
            std::string link = utils::html_unescape((*eit)[1].str());
            std::string inner = (*eit)[2].str();
            std::smatch mt, ms;
            std::string title = std::regex_search(inner, mt, re_ex_title)
                                    ? utils::strip_html_tags(mt[1].str(), false)
                                    : "";
            std::string st_s = std::regex_search(inner, ms, re_ex_st)
                                   ? utils::strip_html_tags(ms[1].str(), false)
                                   : "";
            if (title.empty()) continue;
            std::string plain = utils::strip_html_tags(inner, false);
            int64_t end_at = parse_left_hours_to_end_sec(plain, now);
            bool is_done = (st_s.find("已完成") != std::string::npos ||
                            st_s.find("已结束") != std::string::npos ||
                            st_s.find("已交卷") != std::string::npos ||
                            st_s.find("待批阅") != std::string::npos ||
                            st_s.find("已批阅") != std::string::npos ||
                            st_s.find("已过期") != std::string::npos);
            items.push_back({
                {"id", "cx_ex_" + crypto::md5_hex(cname + "|" + title).substr(0, 10)},
                {"kind", "exam"},
                {"kindName", "考试"},
                {"course", cname},
                {"title", title},
                {"status", st_s},
                {"bucket", is_done ? "done" : "todo"},
                {"endAt", end_at > 0 ? nlohmann::json(end_at) : nlohmann::json(nullptr)},
                {"remainText", format_remain_text(end_at, now, st_s)},
                {"url", link.empty() ? "https://mooc1.chaoxing.com" : utils::resolve_url(ex_url, link)},
            });
          }
        }
      }

      // 第 3 步：抓取课堂活动（签到/随堂练习/讨论）
      std::string act_url = "https://mobilelearn.chaoxing.com/v2/apis/active/student/activelist?fid=" +
                            utils::url_encode(fid) + "&courseId=" + utils::url_encode(cid) +
                            "&classId=" + utils::url_encode(clzid) +
                            "&showNotStartedActive=0&_=" + std::to_string(utils::now_ms());
      auto r_act = sess.get(act_url, {}, false, 12000);
      if (r_act.status == 200) {
        auto aj = nlohmann::json::parse(r_act.text_utf8(), nullptr, false);
        if (aj.is_object() && aj.contains("data") && aj["data"].is_object() &&
            aj["data"].contains("activeList") && aj["data"]["activeList"].is_array()) {
          for (const auto& act : aj["data"]["activeList"]) {
            if (!act.is_object()) continue;
            int st = utils::json_int(act, "status", 2);  // 1 = 进行中
            if (st != 1) continue;
            std::string atype = utils::trim(utils::json_str(act, "activeType", "0"));
            std::string kind = "other";
            std::string kname = "活动";
            if (atype == "2" || atype == "74") { kind = "sign"; kname = "签到"; }
            else if (atype == "42" || atype == "68") { kind = "quiz"; kname = "练习"; }
            else if (atype == "5" || atype == "50") { kind = "topic"; kname = "讨论"; }
            else continue;

            int64_t end_raw = utils::json_i64(act, "endTime", 0);
            int64_t end_sec = (end_raw > 100000000000LL) ? (end_raw / 1000)
                              : (end_raw > 1000000000LL ? end_raw : 0);
            std::string act_title = utils::trim(utils::json_str(act, "nameOne", kname));
            if (act_title.empty()) act_title = kname;

            items.push_back({
                {"id", "cx_act_" + utils::json_str(act, "id", "0")},
                {"kind", kind},
                {"kindName", kname},
                {"course", cname},
                {"title", act_title},
                {"status", "进行中"},
                {"bucket", "todo"},
                {"endAt", end_sec > 0 ? nlohmann::json(end_sec) : nlohmann::json(nullptr)},
                {"url", "https://mobilelearn.chaoxing.com"},
            });
          }
        }
      }
    } catch (const CxError& e) {
      if (e.busy() || e.kind() == "auth") {
        throw;  // 触发 202 风控或登录态彻底失效时立即熔断整轮，绝不连撞剩余课程！
      }
      failed.push_back({{"course", cname}, {"why", e.what()}});
    } catch (const std::exception& ex) {
      failed.push_back({{"course", cname}, {"why", ex.what()}});
    }
  }

  if (out_updated_cookies) {
    *out_updated_cookies = sess.jar().dump_json();
  }
  return {
      {"at", utils::now_sec()},
      {"coursesScanned", scanned_n},
      {"coursesRested", rested_n},
      {"failed", failed},
      {"items", items},
  };
}

nlohmann::json ChaoxingClient::live_check(const std::string& account,
                                          const nlohmann::json& saved_cookies,
                                          const std::vector<std::string>& today_course_names) {
  if (enable_demo_ && SandboxProvider::is_demo_account(account)) {
    return {
        {"checkedAt", utils::now_sec()},
        {"items", nlohmann::json::array({
            {{"id", "cx_live_demo_1"}, {"kind", "quiz"}, {"kindName", "随堂练习"},
             {"course", "数据库系统概论"}, {"title", "B+树索引分裂课堂限时小测"},
             {"status", "进行中"}, {"endAt", utils::now_sec() + 900},
             {"url", "https://mobilelearn.chaoxing.com"}},
        })},
    };
  }
  // 随堂快查轻量直连：严格按今日课程过滤，仅请求 mobilelearn activelist，不抓作业/考试 HTML、不加长等待
  net::HttpSession sess(250);
  sess.jar().load_json(saved_cookies);
  auto courses = fetch_courses_on_session(sess);

  std::set<std::string> white_set;
  for (const auto& nm : today_course_names) {
    std::string n = norm_course_name(nm);
    if (!n.empty()) white_set.insert(n);
  }

  nlohmann::json live_items = nlohmann::json::array();
  int checked = 0;
  for (const auto& c : courses) {
    if (utils::json_bool(c, "archived", false)) continue;
    std::string cname = utils::json_str(c, "name", "");
    if (!white_set.empty() && !course_matches_whitelist(cname, white_set)) continue;
    if (white_set.empty() && checked >= 12) break;
    ++checked;

    std::string cid = utils::json_str(c, "courseId", "");
    std::string clzid = utils::json_str(c, "classId", "");
    std::string fid = utils::json_str(c, "fid", "0");
    std::string act_url = "https://mobilelearn.chaoxing.com/v2/apis/active/student/activelist?fid=" +
                          utils::url_encode(fid) + "&courseId=" + utils::url_encode(cid) +
                          "&classId=" + utils::url_encode(clzid) +
                          "&showNotStartedActive=0&_=" + std::to_string(utils::now_ms());
    auto r_act = sess.get(act_url, {}, false, 8000);
    if (r_act.status == 401 || r_act.status == 302) {
      check_cx_http_status(r_act, cname + "课堂活动");
    }
    if (r_act.status == 202) {
      check_cx_http_status(r_act, cname + "课堂活动");
    }
    if (r_act.status != 200) continue;
    auto aj = nlohmann::json::parse(r_act.text_utf8(), nullptr, false);
    if (!aj.is_object() || !aj.contains("data") || !aj["data"].is_object() ||
        !aj["data"].contains("activeList") || !aj["data"]["activeList"].is_array()) {
      continue;
    }
    for (const auto& act : aj["data"]["activeList"]) {
      if (!act.is_object()) continue;
      if (utils::json_int(act, "status", 2) != 1) continue;
      std::string atype = utils::trim(utils::json_str(act, "activeType", "0"));
      std::string kind, kname;
      if (atype == "2" || atype == "74") { kind = "sign"; kname = "签到"; }
      else if (atype == "42" || atype == "68") { kind = "quiz"; kname = "练习"; }
      else continue;

      int64_t end_raw = utils::json_i64(act, "endTime", 0);
      int64_t end_sec = (end_raw > 100000000000LL) ? (end_raw / 1000)
                        : (end_raw > 1000000000LL ? end_raw : 0);
      std::string act_title = utils::trim(utils::json_str(act, "nameOne", kname));
      if (act_title.empty()) act_title = kname;
      live_items.push_back({
          {"id", "cx_act_" + utils::json_str(act, "id", "0")},
          {"kind", kind},
          {"kindName", kname},
          {"course", cname},
          {"title", act_title},
          {"status", "进行中"},
          {"bucket", "todo"},
          {"endAt", end_sec > 0 ? nlohmann::json(end_sec) : nlohmann::json(nullptr)},
          {"url", "https://mobilelearn.chaoxing.com"},
      });
    }
  }
  return {{"checkedAt", utils::now_sec()}, {"items", live_items}};
}

nlohmann::json ChaoxingClient::fetch_notices(const std::string& account,
                                             const nlohmann::json& saved_cookies) {
  if (enable_demo_ && SandboxProvider::is_demo_account(account)) {
    return SandboxProvider::demo_chaoxing_notices();
  }
  net::HttpSession sess(450);
  sess.jar().load_json(saved_cookies);
  auto r = sess.post_form(
      "https://notice.chaoxing.com/pc/notice/getNoticeList",
      {{"type", "2"}, {"notice_type", ""}, {"lastValue", ""}, {"sort", ""}},
      {{"Referer", "https://i.chaoxing.com/"}},
      false,
      12000);
  check_cx_http_status(r, "收件箱");
  std::string body_txt = r.text_utf8();
  auto j = nlohmann::json::parse(body_txt, nullptr, false);
  if (!j.is_object()) {
    std::string low = utils::to_lower(body_txt.substr(0, std::min<size_t>(body_txt.size(), 800)));
    if (low.find("login") != std::string::npos || low.find("passport2") != std::string::npos) {
      throw CxError("学习通登录已过期，请点击「重新连接」", "auth", false);
    }
    throw CxError("学习通收件箱返回格式异常", "data", false);
  }
  if ((j.contains("status") && j["status"].is_boolean() && !j["status"].get<bool>()) ||
      (j.contains("result") && utils::json_int(j, "result", 1) == 0)) {
    throw CxError("学习通登录已过期或接口拒绝访问，请重新连接", "auth", false);
  }
  if (!j.contains("notices") || !j["notices"].is_object() ||
      !j["notices"].contains("list") || !j["notices"]["list"].is_array()) {
    throw CxError("学习通收件箱数据结构异常，请稍后重试", "data", false);
  }
  nlohmann::json list = nlohmann::json::array();
  for (const auto& n : j["notices"]["list"]) {
    if (!n.is_object()) continue;
    std::string content = utils::strip_html_tags(utils::json_str(n, "content", ""), false);
    if (content.empty()) continue;
    std::string course_title = utils::trim(utils::json_str(n, "title", "学习通通知"));
    if (course_title.empty()) course_title = "学习通通知";
    list.push_back({
        {"id", utils::json_str(n, "id", "")},
        {"title", content},
        {"course", course_title},
        {"sender", utils::json_str(n, "createrName", "")},
        {"time", utils::json_str(n, "completeTime", "")},
        {"kind", content.find("考试") != std::string::npos ? "exam"
                 : (content.find("作业") != std::string::npos ? "work" : "notice")},
        {"url", "https://i.chaoxing.com"},
    });
  }
  return list;
}

}  // namespace hnu::adapters
