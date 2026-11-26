#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace hnu::services {

class CalendarIcsGenerator {
 public:
  // 生成标准 RFC 5545 iCalendar (.ics) 文本，包含课表、考试安排与未交作业截止提醒
  static std::string build_user_ics(const nlohmann::json& timetable,
                                    const nlohmann::json& exams,
                                    const nlohmann::json& cx_todo,
                                    const nlohmann::json& mooc_todo,
                                    const nlohmann::json& prefs,
                                    const nlohmann::json& bell_presets);
};

}  // namespace hnu::services
