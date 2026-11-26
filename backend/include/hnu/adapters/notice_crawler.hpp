#pragma once

#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace hnu::adapters {

using NoticeProgressCallback =
    std::function<void(int percent, int done, int total, const std::string& label)>;

struct NoticeClassifyResult {
  bool keep = true;
  std::string tier = "other";  // impact / action / activity / other / drop
  std::vector<std::string> tags;
  std::string drop_reason;
};

// 纯解析与分类函数（支持单元测试验证）
NoticeClassifyResult classify_hnu_notice(std::string_view title);
std::vector<nlohmann::json> parse_vsb_notice_list_html(std::string_view html,
                                                       std::string_view base_url,
                                                       std::string_view source_name,
                                                       std::string_view dept_name);
nlohmann::json parse_hnu_notice_article_html(std::string_view html);

// 海南大学多源公开通知抓取器（无需登录，抓海大主站、本科生院重要通知/教务运转/学籍管理、海大讲坛等实测源）
class HnuNoticeCrawler {
 public:
  explicit HnuNoticeCrawler(std::vector<std::string> allowed_domains = {"hainanu.edu.cn"});

  // 并发抓取全部海大通知源并去重分类，返回 {items, failed, sourcesOk, totalRaw}
  nlohmann::json fetch_all_sources(NoticeProgressCallback progress = nullptr);

  // 按需抓取单篇通知正文（严格校验 *.hainanu.edu.cn 域名防 SSRF）
  nlohmann::json fetch_article_detail(const std::string& url);

 private:
  struct SourceDef {
    std::string name;
    std::string url;
    std::string dept;
  };
  std::vector<SourceDef> sources_;
  std::vector<std::string> allowed_domains_;
};

}  // namespace hnu::adapters
