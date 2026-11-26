#pragma once

#include <nlohmann/json.hpp>

namespace hnu::services {

class CreditAnalyzer {
 public:
  // 结合海南大学培养方案/执行计划(plan)、历年成绩(grades)、本学期在修课表(timetable)与学籍(profile)核对学分完成度与待选修要求
  static nlohmann::json analyze(const nlohmann::json& plan,
                                const nlohmann::json& grades,
                                const nlohmann::json& timetable,
                                const nlohmann::json& profile = nlohmann::json::object(),
                                const nlohmann::json& preferences = nlohmann::json::object());
};

}  // namespace hnu::services
