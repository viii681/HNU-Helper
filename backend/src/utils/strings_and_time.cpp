#include "hnu/utils/strings_and_time.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <regex>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <iconv.h>
#endif

namespace hnu::utils {

int64_t now_sec() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

int64_t now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

static std::tm to_bj_tm(int64_t unix_sec) {
  if (unix_sec <= 0) unix_sec = now_sec();
  time_t bj = static_cast<time_t>(unix_sec + 8 * 3600);
  std::tm out{};
#ifdef _WIN32
  gmtime_s(&out, &bj);
#else
  gmtime_r(&bj, &out);
#endif
  return out;
}

std::string bj_date_ymd(int64_t unix_sec) {
  std::tm tm = to_bj_tm(unix_sec);
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
                tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
  return buf;
}

std::string bj_datetime_str(int64_t unix_sec) {
  std::tm tm = to_bj_tm(unix_sec);
  char buf[40];
  std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
                tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                tm.tm_hour, tm.tm_min, tm.tm_sec);
  return buf;
}

std::string trim(std::string_view sv) {
  size_t b = 0;
  size_t e = sv.size();
  while (b < e && std::isspace(static_cast<unsigned char>(sv[b]))) ++b;
  while (e > b && std::isspace(static_cast<unsigned char>(sv[e - 1]))) --e;
  return std::string(sv.substr(b, e - b));
}

std::string to_lower(std::string_view sv) {
  std::string out(sv);
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return out;
}

std::vector<std::string> split(std::string_view sv, char delim) {
  std::vector<std::string> out;
  size_t start = 0;
  while (start <= sv.size()) {
    size_t pos = sv.find(delim, start);
    if (pos == std::string_view::npos) {
      out.emplace_back(sv.substr(start));
      break;
    }
    out.emplace_back(sv.substr(start, pos - start));
    start = pos + 1;
  }
  return out;
}

std::string replace_all(std::string_view src, std::string_view from, std::string_view to) {
  if (from.empty()) return std::string(src);
  std::string out;
  out.reserve(src.size());
  size_t pos = 0;
  while (pos < src.size()) {
    size_t f = src.find(from, pos);
    if (f == std::string_view::npos) {
      out.append(src.substr(pos));
      break;
    }
    out.append(src.substr(pos, f - pos));
    out.append(to);
    pos = f + from.size();
  }
  return out;
}

bool starts_with(std::string_view sv, std::string_view prefix) {
  return sv.size() >= prefix.size() && sv.substr(0, prefix.size()) == prefix;
}

bool ends_with(std::string_view sv, std::string_view suffix) {
  return sv.size() >= suffix.size() &&
         sv.substr(sv.size() - suffix.size(), suffix.size()) == suffix;
}

std::string url_encode(std::string_view value) {
  std::ostringstream escaped;
  escaped.fill('0');
  escaped << std::hex << std::uppercase;
  for (unsigned char c : value) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      escaped << static_cast<char>(c);
    } else {
      escaped << '%' << std::setw(2) << static_cast<int>(c);
    }
  }
  return escaped.str();
}

std::string url_decode(std::string_view value) {
  std::string out;
  out.reserve(value.size());
  for (size_t i = 0; i < value.size(); ++i) {
    char c = value[i];
    if (c == '+') {
      out.push_back(' ');
    } else if (c == '%' && i + 2 < value.size()) {
      auto hex_val = [](char ch) -> int {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
        if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
        return -1;
      };
      int hi = hex_val(value[i + 1]);
      int lo = hex_val(value[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
      } else {
        out.push_back(c);
      }
    } else {
      out.push_back(c);
    }
  }
  return out;
}

std::string build_form_urlencoded(const std::vector<std::pair<std::string, std::string>>& params) {
  std::string out;
  for (size_t i = 0; i < params.size(); ++i) {
    if (i > 0) out.push_back('&');
    out.append(url_encode(params[i].first));
    out.push_back('=');
    out.append(url_encode(params[i].second));
  }
  return out;
}

std::string extract_host(std::string_view url) {
  size_t scheme_end = url.find("://");
  size_t start = (scheme_end == std::string_view::npos) ? 0 : scheme_end + 3;
  size_t end = url.find_first_of("/?#\\", start);
  // 拒绝 authority 段中混入反斜杠（防范 Windows WinHttpCrackUrl 将 '\' 解析为路径分隔符导致的 SSRF 绕过）
  if (end != std::string_view::npos && url[end] == '\\') {
    return "";
  }
  std::string_view authority = (end == std::string_view::npos)
                                   ? url.substr(start)
                                   : url.substr(start, end - start);
  if (authority.empty()) return "";
  // 拒绝 userinfo ('@')、空白与控制字符，彻底封堵 http://127.0.0.1@hainanu.edu.cn/ 类绕过
  for (unsigned char c : authority) {
    if (c == '@' || c <= 0x20 || c == 0x7F) return "";
  }
  if (authority.front() == '[') {
    size_t close = authority.find(']');
    if (close == std::string_view::npos) return "";
    return to_lower(authority.substr(0, close + 1));
  }
  size_t colon = authority.find(':');
  if (colon != std::string_view::npos) authority = authority.substr(0, colon);
  return to_lower(authority);
}

bool host_matches_allowlist(std::string_view host, const std::vector<std::string>& allowed_domains) {
  std::string h = to_lower(trim(host));
  if (h.empty()) return false;
  for (const auto& dom_raw : allowed_domains) {
    std::string dom = to_lower(trim(dom_raw));
    if (dom.empty()) continue;
    if (h == dom) return true;
    if (h.size() > dom.size() + 1 &&
        h[h.size() - dom.size() - 1] == '.' &&
        ends_with(h, dom)) {
      return true;
    }
  }
  return false;
}

bool is_http_url_allowed(std::string_view url, const std::vector<std::string>& allowed_domains) {
  std::string trimmed = trim(url);
  if (trimmed.empty()) return false;
  for (unsigned char c : trimmed) {
    if (c < 0x20 || c == 0x7F) return false;
  }
  std::string low = to_lower(trimmed);
  if (!starts_with(low, "http://") && !starts_with(low, "https://")) {
    return false;
  }
  std::string host = extract_host(trimmed);
  if (host.empty()) return false;
  if (allowed_domains.empty()) return true;
  return host_matches_allowlist(host, allowed_domains);
}

std::string resolve_url(std::string_view base_url, std::string_view href) {
  std::string h = trim(href);
  if (h.empty()) return std::string(base_url);
  std::string h_low = to_lower(h);
  if (starts_with(h_low, "http://") || starts_with(h_low, "https://")) return h;
  size_t scheme_end = base_url.find("://");
  if (scheme_end == std::string_view::npos) return h;
  std::string_view scheme = base_url.substr(0, scheme_end);
  if (starts_with(h, "//")) return std::string(scheme) + ":" + h;

  size_t auth_start = scheme_end + 3;
  size_t path_start = base_url.find_first_of("/?#", auth_start);
  std::string origin = (path_start == std::string_view::npos)
                           ? std::string(base_url)
                           : std::string(base_url.substr(0, path_start));
  std::string base_path = "/";
  if (path_start != std::string_view::npos && base_url[path_start] == '/') {
    base_path = std::string(base_url.substr(path_start));
    size_t qpos = base_path.find_first_of("?#");
    if (qpos != std::string::npos) base_path = base_path.substr(0, qpos);
  }

  if (starts_with(h, "#")) {
    size_t frag = base_url.find('#');
    std::string base_no_frag = (frag == std::string_view::npos)
                                   ? std::string(base_url)
                                   : std::string(base_url.substr(0, frag));
    return base_no_frag + h;
  }
  if (starts_with(h, "?")) {
    return origin + base_path + h;
  }

  size_t h_qpos = h.find_first_of("?#");
  std::string h_path = (h_qpos == std::string::npos) ? h : h.substr(0, h_qpos);
  std::string h_suffix = (h_qpos == std::string::npos) ? "" : h.substr(h_qpos);

  std::string combined;
  if (starts_with(h_path, "/")) {
    combined = h_path;
  } else {
    size_t last_slash = base_path.rfind('/');
    std::string dir = (last_slash == std::string::npos) ? "/" : base_path.substr(0, last_slash + 1);
    combined = dir + h_path;
  }

  bool trailing_slash = ends_with(combined, "/") || ends_with(combined, "/.") ||
                        ends_with(combined, "/..") || combined == "." || combined == "..";
  // 归一化 ../ 与 ./（仅对 path 部分归一化，避免破坏 ?query 中的 '/' 或 '..'）
  std::vector<std::string> segs;
  for (const auto& seg : split(combined, '/')) {
    if (seg.empty() || seg == ".") continue;
    if (seg == "..") {
      if (!segs.empty()) segs.pop_back();
    } else {
      segs.push_back(seg);
    }
  }
  std::string norm_path = "/";
  for (size_t i = 0; i < segs.size(); ++i) {
    if (i > 0) norm_path.push_back('/');
    norm_path.append(segs[i]);
  }
  if (trailing_slash && norm_path != "/") norm_path.push_back('/');
  return origin + norm_path + h_suffix;
}

static void append_utf8_codepoint(std::string& out, uint32_t cp) {
  if (cp == 0xA0) {
    out.push_back(' ');  // 将 NBSP 统一归一化为空格
    return;
  }
  if (cp <= 0x7F) {
    out.push_back(static_cast<char>(cp));
  } else if (cp <= 0x7FF) {
    out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp <= 0xFFFF) {
    out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp <= 0x10FFFF) {
    out.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

std::string html_unescape(std::string_view html) {
  std::string out;
  out.reserve(html.size());
  for (size_t i = 0; i < html.size(); ++i) {
    // 顺手把 UTF-8 的 \xC2\xA0 (NBSP) 折成普通空格
    if (static_cast<unsigned char>(html[i]) == 0xC2 &&
        i + 1 < html.size() &&
        static_cast<unsigned char>(html[i + 1]) == 0xA0) {
      out.push_back(' ');
      ++i;
      continue;
    }
    if (html[i] != '&') {
      out.push_back(html[i]);
      continue;
    }
    size_t semi = html.find(';', i + 1);
    if (semi == std::string_view::npos || semi - i > 12) {
      out.push_back('&');
      continue;
    }
    std::string_view ent = html.substr(i + 1, semi - i - 1);
    if (ent == "nbsp") {
      out.push_back(' ');
    } else if (ent == "amp") {
      out.push_back('&');
    } else if (ent == "lt") {
      out.push_back('<');
    } else if (ent == "gt") {
      out.push_back('>');
    } else if (ent == "quot") {
      out.push_back('"');
    } else if (ent == "apos" || ent == "#39") {
      out.push_back('\'');
    } else if (ent == "mdash") {
      out.append("—");
    } else if (ent == "ndash") {
      out.append("–");
    } else if (ent == "middot") {
      out.append("·");
    } else if (!ent.empty() && ent[0] == '#') {
      uint32_t cp = 0;
      bool ok = false;
      try {
        if (ent.size() > 2 && (ent[1] == 'x' || ent[1] == 'X')) {
          cp = static_cast<uint32_t>(std::stoul(std::string(ent.substr(2)), nullptr, 16));
          ok = true;
        } else if (ent.size() > 1) {
          cp = static_cast<uint32_t>(std::stoul(std::string(ent.substr(1)), nullptr, 10));
          ok = true;
        }
      } catch (...) {
        ok = false;
      }
      if (ok && cp > 0) {
        append_utf8_codepoint(out, cp);
      } else {
        out.append(html.substr(i, semi - i + 1));
      }
    } else {
      out.append(html.substr(i, semi - i + 1));
    }
    i = semi;
  }
  return out;
}

static std::string strip_html_block_ci(std::string_view src,
                                       std::string_view open_prefix,
                                       std::string_view close_tag,
                                       std::string_view replacement) {
  std::string lower = to_lower(src);
  std::string out;
  out.reserve(src.size());
  size_t pos = 0;
  while (pos < src.size()) {
    size_t open = lower.find(open_prefix, pos);
    if (open == std::string::npos) {
      out.append(src.substr(pos));
      break;
    }
    // 确认标签名边界（非更长标签名前缀，注释除外）
    if (open_prefix != "<!--") {
      size_t after = open + open_prefix.size();
      if (after < lower.size()) {
        char ch = lower[after];
        if (ch != '>' && ch != '/' && !std::isspace(static_cast<unsigned char>(ch))) {
          out.append(src.substr(pos, after - pos));
          pos = after;
          continue;
        }
      }
    }
    out.append(src.substr(pos, open - pos));
    size_t search_from = open + open_prefix.size();
    if (open_prefix != "<!--") {
      size_t gt = lower.find('>', search_from);
      if (gt == std::string::npos) break;
      search_from = gt + 1;
    }
    size_t close = lower.find(close_tag, search_from);
    out.append(replacement);
    if (close == std::string::npos) break;
    pos = close + close_tag.size();
  }
  return out;
}

std::string strip_html_tags(std::string_view html, bool preserve_newlines) {
  // 线性 O(n) 剔除 script / style / 注释块，避免 GCC libstdc++ std::regex 在大块脚本上栈溢出
  std::string s = strip_html_block_ci(html, "<script", "</script>", " ");
  s = strip_html_block_ci(s, "<style", "</style>", " ");
  s = strip_html_block_ci(s, "<!--", "-->", "");

  if (preserve_newlines) {
    static const std::regex re_br(R"(<(?:br\s*/?|/p|/div|/li|/tr|/h[1-6])\b[^>]*>)",
                                  std::regex::icase);
    s = std::regex_replace(s, re_br, "\n");
  }
  std::string clean;
  clean.reserve(s.size());
  bool in_tag = false;
  for (char c : s) {
    if (c == '<') {
      in_tag = true;
    } else if (c == '>') {
      in_tag = false;
    } else if (!in_tag) {
      clean.push_back(c);
    }
  }
  clean = html_unescape(clean);
  if (!preserve_newlines) {
    return collapse_whitespace(clean);
  }
  // 保留换行但清理每行首尾空白
  std::ostringstream oss;
  bool first = true;
  for (const auto& line : split(clean, '\n')) {
    std::string t = collapse_whitespace(line);
    if (!first) oss << '\n';
    oss << t;
    first = false;
  }
  return trim(oss.str());
}

std::string collapse_whitespace(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  bool prev_space = false;
  for (unsigned char c : text) {
    if (std::isspace(c)) {
      if (!prev_space) {
        out.push_back(' ');
        prev_space = true;
      }
    } else {
      out.push_back(static_cast<char>(c));
      prev_space = false;
    }
  }
  return trim(out);
}

std::vector<std::vector<std::vector<std::string>>> parse_html_tables(std::string_view html) {
  std::vector<std::vector<std::vector<std::string>>> tables;
  std::string lower = to_lower(html);
  size_t pos = 0;

  auto is_tag_at = [&](size_t idx, std::string_view tag_with_lt) -> bool {
    if (idx + tag_with_lt.size() > lower.size()) return false;
    if (std::string_view(lower).substr(idx, tag_with_lt.size()) != tag_with_lt) return false;
    if (idx + tag_with_lt.size() == lower.size()) return true;
    char next = lower[idx + tag_with_lt.size()];
    return next == '>' || next == '/' || std::isspace(static_cast<unsigned char>(next));
  };

  while (pos < lower.size()) {
    size_t t_open = lower.find("<table", pos);
    if (t_open == std::string::npos) break;
    if (!is_tag_at(t_open, "<table")) {
      pos = t_open + 6;
      continue;
    }

    size_t gt = lower.find('>', t_open + 6);
    if (gt == std::string::npos) break;

    // 找到与当前 <table 配对的 </table>
    size_t scan = gt + 1;
    int depth = 1;
    size_t t_close = std::string::npos;
    while (depth > 0 && scan < lower.size()) {
      size_t next_lt = lower.find('<', scan);
      if (next_lt == std::string::npos) break;
      if (is_tag_at(next_lt, "<table")) {
        ++depth;
        scan = next_lt + 6;
      } else if (is_tag_at(next_lt, "</table")) {
        --depth;
        if (depth == 0) t_close = next_lt;
        scan = next_lt + 7;
      } else {
        scan = next_lt + 1;
      }
    }
    if (t_close == std::string::npos) break;

    // 仅采集属于当前这一层 table（nested == 0）的直接 <tr> 与直接 <td>/<th>，
    // 这样当外层布局 table（如强智 #xjkpTable）包裹内层子 table 时，内外两层都能被完整无损提取！
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> cur_cells;
    bool in_own_tr = false;
    bool in_own_cell = false;
    size_t cell_content_start = 0;
    int nested_tbl = 0;

    size_t cur = gt + 1;
    while (cur < t_close) {
      size_t lt = lower.find('<', cur);
      if (lt == std::string::npos || lt >= t_close) break;
      size_t tag_end = lower.find('>', lt + 1);
      if (tag_end == std::string::npos || tag_end > t_close + 16) break;

      if (is_tag_at(lt, "<table")) {
        ++nested_tbl;
      } else if (is_tag_at(lt, "</table")) {
        if (nested_tbl > 0) --nested_tbl;
      } else if (nested_tbl == 0) {
        if (is_tag_at(lt, "<tr")) {
          in_own_tr = true;
          cur_cells.clear();
          in_own_cell = false;
        } else if (is_tag_at(lt, "</tr")) {
          if (in_own_cell) {
            cur_cells.push_back(strip_html_tags(html.substr(cell_content_start, lt - cell_content_start), true));
            in_own_cell = false;
          }
          if (in_own_tr && !cur_cells.empty()) {
            rows.push_back(std::move(cur_cells));
          }
          cur_cells.clear();
          in_own_tr = false;
        } else if (in_own_tr && (is_tag_at(lt, "<td") || is_tag_at(lt, "<th"))) {
          if (in_own_cell) {
            cur_cells.push_back(strip_html_tags(html.substr(cell_content_start, lt - cell_content_start), true));
          }
          in_own_cell = true;
          cell_content_start = tag_end + 1;
        } else if (in_own_cell && (is_tag_at(lt, "</td") || is_tag_at(lt, "</th"))) {
          cur_cells.push_back(strip_html_tags(html.substr(cell_content_start, lt - cell_content_start), true));
          in_own_cell = false;
        }
      }
      cur = tag_end + 1;
    }

    if (!rows.empty()) {
      tables.push_back(std::move(rows));
    }
    // 推进到当前 <table 开标签之后（而非 t_close 之后），以便继续解析其内部嵌套的子 table！
    pos = gt + 1;
  }
  return tables;
}

static bool is_valid_utf8(std::string_view s) {
  const auto* bytes = reinterpret_cast<const unsigned char*>(s.data());
  size_t n = s.size();
  size_t i = 0;
  while (i < n) {
    unsigned char c = bytes[i];
    if (c <= 0x7F) {
      ++i;
    } else if ((c & 0xE0) == 0xC0) {
      if (i + 1 >= n || (bytes[i + 1] & 0xC0) != 0x80 || c < 0xC2) return false;
      i += 2;
    } else if ((c & 0xF0) == 0xE0) {
      if (i + 2 >= n || (bytes[i + 1] & 0xC0) != 0x80 || (bytes[i + 2] & 0xC0) != 0x80)
        return false;
      unsigned char b1 = bytes[i + 1];
      // 拒绝 3 字节过长编码 (E0 80..9F) 与 UTF-16 代理区 (ED A0..BF)
      if ((c == 0xE0 && b1 < 0xA0) || (c == 0xED && b1 >= 0xA0)) return false;
      i += 3;
    } else if ((c & 0xF8) == 0xF0) {
      if (i + 3 >= n || (bytes[i + 1] & 0xC0) != 0x80 ||
          (bytes[i + 2] & 0xC0) != 0x80 || (bytes[i + 3] & 0xC0) != 0x80 || c > 0xF4)
        return false;
      unsigned char b1 = bytes[i + 1];
      // 拒绝 4 字节过长编码 (F0 80..8F) 与超出 U+10FFFF (F4 90..BF)
      if ((c == 0xF0 && b1 < 0x90) || (c == 0xF4 && b1 > 0x8F)) return false;
      i += 4;
    } else {
      return false;
    }
  }
  return true;
}

std::string ensure_utf8(std::string_view raw_bytes) {
  if (raw_bytes.empty() || is_valid_utf8(raw_bytes)) {
    return std::string(raw_bytes);
  }
#ifdef _WIN32
  int wlen = MultiByteToWideChar(54936 /* GB18030 */, 0,
                                 raw_bytes.data(), static_cast<int>(raw_bytes.size()),
                                 nullptr, 0);
  if (wlen > 0) {
    std::wstring wbuf(static_cast<size_t>(wlen), L'\0');
    MultiByteToWideChar(54936, 0, raw_bytes.data(), static_cast<int>(raw_bytes.size()),
                        wbuf.data(), wlen);
    int ulen = WideCharToMultiByte(CP_UTF8, 0, wbuf.data(), wlen, nullptr, 0, nullptr, nullptr);
    if (ulen > 0) {
      std::string ubuf(static_cast<size_t>(ulen), '\0');
      WideCharToMultiByte(CP_UTF8, 0, wbuf.data(), wlen, ubuf.data(), ulen, nullptr, nullptr);
      if (is_valid_utf8(ubuf)) return ubuf;
    }
  }
#else
  iconv_t cd = iconv_open("UTF-8//IGNORE", "GB18030");
  if (cd == reinterpret_cast<iconv_t>(-1)) {
    cd = iconv_open("UTF-8", "GB18030");
  }
  if (cd == reinterpret_cast<iconv_t>(-1)) {
    cd = iconv_open("UTF-8", "GBK");
  }
  if (cd != reinterpret_cast<iconv_t>(-1)) {
    std::string in_buf(raw_bytes);
    char* in_ptr = in_buf.data();
    size_t in_left = in_buf.size();
    std::string out;
    out.reserve(raw_bytes.size() * 2);
    char chunk[4096];
    bool ok = true;
    while (in_left > 0) {
      char* out_ptr = chunk;
      size_t out_left = sizeof(chunk);
      size_t rc = iconv(cd, &in_ptr, &in_left, &out_ptr, &out_left);
      out.append(chunk, sizeof(chunk) - out_left);
      if (rc == static_cast<size_t>(-1)) {
        if (errno == E2BIG) continue;
        if (errno == EILSEQ || errno == EINVAL) {
          out.push_back('?');
          ++in_ptr;
          --in_left;
          continue;
        }
        ok = false;
        break;
      }
    }
    iconv_close(cd);
    if (ok && is_valid_utf8(out)) return out;
  }
#endif
  // 兜底：保留合法 ASCII 与可识别序列，替换坏字节避免 JSON 序列化抛异常
  std::string safe;
  safe.reserve(raw_bytes.size());
  for (unsigned char c : raw_bytes) {
    safe.push_back(c < 0x80 ? static_cast<char>(c) : '?');
  }
  return safe;
}

std::string mask_account(std::string_view account) {
  std::string s = trim(account);
  if (s.size() >= 8) {
    return s.substr(0, 4) + "****" + s.substr(s.size() - 2);
  }
  if (s.size() >= 4) {
    return s.substr(0, 2) + "**" + s.substr(s.size() - 1);
  }
  return s;
}

std::string mask_ip(std::string_view ip) {
  std::string s = trim(ip);
  auto parts = split(s, '.');
  if (parts.size() == 4) {
    return parts[0] + "." + parts[1] + ".*.*";
  }
  if (s.find(':') != std::string::npos) {
    auto v6 = split(s, ':');
    if (v6.size() >= 2) return v6[0] + ":" + v6[1] + "::*";
  }
  return "*.*.*.*";
}

std::string scrub_pii(std::string_view raw, size_t max_len) {
  // 敏感信息脱敏替换顺序：
  // 1. 已打码学号 (2024****28) -> #
  // 2. 邮箱 -> #
  // 3. 敏感键名 (password/pwd/token/sign/auth/session/cookie/encoded/RANDOMCODE) -> key=#
  // 4. IPv4 -> a.b.*.*
  // 5. 连续 5 位以上数字（学号、手机号、卡号、单号） -> #
  std::string s = ensure_utf8(raw);
  static const std::regex r_masked(R"(\d{3,4}\*{2,}\d{2,4})");
  static const std::regex r_email(R"([A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+)");
  static const std::regex r_kv(
      R"(\b(password|passwd|pwd|userPassword|encoded|token|ics_token|sign|auth|Authorization|session|JSESSIONID|bzb_jsxsd|CASTGC|cookie|STUDY_SESS|NTESSTUDYSI|uid|userId)("?\s*[=:]\s*(?:(?:Bearer|Basic)\s+)?["']?)([^&\s"',;]+))",
      std::regex::icase);
  static const std::regex r_ip(R"(\b(\d{1,3}\.\d{1,3})\.\d{1,3}\.\d{1,3}\b)");
  static const std::regex r_digits(R"(\d{5,})");

  s = std::regex_replace(s, r_masked, "#");
  s = std::regex_replace(s, r_email, "#");
  s = std::regex_replace(s, r_kv, "$1$2#");
  s = std::regex_replace(s, r_ip, "$1.*.*");
  s = std::regex_replace(s, r_digits, "#");
  s = collapse_whitespace(s);
  if (max_len > 0 && s.size() > max_len) {
    // 按 UTF-8 码点边界安全截断，禁止劈断多字节中文字符导致 json::dump() 抛 type_error.316
    size_t cut = max_len;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) {
      --cut;
    }
    s.resize(cut);
  }
  return s;
}

}  // namespace hnu::utils
