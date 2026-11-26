#include "hnu/services/calendar_ics.hpp"
#include "hnu/crypto/crypto.hpp"
#include "hnu/utils/json_safe.hpp"
#include "hnu/utils/strings_and_time.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <vector>

namespace hnu::services {

namespace {

std::string ics_escape(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    if (c == '\\' || c == ';' || c == ',') {
      out.push_back('\\');
      out.push_back(c);
    } else if (c == '\n') {
      out.append("\\n");
    } else if (c != '\r') {
      out.push_back(c);
    }
  }
  return out;
}

bool parse_ymd_to_tm(const std::string& ymd, std::tm& out) {
  if (ymd.size() < 10 || ymd[4] != '-' || ymd[7] != '-') return false;
  for (size_t idx : {size_t{0}, size_t{1}, size_t{2}, size_t{3}, size_t{5}, size_t{6}, size_t{8}, size_t{9}}) {
    if (!std::isdigit(static_cast<unsigned char>(ymd[idx]))) return false;
  }
  try {
    int y = std::stoi(ymd.substr(0, 4));
    int m = std::stoi(ymd.substr(5, 2));
    int d = std::stoi(ymd.substr(8, 2));
    if (y < 1970 || y > 2100 || m < 1 || m > 12 || d < 1 || d > 31) return false;
    out = std::tm{};
    out.tm_year = y - 1900;
    out.tm_mon = m - 1;
    out.tm_mday = d;
    out.tm_hour = 12;
    std::tm check = out;
#ifdef _WIN32
    time_t ts = _mkgmtime(&check);
#else
    time_t ts = timegm(&check);
#endif
    if (ts == static_cast<time_t>(-1)) return false;
    return check.tm_year == out.tm_year && check.tm_mon == out.tm_mon && check.tm_mday == out.tm_mday;
  } catch (...) {
    return false;
  }
}

std::string add_days_ics_ymd(const std::tm& base, int add_days) {
  std::tm t = base;
#ifdef _WIN32
  time_t ts = _mkgmtime(&t);
#else
  time_t ts = timegm(&t);
#endif
  if (ts == static_cast<time_t>(-1)) return "";
  ts += static_cast<time_t>(add_days) * 86400;
  std::tm r{};
#ifdef _WIN32
  if (gmtime_s(&r, &ts) != 0) return "";
#else
  if (!gmtime_r(&ts, &r)) return "";
#endif
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%04d%02d%02d",
                r.tm_year + 1900, r.tm_mon + 1, r.tm_mday);
  return buf;
}

std::string hhmm_compact(const std::string& hhmm) {
  std::string d;
  for (char c : hhmm) {
    if (c >= '0' && c <= '9') d.push_back(c);
  }
  while (d.size() < 4) d.insert(d.begin(), '0');
  return d.substr(0, 4) + "00";
}

std::string unix_to_bj_ics_dt(int64_t unix_sec) {
  time_t bj = static_cast<time_t>(unix_sec + 8 * 3600);
  std::tm r{};
#ifdef _WIN32
  gmtime_s(&r, &bj);
#else
  gmtime_r(&bj, &r);
#endif
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%04d%02d%02dT%02d%02d%02d",
                r.tm_year + 1900, r.tm_mon + 1, r.tm_mday,
                r.tm_hour, r.tm_min, r.tm_sec);
  return buf;
}

std::string unix_to_utc_ics_dt(int64_t unix_sec) {
  time_t utc = static_cast<time_t>(unix_sec);
  std::tm r{};
#ifdef _WIN32
  gmtime_s(&r, &utc);
#else
  gmtime_r(&utc, &r);
#endif
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%04d%02d%02dT%02d%02d%02dZ",
                r.tm_year + 1900, r.tm_mon + 1, r.tm_mday,
                r.tm_hour, r.tm_min, r.tm_sec);
  return buf;
}

}  // namespace

std::string CalendarIcsGenerator::build_user_ics(const nlohmann::json& timetable,
                                                 const nlohmann::json& exams,
                                                 const nlohmann::json& cx_todo,
                                                 const nlohmann::json& mooc_todo,
                                                 const nlohmann::json& prefs,
                                                 const nlohmann::json& bell_presets) {
  std::string term_start = utils::json_str(prefs, "termStartDate", "2026-08-31");
  std::string preset = utils::json_str(prefs, "campusPreset", "haidian");
  int remind_min = 15;
  if (prefs.is_object() && prefs.contains("remindCfg") && prefs["remindCfg"].is_object()) {
    remind_min = std::clamp(utils::json_int(prefs["remindCfg"], "courseBeforeMin", 15), 5, 120);
  }

  // 解析 11 节作息表
  nlohmann::json periods = nlohmann::json::array();
  if (prefs.is_object() && prefs.contains("bellSchedule") && prefs["bellSchedule"].is_array()) {
    periods = prefs["bellSchedule"];
  }
  if (!periods.is_array() || periods.size() < 8) {
    if (bell_presets.is_object() && bell_presets.contains(preset) &&
        bell_presets[preset].is_object() && bell_presets[preset].contains("periods") &&
        bell_presets[preset]["periods"].is_array()) {
      periods = bell_presets[preset]["periods"];
    } else if (bell_presets.is_object() && bell_presets.contains("haidian") &&
               bell_presets["haidian"].is_object() && bell_presets["haidian"].contains("periods") &&
               bell_presets["haidian"]["periods"].is_array()) {
      periods = bell_presets["haidian"]["periods"];
    }
  }
  if (!periods.is_array() || periods.empty()) {
    periods = nlohmann::json::array({
        nlohmann::json::array({"07:40", "08:25"}), nlohmann::json::array({"08:35", "09:20"}),
        nlohmann::json::array({"09:45", "10:30"}), nlohmann::json::array({"10:40", "11:25"}),
        nlohmann::json::array({"14:30", "15:15"}), nlohmann::json::array({"15:25", "16:10"}),
        nlohmann::json::array({"16:35", "17:20"}), nlohmann::json::array({"17:30", "18:15"}),
        nlohmann::json::array({"19:20", "20:05"}), nlohmann::json::array({"20:15", "21:00"}),
        nlohmann::json::array({"21:10", "21:55"})
    });
  }

  auto period_time_pair = [&](size_t idx, std::string& start_out, std::string& end_out) -> bool {
    if (idx >= periods.size() || !periods[idx].is_array() || periods[idx].size() < 2) return false;
    if (!periods[idx][0].is_string() || !periods[idx][1].is_string()) return false;
    start_out = periods[idx][0].get<std::string>();
    end_out = periods[idx][1].get<std::string>();
    return !start_out.empty() && !end_out.empty();
  };

  std::set<std::string> hidden_codes;
  if (prefs.is_object() && prefs.contains("hiddenCourses") && prefs["hiddenCourses"].is_array()) {
    for (const auto& c : prefs["hiddenCourses"]) {
      if (c.is_string()) hidden_codes.insert(c.get<std::string>());
    }
  }

  std::ostringstream ics;
  std::string dtstamp = unix_to_utc_ics_dt(utils::now_sec());

  ics << "BEGIN:VCALENDAR\r\n"
      << "VERSION:2.0\r\n"
      << "PRODID:-//HnuTong//Hainan University Campus System//CN\r\n"
      << "CALSCALE:GREGORIAN\r\n"
      << "METHOD:PUBLISH\r\n"
      << "X-WR-CALNAME:海大通·课表与考试待办\r\n"
      << "X-WR-TIMEZONE:Asia/Shanghai\r\n"
      << "BEGIN:VTIMEZONE\r\n"
      << "TZID:Asia/Shanghai\r\n"
      << "X-LIC-LOCATION:Asia/Shanghai\r\n"
      << "BEGIN:STANDARD\r\n"
      << "TZOFFSETFROM:+0800\r\n"
      << "TZOFFSETTO:+0800\r\n"
      << "TZNAME:CST\r\n"
      << "DTSTART:19700101T000000\r\n"
      << "END:STANDARD\r\n"
      << "END:VTIMEZONE\r\n";

  // 1. 导出课程（合并教务课表 + 用户自定义加课）
  std::tm start_tm{};
  if (parse_ymd_to_tm(term_start, start_tm)) {
    nlohmann::json all_courses = nlohmann::json::array();
    if (timetable.is_object() && timetable.contains("courses") && timetable["courses"].is_array()) {
      for (const auto& c : timetable["courses"]) all_courses.push_back(c);
    }
    if (prefs.is_object() && prefs.contains("customCourses") && prefs["customCourses"].is_array()) {
      for (const auto& c : prefs["customCourses"]) {
        all_courses.push_back(c);
      }
    }

    for (const auto& c : all_courses) {
      if (!c.is_object()) continue;
      std::string code = utils::json_str(c, "code", "");
      std::string name = utils::json_str(c, "name", "");
      if (name.empty()) continue;
      if ((!code.empty() && hidden_codes.count(code)) || hidden_codes.count("NAME:" + name)) continue;
      std::string room = utils::json_str(c, "room", "");
      std::string teacher = utils::json_str(c, "teacher", "");

      // 按 day 将连续节次合并为一个日历事件
      std::map<int, std::vector<int>> day_periods;
      if (c.contains("slots") && c["slots"].is_array()) {
        for (const auto& s : c["slots"]) {
          if (!s.is_object()) continue;
          int d = utils::json_int(s, "day", -1);
          int p = utils::json_int(s, "period", -1);
          if (d >= 0 && d < 7 && p >= 0 && static_cast<size_t>(p) < periods.size()) {
            day_periods[d].push_back(p);
          }
        }
      }

      for (auto& [day, ps] : day_periods) {
        std::sort(ps.begin(), ps.end());
        ps.erase(std::unique(ps.begin(), ps.end()), ps.end());
        size_t idx = 0;
        while (idx < ps.size()) {
          int p_start = ps[idx];
          int p_end = p_start;
          while (idx + 1 < ps.size() && ps[idx + 1] == p_end + 1) {
            ++idx;
            p_end = ps[idx];
          }
          ++idx;

          std::string s_start, s_dummy1, s_dummy2, s_end;
          if (!period_time_pair(static_cast<size_t>(p_start), s_start, s_dummy1) ||
              !period_time_pair(static_cast<size_t>(p_end), s_dummy2, s_end)) {
            continue;
          }
          std::string t_begin = hhmm_compact(s_start);
          std::string t_end = hhmm_compact(s_end);

          if (!c.contains("weeks") || !c["weeks"].is_array()) continue;
          for (const auto& wk_j : c["weeks"]) {
            int wk = 0;
            if (wk_j.is_number_integer()) wk = wk_j.get<int>();
            else if (wk_j.is_string()) {
              try { wk = std::stoi(wk_j.get<std::string>()); } catch (...) { wk = 0; }
            }
            if (wk < 1 || wk > 26) continue;
            std::string ymd = add_days_ics_ymd(start_tm, (wk - 1) * 7 + day);
            if (ymd.empty()) continue;
            std::string uid = "cls-" + crypto::md5_hex(name + code + ymd + t_begin).substr(0, 16) + "@hnutong";

            ics << "BEGIN:VEVENT\r\n"
                << "UID:" << uid << "\r\n"
                << "DTSTAMP:" << dtstamp << "\r\n"
                << "DTSTART;TZID=Asia/Shanghai:" << ymd << "T" << t_begin << "\r\n"
                << "DTEND;TZID=Asia/Shanghai:" << ymd << "T" << t_end << "\r\n"
                << "SUMMARY:" << ics_escape(name) << "\r\n";
            if (!room.empty()) {
              ics << "LOCATION:" << ics_escape(room) << "\r\n";
            }
            std::string desc = "第" + std::to_string(wk) + "周 · 第" +
                               std::to_string(p_start + 1) + "-" + std::to_string(p_end + 1) + "节";
            if (!teacher.empty()) desc += " · 教师：" + teacher;
            ics << "DESCRIPTION:" << ics_escape(desc) << "\r\n"
                << "BEGIN:VALARM\r\n"
                << "TRIGGER:-PT" << remind_min << "M\r\n"
                << "ACTION:DISPLAY\r\n"
                << "DESCRIPTION:即将上课：" << ics_escape(name) << "\r\n"
                << "END:VALARM\r\n"
                << "END:VEVENT\r\n";
          }
        }
      }
    }
  }

  // 2. 导出教务考试安排
  if (exams.is_object() && exams.contains("exams") && exams["exams"].is_array()) {
    static const std::regex re_time_range(R"((\d{1,2}:\d{2})\s*(?:~|-|～)\s*(\d{1,2}:\d{2}))");
    for (const auto& ex : exams["exams"]) {
      if (!ex.is_object()) continue;
      std::string name = utils::json_str(ex, "name", "");
      std::string date = utils::replace_all(utils::json_str(ex, "date", ""), "-", "");
      date = utils::replace_all(date, "/", "");
      if (name.empty() || date.size() != 8) continue;

      std::string arrange = utils::json_str(ex, "arrange", "09:00~11:00");
      std::string t1 = "090000", t2 = "110000";
      std::smatch tm_m;
      if (std::regex_search(arrange, tm_m, re_time_range)) {
        t1 = hhmm_compact(tm_m[1].str());
        t2 = hhmm_compact(tm_m[2].str());
      }
      std::string loc = utils::json_str(ex, "campus", "") + " " + utils::json_str(ex, "room", "");
      std::string seat = utils::json_str(ex, "seat", "");
      if (!seat.empty()) loc += " (座位" + seat + ")";

      std::string uid = "exam-" + crypto::md5_hex(name + date + t1).substr(0, 16) + "@hnutong";
      ics << "BEGIN:VEVENT\r\n"
          << "UID:" << uid << "\r\n"
          << "DTSTAMP:" << dtstamp << "\r\n"
          << "DTSTART;TZID=Asia/Shanghai:" << date << "T" << t1 << "\r\n"
          << "DTEND;TZID=Asia/Shanghai:" << date << "T" << t2 << "\r\n"
          << "SUMMARY:[考试] " << ics_escape(name) << "\r\n"
          << "LOCATION:" << ics_escape(utils::trim(loc)) << "\r\n"
          << "BEGIN:VALARM\r\n"
          << "TRIGGER:-PT60M\r\n"
          << "ACTION:DISPLAY\r\n"
          << "DESCRIPTION:考试提醒：" << ics_escape(name) << "\r\n"
          << "END:VALARM\r\n"
          << "END:VEVENT\r\n";
    }
  }

  // 3. 导出未完成作业与在线测验截止提醒（尊重本地「我交过了」doneMarks 标记）
  nlohmann::json done_marks = (prefs.is_object() && prefs.contains("doneMarks") && prefs["doneMarks"].is_object())
                                  ? prefs["doneMarks"]
                                  : nlohmann::json::object();
  auto emit_due_items = [&](const nlohmann::json& src_obj, const char* tag) {
    if (!src_obj.is_object() || !src_obj.contains("items") || !src_obj["items"].is_array()) return;
    for (const auto& it : src_obj["items"]) {
      if (!it.is_object()) continue;
      std::string state = utils::json_str(it, "bucket", utils::json_str(it, "state", ""));
      if (state != "todo") continue;
      int64_t end_at = utils::json_i64(it, "endAt", 0);
      if (end_at <= 0) continue;

      std::string course = utils::json_str(it, "course", "");
      std::string title = utils::json_str(it, "title", "");
      std::string dkey = course + "|" + title;
      if (done_marks.contains(dkey)) continue;

      std::string dt_end = unix_to_bj_ics_dt(end_at);
      std::string dt_start = unix_to_bj_ics_dt(end_at - 1800);
      std::string uid = "due-" + crypto::md5_hex(dkey + std::to_string(end_at)).substr(0, 16) + "@hnutong";

      ics << "BEGIN:VEVENT\r\n"
          << "UID:" << uid << "\r\n"
          << "DTSTAMP:" << dtstamp << "\r\n"
          << "DTSTART;TZID=Asia/Shanghai:" << dt_start << "\r\n"
          << "DTEND;TZID=Asia/Shanghai:" << dt_end << "\r\n"
          << "SUMMARY:[" << tag << "截止] " << ics_escape(course + " · " + title) << "\r\n"
          << "BEGIN:VALARM\r\n"
          << "TRIGGER:-PT180M\r\n"
          << "ACTION:DISPLAY\r\n"
          << "DESCRIPTION:作业即将截止：" << ics_escape(title) << "\r\n"
          << "END:VALARM\r\n"
          << "END:VEVENT\r\n";
    }
  };
  emit_due_items(cx_todo, "学习通");
  emit_due_items(mooc_todo, "MOOC");

  ics << "END:VCALENDAR\r\n";
  return ics.str();
}

}  // namespace hnu::services
