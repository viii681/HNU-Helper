#include "hnu/adapters/notice_crawler.hpp"
#include "hnu/net/http_client.hpp"
#include "hnu/utils/strings_and_time.hpp"

#include <algorithm>
#include <atomic>
#include <future>
#include <mutex>
#include <regex>
#include <set>

namespace hnu::adapters {

namespace {

bool contains_any(std::string_view text, const std::vector<const char*>& words) {
  for (const char* w : words) {
    if (text.find(w) != std::string_view::npos) return true;
  }
  return false;
}

std::string strip_scripts_and_styles(std::string_view html) {
  std::string low = utils::to_lower(html);
  std::string out;
  out.reserve(html.size());
  size_t pos = 0;
  while (pos < html.size()) {
    size_t lt = low.find('<', pos);
    if (lt == std::string::npos) {
      out.append(html.substr(pos));
      break;
    }
    out.append(html.substr(pos, lt - pos));
    if (low.compare(lt, 7, "<script") == 0) {
      size_t end = low.find("</script>", lt + 7);
      pos = (end == std::string::npos) ? html.size() : (end + 9);
    } else if (low.compare(lt, 6, "<style") == 0) {
      size_t end = low.find("</style>", lt + 6);
      pos = (end == std::string::npos) ? html.size() : (end + 8);
    } else if (low.compare(lt, 4, "<!--") == 0) {
      size_t end = low.find("-->", lt + 4);
      pos = (end == std::string::npos) ? html.size() : (end + 3);
    } else {
      out.push_back(html[lt]);
      pos = lt + 1;
    }
  }
  return out;
}

std::string find_date_near(std::string_view page, size_t start, size_t end, size_t window = 360) {
  static const std::regex re_date(R"((20\d{2})\s*[-/.年]\s*(\d{1,2})\s*[-/.月]\s*(\d{1,2}))");
  static const std::regex re_ym_day(R"((20\d{2})[-/.年](\d{1,2})\D{1,24}(\d{1,2}))");
  static const std::regex re_day_ym(R"(\b(\d{1,2})\b\D{1,24}(20\d{2})[-/.年](\d{1,2}))");

  auto search_slice = [&](size_t s, size_t len) -> std::string {
    std::string clean = utils::strip_html_tags(page.substr(s, len), false);
    std::smatch m;
    if (std::regex_search(clean, m, re_date)) {
      int mo = std::stoi(m[2].str());
      int dy = std::stoi(m[3].str());
      if (mo >= 1 && mo <= 12 && dy >= 1 && dy <= 31) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%s-%02d-%02d", m[1].str().c_str(), mo, dy);
        return buf;
      }
    }
    if (std::regex_search(clean, m, re_day_ym)) {
      int dy = std::stoi(m[1].str());
      int mo = std::stoi(m[3].str());
      if (mo >= 1 && mo <= 12 && dy >= 1 && dy <= 31) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%s-%02d-%02d", m[2].str().c_str(), mo, dy);
        return buf;
      }
    }
    if (std::regex_search(clean, m, re_ym_day)) {
      int mo = std::stoi(m[2].str());
      int dy = std::stoi(m[3].str());
      if (mo >= 1 && mo <= 12 && dy >= 1 && dy <= 31) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%s-%02d-%02d", m[1].str().c_str(), mo, dy);
        return buf;
      }
    }
    return "";
  };

  // 1. 先检查 <a>...</a> 标签内部（部分 VSB9 模板将发布日期直接放在 <a> 内部）
  if (end > start) {
    std::string d_in = search_slice(start, end - start);
    if (!d_in.empty()) return d_in;
  }

  // 2. 其次匹配链接右侧日期（以 </li> / </tr> 为边界，避免串到下一条公告的日期）
  size_t after_len = std::min(window, page.size() - end);
  std::string after_low = utils::to_lower(page.substr(end, after_len));
  size_t stop_li = after_low.find("</li>");
  size_t stop_tr = after_low.find("</tr>");
  size_t bound = after_len;
  if (stop_li != std::string::npos) bound = std::min(bound, stop_li);
  if (stop_tr != std::string::npos) bound = std::min(bound, stop_tr);
  std::string d = search_slice(end, bound);
  if (!d.empty()) return d;

  // 3. 最后匹配链接左侧同一列表项/行内日期
  size_t before_start = (start > window) ? (start - window) : 0;
  std::string before_low = utils::to_lower(page.substr(before_start, start - before_start));
  size_t prev_li = before_low.rfind("<li");
  size_t prev_tr = before_low.rfind("<tr");
  size_t offset = 0;
  if (prev_li != std::string::npos) offset = std::max(offset, prev_li);
  if (prev_tr != std::string::npos) offset = std::max(offset, prev_tr);
  return search_slice(before_start + offset, (start - before_start) - offset);
}

}  // namespace

NoticeClassifyResult classify_hnu_notice(std::string_view title) {
  // 1. 海南特色最高优先级：台风防汛 + 停水停电断网交通管制 -> impact（绝不能被噪音过滤误杀！）
  static const std::vector<const char*> kTyphoonKw = {
      "台风", "防汛", "防风", "暴雨", "极端天气", "应急响应", "停课通知"};
  static const std::vector<const char*> kImpactKw = {
      "停水", "停电", "断网", "割接", "封楼", "闭馆", "维修", "检修",
      "调休", "停课", "暂停开放", "交通管制", "封路", "施工", "班车调整"};
  if (contains_any(title, kTyphoonKw)) {
    return {true, "impact", {"台风防汛", "影响出行"}, ""};
  }
  if (contains_any(title, kImpactKw)) {
    return {true, "impact", {"影响出行"}, ""};
  }

  // 2. 过滤与本科/研究生日常无关的噪音公告
  static const std::pair<const char*, std::vector<const char*>> kDrops[] = {
      {"采购招标", {"采购公告", "中标", "成交公告", "招标", "询价", "比选", "磋商", "废标"}},
      {"附属中小学", {"附属幼儿园", "附小", "附属小学"}},
      {"会议走访", {"来校调研", "一行到访", "走访慰问", "座谈交流", "工作例会"}},
      {"人事招聘", {"公开招聘事业编制", "博士后招收", "劳务派遣人员招聘"}},
  };
  for (const auto& [reason, kws] : kDrops) {
    if (contains_any(title, kws)) {
      return {false, "drop", {}, reason};
    }
  }

  // 3. 学生需要动手的教务/学工待办 -> action
  static const std::vector<const char*> kActionKw = {
      "选课", "报名", "缴费", "申报", "提交", "补办", "确认", "注册",
      "转专业", "推免", "免试攻读", "评教", "缓考", "重修", "补考",
      "四、六级", "四六级", "毕业论文", "毕业设计", "学籍异动", "奖学金",
      "助学金", "综合测评", "医保", "选拔"};
  if (contains_any(title, kActionKw)) {
    return {true, "action", {"要办事"}, ""};
  }

  // 4. 讲座竞赛文体活动 -> activity
  static const std::vector<const char*> kActivityKw = {
      "讲坛", "讲座", "报告会", "论坛", "大赛", "竞赛", "比赛", "文化节",
      "运动会", "晚会", "演出", "展览", "招募", "志愿服务", "社会实践", "沙龙"};
  if (contains_any(title, kActivityKw)) {
    return {true, "activity", {"活动"}, ""};
  }

  return {true, "other", {}, ""};
}

std::vector<nlohmann::json> parse_vsb_notice_list_html(std::string_view html,
                                                       std::string_view base_url,
                                                       std::string_view source_name,
                                                       std::string_view dept_name) {
  std::string page(html);
  std::vector<nlohmann::json> out;
  std::set<std::string> seen;

  // 海南大学博达 VSB9 站群通用文章链接模式：info/栏目号/文章号.htm
  static const std::regex re_info(R"(href\s*=\s*["']([^"']*?info/\d+/\d+\.htm)["'])",
                                  std::regex::icase);
  static const std::regex re_title_attr(R"(title\s*=\s*["']([^"']{4,160})["'])",
                                        std::regex::icase);
  static const std::regex re_anchor_inner(R"(<a\b[^>]*>([\s\S]*?)</a>)",
                                          std::regex::icase);

  for (std::sregex_iterator it(page.begin(), page.end(), re_info), end; it != end; ++it) {
    size_t pos = static_cast<size_t>((*it).position());
    size_t match_end = pos + static_cast<size_t>((*it).length());
    std::string full_url = utils::resolve_url(base_url, (*it)[1].str());
    if (seen.find(full_url) != seen.end()) continue;

    // 截取 <a>...</a> 片段提取完整标题
    size_t a_start = page.rfind("<a", pos);
    if (a_start == std::string::npos || pos - a_start > 200) a_start = pos;
    size_t a_end = page.find("</a>", match_end);
    size_t slice_len = (a_end != std::string::npos && a_end - a_start < 800)
                           ? (a_end - a_start + 4)
                           : std::min<size_t>(450, page.size() - a_start);
    std::string a_chunk = page.substr(a_start, slice_len);

    std::string title;
    std::smatch tm;
    if (std::regex_search(a_chunk, tm, re_title_attr)) {
      title = utils::strip_html_tags(tm[1].str(), false);
    } else if (std::regex_search(a_chunk, tm, re_anchor_inner)) {
      title = utils::strip_html_tags(tm[1].str(), false);
      // 去除内嵌的结尾日期如 "2026.11.24"
      static const std::regex re_tail_date(R"(\s*20\d{2}[./-]\d{1,2}[./-]\d{1,2}\s*$)");
      title = std::regex_replace(title, re_tail_date, "");
    }
    title = utils::trim(title);
    if (title.size() < 8) continue;

    std::string pub_date = find_date_near(page, a_start, a_start + slice_len);
    auto cls = classify_hnu_notice(title);
    seen.insert(full_url);

    out.push_back({
        {"title", title},
        {"url", full_url},
        {"date", pub_date},
        {"source", std::string(source_name)},
        {"dept", std::string(dept_name)},
        {"keep", cls.keep},
        {"tier", cls.tier},
        {"tags", cls.tags},
        {"drop", cls.drop_reason},
    });
  }
  return out;
}

nlohmann::json parse_hnu_notice_article_html(std::string_view html) {
  std::string full_str = strip_scripts_and_styles(html);
  std::string lower = utils::to_lower(full_str);

  // 优先定位博达站群标准正文容器 (vsb_content / vsb_content_2 / v_news_content / news_content / article_content)
  std::string content_slice;
  static const char* kMarkers[] = {
      "id=\"vsb_content", "id='vsb_content", "class=\"v_news_content", "class='v_news_content",
      "class=\"arti_content", "class='arti_content", "class=\"wp_articlecontent",
      "class=\"news_content", "class=\"article_content", "class=\"content_main"
  };
  for (const char* mk : kMarkers) {
    size_t mpos = lower.find(mk);
    if (mpos == std::string::npos) continue;
    size_t d_open = lower.rfind("<div", mpos);
    if (d_open == std::string::npos || mpos - d_open > 180) continue;
    size_t gt = lower.find('>', mpos);
    if (gt == std::string::npos) continue;

    // 按 <div> 嵌套深度配对闭合标签
    size_t scan = gt + 1;
    int depth = 1;
    while (depth > 0 && scan < lower.size()) {
      size_t lt = lower.find('<', scan);
      if (lt == std::string::npos) break;
      if (lt + 4 <= lower.size() && lower.substr(lt, 4) == "<div" &&
          (lt + 4 == lower.size() || lower[lt + 4] == '>' || std::isspace(static_cast<unsigned char>(lower[lt + 4])))) {
        ++depth;
        scan = lt + 4;
      } else if (lt + 5 <= lower.size() && lower.substr(lt, 5) == "</div") {
        --depth;
        if (depth == 0) {
          content_slice = full_str.substr(gt + 1, lt - gt - 1);
          break;
        }
        scan = lt + 5;
      } else {
        scan = lt + 1;
      }
    }
    if (!content_slice.empty()) break;
  }

  std::string all_txt = utils::strip_html_tags(full_str, true);
  auto head_lines = utils::split(all_txt, '\n');

  std::string pub, dept, when_txt, where_txt;
  static const std::regex re_pub(R"((?:发布时间|发布日期|日期)[：:]?\s*(20\d{2}[-/.年]\d{1,2}[-/.月]\d{1,2}))");
  static const std::regex re_dept(R"((?:来源|供稿|作者|发布单位)[：:]\s*([^\s｜|]{2,24}))");
  static const std::regex re_when(R"((?:活动时间|讲座时间|报告时间|比赛时间|报名时间|考试时间|停电时间|停水时间)[：:]\s*([^\n]{3,60}))");
  static const std::regex re_where(R"((?:活动地点|讲座地点|报告地点|会议地点|举办地点|地点)[：:]\s*([^\n]{2,60}))");

  for (size_t i = 0; i < head_lines.size() && i < 260; ++i) {
    const std::string& l = head_lines[i];
    std::smatch m;
    if (pub.empty() && std::regex_search(l, m, re_pub)) pub = m[1].str();
    if (dept.empty() && std::regex_search(l, m, re_dept)) dept = m[1].str();
    if (when_txt.empty() && std::regex_search(l, m, re_when)) when_txt = utils::trim(m[1].str());
    if (where_txt.empty() && std::regex_search(l, m, re_where)) where_txt = utils::trim(m[1].str());
  }

  std::string summary;
  if (!content_slice.empty()) {
    std::string body_clean = utils::strip_html_tags(content_slice, true);
    for (const auto& raw_l : utils::split(body_clean, '\n')) {
      std::string l = utils::trim(raw_l);
      if (l.empty()) continue;
      if (!summary.empty()) summary.push_back('\n');
      summary.append(l);
      if (summary.size() >= 850) {
        summary.append("…");
        break;
      }
    }
  }
  if (summary.empty()) {
    // 兜底：若非标准 VSB9 模板或正文容器内仅含图片/PDF，过滤导航短词提取正文长句
    for (const auto& raw_l : head_lines) {
      std::string l = utils::trim(raw_l);
      if (l.size() < 18) continue;
      if (l.find("版权所有") != std::string::npos || l.find("琼ICP备") != std::string::npos ||
          l.find("上一条") == 0 || l.find("下一条") == 0) {
        break;
      }
      if (!summary.empty()) summary.push_back('\n');
      summary.append(l);
      if (summary.size() >= 750) break;
    }
  }

  return {
      {"publish", pub},
      {"dept", dept},
      {"when", when_txt},
      {"where", where_txt},
      {"summary", summary},
  };
}

HnuNoticeCrawler::HnuNoticeCrawler(std::vector<std::string> allowed_domains)
    : allowed_domains_(std::move(allowed_domains)) {
  // 海南大学真实官方栏目地址
  sources_ = {
      {"重要通知", "https://ugs.hainanu.edu.cn/index/zytz.htm", "本科生院"},
      {"教务运转", "https://ugs.hainanu.edu.cn/rcpy/jwyz.htm", "本科生院"},
      {"学籍管理", "https://ugs.hainanu.edu.cn/rcpy/xjgl.htm", "本科生院"},
      {"教务动态", "https://ugs.hainanu.edu.cn/index/jwdt1.htm", "本科生院"},
      {"综合时讯", "https://www.hainanu.edu.cn/hdxw/zhsx.htm", "海南大学主站"},
      {"海大讲坛", "https://www.hainanu.edu.cn/xywh/hdjt/djkxjjt.htm", "学术讲坛"},
  };
}

nlohmann::json HnuNoticeCrawler::fetch_all_sources(NoticeProgressCallback progress) {
  const int total_sources = static_cast<int>(sources_.size());
  if (progress) {
    progress(10, 0, total_sources, "正在并发连接海南大学 6 个官方公告源…");
  }

  struct SourceOutcome {
    std::string name;
    int status = 0;
    std::vector<nlohmann::json> parsed;
  };

  std::atomic<int> done_count{0};
  std::mutex prog_mu;
  std::vector<SourceOutcome> outcomes;
  outcomes.reserve(sources_.size());

  // 分批（每批最多 3 并发）抓取海大 6 个公告源，避免同时向校内同一网关 IP 发起过多并发触发限流，同时保持 ~3 秒内完成
  constexpr size_t kBatchSize = 3;
  for (size_t base = 0; base < sources_.size(); base += kBatchSize) {
    std::vector<std::future<SourceOutcome>> futs;
    size_t end_idx = std::min(base + kBatchSize, sources_.size());
    futs.reserve(end_idx - base);
    for (size_t i = base; i < end_idx; ++i) {
      auto src = sources_[i];
      futs.push_back(std::async(std::launch::async, [src, &done_count, &prog_mu, &progress, total_sources]() {
        SourceOutcome out;
        out.name = src.name;
        net::HttpSession sess(0);
        auto r = sess.get(src.url, {}, true, 6500);
        out.status = r.status;
        if (r.status == 200 && !r.body.empty()) {
          out.parsed = parse_vsb_notice_list_html(r.text_utf8(), r.final_url, src.name, src.dept);
        }
        int cur = ++done_count;
        if (progress) {
          std::lock_guard<std::mutex> lk(prog_mu);
          int pct = 15 + (cur * 75) / std::max(1, total_sources);
          std::string status_txt = out.parsed.empty()
                                       ? ("栏目响应较慢：" + src.name)
                                       : ("已拉取：" + src.name + "（" + std::to_string(out.parsed.size()) + " 条）");
          progress(pct, cur, total_sources, status_txt);
        }
        return out;
      }));
    }
    for (auto& f : futs) {
      outcomes.push_back(f.get());
    }
  }

  std::vector<nlohmann::json> all_kept;
  nlohmann::json failed = nlohmann::json::array();
  std::set<std::string> seen_info_key;
  int total_raw = 0;

  for (auto& res : outcomes) {
    if (res.status != 200 || res.parsed.empty()) {
      failed.push_back({{"source", res.name}, {"status", res.status}, {"count", 0}});
      continue;
    }
    for (auto& item : res.parsed) {
      std::string url = item.value("url", "");
      std::string key = url;
      size_t p = url.find("/info/");
      if (p != std::string::npos) {
        key = utils::extract_host(url) + "|" + url.substr(p + 6);
      }
      if (!seen_info_key.insert(key).second) continue;
      ++total_raw;
      if (item.value("keep", true)) {
        all_kept.push_back(std::move(item));
      }
    }
  }

  std::sort(all_kept.begin(), all_kept.end(),
            [](const nlohmann::json& a, const nlohmann::json& b) {
              return a.value("date", "0000") > b.value("date", "0000");
            });

  if (progress) {
    progress(100, total_sources, total_sources,
             all_kept.empty() ? "官网源暂不可达，已保留本地缓存公告"
                              : ("已聚合 " + std::to_string(all_kept.size()) + " 条海大官方公告"));
  }

  return {
      {"ok", !all_kept.empty() || failed.size() < sources_.size()},
      {"totalRaw", total_raw},
      {"sourcesOk", static_cast<int>(sources_.size() - failed.size())},
      {"failed", failed},
      {"items", all_kept},
  };
}

nlohmann::json HnuNoticeCrawler::fetch_article_detail(const std::string& url) {
  if (!utils::is_http_url_allowed(url, allowed_domains_)) {
    return {{"ok", false}, {"error", "安全限制：仅允许查看海南大学校内站点（*.hainanu.edu.cn）的 HTTP/HTTPS 通知正文"}};
  }
  net::HttpSession sess(0);
  net::HttpRequestOptions opts;
  opts.method = "GET";
  opts.url = url;
  opts.follow_redirects = true;
  opts.max_redirects = 5;
  opts.timeout_ms = 9000;
  opts.allowed_redirect_domains = allowed_domains_;
  auto r = sess.request(opts);
  if (r.status != 200) {
    std::string reason;
    if (r.error.find("blocked") != std::string::npos || r.error.find("Blocked") != std::string::npos) {
      reason = "安全拦截：通知原文跳转到了非海南大学校内域名，已阻止访问";
    } else if (r.status == 0) {
      reason = "连接海南大学官网超时（请检查网络或代理设置后重试）";
    } else {
      reason = "打开通知原文失败（HTTP " + std::to_string(r.status) + "）";
    }
    return {{"ok", false}, {"error", reason}};
  }
  std::string html = r.text_utf8();
  auto detail = parse_hnu_notice_article_html(html);

  // 提取海大博达 VSB9 红头文件内嵌 PDF (showVsbpdfIframe / iframe / embed) 与附件下载列表 (download.jsp)
  nlohmann::json attachments = nlohmann::json::array();
  std::set<std::string> seen_att;
  static const std::regex re_pdf(R"(showVsbpdfIframe\s*\(\s*["']([^"']+\.pdf)["'])", std::regex::icase);
  std::smatch m_pdf;
  if (std::regex_search(html, m_pdf, re_pdf)) {
    std::string pdf_u = utils::resolve_url(r.final_url, m_pdf[1].str());
    if (seen_att.insert(pdf_u).second) {
      attachments.push_back({{"name", "📄 查看通知红头正文 PDF"}, {"url", pdf_u}});
    }
  }

  static const std::regex re_iframe_pdf(R"(<(?:iframe|embed)\b[^>]*\bsrc\s*=\s*["']([^"']+\.pdf[^"']*)["'])",
                                        std::regex::icase);
  for (std::sregex_iterator it(html.begin(), html.end(), re_iframe_pdf), end; it != end; ++it) {
    std::string pdf_u = utils::resolve_url(r.final_url, utils::html_unescape((*it)[1].str()));
    if (seen_att.insert(pdf_u).second) {
      attachments.push_back({{"name", "📄 查看内嵌 PDF 正文"}, {"url", pdf_u}});
    }
  }

  static const std::regex re_att(
      R"(<a[^>]*href\s*=\s*["']([^"']*?(?:download\.jsp[^"']*|\.(?:pdf|docx?|xlsx?|zip|rar)))["'][^>]*>([\s\S]*?)</a>)",
      std::regex::icase);
  for (std::sregex_iterator it(html.begin(), html.end(), re_att), end; it != end; ++it) {
    std::string a_url = utils::resolve_url(r.final_url, utils::html_unescape((*it)[1].str()));
    std::string a_name = utils::strip_html_tags((*it)[2].str(), false);
    if (a_name.size() < 3 || !seen_att.insert(a_url).second) continue;
    attachments.push_back({{"name", a_name}, {"url", a_url}});
    if (attachments.size() >= 12) break;
  }

  if (detail.value("summary", "").empty() && !attachments.empty()) {
    detail["summary"] = "本通知以海南大学官方红头 PDF 文件及附件形式发布，可直接点击下方文档查阅或下载：";
  }
  detail["attachments"] = attachments;
  detail["ok"] = true;
  detail["url"] = url;
  return detail;
}

}  // namespace hnu::adapters
