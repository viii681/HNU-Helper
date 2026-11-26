#include "hnu/adapters/qiangzhi_client.hpp"
#include "hnu/adapters/sandbox_provider.hpp"
#include "hnu/crypto/crypto.hpp"
#include "hnu/utils/json_safe.hpp"
#include "hnu/utils/logger.hpp"
#include "hnu/utils/strings_and_time.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <thread>

namespace hnu::adapters {

namespace {

bool looks_logged_out_qz(int status, std::string_view final_url, std::string_view html) {
  // 严禁把 status == 0（网络超时/代理异常）误判为「会话过期」！
  if (status == 0) return false;
  if (status == 401 || status == 403) return true;
  if (final_url.find("LoginToXk") != std::string_view::npos ||
      final_url.find("authserver/login") != std::string_view::npos) {
    return true;
  }
  return html.find("id=\"loginForm\"") != std::string_view::npos ||
         html.find("name=\"loginForm\"") != std::string_view::npos ||
         html.find("name=\"userAccount\"") != std::string_view::npos ||
         html.find("id=\"pwdFromId\"") != std::string_view::npos ||
         html.find("authserver/login") != std::string_view::npos ||
         html.find("统一身份认证平台") != std::string_view::npos ||
         html.find("请先登录") != std::string_view::npos ||
         html.find("重新登录") != std::string_view::npos ||
         html.find("会话已过期") != std::string_view::npos ||
         html.find("您当前登录超时") != std::string_view::npos;
}

// 海南大学全部 17 个书院官方标准名称（覆盖五校区）
std::string detect_hnu_academy_in_text(std::string_view text) {
  static const char* kAllHnuAcademies[] = {
      "椰风书院", "乘风书院", "凤翔书院", "子衿书院", "青云书院",
      "南海书院", "淳明书院", "海德书院", "天工书院", "朴诚书院",
      "中和书院", "今朝书院", "日新书院", "丹心书院", "檀雅书院",
      "晨曦书院", "崖州书院"
  };
  for (const char* ac : kAllHnuAcademies) {
    if (text.find(ac) != std::string_view::npos) {
      return ac;
    }
  }
  return "";
}

std::string infer_hnu_campus_from_academy(std::string_view academy) {
  if (academy == "今朝书院" || academy == "日新书院") return "观澜湖校区";
  if (academy == "丹心书院" || academy == "檀雅书院") return "儋州校区";
  if (academy == "晨曦书院") return "城西教学点";
  if (academy == "崖州书院") return "崖州湾科教园";
  if (!academy.empty()) return "海甸校区";
  return "";
}

// 检查 HTML 标签名在 lower[idx] 处是否精确匹配（如 "<table" / "</div"）
bool is_html_tag_at(std::string_view lower, size_t idx, std::string_view tag_prefix) {
  if (idx + tag_prefix.size() > lower.size()) return false;
  if (lower.substr(idx, tag_prefix.size()) != tag_prefix) return false;
  if (idx + tag_prefix.size() == lower.size()) return true;
  char c = lower[idx + tag_prefix.size()];
  return c == '>' || c == '/' || std::isspace(static_cast<unsigned char>(c));
}

int parse_tag_span_attr(std::string_view open_tag_lower, std::string_view attr_name) {
  size_t pos = 0;
  while ((pos = open_tag_lower.find(attr_name, pos)) != std::string_view::npos) {
    if (pos > 0 && std::isalnum(static_cast<unsigned char>(open_tag_lower[pos - 1]))) {
      pos += attr_name.size();
      continue;
    }
    size_t i = pos + attr_name.size();
    while (i < open_tag_lower.size() && std::isspace(static_cast<unsigned char>(open_tag_lower[i]))) ++i;
    if (i < open_tag_lower.size() && open_tag_lower[i] == '=') {
      ++i;
      while (i < open_tag_lower.size() && std::isspace(static_cast<unsigned char>(open_tag_lower[i]))) ++i;
      if (i < open_tag_lower.size() && (open_tag_lower[i] == '"' || open_tag_lower[i] == '\'')) ++i;
      int val = 0;
      while (i < open_tag_lower.size() && std::isdigit(static_cast<unsigned char>(open_tag_lower[i]))) {
        val = val * 10 + (open_tag_lower[i] - '0');
        ++i;
      }
      return std::clamp(val > 0 ? val : 1, 1, 60);
    }
    pos += attr_name.size();
  }
  return 1;
}

struct RichCell {
  std::string text;
  std::string raw_html;
  int rowspan = 1;
  int colspan = 1;
};

// 提取完整表格并保留每个单元格的原始内部 HTML（用于抓取成绩明细链接）与 rowspan/colspan（用于空教室二维矩阵展开）
std::vector<std::vector<std::vector<RichCell>>> parse_html_tables_rich(std::string_view html) {
  std::vector<std::vector<std::vector<RichCell>>> tables;
  std::string lower = utils::to_lower(html);
  size_t pos = 0;

  while (pos < lower.size()) {
    size_t t_open = lower.find("<table", pos);
    if (t_open == std::string::npos) break;
    if (!is_html_tag_at(lower, t_open, "<table")) {
      pos = t_open + 6;
      continue;
    }
    size_t gt = lower.find('>', t_open + 6);
    if (gt == std::string::npos) break;

    size_t scan = gt + 1;
    int depth = 1;
    size_t t_close = std::string::npos;
    while (depth > 0 && scan < lower.size()) {
      size_t next_lt = lower.find('<', scan);
      if (next_lt == std::string::npos) break;
      if (is_html_tag_at(lower, next_lt, "<table")) {
        ++depth;
        scan = next_lt + 6;
      } else if (is_html_tag_at(lower, next_lt, "</table")) {
        --depth;
        if (depth == 0) t_close = next_lt;
        scan = next_lt + 7;
      } else {
        scan = next_lt + 1;
      }
    }
    if (t_close == std::string::npos) break;

    std::vector<std::vector<RichCell>> rows;
    std::vector<RichCell> cur_cells;
    bool in_own_tr = false;
    bool in_own_cell = false;
    size_t cell_content_start = 0;
    int cur_rspan = 1;
    int cur_cspan = 1;
    int nested_tbl = 0;

    auto flush_cell = [&](size_t end_pos) {
      std::string_view raw = html.substr(cell_content_start, end_pos - cell_content_start);
      cur_cells.push_back(RichCell{
          utils::strip_html_tags(raw, false),
          std::string(raw),
          cur_rspan,
          cur_cspan,
      });
      in_own_cell = false;
    };

    size_t cur = gt + 1;
    while (cur < t_close) {
      size_t lt = lower.find('<', cur);
      if (lt == std::string::npos || lt >= t_close) break;
      size_t tag_end = lower.find('>', lt + 1);
      if (tag_end == std::string::npos || tag_end > t_close + 16) break;

      if (is_html_tag_at(lower, lt, "<table")) {
        ++nested_tbl;
      } else if (is_html_tag_at(lower, lt, "</table")) {
        if (nested_tbl > 0) --nested_tbl;
      } else if (nested_tbl == 0) {
        if (is_html_tag_at(lower, lt, "<tr")) {
          in_own_tr = true;
          cur_cells.clear();
          in_own_cell = false;
        } else if (is_html_tag_at(lower, lt, "</tr")) {
          if (in_own_cell) flush_cell(lt);
          if (in_own_tr && !cur_cells.empty()) {
            rows.push_back(std::move(cur_cells));
          }
          cur_cells.clear();
          in_own_tr = false;
        } else if (in_own_tr && (is_html_tag_at(lower, lt, "<td") || is_html_tag_at(lower, lt, "<th"))) {
          if (in_own_cell) flush_cell(lt);
          std::string_view open_tag = std::string_view(lower).substr(lt, tag_end - lt + 1);
          cur_rspan = parse_tag_span_attr(open_tag, "rowspan");
          cur_cspan = parse_tag_span_attr(open_tag, "colspan");
          in_own_cell = true;
          cell_content_start = tag_end + 1;
        } else if (in_own_cell && (is_html_tag_at(lower, lt, "</td") || is_html_tag_at(lower, lt, "</th"))) {
          flush_cell(lt);
        }
      }
      cur = tag_end + 1;
    }

    if (!rows.empty()) {
      tables.push_back(std::move(rows));
    }
    pos = gt + 1;
  }
  return tables;
}

// 检查标签属性串中是否包含某 class 词元（如精确匹配 "kbcontent" 而排除 "kbcontent1"）
bool attr_has_class_token(std::string_view open_tag_lower, std::string_view target_cls) {
  size_t pos = open_tag_lower.find("class");
  while (pos != std::string_view::npos) {
    size_t i = pos + 5;
    while (i < open_tag_lower.size() && std::isspace(static_cast<unsigned char>(open_tag_lower[i]))) ++i;
    if (i < open_tag_lower.size() && open_tag_lower[i] == '=') {
      ++i;
      while (i < open_tag_lower.size() && std::isspace(static_cast<unsigned char>(open_tag_lower[i]))) ++i;
      std::string_view val;
      if (i < open_tag_lower.size() && (open_tag_lower[i] == '"' || open_tag_lower[i] == '\'')) {
        char q = open_tag_lower[i++];
        size_t end_q = open_tag_lower.find(q, i);
        if (end_q != std::string_view::npos) val = open_tag_lower.substr(i, end_q - i);
      } else {
        size_t end_sp = i;
        while (end_sp < open_tag_lower.size() &&
               !std::isspace(static_cast<unsigned char>(open_tag_lower[end_sp])) &&
               open_tag_lower[end_sp] != '>' && open_tag_lower[end_sp] != '/') {
          ++end_sp;
        }
        val = open_tag_lower.substr(i, end_sp - i);
      }
      size_t p = 0;
      while (p < val.size()) {
        while (p < val.size() && std::isspace(static_cast<unsigned char>(val[p]))) ++p;
        size_t e = p;
        while (e < val.size() && !std::isspace(static_cast<unsigned char>(val[e]))) ++e;
        if (e > p && val.substr(p, e - p) == target_cls) return true;
        p = e;
      }
    }
    pos = open_tag_lower.find("class", pos + 5);
  }
  return false;
}

bool attr_has_id(std::string_view open_tag_lower, std::string_view target_id) {
  size_t pos = 0;
  while ((pos = open_tag_lower.find("id", pos)) != std::string_view::npos) {
    if (pos > 0 && std::isalnum(static_cast<unsigned char>(open_tag_lower[pos - 1]))) {
      pos += 2;
      continue;
    }
    size_t i = pos + 2;
    while (i < open_tag_lower.size() && std::isspace(static_cast<unsigned char>(open_tag_lower[i]))) ++i;
    if (i < open_tag_lower.size() && open_tag_lower[i] == '=') {
      ++i;
      while (i < open_tag_lower.size() && std::isspace(static_cast<unsigned char>(open_tag_lower[i]))) ++i;
      std::string_view val;
      if (i < open_tag_lower.size() && (open_tag_lower[i] == '"' || open_tag_lower[i] == '\'')) {
        char q = open_tag_lower[i++];
        size_t eq = open_tag_lower.find(q, i);
        if (eq != std::string_view::npos) val = open_tag_lower.substr(i, eq - i);
      } else {
        size_t es = i;
        while (es < open_tag_lower.size() &&
               !std::isspace(static_cast<unsigned char>(open_tag_lower[es])) &&
               open_tag_lower[es] != '>') {
          ++es;
        }
        val = open_tag_lower.substr(i, es - i);
      }
      if (val == target_id) return true;
    }
    pos += 2;
  }
  return false;
}

// 精确按嵌套深度提取 #kbtable 完整 <table>...</table>
std::string extract_kbtable_html(std::string_view html) {
  std::string lower = utils::to_lower(html);
  std::string fallback_with_kbcontent;

  size_t pos = 0;
  while (pos < lower.size()) {
    size_t t_open = lower.find("<table", pos);
    if (t_open == std::string::npos) break;
    if (!is_html_tag_at(lower, t_open, "<table")) {
      pos = t_open + 6;
      continue;
    }
    size_t gt = lower.find('>', t_open + 6);
    if (gt == std::string::npos) break;

    std::string_view open_tag = std::string_view(lower).substr(t_open, gt - t_open + 1);
    bool is_kbtable_id = attr_has_id(open_tag, "kbtable");

    size_t scan = gt + 1;
    int depth = 1;
    size_t t_close_end = std::string::npos;
    while (depth > 0 && scan < lower.size()) {
      size_t lt = lower.find('<', scan);
      if (lt == std::string::npos) break;
      if (is_html_tag_at(lower, lt, "<table")) {
        ++depth;
        scan = lt + 6;
      } else if (is_html_tag_at(lower, lt, "</table")) {
        --depth;
        size_t cgt = lower.find('>', lt + 7);
        if (depth == 0) {
          t_close_end = (cgt == std::string::npos) ? lower.size() : cgt + 1;
        }
        scan = (cgt == std::string::npos) ? lower.size() : cgt + 1;
      } else {
        scan = lt + 1;
      }
    }

    if (t_close_end != std::string::npos) {
      std::string_view slice = html.substr(t_open, t_close_end - t_open);
      if (is_kbtable_id) {
        return std::string(slice);
      }
      if (fallback_with_kbcontent.empty() &&
          std::string_view(lower).substr(t_open, t_close_end - t_open).find("kbcontent") != std::string_view::npos) {
        fallback_with_kbcontent = std::string(slice);
      }
    }
    pos = gt + 1;
  }
  return fallback_with_kbcontent;
}

std::optional<std::string> extract_div_by_class(std::string_view td_html, std::string_view cls_name) {
  std::string lower = utils::to_lower(td_html);
  size_t pos = 0;
  while (pos < lower.size()) {
    size_t d_open = lower.find("<div", pos);
    if (d_open == std::string::npos) break;
    if (!is_html_tag_at(lower, d_open, "<div")) {
      pos = d_open + 4;
      continue;
    }
    size_t gt = lower.find('>', d_open + 4);
    if (gt == std::string::npos) break;

    std::string_view open_tag = std::string_view(lower).substr(d_open, gt - d_open + 1);
    if (!attr_has_class_token(open_tag, cls_name)) {
      pos = gt + 1;
      continue;
    }

    size_t scan = gt + 1;
    int depth = 1;
    while (depth > 0 && scan < lower.size()) {
      size_t lt = lower.find('<', scan);
      if (lt == std::string::npos) break;
      if (is_html_tag_at(lower, lt, "<div")) {
        ++depth;
        scan = lt + 4;
      } else if (is_html_tag_at(lower, lt, "</div")) {
        --depth;
        if (depth == 0) {
          return std::string(td_html.substr(gt + 1, lt - gt - 1));
        }
        scan = lt + 5;
      } else {
        scan = lt + 1;
      }
    }
    break;
  }
  return std::nullopt;
}

void parse_week_and_period_expr(const std::string& expr,
                                std::vector<int>& out_weeks,
                                std::vector<int>& out_periods_0based) {
  out_weeks.clear();
  out_periods_0based.clear();
  if (expr.empty()) return;

  static const std::regex re_bracket(R"(\[([^\]]+)\])");
  std::smatch bm;
  std::string week_part = expr;
  if (std::regex_search(expr, bm, re_bracket)) {
    std::string p_str = bm[1].str();
    p_str = utils::replace_all(p_str, "至", "-");
    p_str = utils::replace_all(p_str, "～", "-");
    p_str = utils::replace_all(p_str, "~", "-");
    p_str = utils::replace_all(p_str, "，", ",");
    p_str = utils::replace_all(p_str, "、", ",");
    std::string p_clean;
    for (char c : p_str) {
      if (std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == ',') {
        p_clean.push_back(c);
      }
    }
    p_str = std::move(p_clean);
    week_part = expr.substr(0, static_cast<size_t>(bm.position()));

    for (const auto& comma_seg : utils::split(p_str, ',')) {
      auto dash_parts = utils::split(utils::trim(comma_seg), '-');
      if (dash_parts.size() == 2) {
        try {
          int p1 = std::max(1, std::stoi(dash_parts[0]));
          int p2 = std::min(14, std::stoi(dash_parts[1]));
          for (int p = p1; p <= p2; ++p) {
            out_periods_0based.push_back(p - 1);
          }
        } catch (...) {
        }
      } else {
        for (const auto& tok : dash_parts) {
          try {
            int p = std::stoi(tok);
            if (p >= 1 && p <= 14) out_periods_0based.push_back(p - 1);
          } catch (...) {
          }
        }
      }
    }
  }

  int global_parity = 0;
  if (week_part.find("单周") != std::string::npos || week_part.find("(单)") != std::string::npos ||
      week_part.find("（单") != std::string::npos) {
    global_parity = 1;
  } else if (week_part.find("双周") != std::string::npos || week_part.find("(双)") != std::string::npos ||
             week_part.find("（双") != std::string::npos) {
    global_parity = 2;
  }

  std::string norm_wp = utils::replace_all(week_part, "，", ",");
  norm_wp = utils::replace_all(norm_wp, "、", ",");

  std::set<int> wk_set;
  for (const auto& raw_seg : utils::split(norm_wp, ',')) {
    std::string seg = utils::trim(raw_seg);
    if (seg.empty()) continue;

    int seg_parity = 0;
    if (seg.find("单") != std::string::npos) seg_parity = 1;
    else if (seg.find("双") != std::string::npos) seg_parity = 2;
    else if (seg.find("周") == std::string::npos && seg.find('(') == std::string::npos) {
      seg_parity = global_parity;
    }

    std::string num_range;
    for (size_t i = 0; i < seg.size(); ++i) {
      char c = seg[i];
      if (c == '(' || (i + 2 < seg.size() && seg.substr(i, 3) == "（")) break;
      if (std::isdigit(static_cast<unsigned char>(c)) || c == '-') {
        num_range.push_back(c);
      }
    }
    if (num_range.empty()) continue;

    size_t dash = num_range.find('-');
    try {
      if (dash != std::string::npos) {
        int w1 = std::max(1, std::stoi(num_range.substr(0, dash)));
        int w2 = std::min(30, std::stoi(num_range.substr(dash + 1)));
        for (int w = w1; w <= w2; ++w) {
          if (seg_parity == 1 && (w % 2 == 0)) continue;
          if (seg_parity == 2 && (w % 2 != 0)) continue;
          wk_set.insert(w);
        }
      } else {
        int w = std::stoi(num_range);
        if (w >= 1 && w <= 30) wk_set.insert(w);
      }
    } catch (...) {
    }
  }
  out_weeks.assign(wk_set.begin(), wk_set.end());
}

std::string extract_font_by_title(const std::string& block_html, std::string_view title_kw) {
  std::regex re("<font[^>]*title\\s*=\\s*['\"][^'\"]*" + std::string(title_kw) +
                    "[^'\"]*['\"][^>]*>([\\s\\S]*?)</font>",
                std::regex::icase);
  std::smatch m;
  if (std::regex_search(block_html, m, re)) {
    return utils::strip_html_tags(m[1].str(), false);
  }
  return "";
}

double score_to_gpa(const std::string& score_str, const std::string& raw_gpa) {
  std::string g = utils::trim(raw_gpa);
  if (!g.empty()) {
    try {
      return std::stod(g);
    } catch (...) {
    }
  }
  std::string s = utils::trim(score_str);
  if (s == "优" || s == "优秀") return 4.0;
  if (s == "良" || s == "良好") return 3.5;
  if (s == "中" || s == "中等") return 2.5;
  if (s == "及格" || s == "合格" || s == "通过") return 1.5;
  if (s == "不及格" || s == "不合格" || s == "未通过") return 0.0;
  try {
    double v = std::stod(s);
    if (v < 60.0) return 0.0;
    if (v >= 90.0) return 4.0;
    if (v >= 85.0) return 3.7;
    if (v >= 82.0) return 3.3;
    if (v >= 78.0) return 3.0;
    if (v >= 75.0) return 2.7;
    if (v >= 72.0) return 2.3;
    if (v >= 68.0) return 2.0;
    if (v >= 64.0) return 1.5;
    return 1.0;
  } catch (...) {
    return 0.0;
  }
}

std::string fmt_2dec(double v) {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(2) << v;
  return oss.str();
}

int parse_total_pages_hint(std::string_view html) {
  std::string plain = utils::strip_html_tags(html, false);
  static const std::regex re_pages(R"(共\s*(\d{1,3})\s*页)");
  std::smatch m;
  if (std::regex_search(plain, m, re_pages)) {
    try {
      return std::clamp(std::stoi(m[1].str()), 1, 20);
    } catch (...) {
    }
  }
  std::string src(html);
  static const std::regex re_page_cnt(R"(id\s*=\s*["']pageCount["'][^>]*value\s*=\s*["'](\d+)["'])",
                                      std::regex::icase);
  if (std::regex_search(src, m, re_page_cnt)) {
    try {
      return std::clamp(std::stoi(m[1].str()), 1, 20);
    } catch (...) {
    }
  }
  return 1;
}

}  // namespace

// ---------------------------------------------------------------------------
// 强智 HTML 纯函数解析器实现
// ---------------------------------------------------------------------------
nlohmann::json parse_qiangzhi_semesters_html(std::string_view html) {
  std::string src(html);
  nlohmann::json out = nlohmann::json::array();
  std::set<std::string> seen;

  static const std::regex re_sel(
      R"(<select[^>]*\b(?:id|name)\s*=\s*["'](?:xnxq01id|xnxqid|kksj)["'][^>]*>([\s\S]*?)</select>)",
      std::regex::icase);
  static const std::regex re_opt(
      R"(<option[^>]*value\s*=\s*["']([^"']+)["'][^>]*>([\s\S]*?)</option>)",
      std::regex::icase);

  std::smatch sm;
  std::string pool = std::regex_search(src, sm, re_sel) ? sm[1].str() : src;

  for (std::sregex_iterator it(pool.begin(), pool.end(), re_opt), end; it != end; ++it) {
    std::string val = utils::trim((*it)[1].str());
    std::string label = utils::strip_html_tags((*it)[2].str(), false);
    if (val.empty() || label.empty() || val.find("请选择") != std::string::npos) continue;
    static const std::regex re_sem_id(R"(^20\d{2}-20\d{2}-[123]$)");
    if (!std::regex_match(val, re_sem_id)) continue;
    if (seen.insert(val).second) {
      out.push_back({{"id", val}, {"name", label}});
    }
  }
  return out;
}

nlohmann::json parse_qiangzhi_timetable_html(std::string_view html,
                                             std::string_view fallback_semester) {
  if (html.find("kbtable") == std::string_view::npos &&
      html.find("kbcontent") == std::string_view::npos) {
    throw JwError("课表页面结构未识别或本次未返回课表", "readfail", "no_kbtable");
  }

  std::string src(html);
  std::string cur_sem(fallback_semester);
  static const std::regex re_selected(
      R"(<option[^>]*value\s*=\s*["'](20\d{2}-20\d{2}-[123])["'][^>]*selected[^>]*>)",
      std::regex::icase);
  static const std::regex re_selected2(
      R"(<option[^>]*selected[^>]*value\s*=\s*["'](20\d{2}-20\d{2}-[123])["'][^>]*>)",
      std::regex::icase);
  std::smatch sm;
  if (std::regex_search(src, sm, re_selected) || std::regex_search(src, sm, re_selected2)) {
    cur_sem = sm[1].str();
  }

  std::string tbl_html = extract_kbtable_html(html);
  if (tbl_html.empty()) {
    tbl_html = src;
  }
  std::string tbl_low = utils::to_lower(tbl_html);

  struct CourseKey {
    std::string name;
    std::string code;
    std::string teacher;
    std::string room;
    std::vector<int> weeks;
    bool operator<(const CourseKey& o) const {
      return std::tie(name, code, teacher, room, weeks) <
             std::tie(o.name, o.code, o.teacher, o.room, o.weeks);
    }
  };
  std::map<CourseKey, std::vector<std::pair<int, int>>> merged;

  int nested_tbl = 0;
  size_t pos = tbl_low.find('>');
  pos = (pos == std::string::npos) ? 0 : pos + 1;
  int row_slot_idx = 0;

  while (pos < tbl_low.size()) {
    size_t lt = tbl_low.find('<', pos);
    if (lt == std::string::npos) break;
    size_t gt = tbl_low.find('>', lt + 1);
    if (gt == std::string::npos) break;

    if (is_html_tag_at(tbl_low, lt, "<table")) {
      ++nested_tbl;
      pos = gt + 1;
      continue;
    }
    if (is_html_tag_at(tbl_low, lt, "</table")) {
      if (nested_tbl == 0) break;
      --nested_tbl;
      pos = gt + 1;
      continue;
    }

    if (nested_tbl == 0 && is_html_tag_at(tbl_low, lt, "<tr")) {
      size_t rscan = gt + 1;
      int inner_tbl = 0;
      size_t tr_end = std::string::npos;
      std::vector<std::pair<std::string, std::string>> tds;
      bool in_td = false;
      std::string cur_td_open;
      size_t td_start = 0;

      while (rscan < tbl_low.size()) {
        size_t tlt = tbl_low.find('<', rscan);
        if (tlt == std::string::npos) break;
        size_t tgt = tbl_low.find('>', tlt + 1);
        if (tgt == std::string::npos) break;

        if (is_html_tag_at(tbl_low, tlt, "<table")) {
          ++inner_tbl;
        } else if (is_html_tag_at(tbl_low, tlt, "</table")) {
          if (inner_tbl > 0) --inner_tbl;
        } else if (inner_tbl == 0) {
          if (is_html_tag_at(tbl_low, tlt, "</tr")) {
            if (in_td) {
              tds.emplace_back(cur_td_open, tbl_html.substr(td_start, tlt - td_start));
              in_td = false;
            }
            tr_end = tgt + 1;
            break;
          } else if (is_html_tag_at(tbl_low, tlt, "<td")) {
            if (in_td) {
              tds.emplace_back(cur_td_open, tbl_html.substr(td_start, tlt - td_start));
            }
            in_td = true;
            cur_td_open = tbl_low.substr(tlt, tgt - tlt + 1);
            td_start = tgt + 1;
          } else if (in_td && is_html_tag_at(tbl_low, tlt, "</td")) {
            tds.emplace_back(cur_td_open, tbl_html.substr(td_start, tlt - td_start));
            in_td = false;
          }
        }
        rscan = tgt + 1;
      }

      std::vector<std::string> day_divs;
      for (const auto& [open_low, td_inner] : tds) {
        if (open_low.find("colspan") != std::string::npos) continue;
        auto kb = extract_div_by_class(td_inner, "kbcontent");
        if (!kb.has_value()) kb = extract_div_by_class(td_inner, "kbcontent1");
        if (!kb.has_value()) kb = extract_div_by_class(td_inner, "kbcontent2");
        if (kb.has_value()) {
          day_divs.push_back(std::move(*kb));
        }
      }

      if (!day_divs.empty()) {
        std::vector<int> fallback_periods;
        if (row_slot_idx == 0) fallback_periods = {0, 1};
        else if (row_slot_idx == 1) fallback_periods = {2, 3};
        else if (row_slot_idx == 2) fallback_periods = {4, 5};
        else if (row_slot_idx == 3) fallback_periods = {6, 7};
        else fallback_periods = {8, 9};

        for (size_t day = 0; day < day_divs.size() && day < 7; ++day) {
          static const std::regex re_hr(R"(<hr\b[^>]*>)", std::regex::icase);
          std::string norm_cell = std::regex_replace(day_divs[day], re_hr, "-----");

          static const std::regex re_sep(R"(-{4,})");
          std::sregex_token_iterator bit(norm_cell.begin(), norm_cell.end(), re_sep, -1);
          std::sregex_token_iterator bend;
          for (; bit != bend; ++bit) {
            std::string seg_html = bit->str();
            static const std::regex re_font_nl(R"(<font\b)", std::regex::icase);
            std::string seg_for_lines = std::regex_replace(seg_html, re_font_nl, "\n<font");

            std::string plain_lines = utils::strip_html_tags(seg_for_lines, true);
            if (plain_lines.empty() || plain_lines == "&nbsp;") continue;

            auto lines = utils::split(plain_lines, '\n');
            std::string cname;
            for (const auto& ln : lines) {
              std::string t = utils::trim(ln);
              if (!t.empty() && t != "&nbsp;") {
                cname = t;
                break;
              }
            }
            if (cname.empty()) continue;

            std::string teacher = extract_font_by_title(seg_html, "老师");
            if (teacher.empty()) teacher = extract_font_by_title(seg_html, "教师");
            std::string room = extract_font_by_title(seg_html, "教室");
            if (room.empty()) room = extract_font_by_title(seg_html, "地点");
            std::string wp_expr = extract_font_by_title(seg_html, "周次");
            std::string jc_expr = extract_font_by_title(seg_html, "节次");
            if (wp_expr.empty()) {
              wp_expr = jc_expr;
            } else if (!jc_expr.empty() && wp_expr.find('[') == std::string::npos) {
              if (jc_expr.find('[') == std::string::npos) wp_expr += "[" + jc_expr + "]";
              else wp_expr += jc_expr;
            }

            if (wp_expr.empty()) {
              for (const auto& ln : lines) {
                if (ln.find("周") != std::string::npos || ln.find("节]") != std::string::npos) {
                  wp_expr = ln;
                  break;
                }
              }
            }

            std::vector<int> weeks;
            std::vector<int> periods;
            parse_week_and_period_expr(wp_expr, weeks, periods);
            if (weeks.empty()) {
              for (int w = 1; w <= 16; ++w) weeks.push_back(w);
            }
            if (periods.empty()) {
              periods = fallback_periods;
            }

            std::string code;
            static const std::regex re_code(R"(^(.*?)\s*\[([A-Za-z0-9_.-]{4,20})\]\s*$)");
            std::smatch cm;
            if (std::regex_match(cname, cm, re_code)) {
              cname = utils::trim(cm[1].str());
              code = cm[2].str();
            } else {
              code = "HNU_" + crypto::md5_hex(cname).substr(0, 8);
            }

            CourseKey key{cname, code, teacher, room, weeks};
            auto& slots_vec = merged[key];
            for (int p : periods) {
              std::pair<int, int> sp{static_cast<int>(day), p};
              if (std::find(slots_vec.begin(), slots_vec.end(), sp) == slots_vec.end()) {
                slots_vec.push_back(sp);
              }
            }
          }
        }
        ++row_slot_idx;
      }

      pos = (tr_end != std::string::npos) ? tr_end : (gt + 1);
      continue;
    }

    pos = gt + 1;
  }

  nlohmann::json courses = nlohmann::json::array();
  int max_week = 20;
  int max_period = 11;
  for (auto& [k, slots] : merged) {
    std::sort(slots.begin(), slots.end());
    nlohmann::json jslots = nlohmann::json::array();
    for (const auto& [d, p] : slots) {
      jslots.push_back({{"day", d}, {"period", p}});
      max_period = std::max(max_period, p + 1);
    }
    if (!k.weeks.empty()) {
      max_week = std::max(max_week, k.weeks.back());
    }
    courses.push_back({
        {"name", k.name},
        {"code", k.code},
        {"teacher", k.teacher},
        {"room", k.room},
        {"weeks", k.weeks},
        {"slots", jslots},
    });
  }

  std::string sem_display = cur_sem;
  static const std::regex re_sem_fmt(R"(^(20\d{2}-20\d{2})-([123])$)");
  if (std::regex_match(cur_sem, sm, re_sem_fmt)) {
    sem_display = sm[1].str() + " 学年第 " + sm[2].str() + " 学期";
  }

  return {
      {"semesterId", cur_sem},
      {"semester", sem_display},
      {"unitCount", std::max(11, max_period)},
      {"totalWeeks", max_week},
      {"weekStart", 1},
      {"courses", courses},
  };
}

// ---------------------------------------------------------------------------
// 强智成绩解析（支持 平时 / 期中 / 期末 / 实验 / 总评，HTML 表格 + LayUI JSON 双格式）
// ---------------------------------------------------------------------------
namespace {

struct GradeRow {
  std::string seq, term, code, name, score;
  std::string reg, regr, mid, midr, fin, finr, lab, labr;
  std::string flag, credit, hours, gpa, makeup, method, exam_nat;
  std::string prop, nature, gen_cat, pscj_url;
};

std::string clean_score_cell(std::string v) {
  v = utils::trim(v);
  if (v == "&nbsp;" || v == "-" || v == "—" || v == "--" || v == "null" || v == "NULL") return "";
  return v;
}

std::string jstr_any(const nlohmann::json& o, std::initializer_list<const char*> keys) {
  if (!o.is_object()) return "";
  for (const char* k : keys) {
    if (!o.contains(k)) continue;
    const auto& v = o[k];
    if (v.is_string()) {
      std::string s = clean_score_cell(v.get<std::string>());
      if (!s.empty()) return s;
    } else if (v.is_number_integer()) {
      return std::to_string(v.get<long long>());
    } else if (v.is_number_float()) {
      std::ostringstream oss;
      oss << v.get<double>();
      return oss.str();
    }
  }
  return "";
}

// 兼容 LayUI / 强智 JSON 返回：{data:[...]} / {rows:[...]} / {list:[...]} / 直接数组 / 单对象
nlohmann::json grade_json_rows(std::string_view html) {
  std::string t = utils::trim(html);
  if (t.empty() || (t[0] != '{' && t[0] != '[')) return nullptr;
  auto j = nlohmann::json::parse(t, nullptr, false);
  if (j.is_discarded()) return nullptr;
  if (j.is_array()) return j;
  if (!j.is_object()) return nullptr;
  for (const char* k : {"data", "rows", "list", "records", "result"}) {
    if (!j.contains(k)) continue;
    const auto& d = j[k];
    if (d.is_array()) return d;
    if (d.is_object()) {
      for (const char* k2 : {"list", "rows", "records", "data"}) {
        if (d.contains(k2) && d[k2].is_array()) return d[k2];
      }
      return nlohmann::json::array({d});
    }
  }
  return nlohmann::json::array({j});
}

bool grade_has_eval_block(std::string_view html) {
  static const char* kKeys[] = {"未评教", "请先评教", "完成评教后", "评教未完成", "尚未完成评教",
                                "请先完成教学评价", "评教后查看", "评教后方可"};
  for (const char* k : kKeys) {
    if (html.find(k) != std::string_view::npos) return true;
  }
  return false;
}

enum class PsCol { None, Reg, Mid, Fin, Lab, Tot };

struct PsLabel {
  PsCol kind = PsCol::None;
  bool ratio = false;
};

PsLabel classify_ps_label(const std::string& raw) {
  PsLabel out;
  std::string h = utils::collapse_whitespace(raw);
  if (h.empty() || h.size() > 60) return out;
  auto has = [&](const char* s) { return h.find(s) != std::string::npos; };
  out.ratio = has("比例") || has("权重") || has("占比");
  if (has("平时") || has("过程")) out.kind = PsCol::Reg;
  else if (has("期中") || has("阶段")) out.kind = PsCol::Mid;
  else if (has("期末") || has("卷面") || has("末考")) out.kind = PsCol::Fin;
  else if (has("实验") || has("实践") || has("上机") || has("实训")) out.kind = PsCol::Lab;
  else if (!out.ratio && (has("总成绩") || has("总评") || has("综合成绩") || h == "成绩")) out.kind = PsCol::Tot;
  return out;
}

std::string extract_pct(const std::string& raw) {
  static const std::regex re_pct(R"RX((\d+(?:\.\d+)?)\s*%)RX");
  std::smatch m;
  if (std::regex_search(raw, m, re_pct)) return m[1].str() + "%";
  return "";
}

void ps_assign(nlohmann::json& out, PsCol kind, bool ratio, const std::string& val) {
  if (val.empty()) return;
  const char* key = nullptr;
  switch (kind) {
    case PsCol::Reg: key = ratio ? "regularRatio" : "regularScore"; break;
    case PsCol::Mid: key = ratio ? "midtermRatio" : "midtermScore"; break;
    case PsCol::Fin: key = ratio ? "finalRatio" : "finalScore"; break;
    case PsCol::Lab: key = ratio ? "labRatio" : "labScore"; break;
    case PsCol::Tot: key = ratio ? nullptr : "totalScore"; break;
    default: break;
  }
  if (!key) return;
  if (!out.contains(key) || !out[key].is_string() || out[key].get<std::string>().empty()) {
    out[key] = val;
  }
}

struct GradeCols {
  int seq = -1, term = -1, code = -1, name = -1, score = -1;
  int reg = -1, regr = -1, mid = -1, midr = -1, fin = -1, finr = -1, lab = -1, labr = -1;
  int flag = -1, credit = -1, hours = -1, gpa = -1, makeup = -1, method = -1, exam_nat = -1;
  int prop = -1, nature = -1, gen_cat = -1;
};

bool classify_grade_header(const std::vector<RichCell>& head, GradeCols& c) {
  auto set_first = [](int& slot, size_t i) {
    if (slot < 0) slot = static_cast<int>(i);
  };
  for (size_t i = 0; i < head.size(); ++i) {
    std::string h = utils::collapse_whitespace(head[i].text);
    if (h.empty()) continue;
    auto has = [&](const char* s) { return h.find(s) != std::string::npos; };
    bool ratio = has("比例") || has("权重") || has("占比");

    if (h == "序号") { set_first(c.seq, i); continue; }
    if (has("补重学期")) { set_first(c.makeup, i); continue; }
    if (has("学期") || has("开课时间")) { set_first(c.term, i); continue; }
    if (has("课程编号") || has("课程代码")) { set_first(c.code, i); continue; }
    if (has("课程名称") || h == "课程名" || h == "课程") { set_first(c.name, i); continue; }
    if (!ratio && (has("总成绩") || has("总评") || has("综合成绩") || h == "总分")) { set_first(c.score, i); continue; }
    if (has("平时") || has("过程")) { set_first(ratio ? c.regr : c.reg, i); continue; }
    if (has("期中") || has("阶段")) { set_first(ratio ? c.midr : c.mid, i); continue; }
    if (has("期末") || has("卷面") || has("末考")) { set_first(ratio ? c.finr : c.fin, i); continue; }
    if (has("实验成绩") || has("实践成绩") || has("上机成绩") || has("实训成绩")) {
      set_first(ratio ? c.labr : c.lab, i);
      continue;
    }
    if (has("成绩标识") || has("成绩标志")) { set_first(c.flag, i); continue; }
    if (h == "成绩" || has("最终") || has("课程成绩") || has("考试成绩") || has("原始成绩") ||
        has("百分制") || has("实得成绩")) {
      if (!ratio) set_first(c.score, i);
      continue;
    }
    if (h == "学分") { set_first(c.credit, i); continue; }
    if (has("学时")) { set_first(c.hours, i); continue; }
    if (has("绩点")) { set_first(c.gpa, i); continue; }
    if (has("考核方式")) { set_first(c.method, i); continue; }
    if (has("考试性质")) { set_first(c.exam_nat, i); continue; }
    if (has("课程属性")) { set_first(c.prop, i); continue; }
    if (has("课程性质")) { set_first(c.nature, i); continue; }
    if (has("通选") || has("课程类别") || has("通识")) { set_first(c.gen_cat, i); continue; }
  }
  return c.name >= 0 && (c.score >= 0 || c.mid >= 0 || c.fin >= 0 || c.reg >= 0 || c.lab >= 0);
}

void parse_grade_tables(std::string_view html, std::vector<GradeRow>& out) {
  auto tables = parse_html_tables_rich(html);
  static const std::regex re_pscj_url(
      R"RX((?:/jsxsd/kscj/)?(?:pscj_list(?:\.do)?|[A-Za-z_]*cjmx[A-Za-z_]*(?:\.do)?|cjcxxq(?:\.do)?|cjcx_list_mx(?:\.do)?)\?[^'"\s)<>]+)RX",
      std::regex::icase);

  for (const auto& rows : tables) {
    if (rows.empty()) continue;
    GradeCols c;
    size_t header_row = 0;
    bool found = false;
    size_t scan_max = std::min<size_t>(3, rows.size());
    for (size_t hi = 0; hi < scan_max; ++hi) {
      GradeCols tmp;
      if (classify_grade_header(rows[hi], tmp)) {
        c = tmp;
        header_row = hi;
        found = true;
        break;
      }
    }
    if (!found) continue;

    auto cell_at = [](const std::vector<RichCell>& r, int idx) -> std::string {
      if (idx < 0 || static_cast<size_t>(idx) >= r.size()) return "";
      return clean_score_cell(utils::trim(r[static_cast<size_t>(idx)].text));
    };

    for (size_t ri = header_row + 1; ri < rows.size(); ++ri) {
      const auto& r = rows[ri];
      if (r.size() < 2) continue;
      GradeRow g;
      g.name = cell_at(r, c.name);
      if (g.name.empty()) continue;
      if (g.name.find("未查询到") != std::string::npos || g.name.find("暂无") != std::string::npos ||
          g.name.find("没有数据") != std::string::npos || g.name == "合计" || g.name == "总计") {
        continue;
      }
      g.seq = cell_at(r, c.seq);
      g.term = cell_at(r, c.term);
      g.code = cell_at(r, c.code);
      g.score = cell_at(r, c.score);
      g.reg = cell_at(r, c.reg);
      g.regr = cell_at(r, c.regr);
      g.mid = cell_at(r, c.mid);
      g.midr = cell_at(r, c.midr);
      g.fin = cell_at(r, c.fin);
      g.finr = cell_at(r, c.finr);
      g.lab = cell_at(r, c.lab);
      g.labr = cell_at(r, c.labr);
      g.flag = cell_at(r, c.flag);
      g.credit = cell_at(r, c.credit);
      g.hours = cell_at(r, c.hours);
      g.gpa = cell_at(r, c.gpa);
      g.makeup = cell_at(r, c.makeup);
      g.method = cell_at(r, c.method);
      g.exam_nat = cell_at(r, c.exam_nat);
      g.prop = cell_at(r, c.prop);
      g.nature = cell_at(r, c.nature);
      g.gen_cat = cell_at(r, c.gen_cat);

      // 提取成绩构成明细弹窗链接：优先总成绩单元格，再扫描整行所有单元格（操作列 / 详情链接）
      std::vector<size_t> scan_cells;
      if (c.score >= 0 && static_cast<size_t>(c.score) < r.size()) scan_cells.push_back(static_cast<size_t>(c.score));
      for (size_t ci = 0; ci < r.size(); ++ci) {
        if (c.score < 0 || ci != static_cast<size_t>(c.score)) scan_cells.push_back(ci);
      }
      for (size_t ci : scan_cells) {
        std::string raw_h = utils::html_unescape(r[ci].raw_html);
        std::smatch pm;
        if (std::regex_search(raw_h, pm, re_pscj_url)) {
          std::string u = utils::trim(pm[0].str());
          if (!u.empty() && u.front() != '/') u = "/jsxsd/kscj/" + u;
          g.pscj_url = u;
          break;
        }
      }
      out.push_back(std::move(g));
    }
  }
}

GradeRow grade_row_from_json(const nlohmann::json& o) {
  GradeRow g;
  g.name = jstr_any(o, {"kcmc", "courseName", "kcmc1", "name"});
  g.code = jstr_any(o, {"kcbh", "kch", "kcdm", "courseCode", "code"});
  g.term = jstr_any(o, {"xnxq01id", "kksj", "xqmc", "xnxq", "term"});
  g.score = jstr_any(o, {"zcj", "cj", "zpcj", "totalScore", "score"});
  g.reg = jstr_any(o, {"pscj", "regularScore"});
  g.regr = jstr_any(o, {"pscjb", "pscjbl", "regularRatio"});
  g.mid = jstr_any(o, {"qzcj", "midtermScore"});
  g.midr = jstr_any(o, {"qzcjb", "qzcjbl", "midtermRatio"});
  g.fin = jstr_any(o, {"qmcj", "finalScore"});
  g.finr = jstr_any(o, {"qmcjb", "qmcjbl", "finalRatio"});
  g.lab = jstr_any(o, {"sycj", "labScore"});
  g.labr = jstr_any(o, {"sycjb", "labRatio"});
  g.credit = jstr_any(o, {"xf", "credit"});
  g.hours = jstr_any(o, {"zxs", "xs", "hours"});
  g.gpa = jstr_any(o, {"jd", "gpa"});
  g.nature = jstr_any(o, {"kcxzmc", "kcxz"});
  g.prop = jstr_any(o, {"kcsxmc", "kcsx"});
  g.method = jstr_any(o, {"khfs", "khfsmc"});
  g.exam_nat = jstr_any(o, {"ksxz", "ksxzmc"});
  g.flag = jstr_any(o, {"cjbsmc", "cjbs"});
  g.seq = jstr_any(o, {"xh", "seq"});
  g.makeup = jstr_any(o, {"bcxq"});
  return g;
}

void grade_push_item(nlohmann::json& courses,
                     GradeRow g,
                     std::string_view source_hint,
                     std::string_view fallback_term) {
  if (g.term.empty() && !fallback_term.empty()) g.term = std::string(fallback_term);
  // 期中 / 平时专用通道返回的「成绩」列只代表该阶段分数，绝不能当作期末总评污染 GPA
  if (source_hint == "midterm" && g.mid.empty() && !g.score.empty()) {
    g.mid = g.score;
    g.score.clear();
    g.gpa.clear();
  } else if (source_hint == "regular" && g.reg.empty() && !g.score.empty()) {
    g.reg = g.score;
    g.score.clear();
    g.gpa.clear();
  }

  bool has_final_total = !g.score.empty();
  std::string gpa_s = g.gpa;
  if (has_final_total && gpa_s.empty()) {
    gpa_s = fmt_2dec(score_to_gpa(g.score, ""));
  }

  nlohmann::json item = {
      {"term", g.term},
      {"code", g.code},
      {"seq", g.seq},
      {"name", g.name},
      {"category", g.nature.empty() ? (g.gen_cat.empty() ? g.prop : g.gen_cat) : g.nature},
      {"nature", g.prop.empty() ? (g.nature.find("选") != std::string::npos ? "选修" : "必修") : g.prop},
      {"generalCategory", g.gen_cat},
      {"credit", g.credit},
      {"hours", g.hours},
      {"score", g.score},
      {"regularScore", g.reg},
      {"regularRatio", g.regr},
      {"midtermScore", g.mid},
      {"midtermRatio", g.midr},
      {"finalScore", g.fin},
      {"finalRatio", g.finr},
      {"labScore", g.lab},
      {"labRatio", g.labr},
      {"final", g.score},
      {"gpa", gpa_s},
      {"scoreFlag", g.flag},
      {"makeupTerm", g.makeup},
      {"examMethod", g.method},
      {"examNature", g.exam_nat},
  };
  if (!g.pscj_url.empty()) item["_pscjUrl"] = g.pscj_url;
  courses.push_back(std::move(item));
}

// 仅用「已出总评」的课程汇总 GPA；只有期中/平时分的课程绝不计入绩点
nlohmann::json finalize_grades_json(nlohmann::json courses, bool eval_blocked) {
  struct TermAgg {
    int count = 0;
    double credits = 0.0;
    double weighted_gpa = 0.0;
  };
  std::map<std::string, TermAgg> term_map;
  int total_count = 0;
  double total_credits = 0.0;
  double total_weighted_gpa = 0.0;
  int mid_n = 0, fin_n = 0, reg_n = 0;

  for (const auto& c : courses) {
    std::string score = utils::json_str(c, "score", "");
    if (!utils::json_str(c, "midtermScore", "").empty()) ++mid_n;
    if (!utils::json_str(c, "regularScore", "").empty()) ++reg_n;
    if (!utils::json_str(c, "finalScore", "").empty() || !score.empty()) ++fin_n;
    if (score.empty()) continue;
    std::string flag = utils::json_str(c, "scoreFlag", "");
    if (flag.find("缓考") != std::string::npos || flag.find("免修") != std::string::npos) continue;
    double cr = 0.0;
    try { cr = std::stod(utils::json_str(c, "credit", "")); } catch (...) { cr = 0.0; }
    double gp = score_to_gpa(score, utils::json_str(c, "gpa", ""));
    std::string term = utils::json_str(c, "term", "");
    if (!term.empty()) {
      auto& ta = term_map[term];
      ta.count += 1;
      ta.credits += cr;
      ta.weighted_gpa += cr * gp;
    }
    total_count += 1;
    total_credits += cr;
    total_weighted_gpa += cr * gp;
  }

  nlohmann::json summary = nlohmann::json::array();
  for (const auto& [sem, ta] : term_map) {
    std::string year = sem.size() >= 9 ? sem.substr(0, 9) : sem;
    std::string tno = sem.size() >= 11 ? sem.substr(10) : "1";
    double avg = ta.credits > 0 ? (ta.weighted_gpa / ta.credits) : 0.0;
    summary.push_back({
        {"year", year},
        {"term", tno},
        {"semester", sem},
        {"count", std::to_string(ta.count)},
        {"credits", fmt_2dec(ta.credits)},
        {"gpa", fmt_2dec(avg)},
    });
  }

  nlohmann::json overall = nullptr;
  if (total_count > 0) {
    double avg = total_credits > 0 ? (total_weighted_gpa / total_credits) : 0.0;
    overall = {
        {"count", std::to_string(total_count)},
        {"credits", fmt_2dec(total_credits)},
        {"gpa", fmt_2dec(avg)},
    };
  }
  return {
      {"summary", summary},
      {"overall", overall},
      {"courses", courses},
      {"midtermCount", mid_n},
      {"finalCount", fin_n},
      {"regularCount", reg_n},
      {"evalBlocked", eval_blocked},
  };
}

// 多通道成绩合并：按 (学期, 课程编号) 或 (学期, 归一化课程名) 去重并补全各阶段分数
void merge_grade_courses(nlohmann::json& dst, const nlohmann::json& src) {
  if (!src.is_array()) return;
  auto norm_name = [](const std::string& n) {
    std::string s = utils::to_lower(utils::trim(n));
    s = utils::replace_all(s, "（", "(");
    s = utils::replace_all(s, "）", ")");
    s = utils::replace_all(s, " ", "");
    return s;
  };
  std::map<std::string, size_t> by_code, by_name;
  for (size_t i = 0; i < dst.size(); ++i) {
    std::string term = utils::json_str(dst[i], "term", "");
    std::string code = utils::json_str(dst[i], "code", "");
    if (!code.empty()) by_code[term + "|" + code] = i;
    by_name[term + "|" + norm_name(utils::json_str(dst[i], "name", ""))] = i;
  }
  static const char* kFill[] = {"score", "final", "gpa", "regularScore", "regularRatio", "midtermScore",
                                "midtermRatio", "finalScore", "finalRatio", "labScore", "labRatio",
                                "credit", "hours", "category", "nature", "generalCategory", "examMethod",
                                "examNature", "scoreFlag", "makeupTerm", "code", "seq", "_pscjUrl"};
  for (const auto& c : src) {
    std::string term = utils::json_str(c, "term", "");
    std::string code = utils::json_str(c, "code", "");
    std::string nn = norm_name(utils::json_str(c, "name", ""));
    long idx = -1;
    if (!code.empty()) {
      auto it = by_code.find(term + "|" + code);
      if (it != by_code.end()) idx = static_cast<long>(it->second);
    }
    if (idx < 0) {
      auto it = by_name.find(term + "|" + nn);
      if (it != by_name.end()) idx = static_cast<long>(it->second);
    }
    if (idx < 0) {
      dst.push_back(c);
      size_t ni = dst.size() - 1;
      if (!code.empty()) by_code[term + "|" + code] = ni;
      by_name[term + "|" + nn] = ni;
      continue;
    }
    auto& e = dst[static_cast<size_t>(idx)];
    for (const char* k : kFill) {
      std::string cur = utils::json_str(e, k, "");
      std::string nv = utils::json_str(c, k, "");
      if (cur.empty() && !nv.empty()) e[k] = nv;
    }
  }
}

}  // namespace

// 解析强智成绩构成明细弹窗 (/jsxsd/kscj/pscj_list.do)：兼容横向表头 / 纵向键值表 / 合并标题行 / JSON
nlohmann::json parse_qiangzhi_pscj_html(std::string_view html) {
  nlohmann::json out = nlohmann::json::object();

  auto jrows = grade_json_rows(html);
  if (jrows.is_array() && !jrows.empty() && jrows[0].is_object()) {
    const auto& o = jrows[0];
    auto put = [&](const char* key, std::initializer_list<const char*> keys) {
      std::string v = jstr_any(o, keys);
      if (!v.empty()) out[key] = v;
    };
    put("regularScore", {"pscj", "regularScore", "psf"});
    put("regularRatio", {"pscjb", "pscjbl", "regularRatio"});
    put("midtermScore", {"qzcj", "midtermScore"});
    put("midtermRatio", {"qzcjb", "qzcjbl", "midtermRatio"});
    put("finalScore", {"qmcj", "finalScore"});
    put("finalRatio", {"qmcjb", "qmcjbl", "finalRatio"});
    put("labScore", {"sycj", "labScore"});
    put("labRatio", {"sycjb", "labRatio"});
    put("totalScore", {"zcj", "totalScore", "cj"});
    return out;
  }

  auto tables = parse_html_tables_rich(html);

  // 1) 横向表头：前 3 行内寻找至少含 2 个成绩分项列的表头行
  for (const auto& rows : tables) {
    size_t scan_max = std::min<size_t>(3, rows.size());
    for (size_t hi = 0; hi < scan_max; ++hi) {
      std::vector<PsLabel> labs;
      int n_cls = 0;
      for (const auto& cell : rows[hi]) {
        PsLabel l = classify_ps_label(cell.text);
        if (l.kind != PsCol::None) ++n_cls;
        labs.push_back(l);
      }
      if (n_cls < 2 || hi + 1 >= rows.size()) continue;

      // 表头文字自带占比，如「平时成绩(30%)」
      for (size_t ci = 0; ci < labs.size(); ++ci) {
        if (labs[ci].kind == PsCol::None || labs[ci].ratio) continue;
        std::string pct = extract_pct(rows[hi][ci].text);
        if (!pct.empty()) ps_assign(out, labs[ci].kind, true, pct);
      }
      for (size_t ri = hi + 1; ri < rows.size(); ++ri) {
        bool any = false;
        for (size_t ci = 0; ci < labs.size() && ci < rows[ri].size(); ++ci) {
          if (labs[ci].kind == PsCol::None) continue;
          std::string v = clean_score_cell(utils::trim(rows[ri][ci].text));
          if (v.empty()) continue;
          ps_assign(out, labs[ci].kind, labs[ci].ratio, v);
          any = true;
        }
        if (any) break;
      }
      break;
    }
    if (!out.empty()) return out;
  }

  // 2) 纵向键值表：<tr><td>期中成绩</td><td>88</td></tr>
  for (const auto& rows : tables) {
    for (const auto& r : rows) {
      for (size_t i = 0; i + 1 < r.size(); ++i) {
        PsLabel l = classify_ps_label(r[i].text);
        if (l.kind == PsCol::None) continue;
        std::string v = clean_score_cell(utils::trim(r[i + 1].text));
        if (v.empty() || classify_ps_label(v).kind != PsCol::None) continue;
        ps_assign(out, l.kind, l.ratio, v);
        if (!l.ratio) {
          std::string pct = extract_pct(r[i].text);
          if (!pct.empty()) ps_assign(out, l.kind, true, pct);
        }
      }
    }
  }
  return out;
}

nlohmann::json parse_qiangzhi_grades_html(std::string_view html,
                                          std::string_view source_hint,
                                          std::string_view fallback_term) {
  std::vector<GradeRow> rows;
  auto jrows = grade_json_rows(html);
  if (jrows.is_array()) {
    for (const auto& o : jrows) {
      if (!o.is_object()) continue;
      GradeRow g = grade_row_from_json(o);
      if (g.name.empty()) continue;
      rows.push_back(std::move(g));
    }
  } else {
    parse_grade_tables(html, rows);
  }

  nlohmann::json courses = nlohmann::json::array();
  for (auto& g : rows) {
    grade_push_item(courses, std::move(g), source_hint, fallback_term);
  }
  return finalize_grades_json(std::move(courses), grade_has_eval_block(html));
}

nlohmann::json parse_qiangzhi_exams_html(std::string_view html) {
  auto tables = utils::parse_html_tables(html);
  nlohmann::json exams = nlohmann::json::array();

  for (const auto& rows : tables) {
    if (rows.empty()) continue;
    const auto& head = rows[0];
    int idx_code = -1, idx_name = -1, idx_time = -1, idx_campus = -1, idx_room = -1, idx_seat = -1;
    for (size_t i = 0; i < head.size(); ++i) {
      const auto& h = head[i];
      if (h.find("课程编号") != std::string::npos || h.find("课程代码") != std::string::npos) idx_code = static_cast<int>(i);
      else if (h.find("课程名称") != std::string::npos) idx_name = static_cast<int>(i);
      else if (h.find("考试时间") != std::string::npos || h.find("时间") != std::string::npos) idx_time = static_cast<int>(i);
      else if (h.find("校区") != std::string::npos) idx_campus = static_cast<int>(i);
      else if (h.find("考场") != std::string::npos || h.find("教室") != std::string::npos || h.find("地点") != std::string::npos) {
        if (idx_room < 0) idx_room = static_cast<int>(i);
      } else if (h.find("座位") != std::string::npos) idx_seat = static_cast<int>(i);
    }
    if (idx_name < 0 || (idx_time < 0 && idx_room < 0)) continue;

    auto cell_at = [](const std::vector<std::string>& r, int idx) -> std::string {
      return (idx >= 0 && static_cast<size_t>(idx) < r.size()) ? utils::trim(r[static_cast<size_t>(idx)]) : "";
    };

    for (size_t ri = 1; ri < rows.size(); ++ri) {
      const auto& r = rows[ri];
      std::string name = cell_at(r, idx_name);
      if (name.empty()) continue;
      std::string raw_time = cell_at(r, idx_time);
      std::string date_part = raw_time;
      std::string arrange_part;
      size_t sp = raw_time.find(' ');
      if (sp != std::string::npos) {
        date_part = utils::trim(raw_time.substr(0, sp));
        arrange_part = utils::trim(raw_time.substr(sp + 1));
      }
      exams.push_back({
          {"code", cell_at(r, idx_code)},
          {"name", name},
          {"category", "期末考试"},
          {"form", ""},
          {"weekText", ""},
          {"date", date_part},
          {"arrange", arrange_part},
          {"campus", cell_at(r, idx_campus)},
          {"room", cell_at(r, idx_room)},
          {"seat", cell_at(r, idx_seat)},
          {"status", "正常"},
      });
    }
  }
  return {{"exams", exams}, {"published", !exams.empty()}};
}

nlohmann::json parse_qiangzhi_profile_html(std::string_view html) {
  auto tables = utils::parse_html_tables(html);
  nlohmann::json out = nlohmann::json::object();
  static const std::map<std::string, std::string> kKeyMap = {
      {"学号", "studentId"}, {"姓名", "name"}, {"性别", "gender"},
      {"院系", "college"}, {"学院", "college"}, {"院系名称", "college"}, {"所属院系", "college"},
      {"书院", "academy"}, {"所属书院", "academy"}, {"所在书院", "academy"}, {"书院名称", "academy"},
      {"隶属书院", "academy"}, {"住宿书院", "academy"}, {"社区书院", "academy"},
      {"学生社区", "academy"}, {"所属社区", "academy"}, {"书院/社区", "academy"},
      {"专业", "major"}, {"专业名称", "major"}, {"录取专业", "major"},
      {"专业方向", "direction"}, {"方向", "direction"},
      {"班级", "className"}, {"班级名称", "className"}, {"行政班级", "className"},
      {"校区", "campus"}, {"所在校区", "campus"},
      {"年级", "grade"}, {"所在年级", "grade"},
      {"入学日期", "enrolledAt"}, {"入校时间", "enrolledAt"},
      {"预计毕业日期", "graduateAt"}, {"毕业日期", "graduateAt"},
      {"学制", "duration"}, {"培养层次", "level"}, {"学历层次", "level"},
  };

  auto normalize_label = [](std::string k) -> std::string {
    k = utils::collapse_whitespace(k);
    while (!k.empty() && (k.back() == ':' || utils::ends_with(k, "："))) {
      if (k.back() == ':') k.pop_back();
      else k.erase(k.size() - 3);
    }
    return utils::trim(k);
  };

  for (const auto& rows : tables) {
    for (const auto& r : rows) {
      for (const auto& cell : r) {
        std::string c = utils::trim(cell);
        size_t colon_pos = c.find("：");
        size_t sep_len = 3;
        if (colon_pos == std::string::npos) {
          colon_pos = c.find(':');
          sep_len = 1;
        }
        if (colon_pos != std::string::npos && colon_pos > 0 && colon_pos < 24) {
          std::string k = normalize_label(c.substr(0, colon_pos));
          std::string v = utils::trim(c.substr(colon_pos + sep_len));
          auto it = kKeyMap.find(k);
          if (it != kKeyMap.end() && !v.empty() && !out.contains(it->second)) {
            out[it->second] = v;
          }
        }
      }

      for (size_t i = 0; i + 1 < r.size(); ++i) {
        std::string k = normalize_label(r[i]);
        std::string v = utils::trim(r[i + 1]);
        auto it = kKeyMap.find(k);
        if (it != kKeyMap.end() && !v.empty() && kKeyMap.find(normalize_label(v)) == kKeyMap.end() &&
            !out.contains(it->second)) {
          out[it->second] = v;
        }
      }
    }
  }
  if (out.contains("college") && !out.contains("dept")) {
    out["dept"] = out["college"];
  }
  // 兜底自动识别：若学籍表格未用标准「书院」标签，但在页面文本/隐藏域/班级/宿舍信息中出现了海大 17 个书院之一，则自动提取
  if (!out.contains("academy") || utils::json_str(out, "academy", "").empty()) {
    std::string detected = detect_hnu_academy_in_text(html);
    if (!detected.empty()) {
      out["academy"] = detected;
    }
  }
  if ((!out.contains("campus") || utils::json_str(out, "campus", "").empty()) &&
      out.contains("academy") && !utils::json_str(out, "academy", "").empty()) {
    std::string inferred = infer_hnu_campus_from_academy(utils::json_str(out, "academy", ""));
    if (!inferred.empty()) out["campus"] = inferred;
  }
  return out;
}

// 解析强智学业完成情况 / 培养方案明细中的毕业总学分及各选修/必修模块最低学分要求表
nlohmann::json parse_qiangzhi_grad_summary_html(std::string_view html) {
  auto tables = utils::parse_html_tables(html);
  nlohmann::json mods = nlohmann::json::array();
  double total_req = 0.0;
  double sum_mods = 0.0;

  for (const auto& rows : tables) {
    if (rows.size() < 2) continue;
    const auto& head = rows[0];
    int idx_mod = -1, idx_req = -1;
    for (size_t i = 0; i < head.size(); ++i) {
      const auto& h = head[i];
      if (h.find("课程性质") != std::string::npos || h.find("课程体系") != std::string::npos ||
          h.find("模块") != std::string::npos || h.find("类别") != std::string::npos) {
        if (idx_mod < 0) idx_mod = static_cast<int>(i);
      } else if (h.find("要求学分") != std::string::npos || h.find("最低学分") != std::string::npos ||
                 h.find("应修学分") != std::string::npos || h.find("规定学分") != std::string::npos) {
        if (idx_req < 0) idx_req = static_cast<int>(i);
      }
    }
    if (idx_mod < 0 || idx_req < 0) continue;

    for (size_t ri = 1; ri < rows.size(); ++ri) {
      const auto& r = rows[ri];
      if (static_cast<size_t>(std::max(idx_mod, idx_req)) >= r.size()) continue;
      std::string mname = utils::trim(r[static_cast<size_t>(idx_mod)]);
      std::string req_s = utils::trim(r[static_cast<size_t>(idx_req)]);
      if (mname.empty() || req_s.empty()) continue;
      double cr = 0.0;
      try { cr = std::stod(req_s); } catch (...) { continue; }
      if (cr <= 0.0) continue;

      if (mname.find("合计") != std::string::npos || mname.find("总计") != std::string::npos ||
          mname.find("毕业") != std::string::npos || mname.find("总学分") != std::string::npos) {
        total_req = std::max(total_req, cr);
      } else {
        mods.push_back({{"name", mname}, {"credit", cr}});
        sum_mods += cr;
      }
    }
    if (!mods.empty()) break;
  }

  if (total_req <= 0.0 && sum_mods >= 120.0) {
    total_req = sum_mods;
  }
  return {
      {"found", total_req >= 120.0 && !mods.empty()},
      {"totalRequired", total_req},
      {"modules", mods},
  };
}

nlohmann::json parse_qiangzhi_plan_html(std::string_view html) {
  auto tables = utils::parse_html_tables(html);
  nlohmann::json courses = nlohmann::json::array();
  std::map<std::string, double> cat_credits;
  double total_cr = 0.0;
  std::set<std::string> seen_codes;

  for (const auto& rows : tables) {
    if (rows.size() < 2) continue;
    const auto& head = rows[0];
    int idx_nature = -1, idx_sys = -1, idx_code = -1, idx_name = -1;
    int idx_cr = -1, idx_term = -1, idx_prop = -1, idx_dept = -1, idx_group = -1;

    for (size_t i = 0; i < head.size(); ++i) {
      const auto& h = head[i];
      if (h.find("课程性质") != std::string::npos) {
        idx_nature = static_cast<int>(i);
      } else if (h.find("体系") != std::string::npos || h.find("类别") != std::string::npos) {
        if (idx_sys < 0) idx_sys = static_cast<int>(i);
      } else if (h.find("课程编号") != std::string::npos || h.find("课程代码") != std::string::npos) {
        idx_code = static_cast<int>(i);
      } else if (h.find("课程名称") != std::string::npos) {
        idx_name = static_cast<int>(i);
      } else if (h == "学分" || h.find("总学分") != std::string::npos) {
        idx_cr = static_cast<int>(i);
      } else if (h.find("开课学期") != std::string::npos || h.find("开设学期") != std::string::npos ||
                 h.find("修读学期") != std::string::npos || h.find("执行学期") != std::string::npos ||
                 h.find("学年学期") != std::string::npos) {
        idx_term = static_cast<int>(i);
      } else if (h.find("属性") != std::string::npos || h.find("修读方式") != std::string::npos ||
                 h.find("是否必修") != std::string::npos) {
        idx_prop = static_cast<int>(i);
      } else if (h.find("开课单位") != std::string::npos || h.find("开课学院") != std::string::npos ||
                 h.find("承担单位") != std::string::npos || h.find("学院") != std::string::npos) {
        idx_dept = static_cast<int>(i);
      } else if (h.find("课程组") != std::string::npos || h.find("组号") != std::string::npos ||
                 h.find("备注") != std::string::npos) {
        idx_group = static_cast<int>(i);
      }
    }
    if (idx_name < 0 || idx_cr < 0) continue;

    auto cell_at = [](const std::vector<std::string>& r, int idx) -> std::string {
      return (idx >= 0 && static_cast<size_t>(idx) < r.size()) ? utils::trim(r[static_cast<size_t>(idx)]) : "";
    };

    for (size_t ri = 1; ri < rows.size(); ++ri) {
      const auto& r = rows[ri];
      std::string name = cell_at(r, idx_name);
      std::string code = cell_at(r, idx_code);
      if (name.empty() || code.empty()) continue;
      if (!seen_codes.insert(code + "|" + name).second) continue;

      double cr = 0.0;
      try { cr = std::stod(cell_at(r, idx_cr)); } catch (...) { cr = 0.0; }

      std::string cat = cell_at(r, idx_nature);
      if (cat.empty()) cat = cell_at(r, idx_sys);
      if (cat.empty()) cat = "专业与通识课程";

      std::string prop = cell_at(r, idx_prop);
      bool is_selective_compulsory = (name.find("选择性必修") != std::string::npos ||
                                      cat.find("选择性必修") != std::string::npos);
      bool req = is_selective_compulsory ||
                 (prop.find("选") == std::string::npos &&
                  cat.find("选") == std::string::npos &&
                  name.find("选修") == std::string::npos);

      std::string raw_term = cell_at(r, idx_term);
      std::vector<int> terms;
      static const std::regex re_sem_term(R"(^(20\d{2})-20\d{2}-([123])$)");
      std::smatch tm;
      if (!raw_term.empty()) {
        if (std::regex_match(raw_term, tm, re_sem_term)) {
          // 例如 2026-2027-1 保留完整学年学期文本
        } else {
          try {
            int t = std::stoi(raw_term);
            if (t >= 1 && t <= 10) terms.push_back(t);
          } catch (...) {
          }
        }
      }

      courses.push_back({
          {"code", code},
          {"name", name},
          {"credit", cr},
          {"cat", cat},
          {"terms", terms},
          {"termLabel", raw_term},
          {"required", req},
          {"dept", cell_at(r, idx_dept)},
          {"note", cell_at(r, idx_group)},
      });
      cat_credits[cat] += cr;
      total_cr += cr;
    }
  }

  nlohmann::json cats = nlohmann::json::array();
  for (const auto& [c, cr] : cat_credits) {
    cats.push_back({{"name", c}, {"credit", cr}});
  }
  return {
      {"found", !courses.empty()},
      {"total", total_cr},
      {"cats", cats},
      {"courses", courses},
      {"subs", nlohmann::json::array()},
      {"specials", nlohmann::json::array()},
  };
}

// ---------------------------------------------------------------------------
// QiangzhiClient 网络与双通道会话编排
// ---------------------------------------------------------------------------
QiangzhiClient::QiangzhiClient(std::string base_url,
                               int polite_delay_ms,
                               bool enable_demo,
                               std::string cas_login_url)
    : base_url_(std::move(base_url)),
      cas_login_url_(std::move(cas_login_url)),
      polite_delay_ms_(polite_delay_ms),
      enable_demo_(enable_demo) {
  while (!base_url_.empty() && base_url_.back() == '/') base_url_.pop_back();
  if (!base_url_.empty()) {
    trigger_warm_replenish();
  }
}

QiangzhiClient::~QiangzhiClient() {
  shutting_down_.store(true);
  std::lock_guard<std::mutex> lk(warm_thread_mu_);
  if (warm_thread_.joinable()) {
    warm_thread_.join();
  }
}

bool QiangzhiClient::fetch_live_captcha_raw(const nlohmann::json& seed_cookies,
                                            std::string& out_data_uri,
                                            nlohmann::json& out_cookies) {
  for (int attempt = 0; attempt < 2; ++attempt) {
    if (shutting_down_.load()) return false;
    net::HttpSession sess(0);
    if (attempt == 0 && seed_cookies.is_array() && !seed_cookies.empty()) {
      sess.jar().load_json(seed_cookies);
    }
    std::string url = base_url_ + "/jsxsd/verifycode.servlet?t=" + std::to_string(utils::now_ms());
    auto r_img = sess.get(url, {{"Referer", base_url_ + "/jsxsd/"}}, false, 6000);
    if (r_img.status == 200 && r_img.body.size() > 64 && sess.jar().has_any_cookie()) {
      std::string mime = "image/jpeg";
      auto it = r_img.headers.find("content-type");
      if (it != r_img.headers.end() && it->second.find("image/") != std::string::npos) {
        mime = utils::split(it->second, ';')[0];
      }
      out_data_uri = "data:" + mime + ";base64," +
                     crypto::base64_encode(
                         reinterpret_cast<const uint8_t*>(r_img.body.data()),
                         r_img.body.size());
      out_cookies = sess.jar().dump_json();
      return true;
    }
  }
  return false;
}

void QiangzhiClient::trigger_warm_replenish() {
  if (shutting_down_.load()) return;
  bool expected = false;
  if (!warm_inflight_.compare_exchange_strong(expected, true)) return;

  std::lock_guard<std::mutex> lk(warm_thread_mu_);
  if (warm_thread_.joinable()) {
    warm_thread_.join();
  }
  warm_thread_ = std::thread([this]() {
    std::string uri;
    nlohmann::json cks;
    if (!shutting_down_.load() && fetch_live_captcha_raw(nlohmann::json::array(), uri, cks)) {
      std::lock_guard<std::mutex> clk(cap_mu_);
      warm_captcha_.ready = true;
      warm_captcha_.img_data_uri = std::move(uri);
      warm_captcha_.cookies = std::move(cks);
      warm_captcha_.fetched_at = utils::now_sec();
    }
    warm_inflight_.store(false);
  });
}

nlohmann::json QiangzhiClient::fetch_login_captcha(const std::string& reuse_captcha_id) {
  std::string cid = crypto::random_urlsafe_token(16);
  int64_t now = utils::now_sec();
  constexpr int kCaptchaTtlSec = 900;  // 延长本地验证码保留时间至 15 分钟，避免用户输入时频繁失效

  nlohmann::json seed_cookies = nlohmann::json::array();
  {
    std::lock_guard<std::mutex> lk(cap_mu_);
    for (auto p = pending_captchas_.begin(); p != pending_captchas_.end();) {
      if (p->second.expires_at < now) p = pending_captchas_.erase(p);
      else ++p;
    }
    if (!reuse_captcha_id.empty()) {
      auto it = pending_captchas_.find(reuse_captcha_id);
      if (it != pending_captchas_.end()) {
        if (!it->second.demo_only) seed_cookies = it->second.cookies;
        pending_captchas_.erase(it);
      }
    }
    // 仅复用 45 秒内刚预热的新鲜验证码，确保上游教务 Tomcat 会话剩余有效期最大化
    if (warm_captcha_.ready && (now - warm_captcha_.fetched_at) < 45) {
      std::string uri = std::move(warm_captcha_.img_data_uri);
      pending_captchas_[cid] = PendingCaptcha{std::move(warm_captcha_.cookies), "", now + kCaptchaTtlSec, false};
      warm_captcha_.ready = false;
      trigger_warm_replenish();
      return {{"ok", true}, {"captchaId", cid}, {"img", uri}, {"timeout", kCaptchaTtlSec},
              {"mode", "live_warm"}, {"liveAvailable", true}};
    }
  }

  std::string live_uri;
  nlohmann::json live_cookies;
  if (fetch_live_captcha_raw(seed_cookies, live_uri, live_cookies)) {
    {
      std::lock_guard<std::mutex> lk(cap_mu_);
      pending_captchas_[cid] = PendingCaptcha{std::move(live_cookies), "", now + kCaptchaTtlSec, false};
    }
    trigger_warm_replenish();
    return {{"ok", true}, {"captchaId", cid}, {"img", live_uri}, {"timeout", kCaptchaTtlSec},
            {"mode", "live"}, {"liveAvailable", true}};
  }

  auto [ans, svg_uri] = SandboxProvider::generate_code_captcha_svg();
  {
    std::lock_guard<std::mutex> lk(cap_mu_);
    pending_captchas_[cid] = PendingCaptcha{nlohmann::json::array(), ans, now + kCaptchaTtlSec, true};
  }
  return {
      {"ok", true},
      {"captchaId", cid},
      {"img", svg_uri},
      {"timeout", kCaptchaTtlSec},
      {"mode", "offline"},
      {"liveAvailable", false},
      {"notice", "暂未连通海大教务验证码服务（如开启 VPN/代理请将 hainanu.edu.cn 加入直连规则后点右侧图片刷新；体验账号可直接登录）"},
  };
}

bool QiangzhiClient::has_qz_session_cookie(const net::HttpSession& sess) const {
  return !sess.jar().get_cookie("bzb_jsxsd").empty() ||
         !sess.jar().get_cookie("JSESSIONID").empty();
}

bool QiangzhiClient::try_cas_sso_login(net::HttpSession& sess,
                                       const std::string& username,
                                       const std::string& password,
                                       bool* out_net_error) {
  if (out_net_error) *out_net_error = false;
  if (username.empty() || cas_login_url_.empty()) return false;

  std::string sso_service = "http://jxgl.hainanu.edu.cn/jsxsd/sso.jsp";
  std::string login_url = cas_login_url_ + "?service=" + utils::url_encode(sso_service);

  auto verify_qz_main = [&]() -> bool {
    auto r_main = sess.get(base_url_ + "/jsxsd/framework/xsMain.jsp", {}, true, 10000);
    if (r_main.status == 0) {
      if (out_net_error) *out_net_error = true;
      return false;
    }
    return r_main.status == 200 &&
           !looks_logged_out_qz(r_main.status, r_main.final_url, r_main.text_utf8());
  };

  auto r_sso = sess.get(base_url_ + "/jsxsd/sso.jsp", {}, false, 10000);
  if (r_sso.status == 0) {
    if (out_net_error) *out_net_error = true;
    return false;
  }
  std::string sso_html = r_sso.text_utf8();
  if (r_sso.status == 200 && !looks_logged_out_qz(r_sso.status, r_sso.final_url, sso_html) &&
      sso_html.find("xsMain") != std::string::npos) {
    return verify_qz_main();
  }
  if (r_sso.status == 301 || r_sso.status == 302) {
    auto it_loc = r_sso.headers.find("location");
    if (it_loc != r_sso.headers.end() && !it_loc->second.empty()) {
      std::string loc = utils::resolve_url(base_url_ + "/jsxsd/sso.jsp", it_loc->second);
      if (loc.find("authserver") != std::string::npos) {
        login_url = loc;
      }
    }
  } else if (r_sso.status == 200) {
    static const std::regex re_sso_js(R"(window\.location\.href\s*=\s*['"]([^'"]+)['"])", std::regex::icase);
    std::smatch sm_js;
    if (std::regex_search(sso_html, sm_js, re_sso_js)) {
      std::string js_u = utils::resolve_url(base_url_ + "/jsxsd/sso.jsp", sm_js[1].str());
      if (js_u.find("authserver") != std::string::npos) {
        login_url = js_u;
      }
    }
  }

  size_t auth_pos = cas_login_url_.find("/authserver/");
  std::string cas_origin = (auth_pos != std::string::npos)
                               ? cas_login_url_.substr(0, auth_pos)
                               : "https://authserver.hainanu.edu.cn";

  // 优先直接用已有 Cookie（特别是长效 CASTGC）访问 CAS：若 CASTGC 仍有效，将直接 302 免密免验证码进入教务！
  auto r_get = sess.get(login_url, {}, true, 12000);
  if (r_get.status == 0) {
    if (out_net_error) *out_net_error = true;
    return false;
  }
  std::string get_html = r_get.text_utf8();
  if (r_get.status == 200 &&
      r_get.final_url.find("jxgl.hainanu.edu.cn") != std::string::npos &&
      !looks_logged_out_qz(r_get.status, r_get.final_url, get_html)) {
    static const std::regex re_js_loc(R"(window\.location\.href\s*=\s*['"]([^'"]+)['"])", std::regex::icase);
    std::smatch m_loc;
    if (std::regex_search(get_html, m_loc, re_js_loc)) {
      std::string next_u = utils::resolve_url(r_get.final_url, m_loc[1].str());
      if (next_u.find("authserver") == std::string::npos) {
        r_get = sess.get(next_u, {{"Referer", r_get.final_url}}, true, 10000);
        get_html = r_get.text_utf8();
      }
    }
    if (r_get.status == 200 && r_get.final_url.find("xsMain") != std::string::npos &&
        !looks_logged_out_qz(r_get.status, r_get.final_url, get_html)) {
      return true;
    }
    if (verify_qz_main()) {
      return true;
    }
  }

  if (password.empty()) return false;

  // 若 r_get 之前拿到了 404/失效跳转（例如过期 CASTGC 被拒），用干净的 authserver 页面重新拉一次登录表单
  if (r_get.final_url.find("authserver") == std::string::npos || get_html.find("pwdEncryptSalt") == std::string::npos) {
    sess.jar().remove_cookie("CASTGC");
    r_get = sess.get(login_url, {}, true, 12000);
    if (r_get.status == 0) {
      if (out_net_error) *out_net_error = true;
      return false;
    }
    get_html = r_get.text_utf8();
  }

  auto r_need = sess.get(
      cas_origin + "/authserver/checkNeedCaptcha.htl?username=" +
          utils::url_encode(username) + "&_=" + std::to_string(utils::now_ms()),
      {}, false, 8000);
  if (r_need.status == 0) {
    if (out_net_error) *out_net_error = true;
    return false;
  }
  if (r_need.status == 200 && r_need.body.find("true") != std::string::npos) {
    return false;
  }

  std::string html = get_html;
  std::string pwd_form = html;
  size_t f_pos = html.find("id=\"pwdFromId\"");
  if (f_pos == std::string::npos) f_pos = html.find("id='pwdFromId'");
  if (f_pos != std::string::npos) {
    size_t f_end = html.find("</form>", f_pos);
    if (f_end != std::string::npos) pwd_form = html.substr(f_pos, f_end - f_pos + 7);
  }

  auto extract_cas_input = [](const std::string& src, const std::string& attr_k, const std::string& attr_v) -> std::string {
    std::regex re1("<input\\b[^>]*\\b" + attr_k + "\\s*=\\s*[\"']" + attr_v +
                       "[\"'][^>]*\\bvalue\\s*=\\s*[\"']([^\"']*)[\"']",
                   std::regex::icase);
    std::smatch m;
    if (std::regex_search(src, m, re1)) return m[1].str();
    std::regex re2("<input\\b[^>]*\\bvalue\\s*=\\s*[\"']([^\"']*)[\"'][^>]*\\b" + attr_k +
                       "\\s*=\\s*[\"']" + attr_v + "[\"']",
                   std::regex::icase);
    if (std::regex_search(src, m, re2)) return m[1].str();
    return "";
  };
  static const std::regex re_script_salt(R"(pwdDefaultEncryptSalt\s*=\s*["']([^"']+)["'])");

  std::smatch m_salt;
  std::string salt = extract_cas_input(pwd_form, "id", "pwdEncryptSalt");
  if (salt.empty() && std::regex_search(pwd_form, m_salt, re_script_salt)) {
    salt = m_salt[1].str();
  }
  if (salt.empty()) {
    salt = extract_cas_input(html, "id", "pwdEncryptSalt");
  }
  if (salt.empty() && std::regex_search(html, m_salt, re_script_salt)) {
    salt = m_salt[1].str();
  }
  if (salt.size() != 16) return false;
  std::string execution = extract_cas_input(pwd_form, "name", "execution");
  if (execution.empty()) execution = extract_cas_input(html, "name", "execution");
  if (execution.empty()) execution = "e1s1";
  std::string lt = extract_cas_input(pwd_form, "name", "lt");
  if (lt.empty()) lt = extract_cas_input(html, "name", "lt");

  std::string enc_pwd = crypto::wisedu_cas_encrypt_password(password, salt);
  std::vector<std::pair<std::string, std::string>> form = {
      {"username", username},
      {"password", enc_pwd},
      {"captcha", ""},
      {"_eventId", "submit"},
      {"cllt", "userNameLogin"},
      {"dllt", "generalLogin"},
      {"lt", lt},
      {"execution", execution},
  };

  auto r_post = sess.post_form(r_get.final_url, form,
                               {{"Origin", cas_origin}, {"Referer", r_get.final_url}},
                               true, 18000);
  if (r_post.status == 0) {
    if (out_net_error) *out_net_error = true;
    return false;
  }

  std::string post_html = r_post.text_utf8();
  static const std::regex re_cas_err(
      R"(id\s*=\s*["']showErrorTip["'][^>]*>([\s\S]*?)</span>)", std::regex::icase);
  std::smatch m_err;
  if (std::regex_search(post_html, m_err, re_cas_err)) {
    std::string tip = utils::strip_html_tags(m_err[1].str(), false);
    if (!tip.empty()) {
      if (tip.find("密码") != std::string::npos || tip.find("用户名") != std::string::npos ||
          tip.find("有误") != std::string::npos || tip.find("不存在") != std::string::npos ||
          tip.find("激活") != std::string::npos || tip.find("冻结") != std::string::npos ||
          tip.find("锁定") != std::string::npos) {
        throw JwError("海南大学统一身份认证：" + tip, "auth", "cas_auth_err");
      }
    }
  }

  static const std::regex re_js_loc(R"(window\.location\.href\s*=\s*['"]([^'"]+)['"])", std::regex::icase);
  std::smatch m_loc;
  if (r_post.status == 200 &&
      r_post.final_url.find("jxgl.hainanu.edu.cn") != std::string::npos &&
      std::regex_search(post_html, m_loc, re_js_loc)) {
    std::string next_u = utils::resolve_url(r_post.final_url, m_loc[1].str());
    if (next_u.find("authserver") == std::string::npos) {
      r_post = sess.get(next_u, {{"Referer", r_post.final_url}}, true, 10000);
      post_html = r_post.text_utf8();
    }
  }

  if (r_post.status == 200 &&
      r_post.final_url.find("jxgl.hainanu.edu.cn") != std::string::npos &&
      r_post.final_url.find("xsMain") != std::string::npos &&
      !looks_logged_out_qz(r_post.status, r_post.final_url, post_html)) {
    return true;
  }

  return verify_qz_main();
}

bool QiangzhiClient::try_isolated_cas_relogin(net::HttpSession& sess,
                                              const std::string& username,
                                              const std::string& password,
                                              nlohmann::json* out_updated_cookies,
                                              bool* out_net_error) {
  if (out_net_error) *out_net_error = false;
  if (password.empty() && sess.jar().get_cookie("CASTGC").empty()) {
    return false;
  }
  // 使用干净的 cas_sess，仅保留 authserver.hainanu.edu.cn 的 Cookie，彻底隔离陈旧失效的 jxgl Cookie
  net::HttpSession cas_sess(polite_delay_ms_);
  cas_sess.jar().merge_json(sess.jar().dump_json(), "authserver.hainanu.edu.cn");
  bool net_err = false;
  if (try_cas_sso_login(cas_sess, username, password, &net_err)) {
    sess.jar().load_json(cas_sess.jar().dump_json());
    if (out_updated_cookies) *out_updated_cookies = sess.jar().dump_json();
    return true;
  }
  if (out_net_error) *out_net_error = net_err;
  return false;
}

bool QiangzhiClient::ensure_session_or_relogin(net::HttpSession& sess,
                                               const std::string& username,
                                               const std::string& password,
                                               nlohmann::json* out_updated_cookies) {
  if (has_qz_session_cookie(sess)) {
    return true;
  }
  bool net_err = false;
  if (try_isolated_cas_relogin(sess, username, password, out_updated_cookies, &net_err)) {
    return true;
  }
  if (net_err) {
    throw JwError("连接海南大学教务系统超时（已保留本地登录状态与缓存，请检查网络或将 hainanu.edu.cn 设为直连）",
                  "net", "relogin_net_timeout");
  }
  throw JwError("教务系统单次会话已过期，请输入一次验证码恢复同步", "expired", "no_qz_cookie");
}

bool QiangzhiClient::keepalive_session(const std::string& username,
                                       const std::string& password,
                                       const nlohmann::json& saved_cookies,
                                       nlohmann::json* out_updated_cookies) {
  if (enable_demo_ && SandboxProvider::is_demo_account(username)) {
    return true;
  }
  net::HttpSession sess(0);
  sess.jar().load_json(saved_cookies);

  if (has_qz_session_cookie(sess)) {
    auto r = sess.get(base_url_ + "/jsxsd/framework/xsMain.jsp", {}, true, 8000);
    if (r.status == 0) {
      return false;  // 网络暂时不可达，静默跳过，绝不破坏用户状态
    }
    if (r.status == 200 && !looks_logged_out_qz(r.status, r.final_url, r.text_utf8())) {
      if (out_updated_cookies) *out_updated_cookies = sess.jar().dump_json();
      return true;
    }
  }

  // 上游 Tomcat 会话已超时，尝试通过保存的 CASTGC 或统一身份认证自动无感续期（清理旧 jxgl Cookie 防干扰）
  try {
    net::HttpSession cas_sess(0);
    cas_sess.jar().merge_json(saved_cookies, "authserver.hainanu.edu.cn");
    bool net_err = false;
    if ((!password.empty() || !cas_sess.jar().get_cookie("CASTGC").empty()) &&
        try_cas_sso_login(cas_sess, username, password, &net_err)) {
      if (out_updated_cookies) *out_updated_cookies = cas_sess.jar().dump_json();
      return true;
    }
  } catch (...) {
  }
  return false;
}

nlohmann::json QiangzhiClient::fetch_grades_on_session(net::HttpSession& sess,
                                                       const std::string& cur_sem_id,
                                                       const std::string& filter_semester,
                                                       SyncProgressCallback progress) {
  auto emit = [&](int pct, const std::string& stage, const std::string& detail = "") {
    if (progress) progress(pct, 4, 6, stage, detail);
  };

  nlohmann::json merged = nlohmann::json::array();
  nlohmann::json sources = nlohmann::json::array();
  bool eval_blocked = false;
  bool primary_logged_out = false;
  bool list_ok = false;
  int net_fail = 0;
  std::string resolved_sem = cur_sem_id;
  const std::string query_page = base_url_ + "/jsxsd/kscj/cjcx_query";

  auto absorb = [&](const nlohmann::json& parsed, const std::string& path, const std::string& label) {
    int n = parsed.contains("courses") && parsed["courses"].is_array()
                ? static_cast<int>(parsed["courses"].size())
                : 0;
    sources.push_back({{"path", path}, {"label", label}, {"count", n}});
    if (parsed.value("evalBlocked", false)) eval_blocked = true;
    if (n > 0) merge_grade_courses(merged, parsed["courses"]);
    return n;
  };

  auto usable_primary = [&](const net::HttpResponse& r, bool is_grade_list = false) -> bool {
    if (r.status == 0) {
      ++net_fail;
      return false;
    }
    if (looks_logged_out_qz(r.status, r.final_url, r.text_utf8())) {
      primary_logged_out = true;
      return false;
    }
    if (r.status != 200) return false;
    if (is_grade_list) list_ok = true;
    return true;
  };

  // ---- Step A：读取官方成绩查询入口 /jsxsd/kscj/cjcx_query，提取真实表单 action / 隐藏字段 / 下拉项 ----
  // 严禁访问页面 JS 拼接出来的打印/选学期残缺 URL（如 printXscjk_），强智权限拦截器会直接注销当前会话！
  emit(54, "正在读取海南大学成绩查询页…", "解析课程成绩与期中/期末/平时构成入口");
  std::string list_action = base_url_ + "/jsxsd/kscj/cjcx_list";
  std::string kksj_field = "kksj";
  std::string default_xsfs = "all";
  std::vector<std::pair<std::string, std::string>> hidden_defaults;
  bool has_sfxsbcxq = false;
  struct ModeOpt {
    std::string name, value, text;
  };
  std::vector<ModeOpt> extra_modes;

  auto r_q = sess.get(query_page, {}, true, 10000);
  if (usable_primary(r_q)) {
    std::string q_html = r_q.text_utf8();
    // 偶发遇到强智防连点短提示页（<500B 无表单）时，礼貌等待 800ms 重试一次
    if (q_html.size() < 500 && q_html.find("<form") == std::string::npos && q_html.find("kksj") == std::string::npos) {
      std::this_thread::sleep_for(std::chrono::milliseconds(800));
      auto r_q2 = sess.get(query_page, {}, true, 10000);
      if (usable_primary(r_q2) && r_q2.text_utf8().size() > q_html.size()) {
        q_html = r_q2.text_utf8();
      }
    }
    if (grade_has_eval_block(q_html)) eval_blocked = true;
    has_sfxsbcxq = q_html.find("sfxsbcxq") != std::string::npos;

    std::string q_low = utils::to_lower(q_html);
    std::string form_html = q_html;
    size_t kp = q_low.find("kksj");
    if (kp == std::string::npos) kp = q_low.find("xnxq01id");
    if (kp != std::string::npos) {
      if (q_low.substr(kp, 8) == "xnxq01id") kksj_field = "xnxq01id";
      size_t fp = q_low.rfind("<form", kp);
      if (fp != std::string::npos) {
        size_t fe = q_low.find('>', fp);
        size_t f_close = q_low.find("</form>", fp);
        if (fe != std::string::npos) {
          std::string ftag = q_html.substr(fp, fe - fp + 1);
          static const std::regex re_action(R"RX(action\s*=\s*["']([^"']+)["'])RX", std::regex::icase);
          std::smatch am;
          if (std::regex_search(ftag, am, re_action)) {
            std::string act = utils::trim(am[1].str());
            std::string lowact = utils::to_lower(act);
            if (!act.empty() && (lowact.find("cjcx") != std::string::npos || lowact.find("kscj") != std::string::npos)) {
              list_action = utils::resolve_url(query_page, act);
            }
          }
          if (f_close != std::string::npos && f_close > fp) {
            form_html = q_html.substr(fp, f_close - fp + 7);
          }
        }
      }
    }

    static const std::regex re_hidden(
        R"RX(<input[^>]*type\s*=\s*["']hidden["'][^>]*>)RX", std::regex::icase);
    static const std::regex re_attr_name(R"RX(\bname\s*=\s*["']([^"']+)["'])RX", std::regex::icase);
    static const std::regex re_attr_val(R"RX(\bvalue\s*=\s*["']([^"']*)["'])RX", std::regex::icase);
    for (std::sregex_iterator it(form_html.begin(), form_html.end(), re_hidden), end; it != end; ++it) {
      std::string tag = (*it)[0].str();
      std::smatch nm, vm;
      if (std::regex_search(tag, nm, re_attr_name)) {
        std::string hn = utils::trim(nm[1].str());
        std::string hv = std::regex_search(tag, vm, re_attr_val) ? utils::trim(vm[1].str()) : "";
        if (!hn.empty() && hn != "kksj" && hn != "kcxz" && hn != "kcmc" && hn != "xsfs") {
          hidden_defaults.emplace_back(hn, hv);
        }
      }
    }

    static const std::regex re_select(
        R"RX(<select[^>]*name\s*=\s*["']([A-Za-z0-9_]+)["'][^>]*>([\s\S]*?)</select>)RX", std::regex::icase);
    static const std::regex re_opt(
        R"RX((<option[^>]*value\s*=\s*["']([^"']*)["'][^>]*>)([\s\S]*?)</option>)RX", std::regex::icase);
    static const std::regex re_sem_id(R"(^20\d{2}-20\d{2}-[123]$)");
    for (std::sregex_iterator it(form_html.begin(), form_html.end(), re_select), end; it != end; ++it) {
      std::string raw_name = (*it)[1].str();
      std::string sname = utils::to_lower(raw_name);
      std::string inner = (*it)[2].str();
      bool xsfs_has_all = false;
      std::string xsfs_first;
      for (std::sregex_iterator oit(inner.begin(), inner.end(), re_opt), oend; oit != oend; ++oit) {
        std::string otag = utils::to_lower((*oit)[1].str());
        std::string val = utils::trim((*oit)[2].str());
        std::string txt = utils::strip_html_tags((*oit)[3].str(), false);

        if ((sname == "kksj" || sname == "xnxq01id") && resolved_sem.empty() &&
            otag.find("selected") != std::string::npos && std::regex_match(val, re_sem_id)) {
          resolved_sem = val;
        }
        if (sname == "xsfs") {
          if (val == "all") xsfs_has_all = true;
          if (xsfs_first.empty() && !val.empty()) xsfs_first = val;
        }
        if (sname == "cjlx" || sname == "kslx" || sname == "cjfs" || sname == "kslb") {
          if (!val.empty() && val != "all" &&
              (txt.find("期中") != std::string::npos || txt.find("期末") != std::string::npos ||
               txt.find("原始") != std::string::npos || txt.find("阶段") != std::string::npos ||
               txt.find("过程") != std::string::npos || txt.find("平时") != std::string::npos)) {
            extra_modes.push_back({raw_name, val, txt});
          }
        }
      }
      if (sname == "xsfs" && !xsfs_has_all && !xsfs_first.empty()) {
        default_xsfs = xsfs_first;
      }
    }
  }

  const std::string target_sem = filter_semester.empty() ? resolved_sem : filter_semester;

  // ---- Step B：单次安全查询主成绩单 (cjcx_list)，避免 1 秒内连发两次触发强智防频繁刷新限流 ----
  emit(60, "正在查询课程成绩单…", filter_semester.empty() ? "全部学期" : filter_semester);
  auto make_form = [&](const std::string& kksj) {
    std::vector<std::pair<std::string, std::string>> form = {
        {kksj_field, kksj}, {"kcxz", ""}, {"kcmc", ""}, {"xsfs", default_xsfs}};
    if (has_sfxsbcxq) form.emplace_back("sfxsbcxq", "1");
    for (const auto& kv : hidden_defaults) form.push_back(kv);
    return form;
  };
  auto query_list = [&](const std::string& action,
                        const std::vector<std::pair<std::string, std::string>>& form,
                        const std::string& hint,
                        const std::string& label,
                        const std::string& term_hint) -> std::pair<int, bool> {
    auto r = sess.post_form(action, form, {{"Referer", query_page}}, true, 12000);
    if (!usable_primary(r, true)) return {0, false};
    std::string body = r.text_utf8();
    // 若命中防频繁刷新短提示页（无表格），稍等 800ms 后重查一次
    if (body.size() < 500 && body.find("<table") == std::string::npos && body.find("dataList") == std::string::npos) {
      std::this_thread::sleep_for(std::chrono::milliseconds(800));
      auto r2 = sess.post_form(action, form, {{"Referer", query_page}}, true, 12000);
      if (usable_primary(r2, true) && r2.text_utf8().size() > body.size()) {
        body = r2.text_utf8();
      }
    }
    auto tbls = parse_html_tables_rich(body);
    bool got_table = !tbls.empty();
    auto parsed = parse_qiangzhi_grades_html(body, hint, term_hint);
    int n = absorb(parsed, action.substr(base_url_.size() <= action.size() ? base_url_.size() : 0), label);
    HNU_LOG_INFO("jwxt", label + " len=" + std::to_string(body.size()) +
                             " tables=" + std::to_string(tbls.size()) +
                             " parsed=" + std::to_string(n));
    return {n, got_table};
  };

  if (!primary_logged_out) {
    auto [n_main, got_tbl] = query_list(list_action, make_form(filter_semester), "all", "课程成绩单", filter_semester);
    // 仅当主查询未拿到有效成绩表格时，才间隔 600ms 兜底查一次当前学期
    if (!got_tbl && !primary_logged_out && !target_sem.empty()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(600));
      query_list(list_action, make_form(target_sem), "all", "本学期成绩单", target_sem);
    }

    // 若 cjcx_query 下拉框确实存在期中/阶段/原始等额外选项，间隔 600ms 补充查询
    for (const auto& m : extra_modes) {
      if (primary_logged_out || net_fail >= 2) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(600));
      auto form = make_form(target_sem);
      bool replaced = false;
      for (auto& kv : form) {
        if (utils::to_lower(kv.first) == utils::to_lower(m.name)) {
          kv.second = m.value;
          replaced = true;
        }
      }
      if (!replaced) form.emplace_back(m.name, m.value);
      std::string hint = (m.text.find("期中") != std::string::npos || m.text.find("阶段") != std::string::npos)
                             ? "midterm"
                             : (m.text.find("平时") != std::string::npos ? "regular" : "all");
      query_list(list_action, form, hint, m.text, target_sem);
    }

    // ---- Step B2：海南大学强智教务官方独立「期中成绩查询」通道 (/jsxsd/gsgl/wdcj_qzcjcx -> wdcj_qzcjcxList) ----
    if (!primary_logged_out && net_fail < 2) {
      emit(64, "正在查询期中成绩单…", filter_semester.empty() ? "全部学期" : filter_semester);
      std::this_thread::sleep_for(std::chrono::milliseconds(400));
      const std::string mid_query_page = base_url_ + "/jsxsd/gsgl/wdcj_qzcjcx";
      std::string mid_list_action = base_url_ + "/jsxsd/gsgl/wdcj_qzcjcxList";
      auto r_mq = sess.get(mid_query_page, {{"Referer", base_url_ + "/jsxsd/framework/xsMain.jsp"}}, true, 10000);
      if (r_mq.status == 0) {
        ++net_fail;
      } else if (r_mq.status == 200 && !looks_logged_out_qz(r_mq.status, r_mq.final_url, r_mq.text_utf8())) {
        std::string mq_html = r_mq.text_utf8();
        if (grade_has_eval_block(mq_html)) eval_blocked = true;
        static const std::regex re_mid_act(R"RX((/jsxsd/gsgl/[A-Za-z0-9_]+List[A-Za-z0-9_.]*))RX");
        std::smatch mam;
        if (std::regex_search(mq_html, mam, re_mid_act)) {
          mid_list_action = utils::resolve_url(mid_query_page, mam[1].str());
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        std::vector<std::pair<std::string, std::string>> mid_form = {
            {"xnxq", filter_semester},
            {"kch", ""},
            {"kcmc", ""},
        };
        auto r_ml = sess.post_form(mid_list_action, mid_form, {{"Referer", mid_query_page}}, true, 12000);
        if (r_ml.status == 0) {
          ++net_fail;
        } else if (r_ml.status == 200 && !looks_logged_out_qz(r_ml.status, r_ml.final_url, r_ml.text_utf8())) {
          std::string mbody = r_ml.text_utf8();
          if (mbody.size() < 500 && mbody.find("<table") == std::string::npos) {
            std::this_thread::sleep_for(std::chrono::milliseconds(800));
            auto r_ml2 = sess.post_form(mid_list_action, mid_form, {{"Referer", mid_query_page}}, true, 12000);
            if (r_ml2.status == 200 &&
                !looks_logged_out_qz(r_ml2.status, r_ml2.final_url, r_ml2.text_utf8()) &&
                r_ml2.text_utf8().size() > mbody.size()) {
              mbody = r_ml2.text_utf8();
            }
          }
          list_ok = true;
          auto mid_tbls = parse_html_tables_rich(mbody);
          auto mid_parsed = parse_qiangzhi_grades_html(mbody, "midterm", filter_semester);
          int n_mid = absorb(
              mid_parsed,
              mid_list_action.substr(base_url_.size() <= mid_list_action.size() ? base_url_.size() : 0),
              "期中成绩单");
          HNU_LOG_INFO("jwxt", "期中成绩单 len=" + std::to_string(mbody.size()) +
                                   " tables=" + std::to_string(mid_tbls.size()) +
                                   " parsed=" + std::to_string(n_mid));
        }
      }
    }
  }

  // ---- Step C：逐门课程补拉 平时 / 期中 / 期末 / 实验 成绩构成明细弹窗 (/jsxsd/kscj/pscj_list.do) ----
  emit(68, "正在同步成绩构成明细…", "平时 / 期中 / 期末 / 实验");
  int total_pscj = 0;
  for (const auto& gc : merged) {
    if (!utils::json_str(gc, "_pscjUrl", "").empty()) ++total_pscj;
  }
  total_pscj = std::min(total_pscj, 80);
  int detail_fetched = 0;
  for (auto& gc : merged) {
    std::string pscj_path = utils::json_str(gc, "_pscjUrl", "");
    gc.erase("_pscjUrl");
    if (pscj_path.empty() || detail_fetched >= 80) continue;
    std::string full_url = utils::resolve_url(base_url_ + "/jsxsd/kscj/cjcx_list", pscj_path);
    auto r_ps = sess.get(full_url, {{"Referer", base_url_ + "/jsxsd/kscj/cjcx_list"}}, true, 7000);
    ++detail_fetched;
    if (total_pscj > 0) {
      int sub_pct = 68 + (detail_fetched * 10) / total_pscj;
      emit(sub_pct,
           "正在同步成绩明细 (" + std::to_string(detail_fetched) + "/" + std::to_string(total_pscj) + ")…",
           utils::json_str(gc, "name", ""));
    }
    if (r_ps.status == 200 && !looks_logged_out_qz(r_ps.status, r_ps.final_url, r_ps.text_utf8())) {
      auto detail = parse_qiangzhi_pscj_html(r_ps.text_utf8());
      for (const char* k : {"regularScore", "regularRatio", "midtermScore", "midtermRatio",
                            "finalScore", "finalRatio", "labScore", "labRatio"}) {
        std::string dv = utils::json_str(detail, k, "");
        if (!dv.empty()) gc[k] = dv;
      }
      std::string total = utils::json_str(detail, "totalScore", "");
      if (!total.empty() && utils::json_str(gc, "score", "").empty()) {
        gc["score"] = total;
        gc["final"] = total;
        if (utils::json_str(gc, "gpa", "").empty()) gc["gpa"] = fmt_2dec(score_to_gpa(total, ""));
      }
    }
  }

  if (merged.empty() && !list_ok) {
    if (primary_logged_out) throw JwError("教务登录会话已超时，请重新同步", "expired", "gr_expired");
    if (net_fail > 0) {
      throw JwError("读取成绩页网络超时（已保留上次有效成绩缓存）", "net", "gr_net_timeout");
    }
  }

  auto out = finalize_grades_json(merged, eval_blocked);
  out["queriedSemester"] = filter_semester;
  out["sourcesChecked"] = sources;
  std::string notice;
  if (eval_blocked) {
    notice = "检测到海南大学教务系统要求先完成本学期教学评价后方可查看成绩，请先前往教务系统完成评教后再来查询。";
  } else if (merged.empty()) {
    notice = "已成功连通海南大学强智教务成绩单，但教务系统当前返回「未查询到数据」（0 条）。这说明任课教师尚未在教务系统录入或审核发布" +
             std::string(filter_semester.empty() ? "本学期" : filter_semester + " 学期") +
             "的期中/期末成绩，一旦老师在教务系统提交发布即可在此查到平时、期中、期末与总评明细。";
  }
  out["notice"] = notice;
  HNU_LOG_INFO("jwxt", "grades fetched total=" + std::to_string(merged.size()) +
                           " mid=" + std::to_string(out.value("midtermCount", 0)) +
                           " final=" + std::to_string(out.value("finalCount", 0)) +
                           " sources=" + std::to_string(sources.size()) +
                           " evalBlocked=" + (eval_blocked ? "1" : "0"));
  return out;
}

nlohmann::json QiangzhiClient::fetch_grades_for(const std::string& username,
                                                const std::string& password,
                                                const nlohmann::json& saved_cookies,
                                                const std::string& semester_id,
                                                nlohmann::json* out_updated_cookies,
                                                SyncProgressCallback progress) {
  if (enable_demo_ && SandboxProvider::is_demo_account(username)) {
    if (progress) progress(40, 4, 6, "正在读取演示成绩单…", "含期中 / 期末 / 平时成绩");
    auto g = SandboxProvider::demo_grades(semester_id);
    if (progress) progress(92, 4, 6, "成绩查询完成", "");
    return g;
  }
  net::HttpSession sess(polite_delay_ms_);
  sess.jar().load_json(saved_cookies);
  ensure_session_or_relogin(sess, username, password, out_updated_cookies);

  if (progress) progress(10, 4, 6, "正在连接教务成绩系统…", "校验登录会话");
  auto probe = sess.get(base_url_ + "/jsxsd/kscj/cjcx_query", {}, true, 10000);
  if (probe.status == 0) {
    throw JwError("连接教务系统超时（已保留本地成绩缓存）", "net", "gr_probe_net");
  }
  if (looks_logged_out_qz(probe.status, probe.final_url, probe.text_utf8())) {
    bool relogin_ok = false;
    bool net_err = false;
    if (try_isolated_cas_relogin(sess, username, password, out_updated_cookies, &net_err)) {
      probe = sess.get(base_url_ + "/jsxsd/kscj/cjcx_query", {}, true, 10000);
      if (probe.status == 0) {
        throw JwError("连接教务系统超时（已保留本地成绩缓存）", "net", "gr_probe_net2");
      }
      relogin_ok = probe.status == 200 &&
                   !looks_logged_out_qz(probe.status, probe.final_url, probe.text_utf8());
    } else if (net_err) {
      throw JwError("连接教务系统超时（已保留本地成绩缓存）", "net", "gr_relogin_net");
    }
    if (!relogin_ok) {
      throw JwError("教务登录会话已超时，请输入一次验证码恢复同步", "expired", "gr_expired");
    }
  }
  std::string cur_sem = semester_id;
  if (cur_sem.empty()) {
    static const std::regex re_sel1(
        R"RX(<option[^>]*value\s*=\s*["'](20\d{2}-20\d{2}-[123])["'][^>]*selected)RX", std::regex::icase);
    static const std::regex re_sel2(
        R"RX(<option[^>]*selected[^>]*value\s*=\s*["'](20\d{2}-20\d{2}-[123])["'])RX", std::regex::icase);
    std::string q_html = probe.text_utf8();
    std::smatch sm;
    if (std::regex_search(q_html, sm, re_sel1) || std::regex_search(q_html, sm, re_sel2)) {
      cur_sem = sm[1].str();
    }
  }
  // 稍隔 300ms 避免紧跟 cjcx_query 触发连点保护
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  auto out = fetch_grades_on_session(sess, cur_sem, semester_id, progress);
  if (out_updated_cookies && has_qz_session_cookie(sess)) {
    *out_updated_cookies = sess.jar().dump_json();
  }
  return out;
}

nlohmann::json QiangzhiClient::run_full_sync_on_session(net::HttpSession& sess,
                                                        const std::string& username,
                                                        SyncProgressCallback progress) {
  auto emit = [&](int pct, int step, const std::string& stage, const std::string& detail = "") {
    if (progress) progress(pct, step, 6, stage, detail);
  };

  std::string profile_error, tt_error;
  nlohmann::json profile = nlohmann::json::object();
  nlohmann::json tt;
  nlohmann::json sems = nlohmann::json::array();

  // 1. 学籍卡片 (/jsxsd/grxx/xsxx)
  emit(20, 2, "正在同步学籍档案…", "读取学院、书院、专业与行政班级");
  auto r_prof = sess.get(base_url_ + "/jsxsd/grxx/xsxx");
  if (r_prof.status == 0) {
    throw JwError("连接海南大学教务系统超时（已保留本地登录状态与缓存，请检查网络或关闭代理后重试）",
                  "net", "prof_net_timeout");
  }
  std::string prof_html = r_prof.text_utf8();
  if (r_prof.status == 200 && !looks_logged_out_qz(r_prof.status, r_prof.final_url, prof_html)) {
    profile = parse_qiangzhi_profile_html(prof_html);
    if (!profile.contains("studentId")) profile["studentId"] = username;
    // 若学籍卡中未包含书院字段，再检查教务主页顶部欢迎栏是否展示书院名称
    if (utils::json_str(profile, "academy", "").empty()) {
      auto r_main = sess.get(base_url_ + "/jsxsd/framework/xsMain.jsp", {}, true, 4500);
      if (r_main.status == 200 && !looks_logged_out_qz(r_main.status, r_main.final_url, r_main.text_utf8())) {
        std::string ac = detect_hnu_academy_in_text(r_main.text_utf8());
        if (!ac.empty()) {
          profile["academy"] = ac;
          if (utils::json_str(profile, "campus", "").empty()) {
            profile["campus"] = infer_hnu_campus_from_academy(ac);
          }
        }
      }
    }
  } else if (looks_logged_out_qz(r_prof.status, r_prof.final_url, prof_html)) {
    throw JwError("教务登录会话已超时，请重新同步", "expired", "prof_expired");
  } else {
    profile_error = "学籍信息页本次未读到";
  }

  // 2. 本学期课表与可选学期列表 (/jsxsd/xskb/xskb_list.do)
  emit(36, 3, "正在同步 11 节课表…", "解析本周与全学期课程安排");
  auto r_tt = sess.get(base_url_ + "/jsxsd/xskb/xskb_list.do");
  if (r_tt.status == 0) {
    throw JwError("读取课表页网络超时（已保留上次有效课表缓存）", "net", "tt_net_timeout");
  }
  std::string tt_html = r_tt.text_utf8();
  if (looks_logged_out_qz(r_tt.status, r_tt.final_url, tt_html)) {
    throw JwError("教务登录会话已超时，请重新同步", "expired", "tt_expired");
  }
  if (r_tt.status == 200) {
    sems = parse_qiangzhi_semesters_html(tt_html);
    try {
      tt = parse_qiangzhi_timetable_html(tt_html);
      std::string target_sem = utils::json_str(tt, "semesterId", "");
      if (tt["courses"].empty() && !target_sem.empty()) {
        std::vector<std::pair<std::string, std::string>> tt_form = {
            {"xnxq01id", target_sem},
            {"zc", ""},
            {"sfFD", "1"},
        };
        static const std::regex re_kb_mode(
            R"(<option[^>]*value\s*=\s*["']([^"']+)["'][^>]*selected[^>]*>)", std::regex::icase);
        size_t kbm_pos = tt_html.find("kbjcmsid");
        if (kbm_pos != std::string::npos) {
          size_t sel_end = tt_html.find("</select>", kbm_pos);
          if (sel_end != std::string::npos && sel_end - kbm_pos < 2000) {
            std::string sub = tt_html.substr(kbm_pos, sel_end - kbm_pos);
            std::smatch km;
            if (std::regex_search(sub, km, re_kb_mode)) {
              tt_form.emplace_back("kbjcmsid", km[1].str());
            }
          }
        }
        auto r_tt2 = sess.post_form(base_url_ + "/jsxsd/xskb/xskb_list.do",
                                    tt_form,
                                    {{"Referer", base_url_ + "/jsxsd/xskb/xskb_list.do"}});
        if (r_tt2.status == 200 && !looks_logged_out_qz(r_tt2.status, r_tt2.final_url, r_tt2.text_utf8())) {
          auto tt2 = parse_qiangzhi_timetable_html(r_tt2.text_utf8(), target_sem);
          if (!tt2["courses"].empty()) {
            tt = std::move(tt2);
          }
        }
      }
      if (tt["courses"].empty()) {
        HNU_LOG_WARN("jwxt", "Timetable sync parsed 0 courses sem=" + target_sem +
                                 " html_len=" + std::to_string(tt_html.size()));
      }
    } catch (const JwError& e) {
      tt = {{"semester", ""}, {"unitCount", 11}, {"totalWeeks", 20}, {"courses", nlohmann::json::array()}, {"empty", true}};
      tt_error = e.what();
    } catch (const std::exception& ex) {
      tt = {{"semester", ""}, {"unitCount", 11}, {"totalWeeks", 20}, {"courses", nlohmann::json::array()}, {"empty", true}};
      tt_error = std::string("课表解析异常：") + ex.what();
    }
  } else {
    tt = {{"semester", ""}, {"unitCount", 11}, {"totalWeeks", 20}, {"courses", nlohmann::json::array()}, {"empty", true}};
    tt_error = "课表页返回 HTTP " + std::to_string(r_tt.status);
  }

  // 3. 历年全部成绩 + 期中 / 期末 / 平时 / 实验成绩多通道聚合 (/jsxsd/kscj/*)
  emit(54, 4, "正在同步成绩与 GPA…", "拉取历年总评及平时/期中/期末构成");
  std::string cur_sem_id = utils::json_str(tt, "semesterId", sems.empty() ? "2026-2027-1" : utils::json_str(sems[0], "id", ""));
  nlohmann::json grades;
  try {
    grades = fetch_grades_on_session(sess, cur_sem_id, "", progress);
  } catch (const JwError& e) {
    grades = {{"summary", nlohmann::json::array()}, {"overall", nullptr}, {"courses", nlohmann::json::array()},
              {"error", std::string("成绩页本次未读到：") + e.what()}};
  }

  // 4. 考试安排 (/jsxsd/xsks/xsksap_list)
  emit(78, 5, "正在同步考场安排…", "获取考试时间、考场教室与座位号");
  nlohmann::json exams;
  auto r_ex = sess.post_form(base_url_ + "/jsxsd/xsks/xsksap_list",
                             {{"xqlbmc", ""}, {"xnxqid", cur_sem_id}, {"xqlb", ""}},
                             {{"Referer", base_url_ + "/jsxsd/xsks/xsksap_query"}});
  if (r_ex.status == 200 && !looks_logged_out_qz(r_ex.status, r_ex.final_url, r_ex.text_utf8())) {
    exams = parse_qiangzhi_exams_html(r_ex.text_utf8());
  } else {
    exams = {{"exams", nlohmann::json::array()}, {"published", false}, {"error", "考试安排页本次未读到"}};
  }
  exams["semesters"] = sems;

  // 5. 培养方案 / 执行计划 (/jsxsd/pyfa/pyfa_query，支持多页自动合并) + 学业完成情况毕业要求探测
  emit(88, 6, "正在同步培养方案与学分审核…", "核对必修、选修与毕业学分进度");
  nlohmann::json plan;
  auto r_pl = sess.get(base_url_ + "/jsxsd/pyfa/pyfa_query");
  if (r_pl.status == 200 && !looks_logged_out_qz(r_pl.status, r_pl.final_url, r_pl.text_utf8())) {
    std::string pl_html = r_pl.text_utf8();
    plan = parse_qiangzhi_plan_html(pl_html);
    if (!plan.value("found", false)) {
      auto r_pl2 = sess.post_form(base_url_ + "/jsxsd/pyfa/pyfa_query",
                                  {{"pageIndex", "1"}},
                                  {{"Referer", base_url_ + "/jsxsd/pyfa/pyfa_query"}},
                                  true, 10000);
      if (r_pl2.status == 200 && !looks_logged_out_qz(r_pl2.status, r_pl2.final_url, r_pl2.text_utf8())) {
        auto plan2 = parse_qiangzhi_plan_html(r_pl2.text_utf8());
        if (plan2.value("found", false)) {
          pl_html = r_pl2.text_utf8();
          plan = std::move(plan2);
        }
      }
    }

    int total_pages = parse_total_pages_hint(pl_html);
    if (total_pages > 1 && plan.value("found", false)) {
      std::set<std::string> seen;
      std::map<std::string, double> cat_credits;
      double total_cr = 0.0;
      for (const auto& c : plan["courses"]) {
        seen.insert(utils::json_str(c, "code", "") + "|" + utils::json_str(c, "name", ""));
        double cr = utils::json_double(c, "credit", 0.0);
        cat_credits[utils::json_str(c, "cat", "专业与通识课程")] += cr;
        total_cr += cr;
      }
      for (int p = 2; p <= std::min(total_pages, 12); ++p) {
        auto r_next = sess.post_form(base_url_ + "/jsxsd/pyfa/pyfa_query",
                                     {{"pageIndex", std::to_string(p)}},
                                     {{"Referer", base_url_ + "/jsxsd/pyfa/pyfa_query"}},
                                     true, 10000);
        if (r_next.status != 200 || looks_logged_out_qz(r_next.status, r_next.final_url, r_next.text_utf8())) break;
        auto sub_plan = parse_qiangzhi_plan_html(r_next.text_utf8());
        int added = 0;
        for (const auto& c : sub_plan.value("courses", nlohmann::json::array())) {
          std::string key = utils::json_str(c, "code", "") + "|" + utils::json_str(c, "name", "");
          if (seen.insert(key).second) {
            plan["courses"].push_back(c);
            double cr = utils::json_double(c, "credit", 0.0);
            cat_credits[utils::json_str(c, "cat", "专业与通识课程")] += cr;
            total_cr += cr;
            ++added;
          }
        }
        if (added == 0) break;
      }
      nlohmann::json cats = nlohmann::json::array();
      for (const auto& [cn, cr] : cat_credits) {
        cats.push_back({{"name", cn}, {"credit", cr}});
      }
      plan["cats"] = cats;
      plan["total"] = total_cr;
    }

    // 仅当培养方案页中显式出现毕业要求明细链接时才访问，绝不盲探未授权 URL 以免触发强智踢出会话！
    for (const char* grad_path : {"/jsxsd/xxwcqk/xxwcqk_idxOnzh.do",
                                  "/jsxsd/xxwcqk/xxwcqk_idxOnlb.do",
                                  "/jsxsd/pyfa/topyfamx"}) {
      if (pl_html.find(grad_path) == std::string::npos) continue;
      auto r_gs = sess.get(base_url_ + grad_path, {}, true, 6000);
      if (r_gs.status == 200 && !looks_logged_out_qz(r_gs.status, r_gs.final_url, r_gs.text_utf8())) {
        auto gs = parse_qiangzhi_grad_summary_html(r_gs.text_utf8());
        if (gs.value("found", false)) {
          plan["gradSummary"] = gs;
          break;
        }
      }
    }
  } else {
    plan = {{"found", false}, {"total", 0}, {"cats", nlohmann::json::array()}, {"courses", nlohmann::json::array()}, {"error", "培养方案页本次未读到"}};
  }

  emit(96, 6, "正在保存加密快照…", "写入本地安全数据库");

  nlohmann::json out = {
      {"ok", true},
      {"syncedAt", utils::now_sec()},
      {"profile", profile},
      {"semesterId", utils::json_str(tt, "semesterId", cur_sem_id)},
      {"semester", utils::json_str(tt, "semester", "")},
      {"unitCount", utils::json_int(tt, "unitCount", 11)},
      {"totalWeeks", utils::json_int(tt, "totalWeeks", 20)},
      {"courses", tt["courses"]},
      {"semesters", sems},
      {"grades", grades},
      {"exams", exams},
      {"plan", plan},
  };
  if (!tt_error.empty()) out["timetableError"] = tt_error;
  if (!profile_error.empty()) out["profileError"] = profile_error;
  return out;
}

nlohmann::json QiangzhiClient::login_and_sync(const std::string& username,
                                              const std::string& password,
                                              const std::string& captcha_id,
                                              const std::string& captcha_code,
                                              nlohmann::json* out_cookies_to_save,
                                              SyncProgressCallback progress,
                                              const nlohmann::json& seed_cookies) {
  auto emit = [&](int pct, int step, const std::string& stage, const std::string& detail = "") {
    if (progress) progress(pct, step, 6, stage, detail);
  };

  if (enable_demo_ && SandboxProvider::is_demo_account(username)) {
    emit(15, 1, "正在验证海大体验账号…", "加载沙箱演示数据");
    if (!captcha_id.empty()) {
      std::lock_guard<std::mutex> lk(cap_mu_);
      auto it = pending_captchas_.find(captcha_id);
      if (it != pending_captchas_.end() && !it->second.demo_answer.empty()) {
        std::string expected = it->second.demo_answer;
        pending_captchas_.erase(it);
        if (!captcha_code.empty() &&
            utils::to_lower(utils::trim(captcha_code)) != utils::to_lower(expected)) {
          throw JwError("验证码输入不正确，请重试", "captcha");
        }
      }
    }
    if (!SandboxProvider::verify_demo_password(username, password)) {
      throw JwError("演示账号密码错误（演示学号 2024300128 密码为 hnu123456）", "auth");
    }
    emit(55, 3, "正在载入海大演示课表与成绩…", "11 节作息与五校区数据");
    if (out_cookies_to_save) *out_cookies_to_save = nlohmann::json::array();
    auto tt = SandboxProvider::demo_timetable("2026-2027-1");
    auto ex = SandboxProvider::demo_exams("2026-2027-1");
    ex["semesters"] = SandboxProvider::demo_semesters();
    emit(95, 6, "正在保存本地快照…", "即将完成");
    return {
        {"ok", true},
        {"demo", true},
        {"syncedAt", utils::now_sec()},
        {"profile", SandboxProvider::demo_profile(username)},
        {"semesterId", tt["semesterId"]},
        {"semester", tt["semester"]},
        {"unitCount", tt["unitCount"]},
        {"totalWeeks", tt["totalWeeks"]},
        {"courses", tt["courses"]},
        {"semesters", SandboxProvider::demo_semesters()},
        {"grades", SandboxProvider::demo_grades()},
        {"exams", ex},
        {"plan", SandboxProvider::demo_plan()},
    };
  }

  net::HttpSession sess(polite_delay_ms_);
  if (seed_cookies.is_array() && !seed_cookies.empty()) {
    sess.jar().load_json(seed_cookies);
  }
  std::string code = utils::trim(captcha_code);
  bool logged_in = false;
  bool any_net_error = false;
  bool was_offline_cap = false;
  bool was_expired_cap = false;
  std::string last_srv_msg;

  emit(8, 1, "正在连接海南大学教务系统…", code.empty() ? "尝试统一身份认证免验证码通道" : "提交教务身份认证与验证码");

  if (!code.empty() && !captcha_id.empty()) {
    PendingCaptcha pc;
    bool has_pc = false;
    {
      std::lock_guard<std::mutex> lk(cap_mu_);
      auto it = pending_captchas_.find(captcha_id);
      if (it != pending_captchas_.end()) {
        pc = std::move(it->second);
        pending_captchas_.erase(it);
        has_pc = true;
      }
    }

    if (!has_pc || pc.expires_at < utils::now_sec()) {
      was_expired_cap = true;
    } else if (pc.demo_only) {
      was_offline_cap = true;
    } else if (!pc.cookies.empty()) {
      sess.jar().load_json(pc.cookies);
      // 保留之前保存的 authserver CASTGC 票据（仅合入统一认证域，避免旧 jxgl Cookie 污染验证码会话）
      if (seed_cookies.is_array() && !seed_cookies.empty()) {
        sess.jar().merge_json(seed_cookies, "authserver.hainanu.edu.cn");
      }
      std::string encoded = crypto::qiangzhi_build_ldap_encoded(username, password);
      auto r = sess.post_form(
          base_url_ + "/jsxsd/xk/LoginToXkLdap",
          {
              {"loginMethod", "LoginToXkLdap"},
              {"userAccount", username},
              {"userPassword", ""},
              {"RANDOMCODE", code},
              {"encoded", encoded},
          },
          {
              {"Origin", base_url_},
              {"Referer", base_url_ + "/jsxsd/"},
          },
          true,
          20000);

      if (r.status == 0) {
        any_net_error = true;
      } else {
        std::string body = r.text_utf8();
        logged_in = (r.status == 200 && r.final_url.find("xsMain") != std::string::npos) ||
                    (r.status == 200 && body.find("xsMain") != std::string::npos &&
                     body.find("loginForm") == std::string::npos);

        if (!logged_in) {
          static const std::regex re_msg(R"(id\s*=\s*["']showMsg["'][^>]*>([\s\S]*?)</font>)", std::regex::icase);
          std::smatch m;
          std::string srv_msg = std::regex_search(body, m, re_msg)
                                    ? utils::strip_html_tags(m[1].str(), false)
                                    : "";
          last_srv_msg = srv_msg;
          if (srv_msg.empty() && r.status == 200 && !looks_logged_out_qz(r.status, r.final_url, body) &&
              has_qz_session_cookie(sess)) {
            auto r_chk = sess.get(base_url_ + "/jsxsd/framework/xsMain.jsp", {}, true, 8000);
            if (r_chk.status == 200 && !looks_logged_out_qz(r_chk.status, r_chk.final_url, r_chk.text_utf8())) {
              logged_in = true;
            }
          }
          if (!logged_in && (srv_msg.find("密码") != std::string::npos || srv_msg.find("账号") != std::string::npos ||
              srv_msg.find("用户名") != std::string::npos)) {
            throw JwError(srv_msg, "auth", "bad_cred");
          }
          if (!logged_in && (srv_msg.find("锁") != std::string::npos || srv_msg.find("冻结") != std::string::npos)) {
            throw JwError(srv_msg, "auth", "locked");
          }
          if (!logged_in && srv_msg.find("验证码") != std::string::npos) {
            emit(14, 1, "正在切换统一身份认证通道重试…", "尝试免验证码 SSO 登录");
            net::HttpSession cas_sess(polite_delay_ms_);
            if (seed_cookies.is_array() && !seed_cookies.empty()) {
              cas_sess.jar().merge_json(seed_cookies, "authserver.hainanu.edu.cn");
            }
            bool cas_net = false;
            if (try_cas_sso_login(cas_sess, username, password, &cas_net)) {
              sess.jar().load_json(cas_sess.jar().dump_json());
              logged_in = true;
            } else if (cas_net) {
              any_net_error = true;
            } else {
              throw JwError("验证码输入不正确或已失效，已为您自动切换新验证码", "captcha", "bad_cap");
            }
          }
        }
      }
    }
  }

  if (!logged_in) {
    emit(14, 1, "正在尝试统一身份认证直通登录…", "对接 authserver.hainanu.edu.cn");
    net::HttpSession cas_sess(polite_delay_ms_);
    if (seed_cookies.is_array() && !seed_cookies.empty()) {
      // 仅复用 authserver 长效 CASTGC，确保 jxgl 从干净会话启动 SSO，不被旧 bzb_jsxsd/route 干扰
      cas_sess.jar().merge_json(seed_cookies, "authserver.hainanu.edu.cn");
    }
    bool cas_net = false;
    if (try_cas_sso_login(cas_sess, username, password, &cas_net)) {
      sess.jar().load_json(cas_sess.jar().dump_json());
      logged_in = true;
    } else if (cas_net) {
      any_net_error = true;
    }
  }

  if (!logged_in) {
    if (any_net_error) {
      throw JwError("无法连接海南大学教务系统（网络超时，若本机开启了代理/VPN 请将 hainanu.edu.cn 设为直连后重试）",
                    "net", "login_net_timeout");
    }
    if (!last_srv_msg.empty()) {
      throw JwError("教务系统提示：" + last_srv_msg + "（已自动刷新右侧验证码）", "captcha", "srv_msg");
    }
    if (was_offline_cap) {
      throw JwError("刚才显示的为离线占位验证码，请先确认能访问海大官网后点击右侧刷新真实验证码",
                    "captcha", "offline_cap");
    }
    if (was_expired_cap) {
      throw JwError("验证码停留超时，已为您自动更新，请输入右侧新验证码后登录", "captcha", "cap_expired");
    }
    throw JwError("需要输入右侧教务图形验证码完成身份校验（已为您刷新最新验证码）", "captcha", "need_fresh_cap");
  }

  // 登录成功后把现有 seed_cookies 中的 CASTGC 与最新教务 Cookie 一并保存，延长后续无感续期有效期！
  if (out_cookies_to_save) {
    *out_cookies_to_save = sess.jar().dump_json();
  }
  return run_full_sync_on_session(sess, username, progress);
}

nlohmann::json QiangzhiClient::sync_with_saved(const std::string& username,
                                               const std::string& password,
                                               const nlohmann::json& saved_cookies,
                                               nlohmann::json* out_updated_cookies,
                                               SyncProgressCallback progress) {
  if (enable_demo_ && SandboxProvider::is_demo_account(username)) {
    return login_and_sync(username, password, "", "", out_updated_cookies, progress);
  }

  if (progress) {
    progress(8, 1, 6, "正在连接海南大学教务系统…", "复用已加密保存的教务会话");
  }

  net::HttpSession sess(polite_delay_ms_);
  sess.jar().load_json(saved_cookies);

  if (has_qz_session_cookie(sess)) {
    try {
      auto out = run_full_sync_on_session(sess, username, progress);
      if (out_updated_cookies) *out_updated_cookies = sess.jar().dump_json();
      return out;
    } catch (const JwError& e) {
      // 网络错误直接向上抛出，绝不覆盖有效会话状态！
      if (e.kind() != "expired") throw;
    }
  }

  if (progress) {
    progress(14, 1, 6, "正在自动续期教务会话…", "通过统一身份认证静默重连");
  }
  // 使用干净的教务 Cookie + 已有 authserver CASTGC 发起 SSO 续期，避免旧 bzb_jsxsd/route 干扰
  net::HttpSession cas_sess(polite_delay_ms_);
  cas_sess.jar().merge_json(saved_cookies, "authserver.hainanu.edu.cn");
  bool net_err = false;
  if ((!password.empty() || !cas_sess.jar().get_cookie("CASTGC").empty()) &&
      try_cas_sso_login(cas_sess, username, password, &net_err)) {
    auto out = run_full_sync_on_session(cas_sess, username, progress);
    if (out_updated_cookies) *out_updated_cookies = cas_sess.jar().dump_json();
    return out;
  }

  if (net_err) {
    throw JwError("连接海南大学教务系统超时（已完整保留本地登录与课表缓存，请检查网络或代理后重试）",
                  "net", "sync_net_timeout");
  }
  throw JwError("教务系统单次会话已超时且触发图形风控，请输入一次验证码恢复同步", "expired", "relogin_need_cap");
}

nlohmann::json QiangzhiClient::fetch_timetable_for(const std::string& username,
                                                   const std::string& password,
                                                   const nlohmann::json& saved_cookies,
                                                   const std::string& semester_id,
                                                   nlohmann::json* out_updated_cookies) {
  if (enable_demo_ && SandboxProvider::is_demo_account(username)) {
    return SandboxProvider::demo_timetable(semester_id);
  }
  net::HttpSession sess(polite_delay_ms_);
  sess.jar().load_json(saved_cookies);
  ensure_session_or_relogin(sess, username, password, out_updated_cookies);

  auto do_req = [&]() {
    return sess.post_form(base_url_ + "/jsxsd/xskb/xskb_list.do",
                          {{"xnxq01id", semester_id}, {"zc", ""}},
                          {{"Referer", base_url_ + "/jsxsd/xskb/xskb_list.do"}});
  };
  auto r = do_req();
  if (r.status == 0) {
    throw JwError("查询课表网络超时（请检查网络或代理设置后重试）", "net", "tt_for_net");
  }
  if (looks_logged_out_qz(r.status, r.final_url, r.text_utf8())) {
    bool net_err = false;
    if (try_isolated_cas_relogin(sess, username, password, out_updated_cookies, &net_err)) {
      r = do_req();
    } else if (net_err) {
      throw JwError("查询课表网络超时（请检查网络或代理设置后重试）", "net", "tt_relogin_net");
    }
  }
  if (r.status == 0) {
    throw JwError("查询课表网络超时（请检查网络或代理设置后重试）", "net", "tt_for_net2");
  }
  if (looks_logged_out_qz(r.status, r.final_url, r.text_utf8())) {
    throw JwError("教务登录会话已超时，请重新同步", "expired");
  }
  return parse_qiangzhi_timetable_html(r.text_utf8(), semester_id);
}

nlohmann::json QiangzhiClient::fetch_exams_for(const std::string& username,
                                               const std::string& password,
                                               const nlohmann::json& saved_cookies,
                                               const std::string& semester_id,
                                               nlohmann::json* out_updated_cookies) {
  if (enable_demo_ && SandboxProvider::is_demo_account(username)) {
    return SandboxProvider::demo_exams(semester_id);
  }
  net::HttpSession sess(polite_delay_ms_);
  sess.jar().load_json(saved_cookies);
  ensure_session_or_relogin(sess, username, password, out_updated_cookies);

  auto do_req = [&]() {
    return sess.post_form(base_url_ + "/jsxsd/xsks/xsksap_list",
                          {{"xqlbmc", ""}, {"xnxqid", semester_id}, {"xqlb", ""}},
                          {{"Referer", base_url_ + "/jsxsd/xsks/xsksap_query"}});
  };
  auto r = do_req();
  if (r.status == 0) {
    throw JwError("查询考场安排网络超时（请检查网络或代理设置后重试）", "net", "ex_for_net");
  }
  if (looks_logged_out_qz(r.status, r.final_url, r.text_utf8())) {
    bool net_err = false;
    if (try_isolated_cas_relogin(sess, username, password, out_updated_cookies, &net_err)) {
      r = do_req();
    } else if (net_err) {
      throw JwError("查询考场安排网络超时（请检查网络或代理设置后重试）", "net", "ex_relogin_net");
    }
  }
  if (r.status == 0) {
    throw JwError("查询考场安排网络超时（请检查网络或代理设置后重试）", "net", "ex_for_net2");
  }
  if (looks_logged_out_qz(r.status, r.final_url, r.text_utf8())) {
    throw JwError("教务登录会话已超时，请重新同步", "expired");
  }
  auto out = parse_qiangzhi_exams_html(r.text_utf8());
  out["semesterId"] = semester_id;
  return out;
}

nlohmann::json QiangzhiClient::fetch_semesters(const std::string& username,
                                               const std::string& password,
                                               const nlohmann::json& saved_cookies,
                                               nlohmann::json* out_updated_cookies) {
  if (enable_demo_ && SandboxProvider::is_demo_account(username)) {
    return SandboxProvider::demo_semesters();
  }
  net::HttpSession sess(polite_delay_ms_);
  sess.jar().load_json(saved_cookies);
  ensure_session_or_relogin(sess, username, password, out_updated_cookies);

  auto r = sess.get(base_url_ + "/jsxsd/xskb/xskb_list.do");
  if (r.status == 0) {
    throw JwError("读取学期列表网络超时", "net");
  }
  if (looks_logged_out_qz(r.status, r.final_url, r.text_utf8())) {
    bool net_err = false;
    if (try_isolated_cas_relogin(sess, username, password, out_updated_cookies, &net_err)) {
      r = sess.get(base_url_ + "/jsxsd/xskb/xskb_list.do");
    } else if (net_err) {
      throw JwError("读取学期列表网络超时", "net");
    }
  }
  if (r.status == 0) {
    throw JwError("读取学期列表网络超时", "net");
  }
  if (looks_logged_out_qz(r.status, r.final_url, r.text_utf8())) {
    throw JwError("教务登录会话已超时", "expired");
  }
  return parse_qiangzhi_semesters_html(r.text_utf8());
}

nlohmann::json QiangzhiClient::fetch_jw_todo(const std::string& username,
                                             const std::string& password,
                                             const nlohmann::json& saved_cookies,
                                             nlohmann::json* out_updated_cookies) {
  if (enable_demo_ && SandboxProvider::is_demo_account(username)) {
    return SandboxProvider::demo_jw_todo();
  }
  net::HttpSession sess(polite_delay_ms_);
  sess.jar().load_json(saved_cookies);
  ensure_session_or_relogin(sess, username, password, out_updated_cookies);

  auto r_xk = sess.get(base_url_ + "/jsxsd/xsxk/xklc_list");
  if (r_xk.status == 0) {
    throw JwError("探测教务通道网络超时（请检查网络或代理设置）", "net");
  }
  if (looks_logged_out_qz(r_xk.status, r_xk.final_url, r_xk.text_utf8())) {
    bool net_err = false;
    if (try_isolated_cas_relogin(sess, username, password, out_updated_cookies, &net_err)) {
      r_xk = sess.get(base_url_ + "/jsxsd/xsxk/xklc_list");
    } else if (net_err) {
      throw JwError("探测教务通道网络超时（请检查网络或代理设置）", "net");
    } else {
      throw JwError("教务登录会话已超时", "expired");
    }
  }
  if (r_xk.status == 0) {
    throw JwError("探测教务通道网络超时", "net");
  }
  if (looks_logged_out_qz(r_xk.status, r_xk.final_url, r_xk.text_utf8())) {
    throw JwError("教务登录会话已超时", "expired");
  }

  nlohmann::json items = nlohmann::json::array();
  if (r_xk.status == 200 && !looks_logged_out_qz(r_xk.status, r_xk.final_url, r_xk.text_utf8())) {
    std::string t = utils::strip_html_tags(r_xk.text_utf8());
    bool has_enter = (t.find("进入选课") != std::string::npos);
    items.push_back({
        {"key", "elect"},
        {"name", "本科生选课"},
        {"state", has_enter ? "open" : "closed"},
        {"text", has_enter ? "有正在开放的选课轮次" : "当前无开放的选课轮次"},
        {"detail", ""},
    });
  }
  auto r_pj = sess.get(base_url_ + "/jsxsd/xspj/xspj_find.do");
  if (r_pj.status == 200 && !looks_logged_out_qz(r_pj.status, r_pj.final_url, r_pj.text_utf8())) {
    std::string t = utils::strip_html_tags(r_pj.text_utf8());
    bool open = (t.find("进入评价") != std::string::npos || t.find("评价批次") != std::string::npos);
    items.push_back({
        {"key", "evaluate"},
        {"name", "期末教学评价"},
        {"state", open ? "open" : "closed"},
        {"text", open ? "教学评价通道已开放" : "当前不在评教时段"},
        {"detail", ""},
    });
  }
  return {{"items", items}};
}

}  // namespace hnu::adapters
