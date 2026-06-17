#include "experiment_config.h"

#include <cstdlib>

#include "forensic_trace.h"

namespace morphic {
namespace experiment {
namespace {

std::string EnvStr(const char* name, const std::string& def) {
  char* buf = nullptr;
  size_t len = 0;
  if (_dupenv_s(&buf, &len, name) == 0 && buf) {
    std::string v(buf);
    free(buf);
    if (!v.empty()) return v;
  }
  return def;
}

long EnvLong(const char* name, long def) {
  const std::string v = EnvStr(name, "");
  if (v.empty()) return def;
  return std::strtol(v.c_str(), nullptr, 10);
}

double EnvDouble(const char* name, double def) {
  const std::string v = EnvStr(name, "");
  if (v.empty()) return def;
  return std::strtod(v.c_str(), nullptr);
}

bool EnvBool(const char* name, bool def) {
  const std::string v = EnvStr(name, "");
  if (v.empty()) return def;
  return v == "1" || v == "true" || v == "TRUE";
}

Config Read() {
  Config c;
  c.churn_armed = EnvBool("MORPHIC_CHURN", false);
  c.churn_mode = EnvStr("MORPHIC_CHURN_MODE", "realistic");
  c.churn_min_life_s = EnvLong("MORPHIC_CHURN_MINLIFE_S", 5);
  c.churn_life_span_s = EnvLong("MORPHIC_CHURN_LIFESPAN_S", 25);
  c.churn_population = EnvLong("MORPHIC_CHURN_POP", 5);

  c.td_name = EnvStr("MORPHIC_TD_NAME", "baseline");
  c.td_quiesce_ms = EnvDouble("MORPHIC_TD_QUIESCE_MS", 150.0);
  c.td_cooldown_ticks = EnvLong("MORPHIC_TD_COOLDOWN", 8);
  c.td_drain_pump = EnvBool("MORPHIC_TD_DRAIN", false);
  c.td_present_idle_ms = EnvDouble("MORPHIC_TD_PRESENTIDLE_MS", 0.0);
  c.td_ledger = EnvBool("MORPHIC_TD_LEDGER", false);

  c.run_id = EnvLong("MORPHIC_RUN_ID", 0);
  c.dormant_cap = EnvLong("MORPHIC_DORMANT_CAP", 12);
  return c;
}

}  // namespace

const Config& Get() {
  static const Config c = Read();
  return c;
}

void LogConfig() {
  const Config& c = Get();
  if (!c.churn_armed && c.td_name == "baseline") {
    forensic::Log("EXPERIMENT", "config=BASELINE (no MORPHIC_* env; normal run)");
    return;
  }
  forensic::Log(
      "EXPERIMENT",
      "run_id=" + std::to_string(c.run_id) + " churn=" +
          (c.churn_armed ? c.churn_mode : std::string("off")) + " life=" +
          std::to_string(c.churn_min_life_s) + "-" +
          std::to_string(c.churn_min_life_s + c.churn_life_span_s) + "s pop=" +
          std::to_string(c.churn_population) + " | TD name=" + c.td_name +
          " quiesce=" + std::to_string(static_cast<long>(c.td_quiesce_ms)) +
          "ms cooldown=" + std::to_string(c.td_cooldown_ticks) + "t drain=" +
          (c.td_drain_pump ? "1" : "0") + " presentidle=" +
          std::to_string(static_cast<long>(c.td_present_idle_ms)) + "ms");
}

}  // namespace experiment
}  // namespace morphic
