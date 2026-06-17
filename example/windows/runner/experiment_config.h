#ifndef RUNNER_EXPERIMENT_CONFIG_H_
#define RUNNER_EXPERIMENT_CONFIG_H_

#include <atomic>
#include <string>

// PHASE 11 — TEARDOWN EXPERIMENT CONFIG (env-driven, read once at startup).
//
// The flutter_windows.dll engine-teardown race (crash at flutter_windows.dll+
// 0xCEEE25 on a worker thread during FlutterViewController destruction) is the
// blocker to long-duration soak. To attack it scientifically we need to SWEEP
// shutdown-sequencing strategies and measure crash incidence per teardown
// STATISTICALLY — many runs, many configs, no recompile between them.
//
// So the churn driver and the teardown reaper read their knobs from environment
// variables here. A PowerShell sweep harness sets the vars + auto-restarts on
// crash, accumulating "teardowns survived" per config. When NO vars are set the
// config is the committed BASELINE (harness disarmed, teardown == today) so
// normal runs are unchanged.
namespace morphic {
namespace experiment {

struct Config {
  // --- churn driver (MORPHIC_CHURN_*) ---
  bool churn_armed = false;          // MORPHIC_CHURN=1
  std::string churn_mode = "realistic";  // rolling|sequential|realistic
  long churn_min_life_s = 5;         // realistic: youngest engine age at close
  long churn_life_span_s = 25;       // realistic: + [0,span) -> age band
  long churn_population = 5;          // realistic steady live count

  // --- teardown reaper (MORPHIC_TD_*) ---
  std::string td_name = "baseline";  // MORPHIC_TD_NAME (label only, for the ledger)
  double td_quiesce_ms = 150.0;      // MORPHIC_TD_QUIESCE_MS — quarantine before destroy
  long td_cooldown_ticks = 8;        // MORPHIC_TD_COOLDOWN — gap between destroys
  bool td_drain_pump = false;        // MORPHIC_TD_DRAIN=1 — bounded message-pump drain pre-reset
  double td_present_idle_ms = 0.0;   // MORPHIC_TD_PRESENTIDLE_MS — require present-idle (0=off)
  bool td_ledger = false;            // MORPHIC_TD_LEDGER=1 — emit per-teardown TDSTAT lines

  long run_id = 0;                   // MORPHIC_RUN_ID — sweep harness run counter (ledger tag)

  // --- engine retention (R1) ---
  long dormant_cap = 12;             // MORPHIC_DORMANT_CAP — fixed dormant-pool cap
};

// Lazily reads the environment on first call; cached thereafter.
const Config& Get();

// Dump the resolved config to the forensic trace (called once at startup).
void LogConfig();

// Re-entrancy guard shared between the teardown reaper and the churn driver.
// The "drain" teardown strategy pumps the message loop mid-reap; that can
// re-dispatch the frame-clock tick (which calls both TickTeardown AND the churn
// step). Both check this and no-op while a reap is in flight, so the in-flight
// teardown_ entry + iteration stay valid. One instance across TUs (inline).
inline std::atomic<bool>& Reaping() {
  static std::atomic<bool> r{false};
  return r;
}

}  // namespace experiment
}  // namespace morphic

#endif  // RUNNER_EXPERIMENT_CONFIG_H_
