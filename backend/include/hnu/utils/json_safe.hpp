#pragma once

#include <cmath>
#include <cstdint>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <string_view>

namespace hnu::utils {

// 安全提取字符串：兼容 JSON string / integer / float / boolean / null，绝不抛 type_error.302
inline std::string json_str(const nlohmann::json& obj,
                            std::string_view key,
                            std::string_view fallback = "") {
  if (!obj.is_object()) return std::string(fallback);
  auto it = obj.find(std::string(key));
  if (it == obj.end() || it->is_null()) return std::string(fallback);
  if (it->is_string()) return it->get<std::string>();
  if (it->is_number_integer()) return std::to_string(it->get<int64_t>());
  if (it->is_number_unsigned()) return std::to_string(it->get<uint64_t>());
  if (it->is_number_float()) {
    std::ostringstream oss;
    oss << std::setprecision(12) << it->get<double>();
    return oss.str();
  }
  if (it->is_boolean()) return it->get<bool>() ? "true" : "false";
  return std::string(fallback);
}

// 安全提取 64 位整数：兼容 JSON integer / float / numeric string / boolean / null
inline int64_t json_i64(const nlohmann::json& obj,
                        std::string_view key,
                        int64_t fallback = 0) {
  if (!obj.is_object()) return fallback;
  auto it = obj.find(std::string(key));
  if (it == obj.end() || it->is_null()) return fallback;
  if (it->is_number_integer()) return it->get<int64_t>();
  if (it->is_number_unsigned()) return static_cast<int64_t>(it->get<uint64_t>());
  if (it->is_number_float()) return static_cast<int64_t>(std::llround(it->get<double>()));
  if (it->is_boolean()) return it->get<bool>() ? 1 : 0;
  if (it->is_string()) {
    const std::string& s = it->get_ref<const std::string&>();
    if (s.empty()) return fallback;
    try {
      size_t idx = 0;
      int64_t v = std::stoll(s, &idx, 10);
      return idx > 0 ? v : fallback;
    } catch (...) {
      return fallback;
    }
  }
  return fallback;
}

inline int json_int(const nlohmann::json& obj,
                    std::string_view key,
                    int fallback = 0) {
  return static_cast<int>(json_i64(obj, key, fallback));
}

inline double json_double(const nlohmann::json& obj,
                          std::string_view key,
                          double fallback = 0.0) {
  if (!obj.is_object()) return fallback;
  auto it = obj.find(std::string(key));
  if (it == obj.end() || it->is_null()) return fallback;
  if (it->is_number()) return it->get<double>();
  if (it->is_string()) {
    const std::string& s = it->get_ref<const std::string&>();
    if (s.empty()) return fallback;
    try {
      return std::stod(s);
    } catch (...) {
      return fallback;
    }
  }
  return fallback;
}

inline bool json_bool(const nlohmann::json& obj,
                      std::string_view key,
                      bool fallback = false) {
  if (!obj.is_object()) return fallback;
  auto it = obj.find(std::string(key));
  if (it == obj.end() || it->is_null()) return fallback;
  if (it->is_boolean()) return it->get<bool>();
  if (it->is_number()) return it->get<double>() != 0.0;
  if (it->is_string()) {
    const std::string& s = it->get_ref<const std::string&>();
    if (s == "true" || s == "1" || s == "yes" || s == "ok") return true;
    if (s == "false" || s == "0" || s == "no" || s.empty()) return false;
  }
  return fallback;
}

}  // namespace hnu::utils
