#include "hnu/adapters/sandbox_provider.hpp"
#include "hnu/crypto/crypto.hpp"
#include "hnu/utils/strings_and_time.hpp"

#include <algorithm>
#include <sstream>

namespace hnu::adapters {

bool SandboxProvider::is_demo_account(std::string_view account) {
  std::string a = utils::to_lower(utils::trim(account));
  return a == "2024300128" || a == "demo" || a == "demo_hnu";
}

bool SandboxProvider::verify_demo_password(std::string_view account, std::string_view password) {
  if (!is_demo_account(account)) return false;
  return password == "hnu123456" || password == "demo123" || password == "123456";
}

std::pair<std::string, std::string> SandboxProvider::generate_arithmetic_captcha_svg() {
  auto rnd = crypto::random_bytes(8);
  int op_type = rnd[0] % 3;
  int a = 0, b = 0, ans = 0;
  std::string expr;
  if (op_type == 0) {
    a = 11 + (rnd[1] % 28);
    b = 3 + (rnd[2] % 19);
    ans = a + b;
    expr = std::to_string(a) + " + " + std::to_string(b) + " = ?";
  } else if (op_type == 1) {
    a = 18 + (rnd[1] % 30);
    b = 4 + (rnd[2] % 14);
    ans = a - b;
    expr = std::to_string(a) + " - " + std::to_string(b) + " = ?";
  } else {
    a = 3 + (rnd[1] % 7);
    b = 4 + (rnd[2] % 6);
    ans = a * b;
    expr = std::to_string(a) + " × " + std::to_string(b) + " = ?";
  }

  int y1 = 12 + (rnd[3] % 22);
  int y2 = 14 + (rnd[4] % 20);
  int rot = static_cast<int>(rnd[5] % 5) - 2;

  std::ostringstream svg;
  svg << R"SVG(<svg xmlns="http://www.w3.org/2000/svg" width="132" height="44" viewBox="0 0 132 44">)SVG"
      << R"SVG(<rect width="132" height="44" rx="8" fill="#EEF3FB"/>)SVG"
      << R"SVG(<path d="M8 )SVG" << y1 << R"SVG( Q 66 )SVG" << (44 - y1) << R"SVG( 124 )SVG" << y2
      << R"SVG(" stroke="#B7C8E8" stroke-width="1.4" fill="none"/>)SVG"
      << R"SVG(<circle cx=")SVG" << (20 + (rnd[6] % 90)) << R"SVG(" cy=")SVG" << (10 + (rnd[7] % 24))
      << R"SVG(" r="2.2" fill="#9BB4E0" opacity="0.6"/>)SVG"
      << R"SVG(<text x="66" y="28" text-anchor="middle" font-family="ui-monospace,Consolas,monospace" )SVG"
      << R"SVG(font-size="18" font-weight="700" fill="#1B3A6B" transform="rotate()SVG" << rot << R"SVG( 66 22)">)SVG"
      << expr << R"SVG(</text></svg>)SVG";

  std::string s = svg.str();
  return {std::to_string(ans), "data:image/svg+xml;base64," + crypto::base64_encode(s)};
}

std::pair<std::string, std::string> SandboxProvider::generate_code_captcha_svg() {
  static constexpr char kCharset[] = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";
  auto rnd = crypto::random_bytes(8);
  std::string code;
  for (int i = 0; i < 4; ++i) {
    code.push_back(kCharset[rnd[i] % (sizeof(kCharset) - 1)]);
  }
  std::ostringstream svg;
  svg << R"SVG(<svg xmlns="http://www.w3.org/2000/svg" width="120" height="44" viewBox="0 0 120 44">)SVG"
      << R"SVG(<rect width="120" height="44" rx="8" fill="#F2F4F8"/>)SVG"
      << R"SVG(<line x1="8" y1=")SVG" << (10 + rnd[4] % 24) << R"SVG(" x2="112" y2=")SVG" << (10 + rnd[5] % 24)
      << R"SVG(" stroke="#C5CEE0" stroke-width="1.5"/>)SVG";
  for (int i = 0; i < 4; ++i) {
    int x = 22 + i * 24;
    int rot = static_cast<int>(rnd[4 + i] % 15) - 7;
    svg << R"SVG(<text x=")SVG" << x << R"SVG(" y="29" text-anchor="middle" font-family="monospace" )SVG"
        << R"SVG(font-size="20" font-weight="700" fill="#1E293B" transform="rotate()SVG" << rot << " " << x << R"SVG( 22)">)SVG"
        << code[i] << R"SVG(</text>)SVG";
  }
  svg << "</svg>";
  std::string s = svg.str();
  return {code, "data:image/svg+xml;base64," + crypto::base64_encode(s)};
}

nlohmann::json SandboxProvider::demo_profile(std::string_view account) {
  std::string sid = (account == "demo" || account == "demo_hnu") ? "2024300128" : std::string(account);
  return {
      {"studentId", sid},
      {"name", "林海椰"},
      {"gender", "男"},
      {"dept", "计算机科学与技术学院"},
      {"college", "计算机科学与技术学院"},
      {"academy", "南海书院"},
      {"major", "计算机科学与技术"},
      {"direction", "智能计算与系统架构"},
      {"className", "2024级计科1班"},
      {"campus", "海甸校区"},
      {"grade", "2024"},
      {"level", "本科"},
      {"duration", "4"},
      {"status", "在籍（注册学籍）"},
      {"enrolledAt", "2024-09-01"},
      {"graduateAt", "2028-06-30"},
  };
}

nlohmann::json SandboxProvider::demo_semesters() {
  return nlohmann::json::array({
      {{"id", "2026-2027-1"}, {"name", "2026-2027学年第1学期"}},
      {{"id", "2025-2026-2"}, {"name", "2025-2026学年第2学期"}},
      {{"id", "2025-2026-1"}, {"name", "2025-2026学年第1学期"}},
      {{"id", "2024-2025-2"}, {"name", "2024-2025学年第2学期"}},
      {{"id", "2024-2025-1"}, {"name", "2024-2025学年第1学期"}},
  });
}

static std::vector<int> range_weeks(int a, int b, int step = 1) {
  std::vector<int> v;
  for (int i = a; i <= b; i += step) v.push_back(i);
  return v;
}

nlohmann::json SandboxProvider::demo_timetable(std::string_view semester_id) {
  std::string sem = semester_id.empty() || semester_id == "current"
                        ? "2026-2027-1"
                        : std::string(semester_id);
  if (sem == "2025-2026-2") {
    return {
        {"semesterId", "2025-2026-2"},
        {"semester", "2025-2026 学年第 2 学期"},
        {"unitCount", 11},
        {"totalWeeks", 20},
        {"weekStart", 1},
        {"courses", nlohmann::json::array({
            {{"name", "数据结构与算法"}, {"code", "CS2002"}, {"teacher", "陈明远"}, {"room", "5教203"},
             {"weeks", range_weeks(1, 16)}, {"slots", {{{"day", 0}, {"period", 0}}, {{"day", 0}, {"period", 1}}, {{"day", 2}, {"period", 2}}, {{"day", 2}, {"period", 3}}}}},
            {{"name", "数字逻辑与计算机组成"}, {"code", "CS2006"}, {"teacher", "王海涛"}, {"room", "信息楼308"},
             {"weeks", range_weeks(1, 16)}, {"slots", {{{"day", 1}, {"period", 4}}, {{"day", 1}, {"period", 5}}, {{"day", 3}, {"period", 0}}, {{"day", 3}, {"period", 1}}}}},
            {{"name", "概率论与数理统计"}, {"code", "MA1012"}, {"teacher", "符丽娟"}, {"room", "4教105"},
             {"weeks", range_weeks(1, 16)}, {"slots", {{{"day", 4}, {"period", 2}}, {{"day", 4}, {"period", 3}}}}},
        })}
    };
  }

  return {
      {"semesterId", "2026-2027-1"},
      {"semester", "2026-2027 学年第 1 学期"},
      {"unitCount", 11},
      {"totalWeeks", 20},
      {"weekStart", 1},
      {"courses", nlohmann::json::array({
          {
              {"name", "操作系统原理"},
              {"code", "CS3001"},
              {"teacher", "张志强"},
              {"room", "5教302"},
              {"weeks", range_weeks(1, 16)},
              {"slots", {{{"day", 0}, {"period", 0}}, {{"day", 0}, {"period", 1}},
                         {{"day", 2}, {"period", 2}}, {{"day", 2}, {"period", 3}}}},
          },
          {
              {"name", "计算机网络"},
              {"code", "CS3003"},
              {"teacher", "林书豪"},
              {"room", "信息楼402,信息楼404"},
              {"weeks", range_weeks(1, 16)},
              {"slots", {{{"day", 1}, {"period", 0}}, {{"day", 1}, {"period", 1}},
                         {{"day", 3}, {"period", 4}}, {{"day", 3}, {"period", 5}}}},
          },
          {
              {"name", "数据库系统概论"},
              {"code", "CS3005"},
              {"teacher", "黄婉仪"},
              {"room", "5教108"},
              {"weeks", range_weeks(1, 16)},
              {"slots", {{{"day", 0}, {"period", 4}}, {{"day", 0}, {"period", 5}},
                         {{"day", 3}, {"period", 2}}, {{"day", 3}, {"period", 3}}}},
          },
          {
              {"name", "马克思主义基本原理"},
              {"code", "PO2001"},
              {"teacher", "吴琼"},
              {"room", "社科楼B112"},
              {"weeks", range_weeks(1, 16)},
              {"slots", {{{"day", 1}, {"period", 2}}, {{"day", 1}, {"period", 3}}}},
          },
          {
              {"name", "编译原理与系统实现"},
              {"code", "CS3009"},
              {"teacher", "周文斌"},
              {"room", "信息楼506"},
              {"weeks", range_weeks(1, 17, 2)},  // 单周课样本（角标显示「单」）
              {"slots", {{{"day", 2}, {"period", 6}}, {{"day", 2}, {"period", 7}}}},
          },
          {
              {"name", "操作系统内核实验"},
              {"code", "CS3002L"},
              {"teacher", "张志强"},
              {"room", "信息楼205机房"},
              {"weeks", range_weeks(2, 16, 2)},  // 双周课样本（角标显示「双」）
              {"slots", {{{"day", 2}, {"period", 6}}, {{"day", 2}, {"period", 7}}}},
          },
          {
              {"name", "人工智能导论"},
              {"code", "CS3011"},
              {"teacher", "刘晓东"},
              {"room", "思源学堂A201"},
              {"weeks", range_weeks(1, 16)},
              {"slots", {{{"day", 4}, {"period", 0}}, {{"day", 4}, {"period", 1}}}},
          },
          {
              {"name", "热带海洋生态与海南自贸港概论"},
              {"code", "GE1088"},
              {"teacher", "黎南海"},
              {"room", "3教210"},
              {"weeks", range_weeks(1, 15)},
              {"slots", {{{"day", 3}, {"period", 8}}, {{"day", 3}, {"period", 9}}, {{"day", 3}, {"period", 10}}}},
          },
          {
              {"name", "大学体育（热带水上运动/桨板）"},
              {"code", "PE2004"},
              {"teacher", "陈健"},
              {"room", "海甸游泳馆"},
              {"weeks", range_weeks(1, 16)},
              {"slots", {{{"day", 4}, {"period", 6}}, {{"day", 4}, {"period", 7}}}},
          },
      })}
  };
}

nlohmann::json SandboxProvider::demo_grades(std::string_view semester_id) {
  nlohmann::json base = {
      {"overall", {{"count", "24"}, {"credits", "68.5"}, {"gpa", "3.74"}}},
      {"summary", nlohmann::json::array({
          {{"year", "2024-2025"}, {"term", "1"}, {"semester", "2024-2025-1"}, {"count", "6"}, {"credits", "17.5"}, {"gpa", "3.58"}},
          {{"year", "2024-2025"}, {"term", "2"}, {"semester", "2024-2025-2"}, {"count", "6"}, {"credits", "18.0"}, {"gpa", "3.69"}},
          {{"year", "2025-2026"}, {"term", "1"}, {"semester", "2025-2026-1"}, {"count", "6"}, {"credits", "16.5"}, {"gpa", "3.81"}},
          {{"year", "2025-2026"}, {"term", "2"}, {"semester", "2025-2026-2"}, {"count", "6"}, {"credits", "16.5"}, {"gpa", "3.88"}},
      })},
      {"courses", nlohmann::json::array({
          {{"term", "2025-2026-2"}, {"code", "CS2002"}, {"seq", "01"}, {"name", "数据结构与算法"}, {"category", "专业核心课"}, {"nature", "必修"}, {"credit", "4.0"}, {"hours", "64"}, {"regularScore", "95"}, {"midtermScore", "91"}, {"finalScore", "93"}, {"score", "93"}, {"final", "93"}, {"gpa", "4.0"}, {"examMethod", "考试"}, {"examNature", "正常考试"}},
          {{"term", "2025-2026-2"}, {"code", "CS2006"}, {"seq", "01"}, {"name", "数字逻辑与计算机组成"}, {"category", "专业核心课"}, {"nature", "必修"}, {"credit", "3.5"}, {"hours", "56"}, {"regularScore", "92"}, {"midtermScore", "88"}, {"finalScore", "90"}, {"score", "90"}, {"final", "90"}, {"gpa", "4.0"}, {"examMethod", "考试"}, {"examNature", "正常考试"}},
          {{"term", "2025-2026-2"}, {"code", "MA1012"}, {"seq", "02"}, {"name", "概率论与数理统计"}, {"category", "公共基础课"}, {"nature", "必修"}, {"credit", "3.0"}, {"hours", "48"}, {"regularScore", "90"}, {"midtermScore", "86"}, {"finalScore", "88"}, {"score", "88"}, {"final", "88"}, {"gpa", "3.7"}, {"examMethod", "考试"}, {"examNature", "正常考试"}},
          {{"term", "2025-2026-2"}, {"code", "EN1004"}, {"seq", "05"}, {"name", "大学英语（四）"}, {"category", "公共基础课"}, {"nature", "必修"}, {"credit", "2.0"}, {"hours", "32"}, {"regularScore", "92"}, {"midtermScore", "87"}, {"finalScore", "88"}, {"score", "89"}, {"final", "89"}, {"gpa", "3.7"}, {"examMethod", "考试"}, {"examNature", "正常考试"}},
          {{"term", "2025-2026-2"}, {"code", "GE1032"}, {"seq", "01"}, {"name", "东坡诗词与海南历史文化"}, {"category", "通识选修课"}, {"generalCategory", "人文通识经典类"}, {"nature", "选修"}, {"credit", "2.0"}, {"hours", "32"}, {"regularScore", "96"}, {"midtermScore", ""}, {"finalScore", "93"}, {"score", "94"}, {"final", "94"}, {"gpa", "4.0"}, {"examMethod", "考查"}, {"examNature", "正常考试"}},
          {{"term", "2025-2026-2"}, {"code", "CS2010"}, {"seq", "01"}, {"name", "面向对象程序设计(C++)"}, {"category", "专业基础课"}, {"nature", "必修"}, {"credit", "2.0"}, {"hours", "32"}, {"regularScore", "96"}, {"midtermScore", "94"}, {"finalScore", "95"}, {"score", "95"}, {"final", "95"}, {"gpa", "4.0"}, {"examMethod", "考查"}, {"examNature", "正常考试"}},

          {{"term", "2025-2026-1"}, {"code", "CS2001"}, {"seq", "01"}, {"name", "离散数学"}, {"category", "专业基础课"}, {"nature", "必修"}, {"credit", "3.5"}, {"hours", "56"}, {"regularScore", "93"}, {"midtermScore", "89"}, {"finalScore", "91"}, {"score", "91"}, {"final", "91"}, {"gpa", "4.0"}, {"examMethod", "考试"}, {"examNature", "正常考试"}},
          {{"term", "2025-2026-1"}, {"code", "PH1002"}, {"seq", "03"}, {"name", "大学物理（下）"}, {"category", "公共基础课"}, {"nature", "必修"}, {"credit", "3.5"}, {"hours", "56"}, {"regularScore", "90"}, {"midtermScore", "84"}, {"finalScore", "85"}, {"score", "86"}, {"final", "86"}, {"gpa", "3.7"}, {"examMethod", "考试"}, {"examNature", "正常考试"}},
          {{"term", "2025-2026-1"}, {"code", "MA1008"}, {"seq", "01"}, {"name", "线性代数"}, {"category", "公共基础课"}, {"nature", "必修"}, {"credit", "3.0"}, {"hours", "48"}, {"regularScore", "95"}, {"midtermScore", "90"}, {"finalScore", "92"}, {"score", "92"}, {"final", "92"}, {"gpa", "4.0"}, {"examMethod", "考试"}, {"examNature", "正常考试"}},
          {{"term", "2025-2026-1"}, {"code", "PO1003"}, {"seq", "04"}, {"name", "毛泽东思想和中国特色社会主义理论体系概论"}, {"category", "公共基础课"}, {"nature", "必修"}, {"credit", "3.0"}, {"hours", "48"}, {"regularScore", "90"}, {"midtermScore", "85"}, {"finalScore", "86"}, {"score", "87"}, {"final", "87"}, {"gpa", "3.7"}, {"examMethod", "考试"}, {"examNature", "正常考试"}},
          {{"term", "2025-2026-1"}, {"code", "EN1003"}, {"seq", "02"}, {"name", "大学英语（三）"}, {"category", "公共基础课"}, {"nature", "必修"}, {"credit", "2.0"}, {"hours", "32"}, {"regularScore", "88"}, {"midtermScore", "83"}, {"finalScore", "84"}, {"score", "85"}, {"final", "85"}, {"gpa", "3.7"}, {"examMethod", "考试"}, {"examNature", "正常考试"}},
          {{"term", "2025-2026-1"}, {"code", "PE2001"}, {"seq", "08"}, {"name", "大学体育（三）"}, {"category", "公共基础课"}, {"nature", "必修"}, {"credit", "1.5"}, {"hours", "32"}, {"regularScore", "92"}, {"midtermScore", ""}, {"finalScore", "89"}, {"score", "90"}, {"final", "90"}, {"gpa", "4.0"}, {"examMethod", "考查"}, {"examNature", "正常考试"}},

          {{"term", "2024-2025-2"}, {"code", "MA1002"}, {"seq", "01"}, {"name", "高等数学（下）"}, {"category", "公共基础课"}, {"nature", "必修"}, {"credit", "5.0"}, {"hours", "80"}, {"regularScore", "92"}, {"midtermScore", "85"}, {"finalScore", "88"}, {"score", "88"}, {"final", "88"}, {"gpa", "3.7"}, {"examMethod", "考试"}, {"examNature", "正常考试"}},
          {{"term", "2024-2025-2"}, {"code", "PH1001"}, {"seq", "02"}, {"name", "大学物理（上）"}, {"category", "公共基础课"}, {"nature", "必修"}, {"credit", "3.5"}, {"hours", "56"}, {"regularScore", "88"}, {"midtermScore", "82"}, {"finalScore", "83"}, {"score", "84"}, {"final", "84"}, {"gpa", "3.3"}, {"examMethod", "考试"}, {"examNature", "正常考试"}},
          {{"term", "2024-2025-2"}, {"code", "CS1004"}, {"seq", "01"}, {"name", "程序设计实训"}, {"category", "集中实践"}, {"nature", "必修"}, {"credit", "2.5"}, {"hours", "40"}, {"regularScore", "93"}, {"midtermScore", ""}, {"finalScore", "92"}, {"score", "92"}, {"final", "92"}, {"gpa", "4.0"}, {"examMethod", "考查"}, {"examNature", "正常考试"}},
          {{"term", "2024-2025-2"}, {"code", "PO1002"}, {"seq", "02"}, {"name", "中国近现代史纲要"}, {"category", "公共基础课"}, {"nature", "必修"}, {"credit", "3.0"}, {"hours", "48"}, {"regularScore", "92"}, {"midtermScore", "87"}, {"finalScore", "88"}, {"score", "89"}, {"final", "89"}, {"gpa", "3.7"}, {"examMethod", "考试"}, {"examNature", "正常考试"}},
          {{"term", "2024-2025-2"}, {"code", "EN1002"}, {"seq", "01"}, {"name", "大学英语（二）"}, {"category", "公共基础课"}, {"nature", "必修"}, {"credit", "2.0"}, {"hours", "32"}, {"regularScore", "89"}, {"midtermScore", "84"}, {"finalScore", "85"}, {"score", "86"}, {"final", "86"}, {"gpa", "3.7"}, {"examMethod", "考试"}, {"examNature", "正常考试"}},
          {{"term", "2024-2025-2"}, {"code", "GE1015"}, {"seq", "01"}, {"name", "人工智能伦理与审美（美育）"}, {"category", "通识选修课"}, {"generalCategory", "公共艺术类"}, {"nature", "选修"}, {"credit", "2.0"}, {"hours", "32"}, {"regularScore", "94"}, {"midtermScore", ""}, {"finalScore", "90"}, {"score", "91"}, {"final", "91"}, {"gpa", "4.0"}, {"examMethod", "考查"}, {"examNature", "正常考试"}},

          {{"term", "2024-2025-1"}, {"code", "MA1001"}, {"seq", "01"}, {"name", "高等数学（上）"}, {"category", "公共基础课"}, {"nature", "必修"}, {"credit", "5.0"}, {"hours", "80"}, {"regularScore", "90"}, {"midtermScore", "83"}, {"finalScore", "84"}, {"score", "85"}, {"final", "85"}, {"gpa", "3.7"}, {"examMethod", "考试"}, {"examNature", "正常考试"}},
          {{"term", "2024-2025-1"}, {"code", "CS1001"}, {"seq", "01"}, {"name", "C语言程序设计"}, {"category", "专业基础课"}, {"nature", "必修"}, {"credit", "4.0"}, {"hours", "64"}, {"regularScore", "96"}, {"midtermScore", "92"}, {"finalScore", "94"}, {"score", "94"}, {"final", "94"}, {"gpa", "4.0"}, {"examMethod", "考试"}, {"examNature", "正常考试"}},
          {{"term", "2024-2025-1"}, {"code", "PO1001"}, {"seq", "03"}, {"name", "思想道德与法治"}, {"category", "公共基础课"}, {"nature", "必修"}, {"credit", "3.0"}, {"hours", "48"}, {"regularScore", "88"}, {"midtermScore", "81"}, {"finalScore", "82"}, {"score", "83"}, {"final", "83"}, {"gpa", "3.3"}, {"examMethod", "考试"}, {"examNature", "正常考试"}},
          {{"term", "2024-2025-1"}, {"code", "EN1001"}, {"seq", "01"}, {"name", "大学英语（一）"}, {"category", "公共基础课"}, {"nature", "必修"}, {"credit", "2.0"}, {"hours", "32"}, {"regularScore", "86"}, {"midtermScore", "80"}, {"finalScore", "81"}, {"score", "82"}, {"final", "82"}, {"gpa", "3.3"}, {"examMethod", "考试"}, {"examNature", "正常考试"}},
          {{"term", "2024-2025-1"}, {"code", "MIL101"}, {"seq", "01"}, {"name", "军事技能与理论"}, {"category", "公共基础课"}, {"nature", "必修"}, {"credit", "2.0"}, {"hours", "36"}, {"regularScore", "90"}, {"midtermScore", ""}, {"finalScore", "87"}, {"score", "88"}, {"final", "88"}, {"gpa", "3.7"}, {"examMethod", "考查"}, {"examNature", "正常考试"}},
          {{"term", "2024-2025-1"}, {"code", "PE1001"}, {"seq", "05"}, {"name", "大学体育（一）"}, {"category", "公共基础课"}, {"nature", "必修"}, {"credit", "1.5"}, {"hours", "32"}, {"regularScore", "88"}, {"midtermScore", ""}, {"finalScore", "85"}, {"score", "86"}, {"final", "86"}, {"gpa", "3.7"}, {"examMethod", "考查"}, {"examNature", "正常考试"}},
      })}
  };

  // 本学期已录入期中成绩、尚未出期末总评的样本课程（不计入 GPA）
  nlohmann::json mid_samples = nlohmann::json::array({
      {{"term", "2026-2027-1"}, {"code", "CS3001"}, {"seq", "01"}, {"name", "操作系统原理"}, {"category", "专业核心课"}, {"nature", "必修"}, {"credit", "4.0"}, {"hours", "64"}, {"regularScore", "94"}, {"regularRatio", "30%"}, {"midtermScore", "91"}, {"midtermRatio", "20%"}, {"finalScore", ""}, {"finalRatio", "50%"}, {"score", ""}, {"final", ""}, {"gpa", ""}, {"examMethod", "考试"}, {"examNature", "正常考试"}},
      {{"term", "2026-2027-1"}, {"code", "CS3003"}, {"seq", "01"}, {"name", "计算机网络"}, {"category", "专业核心课"}, {"nature", "必修"}, {"credit", "3.5"}, {"hours", "56"}, {"regularScore", "92"}, {"regularRatio", "30%"}, {"midtermScore", "88"}, {"midtermRatio", "20%"}, {"finalScore", ""}, {"finalRatio", "50%"}, {"score", ""}, {"final", ""}, {"gpa", ""}, {"examMethod", "考试"}, {"examNature", "正常考试"}},
  });
  nlohmann::json all_courses = mid_samples;
  for (const auto& c : base["courses"]) all_courses.push_back(c);

  std::string want(semester_id);
  if (want == "all" || want == "current") want.clear();

  nlohmann::json courses = nlohmann::json::array();
  for (const auto& c : all_courses) {
    if (want.empty() || c.value("term", "") == want) courses.push_back(c);
  }
  nlohmann::json summary = nlohmann::json::array();
  for (const auto& s : base["summary"]) {
    if (want.empty() || s.value("semester", "") == want) summary.push_back(s);
  }
  int mid_n = 0, fin_n = 0, reg_n = 0;
  for (const auto& c : courses) {
    if (!c.value("midtermScore", "").empty()) ++mid_n;
    if (!c.value("score", "").empty()) ++fin_n;
    if (!c.value("regularScore", "").empty()) ++reg_n;
  }
  nlohmann::json overall = nullptr;
  if (want.empty()) {
    overall = base["overall"];
  } else if (summary.size() == 1) {
    overall = {{"count", summary[0]["count"]}, {"credits", summary[0]["credits"]}, {"gpa", summary[0]["gpa"]}};
  } else if (!summary.empty()) {
    overall = base["overall"];
  }
  return {
      {"overall", overall},
      {"summary", summary},
      {"courses", courses},
      {"midtermCount", mid_n},
      {"finalCount", fin_n},
      {"regularCount", reg_n},
      {"queriedSemester", want},
      {"evalBlocked", false},
      {"sourcesChecked", nlohmann::json::array({{{"path", "/jsxsd/kscj/cjcx_list"}, {"label", "演示成绩单"}, {"count", static_cast<int>(courses.size())}}})},
  };
}

nlohmann::json SandboxProvider::demo_exams(std::string_view semester_id) {
  std::string sem = semester_id.empty() || semester_id == "current"
                        ? "2026-2027-1"
                        : std::string(semester_id);
  if (sem != "2026-2027-1") {
    return {
        {"semesterId", sem},
        {"published", true},
        {"exams", nlohmann::json::array({
            {{"code", "CS2002"}, {"name", "数据结构与算法"}, {"category", "期末考试"}, {"form", "闭卷笔试"},
             {"weekText", "第19周 周二"}, {"date", "2026-06-30"}, {"arrange", "09:00~11:00"},
             {"campus", "海甸校区"}, {"room", "5教203"}, {"seat", "18"}, {"status", "正常"}},
        })}
    };
  }
  return {
      {"semesterId", "2026-2027-1"},
      {"published", true},
      {"exams", nlohmann::json::array({
          {{"code", "PO2001"}, {"name", "马克思主义基本原理"}, {"category", "随堂结课考"}, {"form", "闭卷"},
           {"weekText", "第13周 周五"}, {"date", "2026-11-27"}, {"arrange", "14:30~16:10"},
           {"campus", "海甸校区"}, {"room", "社科楼B112"}, {"seat", "24"}, {"status", "正常"}},
          {{"code", "CS3001"}, {"name", "操作系统原理"}, {"category", "期末集中考试"}, {"form", "闭卷笔试"},
           {"weekText", "第19周 周二"}, {"date", "2027-01-05"}, {"arrange", "09:00~11:00"},
           {"campus", "海甸校区"}, {"room", "5教302"}, {"seat", "09"}, {"status", "正常"}},
          {{"code", "CS3003"}, {"name", "计算机网络"}, {"category", "期末集中考试"}, {"form", "闭卷笔试"},
           {"weekText", "第19周 周四"}, {"date", "2027-01-07"}, {"arrange", "14:30~16:30"},
           {"campus", "海甸校区"}, {"room", "4教206"}, {"seat", "31"}, {"status", "正常"}},
          {{"code", "CS3005"}, {"name", "数据库系统概论"}, {"category", "期末集中考试"}, {"form", "闭卷笔试"},
           {"weekText", "第20周 周一"}, {"date", "2027-01-11"}, {"arrange", "09:00~11:00"},
           {"campus", "海甸校区"}, {"room", "5教108"}, {"seat", "15"}, {"status", "正常"}},
      })}
  };
}

nlohmann::json SandboxProvider::demo_plan() {
  return {
      {"found", true},
      {"total", 160.0},
      {"cats", nlohmann::json::array({
          {{"name", "公共基础必修课"}, {"credit", 48.0}},
          {{"name", "专业基础与核心必修课"}, {"credit", 62.0}},
          {{"name", "专业方向选修课"}, {"credit", 22.0}},
          {{"name", "通识教育选修课"}, {"credit", 10.0}},
          {{"name", "集中实践与毕业设计"}, {"credit", 18.0}},
      })},
      {"specials", nlohmann::json::array({
          {{"cat", "通识教育选修课 / 美育模块"}, {"name", "应修学分"}, {"credit", 2.0}, {"note", "艺术审美类"}},
          {{"cat", "通识教育选修课 / 海南自贸港与海洋特色"}, {"name", "应修学分"}, {"credit", 2.0}, {"note", "校本特色"}},
      })},
      {"subs", nlohmann::json::array({
          {{"from", {{"code", "CS2009"}, {"name", "C++高级编程"}, {"credit", 2.0}}},
           {"to", {{"code", "CS2010"}, {"name", "面向对象程序设计(C++)"}, {"credit", 2.0}}}}
      })},
      {"courses", nlohmann::json::array({
          {{"code", "MA1001"}, {"name", "高等数学（上）"}, {"credit", 5.0}, {"cat", "公共基础必修课"}, {"terms", {1}}, {"required", true}, {"dept", "数学与统计学院"}, {"note", ""}},
          {{"code", "MA1002"}, {"name", "高等数学（下）"}, {"credit", 5.0}, {"cat", "公共基础必修课"}, {"terms", {2}}, {"required", true}, {"dept", "数学与统计学院"}, {"note", ""}},
          {{"code", "MA1008"}, {"name", "线性代数"}, {"credit", 3.0}, {"cat", "公共基础必修课"}, {"terms", {3}}, {"required", true}, {"dept", "数学与统计学院"}, {"note", ""}},
          {{"code", "MA1012"}, {"name", "概率论与数理统计"}, {"credit", 3.0}, {"cat", "公共基础必修课"}, {"terms", {4}}, {"required", true}, {"dept", "数学与统计学院"}, {"note", ""}},
          {{"code", "CS1001"}, {"name", "C语言程序设计"}, {"credit", 4.0}, {"cat", "专业基础与核心必修课"}, {"terms", {1}}, {"required", true}, {"dept", "计算机科学与技术学院"}, {"note", ""}},
          {{"code", "CS2001"}, {"name", "离散数学"}, {"credit", 3.5}, {"cat", "专业基础与核心必修课"}, {"terms", {3}}, {"required", true}, {"dept", "计算机科学与技术学院"}, {"note", ""}},
          {{"code", "CS2002"}, {"name", "数据结构与算法"}, {"credit", 4.0}, {"cat", "专业基础与核心必修课"}, {"terms", {4}}, {"required", true}, {"dept", "计算机科学与技术学院"}, {"note", ""}},
          {{"code", "CS2006"}, {"name", "数字逻辑与计算机组成"}, {"credit", 3.5}, {"cat", "专业基础与核心必修课"}, {"terms", {4}}, {"required", true}, {"dept", "计算机科学与技术学院"}, {"note", ""}},
          {{"code", "CS2010"}, {"name", "面向对象程序设计(C++)"}, {"credit", 2.0}, {"cat", "专业基础与核心必修课"}, {"terms", {4}}, {"required", true}, {"dept", "计算机科学与技术学院"}, {"note", ""}},
          {{"code", "CS3001"}, {"name", "操作系统原理"}, {"credit", 4.0}, {"cat", "专业基础与核心必修课"}, {"terms", {5}}, {"required", true}, {"dept", "计算机科学与技术学院"}, {"note", "本学期在修"}},
          {{"code", "CS3003"}, {"name", "计算机网络"}, {"credit", 3.5}, {"cat", "专业基础与核心必修课"}, {"terms", {5}}, {"required", true}, {"dept", "计算机科学与技术学院"}, {"note", "本学期在修"}},
          {{"code", "CS3005"}, {"name", "数据库系统概论"}, {"credit", 3.5}, {"cat", "专业基础与核心必修课"}, {"terms", {5}}, {"required", true}, {"dept", "计算机科学与技术学院"}, {"note", "本学期在修"}},
          {{"code", "CS3009"}, {"name", "编译原理与系统实现"}, {"credit", 3.0}, {"cat", "专业基础与核心必修课"}, {"terms", {5}}, {"required", true}, {"dept", "计算机科学与技术学院"}, {"note", "本学期在修"}},
          {{"code", "CS3011"}, {"name", "人工智能导论"}, {"credit", 3.0}, {"cat", "专业方向选修课"}, {"terms", {5}}, {"required", false}, {"dept", "计算机科学与技术学院"}, {"note", "本学期在修"}},
          {{"code", "CS4001"}, {"name", "计算机系统结构"}, {"credit", 3.0}, {"cat", "专业基础与核心必修课"}, {"terms", {6}}, {"required", true}, {"dept", "计算机科学与技术学院"}, {"note", "待修"}},
          {{"code", "CS4099"}, {"name", "毕业设计（论文）"}, {"credit", 12.0}, {"cat", "集中实践与毕业设计"}, {"terms", {8}}, {"required", true}, {"dept", "计算机科学与技术学院"}, {"note", "待修"}},
      })}
  };
}

nlohmann::json SandboxProvider::demo_jw_todo() {
  return {
      {"items", nlohmann::json::array({
          {{"key", "elect"}, {"name", "本科生选课"}, {"state", "closed"}, {"text", "当前非选课开放轮次"}, {"detail", "2026-2027-2 预选将于第 16 周开启"}},
          {{"key", "evaluate"}, {"name", "期末教学评价"}, {"state", "open"}, {"text", "2026-2027-1 评教通道已开启"}, {"detail", "请在期末查分前完成本学期在修课程评教"}},
          {{"key", "retake"}, {"name", "重修/缓考报名"}, {"state", "closed"}, {"text", "本学期重修报名已截止"}, {"detail", ""}},
      })}
  };
}

nlohmann::json SandboxProvider::demo_portal_card() {
  std::string today = utils::bj_date_ymd();
  return {
      {"balance", "68.50"},
      {"cardStatus", "正常"},
      {"cardNo", "2024****28"},
      {"updatedAt", utils::bj_datetime_str()},
      {"records", nlohmann::json::array({
          {{"time", today + " 12:18"}, {"place", "海甸第五食堂·海南鸡饭窗口"}, {"amount", "-13.50"}, {"balance", "68.50"}},
          {{"time", today + " 07:26"}, {"place", "海甸南海书院食堂·清补凉早餐档"}, {"amount", "-6.00"}, {"balance", "82.00"}},
          {{"time", "2026-11-23 18:42"}, {"place", "海甸第三食堂·自选称重"}, {"amount", "-15.20"}, {"balance", "88.00"}},
          {{"time", "2026-11-23 12:10"}, {"place", "海甸东坡餐厅·抱罗粉档口"}, {"amount", "-11.00"}, {"balance", "103.20"}},
          {{"time", "2026-11-22 21:30"}, {"place", "海大e卡微信圈存充值"}, {"amount", "+100.00"}, {"balance", "114.20"}},
          {{"time", "2026-11-22 17:55"}, {"place", "海甸博学超市"}, {"amount", "-8.80"}, {"balance", "14.20"}},
      })},
  };
}

nlohmann::json SandboxProvider::demo_portal_apps() {
  return nlohmann::json::array({
      {{"id", "jwxt"}, {"name", "强智教务一体化平台"}, {"dept", "本科生院"}, {"url", "https://jxgl.hainanu.edu.cn/jsxsd/"}},
      {{"id", "leave"}, {"name", "学生日常请假/离岛报备"}, {"dept", "学生工作部/书院"}, {"url", "https://ehall.hainanu.edu.cn"}},
      {{"id", "ecard"}, {"name", "海大 e 卡服务大厅"}, {"dept", "信息化服务中心"}, {"url", "https://ehall.hainanu.edu.cn"}},
      {{"id", "repair"}, {"name", "智慧后勤水电空调报修"}, {"dept", "后勤保障部"}, {"url", "https://ehall.hainanu.edu.cn"}},
      {{"id", "dorm_elec"}, {"name", "学生公寓智能电表购电"}, {"dept", "后勤保障部"}, {"url", "https://ehall.hainanu.edu.cn"}},
      {{"id", "lib_seat"}, {"name", "图书馆研修间与座位预约"}, {"dept", "图书馆"}, {"url", "https://lib.hainanu.edu.cn"}},
      {{"id", "transcript"}, {"name", "电子成绩单与在读证明（可信签章）"}, {"dept", "本科生院"}, {"url", "https://ehall.hainanu.edu.cn"}},
      {{"id", "second_class"}, {"name", "第二课堂与志愿时长认定"}, {"dept", "校团委"}, {"url", "https://ehall.hainanu.edu.cn"}},
      {{"id", "scholarship"}, {"name", "书院综合测评与奖助学金申请"}, {"dept", "学生工作部"}, {"url", "https://ehall.hainanu.edu.cn"}},
      {{"id", "ebike"}, {"name", "校园电动自行车通行码申领"}, {"dept", "保卫处"}, {"url", "https://ehall.hainanu.edu.cn"}},
      {{"id", "webvpn"}, {"name", "WebVPN 校外文献访问"}, {"dept", "信息化服务中心"}, {"url", "https://its.hainanu.edu.cn"}},
      {{"id", "software"}, {"name", "正版软件中心（WPS/MATLAB）"}, {"dept", "信息化服务中心"}, {"url", "https://its.hainanu.edu.cn"}},
      {{"id", "cet"}, {"name", "全国大学英语四六级校内报名"}, {"dept", "本科生院"}, {"url", "https://cet-bm.neea.edu.cn"}},
      {{"id", "pe_test"}, {"name", "体质健康测试预约与成绩查询"}, {"dept", "体育学院"}, {"url", "https://ehall.hainanu.edu.cn"}},
      {{"id", "hospital"}, {"name", "校医院门诊挂号与医保报销"}, {"dept", "校医院"}, {"url", "https://ehall.hainanu.edu.cn"}},
      {{"id", "shuttle"}, {"name", "海甸—观澜湖—儋州跨校区班车预约"}, {"dept", "党政办公室"}, {"url", "https://ehall.hainanu.edu.cn"}},
      {{"id", "classroom_apply"}, {"name", "书院/社团活动教室借用申请"}, {"dept", "本科生院"}, {"url", "https://ehall.hainanu.edu.cn"}},
      {{"id", "tuition"}, {"name", "学生学费与住宿费统一支付"}, {"dept", "财务处"}, {"url", "https://ehall.hainanu.edu.cn"}},
      {{"id", "mail"}, {"name", "海南大学学生教育邮箱 (@hainanu.edu.cn)"}, {"dept", "信息化服务中心"}, {"url", "https://mail.hainanu.edu.cn"}},
  });
}

nlohmann::json SandboxProvider::demo_chaoxing_scan() {
  int64_t now = utils::now_sec();
  return {
      {"at", now},
      {"coursesScanned", 8},
      {"coursesRested", 6},
      {"failed", nlohmann::json::array()},
      {"rest", nlohmann::json::object()},
      {"items", nlohmann::json::array({
          {
              {"id", "cx_work_os_4"},
              {"kind", "work"},
              {"kindName", "作业"},
              {"course", "操作系统原理"},
              {"title", "实验3：页表映射与缺页中断处理报告"},
              {"status", "未交"},
              {"bucket", "todo"},
              {"endAt", now + 26 * 3600},
              {"remainText", "还剩 1 天 2 小时"},
              {"url", "https://mooc1.chaoxing.com"},
          },
          {
              {"id", "cx_work_net_5"},
              {"kind", "work"},
              {"kindName", "作业"},
              {"course", "计算机网络"},
              {"title", "第5章 TCP拥塞控制 Wireshark 抓包分析"},
              {"status", "未交"},
              {"bucket", "todo"},
              {"endAt", now + 76 * 3600},
              {"remainText", "还剩 3 天 4 小时"},
              {"url", "https://mooc1.chaoxing.com"},
          },
          {
              {"id", "cx_quiz_db_2"},
              {"kind", "quiz"},
              {"kindName", "练习"},
              {"course", "数据库系统概论"},
              {"title", "B+树索引分裂与事务隔离级别随堂练习"},
              {"status", "进行中"},
              {"bucket", "todo"},
              {"endAt", now + 5 * 3600},
              {"remainText", "今天截止"},
              {"url", "https://mobilelearn.chaoxing.com"},
          },
          {
              {"id", "cx_exam_ai_1"},
              {"kind", "exam"},
              {"kindName", "考试"},
              {"course", "人工智能导论"},
              {"title", "期中在线阶段测验（神经网络基础）"},
              {"status", "待参加"},
              {"bucket", "todo"},
              {"endAt", now + 52 * 3600},
              {"remainText", "还剩 2 天 4 小时"},
              {"url", "https://mooc1.chaoxing.com"},
          },
          {
              {"id", "cx_done_os_3"},
              {"kind", "work"},
              {"kindName", "作业"},
              {"course", "操作系统原理"},
              {"title", "第3章 进程调度与死锁银行家算法习题"},
              {"status", "已批阅"},
              {"bucket", "done"},
              {"score", "96.0"},
              {"endAt", now - 72 * 3600},
              {"remainText", "已完成 · 96.0 分"},
              {"url", "https://mooc1.chaoxing.com"},
          },
      })},
  };
}

nlohmann::json SandboxProvider::demo_chaoxing_notices() {
  int64_t now = utils::now_sec();
  return nlohmann::json::array({
      {
          {"id", "cx_msg_1"},
          {"title", "《操作系统原理》发布了新作业：实验3 页表映射与缺页中断处理报告"},
          {"course", "操作系统原理"},
          {"sender", "张志强老师"},
          {"time", utils::bj_datetime_str(now - 3600)},
          {"kind", "work"},
          {"url", "https://mooc1.chaoxing.com"},
      },
      {
          {"id", "cx_msg_2"},
          {"title", "《计算机网络》本周四实验改在信息楼 404 机房进行，请自带实验指导书"},
          {"course", "计算机网络"},
          {"sender", "林书豪老师"},
          {"time", utils::bj_datetime_str(now - 18000)},
          {"kind", "notice"},
          {"url", "https://mooc1.chaoxing.com"},
      },
  });
}

nlohmann::json SandboxProvider::demo_mooc_scan() {
  int64_t now = utils::now_sec();
  return {
      {"at", now},
      {"user", {{"name", "林海椰"}, {"school", "海南大学"}}},
      {"courses", nlohmann::json::array({
          {{"courseId", 1008601}, {"termId", 1472001}, {"name", "编译原理（国家级一流本科课程）"}},
          {{"courseId", 1008602}, {"termId", 1472002}, {"name", "海南自贸港制度创新导论"}},
      })},
      {"failed", nlohmann::json::array()},
      {"count", {{"todo", 2}, {"expired", 0}, {"done", 1}}},
      {"items", nlohmann::json::array({
          {{"kind", "quiz"}, {"kindName", "测验"}, {"title", "第6讲 LR(1) 语法分析器构造单元测验"},
           {"course", "编译原理（国家级一流本科课程）"}, {"chapter", "第六章 自底向上优先分析"},
           {"state", "todo"}, {"endAt", now + 40 * 3600}, {"score", nullptr}, {"total", 100},
           {"url", "https://www.icourse163.org"}},
          {{"kind", "work"}, {"kindName", "作业"}, {"title", "封关运作与零关税清单案例简析（互评作业）"},
           {"course", "海南自贸港制度创新导论"}, {"chapter", "第四章 贸易自由便利"},
           {"state", "todo"}, {"endAt", now + 96 * 3600}, {"score", nullptr}, {"total", 100},
           {"url", "https://www.icourse163.org"}},
          {{"kind", "quiz"}, {"kindName", "测验"}, {"title", "第5讲 LL(1) 预测分析表构造"},
           {"course", "编译原理（国家级一流本科课程）"}, {"chapter", "第五章 自顶向下分析"},
           {"state", "done"}, {"endAt", now - 86400}, {"score", 95.0}, {"total", 100},
           {"url", "https://www.icourse163.org"}},
      })},
  };
}

std::vector<nlohmann::json> SandboxProvider::fallback_hnu_notices() {
  return {
      {
          {"title", "本科生院 实验室与设备管理处关于2026年度实验教学中心考核结果的公示"},
          {"url", "https://ugs.hainanu.edu.cn/info/1032/31245.htm"},
          {"source", "本科生院"},
          {"dept", "本科生院"},
          {"date", "2026-11-24"},
          {"tier", "other"},
          {"tags", nlohmann::json::array()},
      },
      {
          {"title", "关于做好2026年秋季学期下半段台风及强降雨极端天气教学应急预案的通知"},
          {"url", "https://ugs.hainanu.edu.cn/rcpy/jwyz.htm"},
          {"source", "教务运转"},
          {"dept", "本科生院"},
          {"date", "2026-11-22"},
          {"tier", "impact"},
          {"tags", nlohmann::json::array({"台风防汛", "影响出行"})},
      },
      {
          {"title", "关于2026-2027学年第1学期期末集中考试安排及学生缓考线上申请须知"},
          {"url", "https://ugs.hainanu.edu.cn/index/zytz.htm"},
          {"source", "重要通知"},
          {"dept", "本科生院"},
          {"date", "2026-11-20"},
          {"tier", "action"},
          {"tags", nlohmann::json::array({"要办事"})},
      },
      {
          {"title", "海甸校区部分学生公寓配电房例行检修及短时停电切换公告"},
          {"url", "https://www.hainanu.edu.cn/hdxw/zhsx.htm"},
          {"source", "海大主站"},
          {"dept", "后勤保障部"},
          {"date", "2026-11-19"},
          {"tier", "impact"},
          {"tags", nlohmann::json::array({"影响出行"})},
      },
      {
          {"title", "海大讲坛：人工智能大模型系统架构与边缘推理前沿报告会"},
          {"url", "https://www.hainanu.edu.cn/xywh/hdjt/djkxjjt.htm"},
          {"source", "海大讲坛"},
          {"dept", "科学技术发展院"},
          {"date", "2026-11-18"},
          {"tier", "activity"},
          {"tags", nlohmann::json::array({"活动"})},
      },
  };
}

}  // namespace hnu::adapters
