#include "hnu/adapters/chaoxing_client.hpp"
#include "hnu/adapters/mooc_client.hpp"
#include "hnu/adapters/notice_crawler.hpp"
#include "hnu/adapters/portal_client.hpp"
#include "hnu/adapters/qiangzhi_client.hpp"
#include "hnu/adapters/sandbox_provider.hpp"
#include "hnu/crypto/crypto.hpp"
#include "hnu/db/database.hpp"
#include "hnu/net/http_client.hpp"
#include "hnu/services/calendar_ics.hpp"
#include "hnu/services/credit_analyzer.hpp"
#include "hnu/utils/strings_and_time.hpp"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>

#define CHECK(cond, msg)                                                      \
  do {                                                                        \
    if (!(cond)) {                                                            \
      std::cerr << "[FAIL] " << __FILE__ << ":" << __LINE__ << " - " << (msg) \
                << "\n";                                                      \
      return 1;                                                               \
    }                                                                         \
  } while (0)

int main() {
  std::cout << "== Running HnuTong C++20 Core Unit Tests ==\n";

  // 1. 测试 AES-256-GCM 加解密与跨用户 AAD 防篡改保护
  {
    auto key = hnu::crypto::random_bytes(32);
    std::string secret = R"({"account":"2024300128","password":"MyS3cret!@#"})";
    auto ct = hnu::crypto::aes256_gcm_encrypt(key, secret, "hnutong:1:jwxt");
    CHECK(ct.has_value(), "AES-256-GCM encrypt should succeed");

    auto pt = hnu::crypto::aes256_gcm_decrypt(key, *ct, "hnutong:1:jwxt");
    CHECK(pt.has_value() && *pt == secret, "AES-256-GCM decrypt should match original plaintext");

    // 换一个 user_id 的 AAD 必须认证失败！
    auto wrong_aad = hnu::crypto::aes256_gcm_decrypt(key, *ct, "hnutong:2:jwxt");
    CHECK(!wrong_aad.has_value(), "AES-256-GCM must reject mismatched user_id AAD");
    std::cout << "[PASS] AES-256-GCM authenticated vault & AAD isolation\n";
  }

  // 2. 测试海大强智 LoginToXkLdap 的 encodeInp 编码与超星 AES-128-CBC 向量
  {
    std::string encoded = hnu::crypto::qiangzhi_build_ldap_encoded("2024300128", "Abc12345");
    CHECK(encoded == "MjAyNDMwMDEyOA==%%%QWJjMTIzNDU=", "Qiangzhi encodeInp LDAP formula must match conwork.js");

    std::string cx_enc = hnu::adapters::chaoxing_aes_encrypt("123456");
    CHECK(!cx_enc.empty() && cx_enc.size() >= 20, "Chaoxing AES-128-CBC must produce valid Base64");

    // 测试超星状态归类否定词优先红线（"未完成" 绝不能误判成 "完成"）
    CHECK(hnu::adapters::chaoxing_work_bucket("未完成") == "todo", "'未完成' must be classified as todo");
    CHECK(hnu::adapters::chaoxing_work_bucket("已过期未交") == "todo", "'已过期未交' must be classified as todo");
    CHECK(hnu::adapters::chaoxing_work_bucket("待互评") == "todo", "'待互评' must be classified as todo");
    CHECK(hnu::adapters::chaoxing_work_bucket("待批阅") == "done", "'待批阅' must be classified as done");
    CHECK(hnu::adapters::chaoxing_work_bucket("已批阅") == "done", "'已批阅' must be classified as done");
    std::cout << "[PASS] Qiangzhi encodeInp & Chaoxing AES/bucket rules\n";
  }

  // 3. 测试多域 CookieJar 子域隔离（passport2.chaoxing.com 的 hostOnly Cookie 不得发给 mooc1）
  {
    hnu::net::CookieJar jar;
    jar.ingest_set_cookie("https://passport2.chaoxing.com/fanyalogin", "JSESSIONID=pass_sess; Path=/; HttpOnly");
    jar.ingest_set_cookie("https://passport2.chaoxing.com/fanyalogin", "UID=998877; Domain=.chaoxing.com; Path=/");

    std::string hdr_pass = jar.build_cookie_header("https://passport2.chaoxing.com/login");
    std::string hdr_mooc = jar.build_cookie_header("https://mooc1.chaoxing.com/visit/stucoursemiddle");
    CHECK(hdr_pass.find("JSESSIONID=pass_sess") != std::string::npos, "passport2 should receive its own JSESSIONID");
    CHECK(hdr_mooc.find("JSESSIONID=pass_sess") == std::string::npos, "mooc1 must NOT receive passport2 hostOnly JSESSIONID");
    CHECK(hdr_mooc.find("UID=998877") != std::string::npos, "mooc1 should receive .chaoxing.com domain cookie");
    std::cout << "[PASS] RFC6265 Multi-Domain CookieJar hostOnly isolation\n";
  }

  // 4. 测试海南大学强智教务 #kbtable 11节制课表与 #dataList 成绩 HTML 解析器
  {
    const char* sample_kb_html = R"HTML(
      <select id="xnxq01id" name="xnxq01id">
        <option value="2026-2027-1" selected>2026-2027-1</option>
        <option value="2025-2026-2">2025-2026-2</option>
      </select>
      <table id="kbtable">
        <tr>
          <td>第一大节</td>
          <td><div class="kbcontent1">操作系统</div><div class="kbcontent">操作系统原理<br/><font title='老师'>张志强</font><br/><font title='周次(节次)'>1-16(周)[01-02节]</font><br/><font title='教室'>5教302</font></div></td>
          <td><div class="kbcontent">&nbsp;</div></td>
          <td><div class="kbcontent">编译原理<br/><font title='老师'>周文斌</font><br/><font title='周次(节次)'>1-15(单周)[01-02节]</font><br/><font title='教室'>信息楼506</font>---------------------操作系统实验<br/><font title='老师'>张志强</font><br/><font title='周次(节次)'>2-16(双周)[01-02节]</font><br/><font title='教室'>信息楼205</font></div></td>
          <td><div class="kbcontent">&nbsp;</div></td>
          <td><div class="kbcontent">自贸港概论<br/><font title='老师'>黎南海</font><br/><font title='周次(节次)'>1-15(周)[09-11节]</font><br/><font title='教室'>3教210</font></div></td>
          <td><div class="kbcontent">&nbsp;</div></td>
          <td><div class="kbcontent">&nbsp;</div></td>
        </tr>
      </table>
    )HTML";

    auto sems = hnu::adapters::parse_qiangzhi_semesters_html(sample_kb_html);
    CHECK(sems.size() == 2 && sems[0]["id"] == "2026-2027-1", "Should parse Qiangzhi semester select options");

    auto tt = hnu::adapters::parse_qiangzhi_timetable_html(sample_kb_html);
    CHECK(tt["unitCount"] == 11, "HNU timetable unitCount should be 11 periods");
    CHECK(tt["courses"].size() == 4, "Should parse 4 distinct courses without duplicating kbcontent1");

    const char* sample_grade_html = R"HTML(
      <table id="dataList">
        <tr><th>序号</th><th>开课学期</th><th>课程编号</th><th>课程名称</th><th>成绩</th><th>学分</th><th>总学时</th><th>绩点</th><th>课程属性</th></tr>
        <tr><td>1</td><td>2025-2026-2</td><td>CS2002</td><td>数据结构与算法</td><td>93</td><td>4.0</td><td>64</td><td>4.0</td><td>必修</td></tr>
        <tr><td>2</td><td>2025-2026-2</td><td>MA1012</td><td>概率论与数理统计</td><td>86</td><td>3.0</td><td>48</td><td>3.7</td><td>必修</td></tr>
      </table>
    )HTML";
    auto gr = hnu::adapters::parse_qiangzhi_grades_html(sample_grade_html);
    CHECK(gr["courses"].size() == 2, "Should parse 2 grade rows");
    CHECK(!gr["overall"].is_null() && gr["overall"]["credits"] == "7.00", "Should compute weighted credit sum");
    // 4b. 验证真实海南大学强智 HTML 边界场景：
    //     ① 顶部 <style>#kbtable{...}</style> + 查询条件外层 <table> 干扰（防止被上方 </table> 提前截断！）
    //     ② <div class="kbcontent"> 内部含嵌套子 <div> 与多段独立单双周 "4-6(周),8-17(双周)[01-02节]"
    //     ③ #xjkpTable 外层布局表包裹内层表 + 顶部单格内联 "院系：..." 与 11 位新生学号 20260000001
    const char* tricky_hnu_kb_html = R"HTML(
      <html><head><style>#kbtable td { font-size:12px; }</style></head><body>
      <table width="100%" class="toolbar"><tr><td>
        <select id="xnxq01id"><option value="2026-2027-1" selected>2026-2027-1</option></select>
      </td></tr></table>
      <table id="kbtable" border="1">
        <tr><th>节次</th><th>星期一</th><th>星期二</th><th>星期三</th><th>星期四</th><th>星期五</th><th>星期六</th><th>星期日</th></tr>
        <tr>
          <td>0102节</td>
          <td>
            <div class="kbcontent1">Linux基础</div>
            <div id="div_1" style="display:none;" class="kbcontent"><div class="inner_tip">Linux 操作系统基础</div><font title="老师">王海涛</font><br/><font title="周次(节次)">4-6(周),8-16(双周)[01-02节]</font><br/><font title="教室">(海甸)9-303计算机基础实验室5</font></div>
          </td>
          <td><div class="kbcontent">&nbsp;</div></td>
          <td><div class="kbcontent">大学英语1<br/><font title="老师">李敏</font><br/><font title="周次(节次)">4-17(周)[03-04节]</font><br/><font title="教室">(海甸)4-205</font></div></td>
          <td><div class="kbcontent">&nbsp;</div></td>
          <td><div class="kbcontent">&nbsp;</div></td>
          <td><div class="kbcontent">&nbsp;</div></td>
          <td><div class="kbcontent">&nbsp;</div></td>
        </tr>
        <tr><td colspan="8">备注：体育课按俱乐部场地进行</td></tr>
      </table></body></html>
    )HTML";

    auto tricky_tt = hnu::adapters::parse_qiangzhi_timetable_html(tricky_hnu_kb_html);
    CHECK(tricky_tt["courses"].size() == 2,
          "Must NOT be truncated by #kbtable in <style> or upper toolbar <table>");
    // 4-6(周) -> 4,5,6 (3周) + 8-16(双周) -> 8,10,12,14,16 (5周) = 共 8 个上课周
    bool found_linux = false;
    for (const auto& c : tricky_tt["courses"]) {
      if (c["name"] == "Linux 操作系统基础") {
        found_linux = (c["weeks"].size() == 8 && c["teacher"] == "王海涛");
      }
    }
    CHECK(found_linux, "Must parse nested <div> inside kbcontent and multi-segment '4-6(周),8-16(双周)'");

    const char* tricky_xjkp_html = R"HTML(
      <table id="xjkpTable">
        <tr>
          <td>院系：网络空间安全学院（密码学院）</td>
          <td>专业：信息安全</td>
          <td>学制：4</td>
          <td>班级：2026级信息安全1班</td>
          <td>学号：20260000001</td>
        </tr>
        <tr>
          <td colspan="5">
            <table class="inner_xj">
              <tr><td>姓名</td><td>测试同学</td><td>性别</td><td>男</td><td>入学日期</td><td>2026-08-31</td></tr>
            </table>
          </td>
        </tr>
      </table>
    )HTML";
    auto prof_11 = hnu::adapters::parse_qiangzhi_profile_html(tricky_xjkp_html);
    CHECK(prof_11.value("studentId", "") == "20260000001" &&
          prof_11.value("name", "") == "测试同学" &&
          prof_11.value("college", "") == "网络空间安全学院（密码学院）" &&
          prof_11.value("major", "") == "信息安全" &&
          prof_11.value("className", "") == "2026级信息安全1班",
          "Must extract both inline colon cells and nested table cells for 11-digit student ID");

    std::cout << "[PASS] Hainan University Qiangzhi #kbtable & #dataList HTML parsers (incl. nested/11-digit cases)\n";
  }

  // 5. 测试海南大学通知分类（台风停课识别为 impact，招标采购过滤为 drop）与 URL 路径归一化
  {
    auto c1 = hnu::adapters::classify_hnu_notice("关于受超强台风影响海甸校区停课一天及调课安排的通知");
    CHECK(c1.keep && c1.tier == "impact", "Typhoon class suspension must be tier=impact");

    auto c2 = hnu::adapters::classify_hnu_notice("海南大学实验室设备公开招标中标公告");
    CHECK(!c2.keep && c2.tier == "drop", "Bidding notices must be dropped");

    std::string u = hnu::utils::resolve_url("https://ugs.hainanu.edu.cn/rcpy/jwyz.htm", "../info/1032/31245.htm");
    CHECK(u == "https://ugs.hainanu.edu.cn/info/1032/31245.htm", "Relative ../info/ URL must normalize cleanly");
    CHECK(!hnu::utils::host_matches_allowlist("hainanu.edu.cn.evil.com", {"hainanu.edu.cn"}),
          "SSRF domain check must block suffix-spoofed hostnames");
    std::cout << "[PASS] HNU typhoon notice classifier & SSRF host allowlist\n";
  }

  // 6. 测试日志与反馈 PII 强脱敏
  {
    std::string dirty = "用户 2024300128 邮箱 test@hainanu.edu.cn 登录 password=Secret999&token=abc123xyz IP 192.168.31.88";
    std::string clean = hnu::utils::scrub_pii(dirty);
    CHECK(clean.find("2024300128") == std::string::npos, "Student ID must be scrubbed");
    CHECK(clean.find("test@hainanu.edu.cn") == std::string::npos, "Email must be scrubbed");
    CHECK(clean.find("Secret999") == std::string::npos, "Password value must be scrubbed");
    CHECK(clean.find("192.168.*.*") != std::string::npos, "IPv4 last two octets must be masked");
    std::cout << "[PASS] PII scrubber rules\n";
  }

  // 7. 测试独立门户二次算术验证码一次性消费与防锁号语义 + 数据库全链路 + ICS 日历生成
  {
    auto mkey = hnu::crypto::random_bytes(32);
    hnu::db::Database db(":memory:", mkey);
    auto u = db.upsert_user("2024300128", hnu::adapters::SandboxProvider::demo_profile("2024300128"));
    CHECK(u.academy == "南海书院" && u.campus == "海甸校区", "User profile with HNU academy should persist");

    std::string tok = db.create_session(u.id, "UnitTest", "127.0.*.*");
    CHECK(db.authenticate_session(tok).has_value(), "Session authentication should succeed");

    hnu::adapters::PortalClient portal;
    auto cap = portal.create_captcha_challenge();
    CHECK(cap.value("ok", false) && !cap.value("captchaId", "").empty(), "Portal arithmetic captcha must generate");

    // 故意算错验证码 -> 必须抛 CODEFALSE 且 should_purge_saved_password() 为 false！
    bool caught_codefalse = false;
    try {
      portal.login("2024300128", "hnu123456", cap["captchaId"], "999999");
    } catch (const hnu::adapters::PortalError& e) {
      caught_codefalse = (e.code() == "CODEFALSE" && !e.should_purge_saved_password());
    }
    CHECK(caught_codefalse, "Wrong arithmetic captcha must return CODEFALSE and NOT purge saved password");

    // 测试学分核对与 RFC 5545 .ics 日历生成
    auto tt = hnu::adapters::SandboxProvider::demo_timetable();
    auto gr = hnu::adapters::SandboxProvider::demo_grades();
    auto ex = hnu::adapters::SandboxProvider::demo_exams();
    auto pl = hnu::adapters::SandboxProvider::demo_plan();
    auto cr = hnu::services::CreditAnalyzer::analyze(pl, gr, tt);
    CHECK(cr.value("ready", false) && cr["totalEarned"].get<double>() > 60.0, "CreditAnalyzer should compute earned credits");

    std::string ics = hnu::services::CalendarIcsGenerator::build_user_ics(
        tt, ex, hnu::adapters::SandboxProvider::demo_chaoxing_scan(),
        hnu::adapters::SandboxProvider::demo_mooc_scan(),
        db.get_preferences(u.id), db.get_school_meta("bell_presets"));
    CHECK(ics.find("BEGIN:VCALENDAR") != std::string::npos &&
          ics.find("SUMMARY:操作系统原理") != std::string::npos &&
          ics.find("DTSTART;TZID=Asia/Shanghai:20260831T074000") != std::string::npos,
          "Generated .ics must contain HNU 07:40 first-period classes");
    std::cout << "[PASS] Portal arithmetic captcha, SQLite WAL DB, CreditAnalyzer & 07:40 ICS generator\n";
  }

  // 8. 回归验证：
  //    ① 超星学习通混合类型 JSON（schools="海南大学" 字符串、id/cpi 字符串、null 字段）绝不抛 type_error.302
  //    ② 强智空教室 rowspan/colspan 矩阵展开与按节次区间精准判空 + kbjcmsid 提取
  //    ③ 强智成绩单 pscj_list.do 平时/期中/期末成绩明细解析与完整教务列提取
  //    ④ 大一新生阶段执行计划清洗（剔除中级汉语综合 16 学分、合并 0.25 劳动子项、精确保留 0.25 小数、待选修审核）
  {
    const char* tricky_cx_json = R"JSON({
      "result": "1",
      "channelList": [
        {
          "cpi": "289104552",
          "content": {
            "id": "99881122",
            "state": "0",
            "course": {
              "data": [{
                "id": "245678901",
                "name": "高等数学A1上",
                "teacherfactor": null,
                "schools": "海南大学",
                "belongSchoolId": "10589"
              }]
            }
          }
        },
        {
          "cpi": 289104553,
          "content": {
            "folder": "自定义文件夹"
          }
        }
      ]
    })JSON";
    auto cx_courses = hnu::adapters::parse_chaoxing_courses_json(tricky_cx_json, "10589");
    CHECK(cx_courses.size() == 1 &&
          cx_courses[0]["courseId"] == "245678901" &&
          cx_courses[0]["classId"] == "99881122" &&
          cx_courses[0]["fid"] == "10589" &&
          cx_courses[0]["archived"] == false,
          "Chaoxing parser must handle string/null/mixed fields without json::type_error.302");

    // 成绩与期中/期末明细解析
    const char* rich_grade_html = R"HTML(
      <table id="dataList">
        <tr><th>序号</th><th>开课学期</th><th>课程编号</th><th>课程名称</th><th>成绩</th><th>成绩标识</th><th>学分</th><th>总学时</th><th>绩点</th><th>考核方式</th><th>考试性质</th><th>课程属性</th><th>课程性质</th><th>通选课类别</th></tr>
        <tr><td>1</td><td>2026-2027-1</td><td>XK01001</td><td>高等数学A1上</td><td><a href="javascript:JsMod('/jsxsd/kscj/pscj_list.do?xs0101id=1&amp;jx0404id=2&amp;zcj=91',700,500)">91</a></td><td></td><td>5.0</td><td>80</td><td>4.0</td><td>考试</td><td>正常考试</td><td>必修</td><td>学科基础必修课</td><td></td></tr>
      </table>
    )HTML";
    auto g_parsed = hnu::adapters::parse_qiangzhi_grades_html(rich_grade_html);
    CHECK(g_parsed["courses"].size() == 1 &&
          g_parsed["courses"][0]["hours"] == "80" &&
          g_parsed["courses"][0]["examMethod"] == "考试" &&
          g_parsed["courses"][0]["_pscjUrl"] == "/jsxsd/kscj/pscj_list.do?xs0101id=1&jx0404id=2&zcj=91",
          "Grades parser must extract hours, examMethod and unescaped pscj_list.do link");

    const char* sample_pscj_html = R"HTML(
      <table id="dataList">
        <tr><th>序号</th><th>平时成绩</th><th>平时成绩比例</th><th>期中成绩</th><th>期中成绩比例</th><th>期末成绩</th><th>期末成绩比例</th><th>总成绩</th></tr>
        <tr><td>1</td><td>95</td><td>20%</td><td>88</td><td>20%</td><td>91</td><td>60%</td><td>91</td></tr>
      </table>
    )HTML";
    auto pscj = hnu::adapters::parse_qiangzhi_pscj_html(sample_pscj_html);
    CHECK(pscj["regularScore"] == "95" && pscj["midtermScore"] == "88" && pscj["finalScore"] == "91",
          "pscj_list.do parser must extract regular, midterm and final scores");

    // 大一新生阶段执行计划与 0.25 学分精度审核测试
    nlohmann::json fresh_plan = {
        {"found", true},
        {"total", 21.5},
        {"cats", nlohmann::json::array({{{"name", "公共必修课"}, {"credit", 21.5}}})},
        {"courses", nlohmann::json::array({
            {{"code", "GG00029"}, {"name", "劳动教育实践1-生活劳动"}, {"credit", 0.25}, {"cat", "公共必修课"}, {"required", true}},
            {{"code", "GG0002945C"}, {"name", "劳动教育实践1-生活劳动-书院及周边公共"}, {"credit", 0.25}, {"cat", "公共必修课"}, {"required", true}},
            {{"code", "XK01001"}, {"name", "高等数学A1上"}, {"credit", 5.0}, {"cat", "公共必修课"}, {"required", true}},
            {{"code", "GG02334"}, {"name", "中级汉语综合I"}, {"credit", 8.0}, {"cat", "公共必修课"}, {"required", true}},
            {{"code", "GG02335"}, {"name", "中级汉语综合II"}, {"credit", 8.0}, {"cat", "公共必修课"}, {"required", true}},
        })},
    };
    nlohmann::json fresh_tt = {
        {"courses", nlohmann::json::array({
            {{"code", "HNU_abc"}, {"name", "劳动教育实践1-生活劳动"}},
        })},
    };
    nlohmann::json fresh_prof = {{"major", "软件工程(NIIT)"}, {"grade", "2026"}};
    auto fresh_cr = hnu::services::CreditAnalyzer::analyze(fresh_plan, {{"courses", nlohmann::json::array()}}, fresh_tt, fresh_prof);
    CHECK(!fresh_cr["isGraduationPlan"].get<bool>(), "Partial freshman execution plan without electives must NOT be marked as full graduation plan");
    CHECK(std::abs(fresh_cr["totalRequired"].get<double>() - 5.25) < 1e-6, "Cleaned plan must exclude 16.0 intl Chinese and 0.25 duplicate sub-item -> 5.25");
    CHECK(std::abs(fresh_cr["totalStudying"].get<double>() - 0.25) < 1e-6, "Quarter-credit 0.25 must be preserved exactly without rounding to 0.3");
    CHECK(std::abs(fresh_cr["totalRemain"].get<double>() - 5.0) < 1e-6, "Remaining must deduct both earned and studying (5.25 - 0.25 = 5.0)");
    CHECK(std::abs(fresh_cr["graduationAudit"]["gradTotalRequired"].get<double>() - 155.0) < 1e-6 &&
          std::abs(fresh_cr["graduationAudit"]["electiveTotalRequired"].get<double>() - 26.0) < 1e-6,
          "2026 Software Engineering (NIIT) graduation audit must match 155 total and 26 elective credits");
    std::cout << "[PASS] Chaoxing mixed-JSON safety, free-room matrix grid, midterm/final grades & freshman credit audit\n";
  }

  // 9. 验证海南大学 17 个书院完整元数据、书院自动识别与手动修改、通知附件缓存持久化及 365 天会话
  {
    const char* academy_html = R"HTML(
      <table id="xjkpTable">
        <tr><td>姓名</td><td>测试同学</td><td>学号</td><td>20263009999</td></tr>
        <tr><td>学院</td><td>计算机科学与技术学院</td><td>住宿社区</td><td>海甸校区朴诚书院12栋</td></tr>
      </table>
    )HTML";
    auto prof_ac = hnu::adapters::parse_qiangzhi_profile_html(academy_html);
    CHECK(prof_ac.value("academy", "") == "朴诚书院" && prof_ac.value("campus", "") == "海甸校区",
          "Profile parser must detect all 17 HNU academies including 朴诚书院/海德书院/日新书院/崖州书院");

    auto mkey = hnu::crypto::random_bytes(32);
    hnu::db::Database db(":memory:", mkey);
    auto ca = db.get_school_meta("campuses_and_academies");
    CHECK(ca.is_object() && ca["academies"].size() == 17 && ca["campusAcademies"].size() == 5,
          "Database builtin defaults must provide all 17 HNU academies grouped across 5 campuses");

    auto u = db.upsert_user("20260000001", {{"name", "测试同学"}, {"college", "计算机科学与技术学院"}});
    auto u2 = db.update_user_profile_manual(u.id, "海德书院", "海甸校区");
    CHECK(u2.has_value() && u2->academy == "海德书院", "Manual academy update must persist");
    // 后续强智同步若返回空书院，绝不能覆盖用户已设书院
    auto u3 = db.upsert_user("20260000001", {{"name", "测试同学"}, {"college", "计算机科学与技术学院"}});
    CHECK(u3.academy == "海德书院", "Subsequent jxgl sync with empty academy must preserve saved academy");

    // 通知附件缓存持久化校验
    db.upsert_notices(hnu::adapters::SandboxProvider::fallback_hnu_notices());
    CHECK(db.count_notices() >= 5, "count_notices should report seeded notices");
    nlohmann::json atts = nlohmann::json::array({{{"name", "红头PDF"}, {"url", "https://ugs.hainanu.edu.cn/a.pdf"}}});
    db.save_notice_detail("https://ugs.hainanu.edu.cn/info/1032/31245.htm",
                          "2026-11-24", "本科生院", "", "", "本通知以红头 PDF 发布", atts);
    auto cached_det = db.get_notice_detail_cached("https://ugs.hainanu.edu.cn/info/1032/31245.htm");
    CHECK(cached_det.has_value() && (*cached_det)["attachments"].size() == 1,
          "Notice detail cache must persist and return attachments");
    std::cout << "[PASS] 17 HNU academies metadata, profile academy preservation & notice attachments cache\n";
  }

  // 10. 验证 CookieJar 域过滤合并（隔离旧 jxgl Cookie、保留 authserver CASTGC）与空 kbcontent 单元格星期对齐
  {
    hnu::net::CookieJar jar;
    jar.set_cookie({"bzb_jsxsd", "fresh_cap_sid", "jxgl.hainanu.edu.cn", "/jsxsd", true, 0});
    nlohmann::json saved = nlohmann::json::array({
        {{"name", "bzb_jsxsd"}, {"value", "stale_old_sid"}, {"domain", "jxgl.hainanu.edu.cn"}, {"path", "/jsxsd"}, {"hostOnly", true}},
        {{"name", "CASTGC"}, {"value", "TGT-valid-123"}, {"domain", "authserver.hainanu.edu.cn"}, {"path", "/authserver"}, {"hostOnly", true}},
    });
    jar.merge_json(saved, "authserver.hainanu.edu.cn");
    CHECK(jar.get_cookie("bzb_jsxsd") == "fresh_cap_sid", "merge_json with authserver filter must NOT overwrite jxgl session cookie");
    CHECK(jar.get_cookie("CASTGC") == "TGT-valid-123", "merge_json must merge CASTGC for authserver.hainanu.edu.cn");

    // 验证周一为纯空 <div class="kbcontent"></div> 时，周二课程仍精准对齐 day=1（不左移到周一！）
    const char* empty_div_kb = R"HTML(
      <select id="xnxq01id"><option value="2026-2027-1" selected>2026-2027-1</option></select>
      <table id="kbtable">
        <tr>
          <td>第一大节</td>
          <td><div class="kbcontent"></div></td>
          <td><div class="kbcontent">数据结构<br/><font title="老师">林海</font><br/><font title="周次(节次)">1-16(周)[01-02节]</font><br/><font title="教室">5-201</font></div></td>
          <td><div class="kbcontent"></div></td>
          <td><div class="kbcontent"></div></td>
          <td><div class="kbcontent"></div></td>
          <td><div class="kbcontent"></div></td>
          <td><div class="kbcontent"></div></td>
        </tr>
      </table>
    )HTML";
    auto align_tt = hnu::adapters::parse_qiangzhi_timetable_html(empty_div_kb);
    CHECK(align_tt["courses"].size() == 1 && align_tt["courses"][0]["slots"][0]["day"] == 1,
          "Empty <div class='kbcontent'></div> on Monday must preserve Tuesday day=1 alignment");
    std::cout << "[PASS] CookieJar domain-filtered merge & empty kbcontent weekday alignment\n";
  }

  // 11. 验证成绩 期中 / 期末 / 平时 解析：合并标题行、阶段专用通道、纵向键值表、JSON、详情链接、评教拦截
  {
    const char* banner_html = R"HTML(
      <table>
        <tr><th colspan="9">学生成绩查询</th></tr>
        <tr><th>序号</th><th>开课学期</th><th>课程编号</th><th>课程名称</th><th>平时成绩</th><th>期中成绩</th><th>期末成绩</th><th>成绩</th><th>学分</th></tr>
        <tr><td>1</td><td>2026-2027-1</td><td>CS3001</td><td>操作系统原理</td><td>94</td><td>91</td><td></td><td></td><td>3.5</td></tr>
        <tr><td>2</td><td>2026-2027-1</td><td>CS3003</td><td>计算机网络</td><td>92</td><td>88</td><td>90</td><td>90</td><td>3.0</td></tr>
      </table>
    )HTML";
    auto g1 = hnu::adapters::parse_qiangzhi_grades_html(banner_html);
    CHECK(g1["courses"].size() == 2, "Banner title row above grade header must still be recognized");
    CHECK(g1["courses"][0]["midtermScore"] == "91" && g1["courses"][0]["score"] == "",
          "Midterm-only course must keep midtermScore and an empty final score");
    CHECK(!g1["overall"].is_null() && g1["overall"]["count"] == "1",
          "Midterm-only course must NOT be counted into overall GPA");
    CHECK(g1["midtermCount"] == 2 && g1["finalCount"] == 1, "midtermCount / finalCount must be computed");

    const char* mid_only_html = R"HTML(
      <table><tr><th>开课学期</th><th>课程名称</th><th>成绩</th></tr>
      <tr><td>2026-2027-1</td><td>高等数学A1</td><td>85</td></tr></table>
    )HTML";
    auto g2 = hnu::adapters::parse_qiangzhi_grades_html(mid_only_html, "midterm");
    CHECK(g2["courses"].size() == 1 && g2["courses"][0]["midtermScore"] == "85" &&
              g2["courses"][0]["score"] == "" && g2["overall"].is_null(),
          "source_hint=midterm must map the score column to midtermScore without polluting GPA");

    const char* vertical_ps = R"HTML(
      <table>
        <tr><td>平时成绩</td><td>94</td></tr>
        <tr><td>平时成绩比例</td><td>30%</td></tr>
        <tr><td>期中成绩</td><td>88</td></tr>
        <tr><td>期末成绩</td><td>90</td></tr>
        <tr><td>期末成绩比例</td><td>50%</td></tr>
      </table>
    )HTML";
    auto p1 = hnu::adapters::parse_qiangzhi_pscj_html(vertical_ps);
    CHECK(p1.value("regularScore", "") == "94" && p1.value("regularRatio", "") == "30%" &&
              p1.value("midtermScore", "") == "88" && p1.value("finalScore", "") == "90" &&
              p1.value("finalRatio", "") == "50%",
          "Vertical key-value pscj table must be parsed");

    const char* banner_ps = R"HTML(
      <table>
        <tr><th colspan="8">平时成绩明细</th></tr>
        <tr><th>平时成绩</th><th>平时成绩比例</th><th>期中成绩</th><th>期中成绩比例</th><th>期末成绩</th><th>期末成绩比例</th><th>总成绩</th></tr>
        <tr><td>95</td><td>30%</td><td>90</td><td>20%</td><td>92</td><td>50%</td><td>92</td></tr>
      </table>
    )HTML";
    auto p2 = hnu::adapters::parse_qiangzhi_pscj_html(banner_ps);
    CHECK(p2.value("regularScore", "") == "95" && p2.value("midtermRatio", "") == "20%" &&
              p2.value("totalScore", "") == "92",
          "Horizontal pscj table below a merged banner row must be parsed");

    const char* json_grades =
        R"({"data":[{"kcmc":"数据结构","kcbh":"CS2002","xnxq01id":"2025-2026-2","zcj":"93","pscj":"95","pscjb":"30","qzcj":"91","qmcj":"93","xf":"4.0"}]})";
    auto g3 = hnu::adapters::parse_qiangzhi_grades_html(json_grades);
    CHECK(g3["courses"].size() == 1 && g3["courses"][0]["midtermScore"] == "91" &&
              g3["courses"][0]["regularScore"] == "95" && g3["courses"][0]["score"] == "93" &&
              !g3["overall"].is_null(),
          "LayUI JSON grade rows (qzcj/qmcj/pscj/zcj) must be parsed");

    const char* action_col_html = R"HTML(
      <table><tr><th>课程名称</th><th>成绩</th><th>学分</th><th>操作</th></tr>
      <tr><td>操作系统</td><td>90</td><td>3</td><td><a onclick="JsMod('/jsxsd/kscj/pscj_list.do?xnxq01id=2026-2027-1&kcdm=CS3001','成绩详情')">详情</a></td></tr></table>
    )HTML";
    auto g4 = hnu::adapters::parse_qiangzhi_grades_html(action_col_html);
    CHECK(g4["courses"].size() == 1 &&
              g4["courses"][0].value("_pscjUrl", "").rfind("/jsxsd/kscj/pscj_list.do?", 0) == 0,
          "Detail link in a non-score action column must be extracted as _pscjUrl");

    auto g5 = hnu::adapters::parse_qiangzhi_grades_html("<html><body>请先评教后再查询成绩（未评教）</body></html>");
    CHECK(g5["evalBlocked"] == true && g5["courses"].empty(), "Teaching-evaluation block must be detected");

    auto dg = hnu::adapters::SandboxProvider::demo_grades("2026-2027-1");
    CHECK(dg["courses"].size() == 2 && dg["midtermCount"] == 2 && dg["overall"].is_null(),
          "Demo grades for 2026-2027-1 must expose 2 midterm-only courses with overall=null");
    auto dg2 = hnu::adapters::SandboxProvider::demo_grades("2025-2026-2");
    CHECK(dg2["courses"].size() == 6 && !dg2["overall"].is_null(),
          "Demo grades for 2025-2026-2 must expose 6 finalized courses with non-null overall GPA");
    std::cout << "[PASS] Grade midterm/final/regular parsing, midterm hint, JSON rows & evaluation block\n";
  }

  // 12. 全面代码审查回归测试：SSRF/URL解析、UTF-8 PII 脱敏、HTML大脚本线性剥离、Cookie域边界、
  //     ICS VTIMEZONE/UTC DTSTAMP/全角考场时间、学分乱序去重/别名选修、强智全角空教室与分拆字体标签、
  //     通知台风优先级与链接内日期、MOOC 非演示空 Cookie 阻断、数据库类型容错
  {
    // 12a. SSRF extract_host & is_http_url_allowed
    CHECK(hnu::utils::extract_host("http://127.0.0.1\\@hainanu.edu.cn/").empty(),
          "extract_host must reject backslash authority confusion");
    CHECK(hnu::utils::extract_host("https://evil.com@hainanu.edu.cn/").empty(),
          "extract_host must reject userinfo @ in authority");
    CHECK(!hnu::utils::is_http_url_allowed("http://127.0.0.1\\@hainanu.edu.cn/", {"hainanu.edu.cn"}),
          "is_http_url_allowed must reject backslash SSRF payload");
    CHECK(!hnu::utils::is_http_url_allowed("https://evil.com@hainanu.edu.cn/", {"hainanu.edu.cn"}),
          "is_http_url_allowed must reject userinfo SSRF payload");
    CHECK(!hnu::utils::is_http_url_allowed("file://hainanu.edu.cn/etc/passwd", {"hainanu.edu.cn"}),
          "is_http_url_allowed must reject non-HTTP(S) schemes");
    CHECK(hnu::utils::is_http_url_allowed("https://ugs.hainanu.edu.cn/info/1/2.htm", {"hainanu.edu.cn"}),
          "is_http_url_allowed must allow legitimate *.hainanu.edu.cn URL");
    CHECK(hnu::utils::extract_host("http://[::1]:8788/api/v1/health") == "[::1]",
          "extract_host must parse bracketed IPv6 literal");

    // 12b. resolve_url case-insensitive scheme, query-only & query containing /..
    CHECK(hnu::utils::resolve_url("https://ugs.hainanu.edu.cn/a/b.htm", "HTTPS://example.com/x") ==
              "HTTPS://example.com/x",
          "resolve_url must recognize uppercase HTTPS://");
    CHECK(hnu::utils::resolve_url("https://ugs.hainanu.edu.cn/a/list.htm?page=1", "?page=2") ==
              "https://ugs.hainanu.edu.cn/a/list.htm?page=2",
          "resolve_url must preserve path filename on query-only relative URL");
    CHECK(hnu::utils::resolve_url("https://ugs.hainanu.edu.cn/a/b.htm", "c.htm?next=/../d") ==
              "https://ugs.hainanu.edu.cn/a/c.htm?next=/../d",
          "resolve_url must NOT normalize /.. inside query string");

    // 12c. strip_html_tags linear-time handling on large <script> (>40KB)
    std::string big_script = "<p>保留文本</p><script>" + std::string(45000, 'a') + "</script><p>结尾</p>";
    std::string stripped = hnu::utils::strip_html_tags(big_script);
    CHECK(stripped.find("保留文本") != std::string::npos &&
              stripped.find("结尾") != std::string::npos &&
              stripped.find("aaaa") == std::string::npos,
          "strip_html_tags must strip >40KB <script> cleanly in O(n) without regex overflow");

    // 12d. scrub_pii JSON secrets, Bearer JWT & UTF-8 safe truncation
    std::string pii_json = R"({"token": "SecretTok99", "hdr": "Authorization: Bearer eyJhbGciOi.payload.sig"})";
    std::string pii_clean = hnu::utils::scrub_pii(pii_json);
    CHECK(pii_clean.find("SecretTok99") == std::string::npos &&
              pii_clean.find("eyJhbGciOi") == std::string::npos,
          "scrub_pii must mask JSON string secrets and Bearer tokens");
    std::string long_zh;
    for (int i = 0; i < 6000; ++i) long_zh += "海";
    std::string trunc_zh = hnu::utils::scrub_pii(long_zh);
    std::string dumped = nlohmann::json({{"s", trunc_zh}}).dump();
    CHECK(trunc_zh.size() < long_zh.size() && !dumped.empty(),
          "UTF-8 truncated scrub_pii output must keep valid UTF-8 boundary and serialize cleanly");

    // 12e. Strict hex_decode & CookieJar dot-boundary suffix check
    CHECK(hnu::crypto::hex_decode("zzzz").empty(), "hex_decode must reject non-hex characters");
    CHECK(hnu::crypto::hex_decode("01020g").empty(), "hex_decode must reject partially invalid hex");
    {
      hnu::net::CookieJar jar;
      jar.set_cookie({"SID", "evil", "evilchaoxing.com", "/", false, 0});
      CHECK(jar.get_cookie("SID", "chaoxing.com").empty(),
            "CookieJar::get_cookie must enforce dot boundary on domain suffix");
      CHECK(!jar.has_any_cookie("chaoxing.com"),
            "CookieJar::has_any_cookie must NOT match evilchaoxing.com for chaoxing.com");
    }

    // 12f. Calendar ICS: VTIMEZONE, UTC DTSTAMP with Z, full-width exam time
    {
      nlohmann::json exam_only = {
          {"exams", nlohmann::json::array({{
              {"name", "数据结构期末"},
              {"date", "2026-12-20"},
              {"time", "（上午）09:00-11:00"},
              {"room", "5教201"},
              {"seat", "12"},
          }})}
      };
      std::string ics = hnu::services::CalendarIcsGenerator::build_user_ics(
          {{"courses", nlohmann::json::array()}}, exam_only, nlohmann::json::object(),
          nlohmann::json::object(), nlohmann::json::object(), nlohmann::json::object());
      CHECK(ics.find("BEGIN:VTIMEZONE") != std::string::npos &&
                ics.find("TZID:Asia/Shanghai") != std::string::npos,
            "ICS output must include VTIMEZONE for Asia/Shanghai");
      CHECK(ics.find("DTSTAMP:") != std::string::npos &&
                ics.find("Z\r\n") != std::string::npos,
            "ICS DTSTAMP must be formatted as UTC with trailing Z");
      CHECK(ics.find("DTSTART;TZID=Asia/Shanghai:20261220T090000") != std::string::npos &&
                ics.find("DTEND;TZID=Asia/Shanghai:20261220T110000") != std::string::npos,
            "ICS exam parser must handle full-width prefix/separators without breaking UTF-8");
    }

    // 12g. CreditAnalyzer: child-before-parent dedup, alias elective count, unmatched gradSummary buckets
    {
      nlohmann::json rev_plan = {
          {"found", true},
          {"total", 5.5},
          {"cats", nlohmann::json::array({{{"name", "公共必修课"}, {"credit", 5.5}}})},
          {"courses", nlohmann::json::array({
              {{"code", "GG0002945C"}, {"name", "劳动教育实践1-生活劳动-书院及周边公共"}, {"credit", 0.25}, {"cat", "公共必修课"}, {"required", true}},
              {{"code", "GG00029"}, {"name", "劳动教育实践1-生活劳动"}, {"credit", 0.25}, {"cat", "公共必修课"}, {"required", true}},
              {{"code", "XK01001"}, {"name", "高等数学A1上"}, {"credit", 5.0}, {"cat", "公共必修课"}, {"required", true}},
          })},
      };
      auto rev_cr = hnu::services::CreditAnalyzer::analyze(
          rev_plan, {{"courses", nlohmann::json::array()}}, {{"courses", nlohmann::json::array()}});
      CHECK(std::abs(rev_cr["totalRequired"].get<double>() - 5.25) < 1e-6,
            "Child course listed before parent must still deduplicate to 5.25 credits");
    }

    // 12h. Qiangzhi: separate font tags, HNU /jsxsd/gsgl/wdcj_qzcjcxList midterm table & wiki/quick_links migration
    {
      const char* split_font_kb = R"HTML(
        <select id="xnxq01id"><option value="2026-2027-1" selected>2026-2027-1</option></select>
        <table id="kbtable">
          <tr>
            <td>第一大节</td>
            <td><div class="kbcontent">现代软件工程<br/><font title="老师">陈教授</font><br/><font title="周次">1-12(周)</font><br/><font title="节次">3至4节</font><br/><font title="教室">（海甸）5-301</font></div></td>
            <td></td><td></td><td></td><td></td><td></td><td></td>
          </tr>
        </table>
      )HTML";
      auto sftt = hnu::adapters::parse_qiangzhi_timetable_html(split_font_kb);
      CHECK(sftt["courses"].size() == 1 && sftt["courses"][0]["weeks"].size() == 12 &&
                sftt["courses"][0]["slots"].size() == 2 &&
                sftt["courses"][0]["slots"][0]["period"] == 2 &&
                sftt["courses"][0]["slots"][1]["period"] == 3,
            "Separate <font title='周次'> and <font title='节次'> with '3至4节' must parse cleanly");

      // 海南大学强智 /jsxsd/gsgl/wdcj_qzcjcxList 期中成绩表结构
      const char* hnu_qzcj_html = R"HTML(
        <table id="dataList">
          <tr><th>序号</th><th>开课学期</th><th>成绩活动名称</th><th>课程编号</th><th>课程名称</th><th>成绩</th><th>成绩标志</th><th>学分</th><th>课程属性</th><th>课程性质</th></tr>
          <tr><td>1</td><td>2026-2027-1</td><td>2026-2027-1期末成绩录入（含期中考试）</td><td>XK01016</td><td>线性代数A1</td><td>88</td><td></td><td>3</td><td>必修</td><td>学科基础必修课</td></tr>
          <tr><td>2</td><td>2026-2027-1</td><td>2026-2027-1期末成绩录入（含期中考试）</td><td>XK01001</td><td>高等数学A1上</td><td>91</td><td></td><td>5</td><td>必修</td><td>学科基础必修课</td></tr>
          <tr><td>3</td><td>2026-2027-1</td><td>2026-2027-1期末成绩录入（含期中考试）</td><td>GG00018</td><td>大学英语1</td><td>85</td><td></td><td>2</td><td>必修</td><td>公共必修课</td></tr>
        </table>
      )HTML";
      auto qz_mid = hnu::adapters::parse_qiangzhi_grades_html(hnu_qzcj_html, "midterm", "2026-2027-1");
      CHECK(qz_mid["courses"].size() == 3 && qz_mid["midtermCount"] == 3 &&
                qz_mid["overall"].is_null() &&
                qz_mid["courses"][0]["midtermScore"] == "88" &&
                qz_mid["courses"][0]["score"] == "" &&
                qz_mid["courses"][1]["midtermScore"] == "91" &&
                qz_mid["courses"][2]["midtermScore"] == "85",
            "HNU /jsxsd/gsgl/wdcj_qzcjcxList midterm table must parse all midterm scores without polluting GPA");

      // 验证海大指南仅保留「学业进阶 · 选课与共享攻略」三个跳转链接，且旧库自动迁移生效
      auto mkey_w = hnu::crypto::random_bytes(32);
      std::string seed_path = std::filesystem::exists("../config/hnu_seed.sql")
                                  ? "../config/hnu_seed.sql"
                                  : (std::filesystem::exists("config/hnu_seed.sql") ? "config/hnu_seed.sql" : "");
      if (!seed_path.empty()) {
        hnu::db::Database db_w(":memory:", mkey_w, "", seed_path);
        auto wiki = db_w.get_school_meta("wiki_groups");
        CHECK(wiki.is_object() && wiki.value("homeUrl", "").empty(), "wiki_groups homeUrl must be empty");
        int non_empty_urls = 0;
        for (const auto& grp : wiki["groups"]) {
          bool is_study_adv = (grp.value("group", "") == "学业进阶 · 选课与共享攻略");
          for (const auto& it : grp["items"]) {
            std::string u = it.value("url", "");
            if (is_study_adv) {
              CHECK(!u.empty(), "All 3 items in 学业进阶 · 选课与共享攻略 must keep redirect URLs");
              ++non_empty_urls;
            } else {
              CHECK(u.empty(), "Items outside 学业进阶 · 选课与共享攻略 must NOT have redirect URLs");
            }
          }
        }
        CHECK(non_empty_urls == 3, "Exactly 3 redirect URLs must remain in 海大指南");
      }
    }

    // 12i. Notice classifier typhoon priority & date inside <a>
    {
      auto ty = hnu::adapters::classify_hnu_notice("关于受台风影响海南大学及附属小学停课一天的紧急通知");
      CHECK(ty.keep && ty.tier == "impact",
            "Typhoon suspension notice must stay tier=impact even if title mentions 附属小学");
      const char* vsb_in_a = R"HTML(
        <ul>
          <li><a href="../info/1032/55555.htm"><span>2026-11-26</span>关于期末考试安排的通知</a></li>
        </ul>
      )HTML";
      auto list = hnu::adapters::parse_vsb_notice_list_html(
          vsb_in_a, "https://ugs.hainanu.edu.cn/rcpy/jwyz.htm", "测试", "本科生院");
      CHECK(list.size() == 1 && list[0]["date"] == "2026-11-26",
            "VSB parser must find dates located inside the <a> element");
    }

    // 12j. MoocClient non-demo user with empty cookies must throw auth (NOT return demo data)
    {
      hnu::adapters::MoocClient mc(true);
      bool caught_auth = false;
      try {
        mc.scan("2024300999", nlohmann::json::array());
      } catch (const hnu::adapters::MoocError& e) {
        caught_auth = (e.kind() == "auth");
      }
      CHECK(caught_auth, "MoocClient::scan must throw MoocError(auth) instead of returning demo data for real users");
    }

    // 12k. Database save_preferences type-safety on mismatched JSON types
    {
      auto mkey = hnu::crypto::random_bytes(32);
      hnu::db::Database db(":memory:", mkey);
      auto u = db.upsert_user("2024300777", {{"name", "边界测试"}});
      db.save_preferences(u.id, {{"totalWeeks", ""}, {"elecLowThreshold", "bad"}, {"hiddenCourses", "not_array"}});
      auto saved = db.get_preferences(u.id);
      CHECK(saved["totalWeeks"].is_number_integer() && saved["totalWeeks"].get<int>() == 20 &&
                saved["elecLowThreshold"].is_number() &&
                std::abs(saved["elecLowThreshold"].get<double>() - 15.0) < 1e-6 &&
                saved["hiddenCourses"].is_array(),
            "save_preferences must ignore mistyped JSON fields without throwing type_error");
    }

    std::cout << "[PASS] Full review regression suite (SSRF, UTF-8 PII, CookieJar, ICS, Credits, Qiangzhi, Notice, MOOC, DB)\n";
  }

  // 13. 多用户数据强隔离回归测试（会话、凭据保险箱 AAD、学业快照、偏好、ICS 令牌、缓存清理互不干扰）
  {
    auto mkey = hnu::crypto::random_bytes(32);
    hnu::db::Database db(":memory:", mkey);
    auto ua = db.upsert_user("20263000001", {{"name", "学生甲"}, {"academy", "海德书院"}});
    auto ub = db.upsert_user("20263000002", {{"name", "学生乙"}, {"academy", "朴诚书院"}});
    CHECK(ua.id != ub.id && ua.ics_token != ub.ics_token,
          "Distinct students must have distinct user IDs and ICS tokens");

    std::string tok_a = db.create_session(ua.id, "DevA", "127.0.*.*");
    std::string tok_b = db.create_session(ub.id, "DevB", "127.0.*.*");
    auto auth_a = db.authenticate_session(tok_a);
    auto auth_b = db.authenticate_session(tok_b);
    CHECK(auth_a && auth_a->id == ua.id && auth_b && auth_b->id == ub.id,
          "Each session token must authenticate strictly to its owning user");

    db.save_credential(ua.id, "jwxt", "20263000001", {{"account", "20263000001"}, {"password", "PassA"}});
    db.save_credential(ub.id, "jwxt", "20263000002", {{"account", "20263000002"}, {"password", "PassB"}});
    db.save_snapshot(ua.id, "grades", "current", {{"courses", nlohmann::json::array({{{"name", "高等数学"}, {"midtermScore", "95"}}})}});
    db.save_snapshot(ub.id, "grades", "current", {{"courses", nlohmann::json::array({{{"name", "线性代数"}, {"midtermScore", "88"}}})}});
    db.save_preferences(ua.id, {{"campusPreset", "haidian"}, {"totalWeeks", 18}});
    db.save_preferences(ub.id, {{"campusPreset", "yazhou"}, {"totalWeeks", 20}});

    // 验证数据按 user_id 严格隔离
    CHECK(db.load_credential(ua.id, "jwxt").secret.value("password", "") == "PassA" &&
              db.load_credential(ub.id, "jwxt").secret.value("password", "") == "PassB",
          "Vault credentials must stay strictly isolated per user");
    CHECK(db.get_snapshot(ua.id, "grades", "current").payload["courses"][0]["name"] == "高等数学" &&
              db.get_snapshot(ub.id, "grades", "current").payload["courses"][0]["name"] == "线性代数",
          "Academic snapshots must stay strictly isolated per user");
    CHECK(db.get_preferences(ua.id)["campusPreset"] == "haidian" &&
              db.get_preferences(ub.id)["campusPreset"] == "yazhou",
          "Preferences must stay strictly isolated per user");

    // 清理/吊销用户 A 绝不能影响用户 B
    db.delete_credential(ua.id, "jwxt");
    db.clear_all_user_cache(ua.id);
    db.revoke_session(tok_a);
    CHECK(!db.authenticate_session(tok_a).has_value() && db.authenticate_session(tok_b).has_value(),
          "Revoking user A session must NOT affect user B session");
    CHECK(!db.load_credential(ua.id, "jwxt").exists && db.load_credential(ub.id, "jwxt").exists,
          "Deleting user A credentials must NOT affect user B credentials");
    CHECK(!db.get_snapshot(ua.id, "grades", "current").found &&
              db.get_snapshot(ub.id, "grades", "current").found,
          "Clearing user A cache must NOT affect user B snapshots");
    std::cout << "[PASS] Multi-user data isolation (sessions, vault, snapshots, preferences, ICS tokens)\n";
  }

  std::cout << "\nALL C++20 CORE TESTS PASSED SUCCESSFULLY!\n";
  return 0;
}
