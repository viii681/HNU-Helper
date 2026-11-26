#include "hnu/adapters/mooc_client.hpp"
#include "hnu/adapters/sandbox_provider.hpp"
#include "hnu/net/http_client.hpp"
#include "hnu/utils/json_safe.hpp"
#include "hnu/utils/strings_and_time.hpp"

#include <algorithm>

namespace hnu::adapters {

namespace {

std::string extract_csrf_si(const net::CookieJar& jar) {
  return jar.get_cookie("NTESSTUDYSI");
}

int64_t ms_to_sec_opt(int64_t v) {
  if (v <= 0) return 0;
  return v > 100000000000LL ? (v / 1000) : v;
}

}  // namespace

nlohmann::json MoocClient::verify_and_bind_cookie(std::string_view raw_cookie_input,
                                                  nlohmann::json* out_cookies_json) {
  std::string trimmed = utils::trim(raw_cookie_input);
  if (enable_demo_ && (trimmed == "demo" || SandboxProvider::is_demo_account(trimmed))) {
    if (out_cookies_json) *out_cookies_json = nlohmann::json::array();
    return {{"ok", true}, {"nickName", "林海椰"}, {"school", "海南大学"}, {"demo", true}};
  }

  net::CookieJar jar;
  // 支持粘贴原始 Cookie 头 ("a=b; c=d") 或 JSON 数组
  if (!trimmed.empty() && trimmed.front() == '[') {
    auto j = nlohmann::json::parse(trimmed, nullptr, false);
    if (j.is_array()) jar.load_json(j);
  } else {
    // 兼容直接复制整行请求头 "Cookie: STUDY_SESS=...; NTESSTUDYSI=..."
    std::string raw_line = trimmed;
    if (utils::starts_with(utils::to_lower(raw_line), "cookie:")) {
      raw_line = utils::trim(raw_line.substr(7));
    }
    for (const auto& pair_s : utils::split(raw_line, ';')) {
      std::string p = utils::trim(pair_s);
      size_t eq = p.find('=');
      if (eq == std::string::npos) continue;
      std::string k = utils::trim(p.substr(0, eq));
      std::string v = utils::trim(p.substr(eq + 1));
      if (!k.empty() && !v.empty()) {
        jar.set_cookie({k, v, "icourse163.org", "/", false, 0});
      }
    }
  }

  std::string si = extract_csrf_si(jar);
  if (si.empty() || jar.get_cookie("STUDY_SESS").empty()) {
    throw MoocError("未检测到完整的登录凭据（需包含 STUDY_SESS 与 NTESSTUDYSI），请按页面指引从已登录的 icourse163.org 复制 Cookie", "auth");
  }

  net::HttpSession sess(800);
  sess.jar().load_json(jar.dump_json());
  std::string url = "https://www.icourse163.org/web/j/mocMemberFollowBean.userInfo.rpc?csrfKey=" + utils::url_encode(si);
  auto r = sess.post_form(url, {}, {{"Origin", "https://www.icourse163.org"}, {"Referer", "https://www.icourse163.org/"}});
  if (r.status != 200) {
    throw MoocError("连接中国大学 MOOC 失败（HTTP " + std::to_string(r.status) + "）", "net");
  }
  auto j = nlohmann::json::parse(r.text_utf8(), nullptr, false);
  if (!j.is_object() || utils::json_int(j, "code", -1) != 0) {
    throw MoocError("MOOC Cookie 已失效或不正确，请重新登录 icourse163.org 获取", "auth");
  }
  const auto& res = (j.contains("result") && j["result"].is_object()) ? j["result"] : nlohmann::json::object();
  std::string nick = utils::json_str(res, "nickName", "MOOC学员");
  std::string school = utils::json_str(res, "schoolName", "");
  if (out_cookies_json) *out_cookies_json = sess.jar().dump_json();
  return {{"ok", true}, {"nickName", nick}, {"school", school}};
}

nlohmann::json MoocClient::scan(const std::string& account_hint,
                                const nlohmann::json& saved_cookies) {
  if (enable_demo_ && SandboxProvider::is_demo_account(account_hint)) {
    return SandboxProvider::demo_mooc_scan();
  }

  net::HttpSession sess(1200);
  sess.jar().load_json(saved_cookies);
  std::string si = extract_csrf_si(sess.jar());
  if (si.empty()) {
    throw MoocError("中国大学 MOOC 登录态缺失，请重新绑定", "auth");
  }

  auto rpc_call = [&](const std::string& path, const std::vector<std::pair<std::string, std::string>>& form) {
    std::string u = "https://www.icourse163.org" + path + "?csrfKey=" + utils::url_encode(si);
    auto r = sess.post_form(u, form, {{"Origin", "https://www.icourse163.org"}, {"Referer", "https://www.icourse163.org/"}});
    if (r.status != 200) throw MoocError("MOOC 接口返回 HTTP " + std::to_string(r.status), "net");
    auto j = nlohmann::json::parse(r.text_utf8(), nullptr, false);
    if (!j.is_object()) throw MoocError("MOOC 响应无法解析", "data");
    int code = utils::json_int(j, "code", -1);
    if (code == -1002) throw MoocError("中国大学 MOOC 登录已过期，请重新导入 Cookie", "auth");
    if (code != 0) throw MoocError(utils::json_str(j, "message", "MOOC 返回业务错误"), "data");
    return j;
  };

  auto jc = rpc_call("/web/j/learnerCourseRpcBean.getMyLearnedCoursePanelList.rpc",
                     {{"type", "30"}, {"p", "1"}, {"psize", "50"}, {"courseType", "1"}});

  nlohmann::json courses = nlohmann::json::array();
  nlohmann::json items = nlohmann::json::array();
  nlohmann::json failed = nlohmann::json::array();
  int n_todo = 0, n_exp = 0, n_done = 0;
  int64_t now = utils::now_sec();

  const auto& rows = (jc.contains("result") && jc["result"].is_object() &&
                      jc["result"].contains("result") && jc["result"]["result"].is_array())
                         ? jc["result"]["result"]
                         : nlohmann::json::array();
  for (const auto& c : rows) {
    if (!c.is_object() || !c.contains("termPanel") || !c["termPanel"].is_object()) continue;
    int64_t tid = utils::json_i64(c["termPanel"], "id", 0);
    int64_t cid = utils::json_i64(c, "id", 0);
    std::string cname = utils::trim(utils::json_str(c, "name", ""));
    if (tid <= 0 || cname.empty()) continue;
    courses.push_back({{"courseId", cid}, {"termId", tid}, {"name", cname}});

    try {
      auto jt = rpc_call("/web/j/courseBean.getLastLearnedMocTermDto.rpc",
                         {{"termId", std::to_string(tid)}});
      const auto& term_dto = (jt.contains("result") && jt["result"].is_object() &&
                              jt["result"].contains("mocTermDto") && jt["result"]["mocTermDto"].is_object())
                                 ? jt["result"]["mocTermDto"]
                                 : nlohmann::json::object();
      std::string page_url = "https://www.icourse163.org/learn/icourse-" + std::to_string(cid) +
                             "?tid=" + std::to_string(tid) + "#/learn/testlist";

      if (term_dto.contains("chapters") && term_dto["chapters"].is_array()) {
        for (const auto& ch : term_dto["chapters"]) {
          if (!ch.is_object()) continue;
          std::string ch_name = utils::trim(utils::json_str(ch, "name", ""));
          auto collect = [&](const nlohmann::json& arr, const char* def_kind, const char* def_kname) {
            if (!arr.is_array()) return;
            for (const auto& row : arr) {
              if (!row.is_object()) continue;
              const auto& t = (row.contains("test") && row["test"].is_object()) ? row["test"] : row;
              std::string title = utils::trim(utils::json_str(t, "name", utils::json_str(row, "name", "")));
              if (title.empty()) continue;
              int tried = utils::json_int(t, "usedTryCount", 0);
              bool has_score = t.contains("userScore") && !t["userScore"].is_null() &&
                               !(t["userScore"].is_string() && utils::trim(t["userScore"].get<std::string>()).empty());
              int64_t end_at = ms_to_sec_opt(utils::json_i64(t, "deadline", 0));

              std::string state = "todo";
              if (tried >= 1 || has_score) { state = "done"; ++n_done; }
              else if (end_at > 0 && end_at < now) { state = "expired"; ++n_exp; }
              else { state = "todo"; ++n_todo; }

              items.push_back({
                  {"kind", def_kind},
                  {"kindName", def_kname},
                  {"title", title},
                  {"course", cname},
                  {"chapter", ch_name},
                  {"state", state},
                  {"endAt", end_at > 0 ? nlohmann::json(end_at) : nlohmann::json(nullptr)},
                  {"score", has_score ? t["userScore"] : nlohmann::json(nullptr)},
                  {"total", utils::json_int(t, "totalScore", 100)},
                  {"url", page_url},
              });
            }
          };
          if (ch.contains("homeworks")) collect(ch["homeworks"], "work", "作业");
          if (ch.contains("quizs")) collect(ch["quizs"], "quiz", "测验");
          if (ch.contains("exam") && ch["exam"].is_object()) {
            collect(nlohmann::json::array({ch["exam"]}), "exam", "考试");
          }
        }
      }
    } catch (const MoocError& e) {
      if (e.kind() == "auth") throw;
      failed.push_back({{"course", cname}, {"why", e.what()}});
    }
  }

  return {
      {"at", now},
      {"courses", courses},
      {"items", items},
      {"failed", failed},
      {"count", {{"todo", n_todo}, {"expired", n_exp}, {"done", n_done}}},
  };
}

}  // namespace hnu::adapters
