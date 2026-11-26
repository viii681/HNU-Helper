#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hnu::utils {

// 当前 Unix 时间戳（秒 / 毫秒）
int64_t now_sec();
int64_t now_ms();

// 北京时间（UTC+8）格式化：YYYY-MM-DD 与 YYYY-MM-DD HH:MM:SS
std::string bj_date_ymd(int64_t unix_sec = 0);
std::string bj_datetime_str(int64_t unix_sec = 0);

// 字符串工具
std::string trim(std::string_view sv);
std::string to_lower(std::string_view sv);
std::vector<std::string> split(std::string_view sv, char delim);
std::string replace_all(std::string_view src, std::string_view from, std::string_view to);
bool starts_with(std::string_view sv, std::string_view prefix);
bool ends_with(std::string_view sv, std::string_view suffix);

// URL 编解码与表单拼接
std::string url_encode(std::string_view value);
std::string url_decode(std::string_view value);
std::string build_form_urlencoded(const std::vector<std::pair<std::string, std::string>>& params);
std::string resolve_url(std::string_view base_url, std::string_view href);
std::string extract_host(std::string_view url);
bool host_matches_allowlist(std::string_view host, const std::vector<std::string>& allowed_domains);
bool is_http_url_allowed(std::string_view url, const std::vector<std::string>& allowed_domains = {});

// HTML 实体解码、去标签与表格提取（用于强智教务 #kbtable / #dataList 与海大通知页）
std::string html_unescape(std::string_view html);
std::string strip_html_tags(std::string_view html, bool preserve_newlines = false);
std::string collapse_whitespace(std::string_view text);
std::vector<std::vector<std::vector<std::string>>> parse_html_tables(std::string_view html);

// 编码兼容：若字节流不是合法 UTF-8，则自动按 GB18030/GBK 转码为 UTF-8（部分旧教务/院系子站可能返回 GBK）
std::string ensure_utf8(std::string_view raw_bytes);

// 隐私保护：学号/手机号脱敏展示（如 2024300128 -> 2024****28）与 IP 脱敏（10.21.*.*）
std::string mask_account(std::string_view account);
std::string mask_ip(std::string_view ip);

// 日志与反馈文本强脱敏（抹除学号、密码、令牌、Cookie、邮箱与长数字串）
std::string scrub_pii(std::string_view raw, size_t max_len = 400);

}  // namespace hnu::utils
