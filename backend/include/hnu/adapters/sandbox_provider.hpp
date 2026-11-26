#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <utility>

namespace hnu::adapters {

// 海南大学高保真演示/沙盒数据与算术验证码生成器
// 作用：
// 1. 生成真实的 SVG 算术验证码图片（供独立门户二次算术验证与演示教务登录使用，无第三方图片库依赖）
// 2. 提供测试学号 `2024300128`（密码 `hnu123456`）或 `demo`（密码 `demo123`），
//    内置真实海南大学海甸校区·计算机科学与技术学院·南海书院 11 节制课表、撞课/单双周样本、
//    多学期成绩与 GPA、期末考试座位、培养方案、一卡通流水与学习通/MOOC 作业，
//    确保在无校内真实账号或公网隔离环境下也能 100% 闭环验证所有核心功能。
class SandboxProvider {
 public:
  static bool is_demo_account(std::string_view account);
  static bool verify_demo_password(std::string_view account, std::string_view password);

  // 生成算术验证码：返回 <答案字符串, data:image/svg+xml;base64,...>
  static std::pair<std::string, std::string> generate_arithmetic_captcha_svg();

  // 生成 4 位字母数字验证码：返回 <答案大写字符串, data:image/svg+xml;base64,...>
  static std::pair<std::string, std::string> generate_code_captcha_svg();

  static nlohmann::json demo_profile(std::string_view account);
  static nlohmann::json demo_semesters();
  static nlohmann::json demo_timetable(std::string_view semester_id = "2026-2027-1");
  static nlohmann::json demo_grades(std::string_view semester_id = "");
  static nlohmann::json demo_exams(std::string_view semester_id = "2026-2027-1");
  static nlohmann::json demo_plan();
  static nlohmann::json demo_jw_todo();

  static nlohmann::json demo_portal_card();
  static nlohmann::json demo_portal_apps();
  static nlohmann::json demo_chaoxing_scan();
  static nlohmann::json demo_chaoxing_notices();
  static nlohmann::json demo_mooc_scan();
  static std::vector<nlohmann::json> fallback_hnu_notices();
};

}  // namespace hnu::adapters
