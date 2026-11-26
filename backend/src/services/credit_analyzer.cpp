#include "hnu/services/credit_analyzer.hpp"
#include "hnu/utils/json_safe.hpp"
#include "hnu/utils/strings_and_time.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace hnu::services {

namespace {

bool is_course_passed(const std::string& score_str) {
  std::string s = utils::trim(score_str);
  if (s.empty()) return false;
  if (s == "优" || s == "优秀" || s == "良" || s == "良好" ||
      s == "中" || s == "中等" || s == "及格" || s == "合格" || s == "通过") {
    return true;
  }
  try {
    return std::stod(s) >= 60.0;
  } catch (...) {
    return false;
  }
}

// 保留两位小数（海南大学劳动教育实践等课程为 0.25 学分，严禁四舍五入成 0.3！）
double round2(double v) {
  return std::round(v * 100.0) / 100.0;
}

std::string norm_cname(std::string_view raw) {
  std::string s = utils::to_lower(utils::trim(raw));
  static const std::pair<const char*, const char*> kNorms[] = {
      {"（", "("}, {"）", ")"}, {" ", ""}, {"\t", ""}, {"·", ""},
  };
  for (const auto& [from, to] : kNorms) {
    s = utils::replace_all(s, from, to);
  }
  return s;
}

bool is_intl_chinese_course(const std::string& code, const std::string& name) {
  // 海南大学强智执行计划表中常同时列出面向来华留学生的替代汉语必修课（如 GG02334 中级汉语综合I 8学分、GG02335 中级汉语综合II 8学分）
  if (name.find("汉语综合") != std::string::npos ||
      name.find("初级汉语") != std::string::npos ||
      name.find("中级汉语") != std::string::npos ||
      name.find("高级汉语") != std::string::npos ||
      name.find("留学生汉语") != std::string::npos) {
    return true;
  }
  return code == "GG02334" || code == "GG02335";
}

bool is_elective_cat_or_prop(const std::string& cat, bool required_flag) {
  if (!required_flag) return true;
  if (cat.find("选择性必修") != std::string::npos) return false;
  return cat.find("选修") != std::string::npos ||
         cat.find("通选") != std::string::npos ||
         cat.find("任选") != std::string::npos ||
         cat.find("限选") != std::string::npos;
}

bool is_general_elective_cat(const std::string& cat, const std::string& gen_cat) {
  if (!gen_cat.empty()) return true;
  return cat.find("通识") != std::string::npos ||
         cat.find("公选") != std::string::npos ||
         cat.find("公共选修") != std::string::npos ||
         cat.find("通选") != std::string::npos;
}

struct MajorGradPreset {
  bool exact_matched = false;
  std::string label;
  double grad_total = 0.0;
  double elective_total = 0.0;
  double general_elective = 6.0;
  double major_elective = 0.0;
  double second_class = 3.0;
  std::string general_rule =
      "人文通识经典类、科学精神与职业素养类、公共艺术类各至少修读 2 学分（合计至少 6 学分）";
};

MajorGradPreset match_hnu_major_preset(const nlohmann::json& profile) {
  MajorGradPreset p;
  std::string major = utils::trim(utils::json_str(profile, "major", ""));
  std::string grade = utils::trim(utils::json_str(profile, "grade", ""));
  std::string sid = utils::trim(utils::json_str(profile, "studentId", ""));
  if (grade.empty() && sid.size() >= 4) {
    grade = sid.substr(0, 4);
  }

  // 精确匹配海南大学官方已发布专业人才培养方案（如 2026级 / 2024级 软件工程(NIIT)）
  if (major.find("软件工程") != std::string::npos &&
      (major.find("NIIT") != std::string::npos || major.find("niit") != std::string::npos)) {
    p.exact_matched = true;
    if (grade == "2024") {
      p.label = "2024级 软件工程(NIIT) 培养方案";
      p.grad_total = 175.0;
      p.elective_total = 20.0;
      p.general_elective = 6.0;
      p.major_elective = 14.0;
      p.second_class = 3.0;
    } else {
      // 2025/2026级最新修订版培养方案：毕业应修 155 学分，选修至少 26 学分（通识≥6 + 专业≥20），另第二课堂≥3学分
      p.label = (grade.empty() ? "2026" : grade) + "级 软件工程(NIIT) 培养方案";
      p.grad_total = 155.0;
      p.elective_total = 26.0;
      p.general_elective = 6.0;
      p.major_elective = 20.0;
      p.second_class = 3.0;
    }
    return p;
  }

  p.exact_matched = false;
  p.label = major.empty() ? "海南大学本科人才培养方案通用标准"
                          : (grade.empty() ? major : (grade + "级 " + major));
  p.grad_total = 0.0;
  p.elective_total = 0.0;
  p.general_elective = 6.0;
  p.major_elective = 0.0;
  p.second_class = 3.0;
  return p;
}

}  // namespace

nlohmann::json CreditAnalyzer::analyze(const nlohmann::json& plan,
                                       const nlohmann::json& grades,
                                       const nlohmann::json& timetable,
                                       const nlohmann::json& profile,
                                       const nlohmann::json& preferences) {
  if (!plan.is_object() || !utils::json_bool(plan, "found", false)) {
    return {
        {"ready", false},
        {"error", plan.is_object() ? utils::json_str(plan, "error", "暂无培养方案数据") : "尚未同步培养方案"},
    };
  }

  // 1. 构建已通过课程索引（按 code 与归一化课程名双索引）
  struct GradeHit {
    std::string code;
    std::string name;
    std::string score;
    std::string category;
    std::string nature;
    std::string general_cat;
    double credit = 0.0;
    bool matched_plan = false;
  };
  std::vector<GradeHit> passed_list;
  std::map<std::string, size_t> passed_by_code;
  std::map<std::string, size_t> passed_by_nname;
  double total_earned = 0.0;

  if (grades.is_object() && grades.contains("courses") && grades["courses"].is_array()) {
    std::set<std::string> counted;
    for (const auto& g : grades["courses"]) {
      std::string code = utils::trim(utils::json_str(g, "code", ""));
      std::string name = utils::trim(utils::json_str(g, "name", ""));
      std::string score = utils::trim(utils::json_str(g, "score", utils::json_str(g, "final", "")));
      if (!is_course_passed(score)) continue;

      double cr = utils::json_double(g, "credit", 0.0);
      std::string uk = code.empty() ? norm_cname(name) : code;
      if (uk.empty() || !counted.insert(uk).second) continue;

      size_t idx = passed_list.size();
      passed_list.push_back(GradeHit{
          code,
          name,
          score,
          utils::trim(utils::json_str(g, "category", "")),
          utils::trim(utils::json_str(g, "nature", "")),
          utils::trim(utils::json_str(g, "generalCategory", "")),
          cr,
          false,
      });
      if (!code.empty()) passed_by_code[code] = idx;
      std::string nn = norm_cname(name);
      if (!nn.empty()) passed_by_nname[nn] = idx;
      total_earned += cr;
    }
  }

  // 应用替代课程映射（若替代课已过，原课视为通过）
  if (plan.contains("subs") && plan["subs"].is_array()) {
    for (const auto& sub : plan["subs"]) {
      if (!sub.is_object() || !sub.contains("from") || !sub.contains("to")) continue;
      std::string from_code = utils::trim(utils::json_str(sub["from"], "code", ""));
      std::string to_code = utils::trim(utils::json_str(sub["to"], "code", ""));
      if (!from_code.empty() && !to_code.empty() &&
          passed_by_code.count(to_code) && !passed_by_code.count(from_code)) {
        passed_by_code[from_code] = passed_by_code[to_code];
      }
    }
  }

  // 2. 本学期在修课程去重集合（注意课表中同一门课因单双周/实验分拆可能出现多条，必须按课程名去重！）
  struct StudyingCourse {
    std::string code;
    std::string name;
    std::string nname;
    bool matched_plan = false;
  };
  std::vector<StudyingCourse> studying_list;
  std::map<std::string, size_t> studying_by_code;
  std::map<std::string, size_t> studying_by_nname;
  if (timetable.is_object() && timetable.contains("courses") && timetable["courses"].is_array()) {
    std::set<std::string> seen_tt;
    for (const auto& c : timetable["courses"]) {
      std::string code = utils::trim(utils::json_str(c, "code", ""));
      std::string name = utils::trim(utils::json_str(c, "name", ""));
      std::string nn = norm_cname(name);
      if (nn.empty() || !seen_tt.insert(nn).second) continue;

      size_t idx = studying_list.size();
      studying_list.push_back({code, name, nn, false});
      if (!code.empty() && !utils::starts_with(code, "HNU_")) {
        studying_by_code[code] = idx;
      }
      studying_by_nname[nn] = idx;
    }
  }

  // 3. 清洗培养方案课程列表：
  //    ① 自动剔除国内学生未修读的来华留学生替代课程（如中级汉语综合I/II 共 16 学分）
  //    ② 自动合并同课程组的父子重复条目（如 GG00029 劳动教育实践1 0.25 与 GG0002945C 劳动教育实践1-书院 0.25）
  struct CleanPlanItem {
    std::string code;
    std::string name;
    std::string nname;
    std::string cat;
    std::string term_label;
    double credit = 0.0;
    bool required = true;
    nlohmann::json terms = nlohmann::json::array();
    std::vector<std::string> alias_codes;
    std::vector<std::string> alias_nnames;
  };
  std::vector<CleanPlanItem> raw_items;
  double filtered_intl_credits = 0.0;

  if (plan.contains("courses") && plan["courses"].is_array()) {
    for (const auto& pc : plan["courses"]) {
      std::string code = utils::trim(utils::json_str(pc, "code", ""));
      std::string name = utils::trim(utils::json_str(pc, "name", ""));
      if (name.empty()) continue;
      std::string nn = norm_cname(name);
      double cr = utils::json_double(pc, "credit", 0.0);
      std::string cat = utils::trim(utils::json_str(pc, "cat", "其他模块"));
      if (cat.empty()) cat = "其他模块";

      bool in_student_record = (!code.empty() && (passed_by_code.count(code) || studying_by_code.count(code))) ||
                               passed_by_nname.count(nn) || studying_by_nname.count(nn);
      if (is_intl_chinese_course(code, name) && !in_student_record) {
        filtered_intl_credits += cr;
        continue;
      }

      bool req = utils::json_bool(pc, "required", true);
      if (cat.find("选") != std::string::npos && cat.find("选择性必修") == std::string::npos) {
        req = false;
      }
      raw_items.push_back(CleanPlanItem{
          code,
          name,
          nn,
          cat,
          utils::trim(utils::json_str(pc, "termLabel", "")),
          cr,
          req,
          pc.contains("terms") && pc["terms"].is_array() ? pc["terms"] : nlohmann::json::array(),
          {},
          {},
      });
    }
  }

  // 按课程号长度升序稳定排序，确保父项（如 GG00029）始终排在子项（如 GG0002945C）之前，避免乱序双计学分
  std::stable_sort(raw_items.begin(), raw_items.end(),
                   [](const CleanPlanItem& a, const CleanPlanItem& b) {
                     return a.code.size() < b.code.size();
                   });

  std::vector<CleanPlanItem> cleaned_items;
  double dedup_sub_credits = 0.0;
  std::vector<bool> merged_as_child(raw_items.size(), false);
  for (size_t i = 0; i < raw_items.size(); ++i) {
    if (merged_as_child[i]) continue;
    CleanPlanItem parent = raw_items[i];
    for (size_t j = 0; j < raw_items.size(); ++j) {
      if (i == j || merged_as_child[j]) continue;
      const auto& cand = raw_items[j];
      // 若 cand 课程号以 parent 课程号为前缀且更长（如 GG00029 vs GG0002945C）、课程名亦以 parent 为前缀且学分相同，则为子方向/场地分项
      if (!parent.code.empty() && cand.code.size() > parent.code.size() &&
          utils::starts_with(cand.code, parent.code) &&
          utils::starts_with(cand.nname, parent.nname) &&
          std::abs(cand.credit - parent.credit) < 1e-6) {
        parent.alias_codes.push_back(cand.code);
        parent.alias_nnames.push_back(cand.nname);
        merged_as_child[j] = true;
        dedup_sub_credits += cand.credit;
      }
    }
    cleaned_items.push_back(std::move(parent));
  }

  // 4. 逐门核对培养方案课程状态并汇总模块学分
  std::map<std::string, double> cat_plan_sum;
  std::map<std::string, double> cat_earned;
  std::map<std::string, double> cat_studying;
  std::vector<std::string> cat_order;
  auto ensure_cat_order = [&](const std::string& c) {
    if (std::find(cat_order.begin(), cat_order.end(), c) == cat_order.end()) {
      cat_order.push_back(c);
    }
  };

  nlohmann::json course_rows = nlohmann::json::array();
  double total_studying = 0.0;
  double cleaned_courses_sum = 0.0;
  bool has_elective_in_plan = false;

  for (const auto& pc : cleaned_items) {
    ensure_cat_order(pc.cat);
    cat_plan_sum[pc.cat] += pc.credit;
    cleaned_courses_sum += pc.credit;
    if (is_elective_cat_or_prop(pc.cat, pc.required)) {
      has_elective_in_plan = true;
    }

    // 匹配成绩（含别名子课程）
    std::optional<size_t> hit_grade;
    if (!pc.code.empty() && passed_by_code.count(pc.code)) hit_grade = passed_by_code[pc.code];
    else if (passed_by_nname.count(pc.nname)) hit_grade = passed_by_nname[pc.nname];
    for (const auto& ac : pc.alias_codes) {
      if (!hit_grade && passed_by_code.count(ac)) hit_grade = passed_by_code[ac];
    }
    for (const auto& an : pc.alias_nnames) {
      if (!hit_grade && passed_by_nname.count(an)) hit_grade = passed_by_nname[an];
    }

    // 匹配在修课表（含别名子课程与前缀模糊匹配）
    std::optional<size_t> hit_study;
    if (!hit_grade) {
      if (!pc.code.empty() && studying_by_code.count(pc.code)) hit_study = studying_by_code[pc.code];
      else if (studying_by_nname.count(pc.nname)) hit_study = studying_by_nname[pc.nname];
      for (const auto& ac : pc.alias_codes) {
        if (!hit_study && studying_by_code.count(ac)) hit_study = studying_by_code[ac];
      }
      for (const auto& an : pc.alias_nnames) {
        if (!hit_study && studying_by_nname.count(an)) hit_study = studying_by_nname[an];
      }
    }

    std::string state = "pending";
    std::string score_str;
    if (hit_grade) {
      state = "passed";
      passed_list[*hit_grade].matched_plan = true;
      score_str = passed_list[*hit_grade].score;
      cat_earned[pc.cat] += pc.credit;
    } else if (hit_study) {
      state = "studying";
      studying_list[*hit_study].matched_plan = true;
      cat_studying[pc.cat] += pc.credit;
      total_studying += pc.credit;
    }

    course_rows.push_back({
        {"code", pc.code},
        {"name", pc.name},
        {"credit", round2(pc.credit)},
        {"cat", pc.cat},
        {"required", pc.required},
        {"terms", pc.terms},
        {"termLabel", pc.term_label},
        {"state", state},
        {"score", score_str},
    });
  }

  // 5. 归集未列在固定计划表中的已修选修课（如全校通识选修课、跨专业选修课）！
  double gen_elec_earned = 0.0, gen_elec_studying = 0.0;
  double maj_elec_earned = 0.0, maj_elec_studying = 0.0;

  // 先统计计划内选修课的已获与在修（同样覆盖父子别名课程）
  for (const auto& pc : cleaned_items) {
    if (!is_elective_cat_or_prop(pc.cat, pc.required)) continue;
    bool is_gen = is_general_elective_cat(pc.cat, "");
    bool passed = (!pc.code.empty() && passed_by_code.count(pc.code)) || passed_by_nname.count(pc.nname);
    if (!passed) {
      for (const auto& ac : pc.alias_codes) {
        if (passed_by_code.count(ac)) { passed = true; break; }
      }
    }
    if (!passed) {
      for (const auto& an : pc.alias_nnames) {
        if (passed_by_nname.count(an)) { passed = true; break; }
      }
    }
    bool studying = false;
    if (!passed) {
      studying = (!pc.code.empty() && studying_by_code.count(pc.code)) || studying_by_nname.count(pc.nname);
      if (!studying) {
        for (const auto& ac : pc.alias_codes) {
          if (studying_by_code.count(ac)) { studying = true; break; }
        }
      }
      if (!studying) {
        for (const auto& an : pc.alias_nnames) {
          if (studying_by_nname.count(an)) { studying = true; break; }
        }
      }
    }
    if (passed) {
      if (is_gen) gen_elec_earned += pc.credit;
      else maj_elec_earned += pc.credit;
    } else if (studying) {
      if (is_gen) gen_elec_studying += pc.credit;
      else maj_elec_studying += pc.credit;
    }
  }

  // 再归集计划表外已通过的选修/通识课程（例如东坡诗词、美育通选等）
  auto match_existing_elective_cat = [&](bool is_gen, const std::string& raw_cat) -> std::string {
    for (const auto& c : plan.value("cats", nlohmann::json::array())) {
      std::string cn = utils::json_str(c, "name", "");
      if (is_gen && is_general_elective_cat(cn, "")) return cn;
      if (!is_gen && cn.find("选修") != std::string::npos && !is_general_elective_cat(cn, "")) return cn;
    }
    if (!raw_cat.empty()) return raw_cat;
    return is_gen ? "通识类选修课" : "专业类选修课";
  };

  for (const auto& g : passed_list) {
    if (g.matched_plan) continue;
    bool is_gen = is_general_elective_cat(g.category, g.general_cat) || g.nature.find("公选") != std::string::npos;
    std::string target_cat = match_existing_elective_cat(is_gen, g.category);
    ensure_cat_order(target_cat);
    cat_earned[target_cat] += g.credit;
    if (is_gen) gen_elec_earned += g.credit;
    else if (g.nature.find("选") != std::string::npos || g.category.find("选") != std::string::npos) {
      maj_elec_earned += g.credit;
    }
    course_rows.push_back({
        {"code", g.code},
        {"name", g.name},
        {"credit", round2(g.credit)},
        {"cat", target_cat},
        {"required", false},
        {"terms", nlohmann::json::array()},
        {"termLabel", ""},
        {"state", "passed"},
        {"score", g.score},
        {"extraElective", true},
    });
  }

  // 6. 构建模块目标列表，精准判定当前数据是「完整毕业要求」还是「阶段执行计划（未含未选选修课）」
  std::map<std::string, double> module_targets;
  double explicit_plan_total = utils::json_double(plan, "total", 0.0);
  double raw_cats_sum = 0.0;
  if (plan.contains("cats") && plan["cats"].is_array()) {
    for (const auto& c : plan["cats"]) {
      std::string cn = utils::trim(utils::json_str(c, "name", ""));
      double cr = utils::json_double(c, "credit", 0.0);
      if (cn.empty()) continue;
      raw_cats_sum += cr;
      if (cn.find("选") != std::string::npos && cn.find("选择性必修") == std::string::npos) {
        has_elective_in_plan = true;
      }
      module_targets[cn] = cr;
      ensure_cat_order(cn);
    }
  }

  // 若原始 cats 仅仅是 pyfa_query 已列课程的直接求和（未单独给出模块定额），则必须同步扣除被清洗掉的留学生课与重复子项学分！
  bool cats_were_just_course_sums = (std::abs(raw_cats_sum - (cleaned_courses_sum + filtered_intl_credits + dedup_sub_credits)) < 0.05) &&
                                    (std::abs(explicit_plan_total - raw_cats_sum) < 0.05);
  if (cats_were_just_course_sums) {
    module_targets = cat_plan_sum;
    explicit_plan_total = cleaned_courses_sum;
  }

  // 若强智学业完成情况 (gradSummary) 返回了权威毕业模块要求，优先采用权威毕业要求！
  bool has_official_grad_summary = plan.contains("gradSummary") &&
                                   plan["gradSummary"].is_object() &&
                                   utils::json_bool(plan["gradSummary"], "found", false);
  if (has_official_grad_summary) {
    const auto& gs = plan["gradSummary"];
    explicit_plan_total = utils::json_double(gs, "totalRequired", explicit_plan_total);
    module_targets.clear();
    cat_order.clear();
    if (gs.contains("modules") && gs["modules"].is_array()) {
      for (const auto& m : gs["modules"]) {
        std::string mn = utils::trim(utils::json_str(m, "name", ""));
        double mcr = utils::json_double(m, "credit", 0.0);
        if (!mn.empty() && mcr > 0.0) {
          module_targets[mn] = mcr;
          ensure_cat_order(mn);
          if (mn.find("选") != std::string::npos) has_elective_in_plan = true;
        }
      }
    }
    // 保留所有存在已获或正修学分但未命中官方模块名的额外类别（如计划外选修课），防止丢学分
    for (const auto& [cname, ev] : cat_earned) {
      if (ev > 0.0) ensure_cat_order(cname);
    }
    for (const auto& [cname, sv] : cat_studying) {
      if (sv > 0.0) ensure_cat_order(cname);
    }
  }

  bool is_graduation_plan = has_official_grad_summary ||
                            (explicit_plan_total >= 135.0 && has_elective_in_plan);

  nlohmann::json modules = nlohmann::json::array();
  double plan_required_total = 0.0;
  for (const auto& cname : cat_order) {
    double req = module_targets.count(cname) ? module_targets[cname] : cat_plan_sum[cname];
    double earned = cat_earned[cname];
    double stud = cat_studying[cname];
    if (req <= 0.0 && earned <= 0.0 && stud <= 0.0) continue;
    plan_required_total += req;

    int pct = req > 0 ? std::min(100, static_cast<int>(std::round((earned / req) * 100.0))) : 0;
    int pct_with_stud = req > 0 ? std::min(100, static_cast<int>(std::round(((earned + stud) / req) * 100.0))) : 0;
    modules.push_back({
        {"name", cname},
        {"required", round2(req)},
        {"earned", round2(earned)},
        {"studying", round2(stud)},
        {"remainAfterEarned", round2(std::max(0.0, req - earned))},
        {"remain", round2(std::max(0.0, req - earned - stud))},
        {"percent", pct},
        {"percentWithStudying", pct_with_stud},
    });
  }

  double primary_target = is_graduation_plan
                              ? std::max(explicit_plan_total, plan_required_total)
                              : plan_required_total;

  // 支持学生在偏好设置中自定义本专业毕业总学分要求
  double user_grad_override = 0.0;
  if (preferences.is_object() && preferences.contains("remindCfg") && preferences["remindCfg"].is_object()) {
    user_grad_override = utils::json_double(preferences["remindCfg"], "gradTargetCredits", 0.0);
  }

  auto preset = match_hnu_major_preset(profile);
  double grad_total_ref = user_grad_override >= 100.0 ? user_grad_override : preset.grad_total;
  double elec_total_ref = preset.elective_total > 0.0 ? preset.elective_total : preset.general_elective;
  double unscheduled_compulsory_est =
      grad_total_ref > 0.0
          ? round2(std::max(0.0, grad_total_ref - elec_total_ref - plan_required_total))
          : 0.0;

  nlohmann::json grad_audit = {
      {"matchedMajor", preset.label},
      {"exactMatched", preset.exact_matched},
      {"customTarget", user_grad_override >= 100.0 ? round2(user_grad_override) : 0.0},
      {"gradTotalRequired", round2(grad_total_ref)},
      {"electiveTotalRequired", round2(preset.elective_total)},
      {"generalElectiveRequired", round2(preset.general_elective)},
      {"generalElectiveEarned", round2(gen_elec_earned)},
      {"generalElectiveStudying", round2(gen_elec_studying)},
      {"generalElectiveRemain", round2(std::max(0.0, preset.general_elective - gen_elec_earned - gen_elec_studying))},
      {"generalElectiveRule", preset.general_rule},
      {"majorElectiveRequired", round2(preset.major_elective)},
      {"majorElectiveEarned", round2(maj_elec_earned)},
      {"majorElectiveStudying", round2(maj_elec_studying)},
      {"majorElectiveRemain", preset.major_elective > 0.0
                                  ? round2(std::max(0.0, preset.major_elective - maj_elec_earned - maj_elec_studying))
                                  : 0.0},
      {"secondClassRequired", round2(preset.second_class)},
      {"unscheduledCompulsoryEst", unscheduled_compulsory_est},
      {"gradRemainAfterEarned", grad_total_ref > 0.0 ? round2(std::max(0.0, grad_total_ref - total_earned)) : 0.0},
      {"gradRemainAfterStudying", grad_total_ref > 0.0
                                      ? round2(std::max(0.0, grad_total_ref - total_earned - total_studying))
                                      : 0.0},
      {"gradPercentWithStudying", grad_total_ref > 0.0
                                      ? std::min(100, static_cast<int>(std::round(((total_earned + total_studying) / grad_total_ref) * 100.0)))
                                      : 0},
  };

  int overall_pct = primary_target > 0
                        ? std::min(100, static_cast<int>(std::round((total_earned / primary_target) * 100.0)))
                        : 0;
  int overall_pct_studying = primary_target > 0
                                 ? std::min(100, static_cast<int>(std::round(((total_earned + total_studying) / primary_target) * 100.0)))
                                 : 0;

  std::string scope_title = is_graduation_plan
                                ? "海南大学本科专业培养方案毕业要求"
                                : "当前教务已下发执行计划（阶段核对）";
  std::string scope_note = is_graduation_plan
                               ? ""
                               : "教务系统「执行计划」目前仅包含已下发的阶段必修/实践课，尚未计入暂未选修的通识选修课、专业选修课及后续学年待下发课程，因此上方数字并非毕业总学分要求。";

  return {
      {"ready", true},
      {"planScope", is_graduation_plan ? "graduation" : "execution_plan"},
      {"isGraduationPlan", is_graduation_plan},
      {"scopeTitle", scope_title},
      {"scopeNote", scope_note},
      {"totalRequired", round2(primary_target)},
      {"planRequired", round2(plan_required_total)},
      {"totalEarned", round2(total_earned)},
      {"totalStudying", round2(total_studying)},
      {"remainAfterEarned", round2(std::max(0.0, primary_target - total_earned))},
      {"totalRemain", round2(std::max(0.0, primary_target - total_earned - total_studying))},
      {"percent", overall_pct},
      {"percentWithStudying", overall_pct_studying},
      {"filteredIntlCredits", round2(filtered_intl_credits)},
      {"dedupSubCredits", round2(dedup_sub_credits)},
      {"modules", modules},
      {"graduationAudit", grad_audit},
      {"courses", course_rows},
      {"specials", plan.value("specials", nlohmann::json::array())},
  };
}

}  // namespace hnu::services
