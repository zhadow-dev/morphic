#include "churn_harness.h"

#include <windows.h>

#include "experiment_config.h"
#include "forensic_trace.h"
#include "surface_policy/surface_ecology.h"
#include "surface_policy/surface_kind.h"

namespace morphic {
namespace {
constexpr long kOpEveryTicks = 20;        // pace one churn op ~ every 20 ticks
constexpr size_t kPopulation = 6;         // rolling live spatial-surface count
constexpr long kBaselineAfterCycles = 10; // settle before snapshotting baseline
constexpr size_t kHistoryMax = 300;       // slope window
constexpr long kLogEveryCycles = 20;
constexpr long long kPrivSlopeLeakKB = 60000;  // ~60MB upward over the window
constexpr long kHoldTicks = 60;   // sequential: ~1s alive before close
constexpr long kWaitTicks = 150;  // sequential: ~2.5s quiescence after close

// Realistic-usage tuning (~60 ticks/s assumed for age reporting). Population +
// lifetime band are env-tunable members (see ctor) so age-at-teardown can be
// swept; the odds below stay fixed.
constexpr long kFps = 60;
constexpr unsigned kRealSpawnOdds = 45; // ~1-in-45 ticks spawns when below pop
constexpr unsigned kRealBurstOdds = 6;  // 1-in-6 spawns is a small burst

// Long-soak reporting (wall-clock, not tick-based — tick rate sags when idle).
constexpr long long kSoakSampleMs = 20000;   // sample resources every 20s
constexpr long long kSoakReportMs = 120000;  // emit a SOAK report every 2min
constexpr long long kSoakWindowMs = 300000;  // slope measured over a 5-min window
constexpr double kPlateauKBPerMin = 1500.0;  // below this slope = settling
constexpr int kPlateauConfirm = 3;           // consecutive settling reports = plateau
}  // namespace

ChurnHarness::ChurnHarness(const flutter::DartProject* project,
                           policy::SurfaceEcology* ecology, void* dxgi_adapter3)
    : project_(project), ecology_(ecology), dxgi_adapter3_(dxgi_adapter3) {
  const experiment::Config& cfg = experiment::Get();
  mode_ = cfg.churn_mode == "sequential" ? ChurnMode::kSequential
          : cfg.churn_mode == "rolling"  ? ChurnMode::kRolling
                                         : ChurnMode::kRealistic;
  min_life_s_ = cfg.churn_min_life_s;
  life_span_s_ = cfg.churn_life_span_s > 0 ? cfg.churn_life_span_s : 1;
  population_ = cfg.churn_population > 0 ? static_cast<size_t>(cfg.churn_population)
                                        : 1;
  forensic::Log("CHURN", "harness armed (mode=" + cfg.churn_mode + " life=" +
                             std::to_string(min_life_s_) + "-" +
                             std::to_string(min_life_s_ + life_span_s_) +
                             "s pop=" + std::to_string(population_) + ")");
}

std::string ChurnHarness::NextId() { return "churn_" + std::to_string(++seq_); }

void ChurnHarness::Tick() {
  // Don't mutate the surface population while a teardown reap is draining the
  // message pump (it can re-dispatch this tick). The reap owns teardown_ then.
  if (experiment::Reaping().load()) return;
  ++tick_;
  if (project_ == nullptr || ecology_ == nullptr) return;
  if (!EnsureWorkspace()) return;
  if (mode_ == ChurnMode::kSequential) {
    StepSequential();
  } else if (mode_ == ChurnMode::kRealistic) {
    StepRealistic();
    SoakTick();
  } else if (tick_ % kOpEveryTicks == 0) {
    Step();
  }
}

unsigned ChurnHarness::Rng(unsigned n) {
  // xorshift32 — deterministic, dependency-free; good enough to spread lifetimes
  // and spawn timing across a realistic band.
  rng_ ^= rng_ << 13;
  rng_ ^= rng_ >> 17;
  rng_ ^= rng_ << 5;
  return n ? rng_ % n : 0;
}

bool ChurnHarness::EnsureWorkspace() {
  if (!churn_ws_.empty()) return true;
  auto id = ecology_->SpawnSurface(*project_, policy::SurfaceKind::Workspace,
                                   "churn_ws", "homeSpawnedMain", 60, 60, 320,
                                   220);
  if (id) {
    churn_ws_ = *id;
    forensic::Log("CHURN", "churn workspace = " + churn_ws_);
  }
  return false;  // usable next tick
}

std::string ChurnHarness::SpawnOne() {
  policy::SpawnOverrides ov;
  ov.backend = "spatial";
  ov.shape = "rounded";
  ov.material = "acrylic";
  ov.chromeless = true;
  ov.corner_radius_px = 20;
  ov.elevation_px = 18;
  const int off = static_cast<int>((seq_ % 8) * 40);
  auto id = ecology_->SpawnSurface(*project_, policy::SurfaceKind::ToolPalette,
                                   NextId(), "homeSpawnedMain", 140 + off,
                                   140 + off, 240, 180, "", churn_ws_, ov);
  return id ? *id : std::string();
}

void ChurnHarness::Step() {
  // ROLLING/OVERLAPPING churn: continuous spawn + destroy — teardowns overlap.
  auto id = SpawnOne();
  if (!id.empty()) live_.push_back(id);
  while (live_.size() > kPopulation) {
    const std::string victim = live_.front();
    live_.pop_front();
    ecology_->DestroySurface(victim);
  }
  ++cycle_;
  SampleAndDetect();
}

void ChurnHarness::StepSequential() {
  ++phase_ticks_;
  switch (phase_) {
    case Phase::kSpawn:
      current_ = SpawnOne();
      forensic::Log("CHURN", "seq spawn " + current_);
      phase_ = Phase::kHold;
      phase_ticks_ = 0;
      break;
    case Phase::kHold:
      if (phase_ticks_ >= kHoldTicks) {
        phase_ = Phase::kClose;
        phase_ticks_ = 0;
      }
      break;
    case Phase::kClose:
      if (!current_.empty()) {
        forensic::Log("CHURN", "seq close " + current_);
        ecology_->DestroySurface(current_);
        current_.clear();
        ++cycle_;
        SampleAndDetect();
      }
      phase_ = Phase::kWait;
      phase_ticks_ = 0;
      break;
    case Phase::kWait:
      // Full quiescence: let the engine fully tear down before the next spawn,
      // so no two teardowns ever overlap (the concurrency-isolation variable).
      if (phase_ticks_ >= kWaitTicks) {
        phase_ = Phase::kSpawn;
        phase_ticks_ = 0;
      }
      break;
  }
}

void ChurnHarness::StepRealistic() {
  // 1) CLOSE: at most ONE aged-out surface per tick (paced like a human closing
  //    a window now and then — never a teardown storm). Engines reach 5-30s of
  //    age here, so we're tearing down LONG-LIVED, fully-settled, mostly-idle
  //    engines — the opposite of torture's ~2s-old back-to-back closes.
  for (size_t i = 0; i < realistic_.size(); ++i) {
    const long age_ticks = tick_ - realistic_[i].spawn_tick;
    if (age_ticks < realistic_[i].lifetime_ticks) continue;
    const long age_s = age_ticks / kFps;
    ++real_closes_;
    age_sum_s_ += age_s;
    if (age_s > age_max_s_) age_max_s_ = age_s;
    forensic::Log("REALCLOSE",
                  "id=" + realistic_[i].id + " age=" + std::to_string(age_s) +
                      "s live=" + std::to_string(realistic_.size()) +
                      " closed=" + std::to_string(real_closes_) +
                      " mean_age=" +
                      std::to_string(age_sum_s_ / real_closes_) +
                      "s max_age=" + std::to_string(age_max_s_) + "s");
    ecology_->DestroySurface(realistic_[i].id);
    realistic_.erase(realistic_.begin() + i);
    ++cycle_;
    SampleAndDetect();  // leak/drift accounting on the real cadence
    return;             // one close per tick — paced
  }

  // 2) MAINTAIN POPULATION: occasionally spawn back toward target, with the odd
  //    burst (user opens two things at once). Each new engine gets a randomized
  //    5-30s lifetime, so the population ages out at staggered times.
  if (realistic_.size() < population_ && Rng(kRealSpawnOdds) == 0) {
    const int burst = (Rng(kRealBurstOdds) == 0) ? 3 : 1;
    for (int b = 0; b < burst && realistic_.size() < population_; ++b) {
      auto id = SpawnOne();
      if (id.empty()) break;
      const long life =
          (min_life_s_ + static_cast<long>(Rng(life_span_s_))) * kFps;
      realistic_.push_back({id, tick_, life});
      forensic::Log("REALSPAWN", "id=" + id + " lifetime=" +
                                     std::to_string(life / kFps) + "s pop=" +
                                     std::to_string(realistic_.size()));
    }
  }
}

void ChurnHarness::SoakTick() {
  const long long now = static_cast<long long>(GetTickCount64());
  if (start_ms_ == 0) {
    start_ms_ = now;
    last_soak_sample_ms_ = now;
    last_soak_report_ms_ = now;
    return;
  }

  // 1) TIME-keyed resource sample (decoupled from close cadence — captures
  //    slow growth during idle stretches too).
  if (now - last_soak_sample_ms_ >= kSoakSampleMs) {
    last_soak_sample_ms_ = now;
    const long topo = res::WgcSessions() * 2 + 1;
    const res::Snapshot s = res::Capture(topo, dxgi_adapter3_);
    soak_.push_back({now - start_ms_, s.private_kb, s.hwnds, s.wgc, s.dcomp,
                     s.handles});
    if (soak_.size() > 4000) soak_.erase(soak_.begin());
  }

  // 2) Periodic SOAK report: growth-from-start + SLOPE over a moving window +
  //    explicit plateau detection (the leak-vs-retention discriminator).
  if (now - last_soak_report_ms_ < kSoakReportMs || soak_.size() < 2) return;
  last_soak_report_ms_ = now;

  const SoakSample& first = soak_.front();
  const SoakSample& last = soak_.back();
  const long long uptime_s = last.t_ms / 1000;

  // Oldest sample still inside the slope window.
  const long long win_start_t = last.t_ms - kSoakWindowMs;
  const SoakSample* w = &soak_.front();
  for (const auto& sm : soak_) {
    if (sm.t_ms >= win_start_t) {
      w = &sm;
      break;
    }
  }
  const double win_min = static_cast<double>(last.t_ms - w->t_ms) / 60000.0;
  const double slope_kb_min =
      win_min > 0.05 ? static_cast<double>(last.priv_kb - w->priv_kb) / win_min
                     : 0.0;

  const bool settling = win_min >= 2.0 && slope_kb_min < kPlateauKBPerMin;
  plateau_streak_ = settling ? plateau_streak_ + 1 : 0;
  const bool plateau = plateau_streak_ >= kPlateauConfirm;

  forensic::Log(
      "SOAK",
      "uptime=" + std::to_string(uptime_s) + "s closes=" +
          std::to_string(real_closes_) + " live=" +
          std::to_string(realistic_.size()) + " priv=" +
          std::to_string(last.priv_kb) + "KB from_start=+" +
          std::to_string(last.priv_kb - first.priv_kb) + "KB slope_" +
          std::to_string(static_cast<long>(win_min)) + "m=" +
          std::to_string(static_cast<long>(slope_kb_min)) + "KB/min hwnd_d=" +
          std::to_string(last.hwnds - first.hwnds) + " wgc_d=" +
          std::to_string(last.wgc - first.wgc) + " dcomp_d=" +
          std::to_string(last.dcomp - first.dcomp) + " handle_d=" +
          std::to_string(last.handles - first.handles) + " mean_age=" +
          std::to_string(real_closes_ ? age_sum_s_ / real_closes_ : 0) +
          "s | " + (plateau ? "PLATEAU" : (settling ? "settling" : "climbing")));

  if (plateau && !plateau_announced_) {
    plateau_announced_ = true;
    forensic::Log("SOAK",
                  "PLATEAU DETECTED — private-commit slope < " +
                      std::to_string(static_cast<long>(kPlateauKBPerMin)) +
                      "KB/min sustained, object counts flat: allocator/pool "
                      "RETENTION, not a leak");
  }
}

void ChurnHarness::SampleAndDetect() {
  // topo proxy: host + engine per live spatial surface, + the shell_root.
  const long topo = res::WgcSessions() * 2 + 1;
  const res::Snapshot s = res::Capture(topo, dxgi_adapter3_);

  if (!baseline_set_ && cycle_ >= kBaselineAfterCycles) {
    baseline_ = s;
    baseline_set_ = true;
    forensic::Log("CHURN", "baseline @cycle " + std::to_string(cycle_) + " | " +
                               res::Format(s));
  }

  history_.push_back(s);
  if (history_.size() > kHistoryMax) history_.erase(history_.begin());

  if (cycle_ % kLogEveryCycles != 0) return;
  forensic::Log("CHURN", "cycle=" + std::to_string(cycle_) + " | " +
                             res::Format(s));

  if (baseline_set_) {
    // Steady-state drift vs baseline (the population is constant, so these
    // SHOULD hover near zero; sustained growth = a leak).
    forensic::Log("CHURN DRIFT",
                  "vs baseline: priv=" +
                      std::to_string(s.private_kb - baseline_.private_kb) +
                      "KB hwnd=" + std::to_string(s.hwnds - baseline_.hwnds) +
                      " handle=" + std::to_string(s.handles - baseline_.handles) +
                      " gdi=" + std::to_string(s.gdi - baseline_.gdi) +
                      " user=" + std::to_string(s.user - baseline_.user) +
                      " wgc=" + std::to_string(s.wgc - baseline_.wgc) +
                      " dcomp=" + std::to_string(s.dcomp - baseline_.dcomp));

    // Slope over the window — delayed/eventual leak signal (not just a snapshot).
    if (history_.size() >= 60) {
      const res::Snapshot& a0 = history_.front();
      const res::Snapshot& a1 = history_.back();
      const long long priv_slope = a1.private_kb - a0.private_kb;
      if (priv_slope > kPrivSlopeLeakKB) {
        forensic::Log("CHURN LEAK?", "private-commit slope +" +
                                         std::to_string(priv_slope) +
                                         "KB over window");
      }
      if (a1.wgc > a0.wgc + 2 || a1.dcomp > a0.dcomp + 2) {
        forensic::Log("CHURN LEAK?",
                      "compositor objects rising: wgc " +
                          std::to_string(a0.wgc) + "->" + std::to_string(a1.wgc) +
                          " dcomp " + std::to_string(a0.dcomp) + "->" +
                          std::to_string(a1.dcomp));
      }
      if (a1.hwnds > a0.hwnds + 3) {
        forensic::Log("CHURN LEAK?", "HWND count rising: " +
                                         std::to_string(a0.hwnds) + "->" +
                                         std::to_string(a1.hwnds));
      }
    }
  }
}

}  // namespace morphic
