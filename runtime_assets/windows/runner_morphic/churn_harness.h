#ifndef RUNNER_CHURN_HARNESS_H_
#define RUNNER_CHURN_HARNESS_H_

#include <flutter/dart_project.h>

#include <deque>
#include <string>
#include <vector>

#include "resource_counters.h"

namespace morphic {
namespace policy {
class SurfaceEcology;
}

// PHASE 11 — CHURN / LEAK HARNESS (permanent stress infrastructure, not a
// one-off). Tick-driven on the UI thread (so spawn/destroy stay thread-safe),
// it rolls a live population of spatial surfaces — continuously spawning and
// destroying — while sampling process + compositor resources and watching for
// upward DRIFT/SLOPE over long runs (the delayed/async leaks that single
// baseline snapshots miss). The invariant assertion layer fires inline at every
// transition it drives, so a correctness break halts at the exact transition.
//
// Long-term this is the seed of CI stress mode / nightly soak / pre-release
// qualification. ARMED + TUNED via environment (experiment_config.h) so a sweep
// harness can vary churn mode / engine-lifetime band / teardown strategy across
// auto-restarted runs WITHOUT recompiling. No MORPHIC_CHURN env -> disarmed.
//
// FINDINGS so far (it earned its keep immediately): fixed an infinite-Drop
// reentrancy, a double-SurfaceDestroyed use-after-free, and a controller-release
// ordering bug. CHARACTERIZED the flutter_windows.dll engine-teardown race
// (crash at +0xCEEE25 on a worker thread during FlutterViewController dtor):
// serialization mitigates but does NOT eliminate it; a single, paced, quiesced,
// 26s-old engine teardown still reproduces it (~1%/close, mode/age-roughly-
// independent). A Flutter EMBEDDER defect, not architectural invalidity — but at
// ~1%/close it BLOCKS long soak, so this campaign sweeps shutdown-sequencing
// strategies (experiment_config.h) to drive incidence down. NOT shared-engine.

// Harness modes (which question are we answering?):
//  kRolling    — abusive overlapping spawn/destroy; teardowns overlap. Finds
//                latent races (found all three teardown bugs + the engine race).
//  kSequential — strictly serialized spawn->hold->close->WAIT for quiescence; no
//                two teardowns overlap. The concurrency-isolation probe.
//  kRealistic  — HUMAN-USAGE distribution: a small population of LONG-LIVED,
//                mostly-idle engines, closed occasionally and PACED (one at a
//                time), with the odd spawn burst. Engine-age band is env-tunable
//                so age-at-teardown sensitivity (2s/30s/5min/...) can be swept.
enum class ChurnMode { kRolling, kSequential, kRealistic };

class ChurnHarness {
 public:
  ChurnHarness(const flutter::DartProject* project,
               policy::SurfaceEcology* ecology, void* dxgi_adapter3);

  // Drive one harness step on the frame tick (paced internally).
  void Tick();

 private:
  enum class Phase { kSpawn, kHold, kClose, kWait };
  void Step();             // rolling/overlapping population
  void StepSequential();   // one-at-a-time with quiescence
  void StepRealistic();    // human-usage distribution (operational envelope)
  void SoakTick();         // time-based slope/plateau reporter (long-soak instrument)
  bool EnsureWorkspace();  // returns true once the churn workspace exists
  std::string SpawnOne();  // spawn one spatial palette; returns its id ("" = failed)
  void SampleAndDetect();
  std::string NextId();
  unsigned Rng(unsigned n);  // xorshift PRNG, [0,n)

  ChurnMode mode_ = ChurnMode::kRealistic;  // set from env config in the ctor
  long min_life_s_ = 5;     // realistic engine-age band (env-tunable)
  long life_span_s_ = 25;
  size_t population_ = 5;   // realistic steady live count

  Phase phase_ = Phase::kSpawn;
  long phase_ticks_ = 0;
  std::string current_;  // the single live surface in sequential mode

  // Realistic mode: a population of long-lived surfaces, each with a randomized
  // target lifetime; closed (paced, one per tick) once it ages out. Tracking the
  // spawn tick lets us log engine AGE at teardown — the correlate we're after.
  struct LiveSurface {
    std::string id;
    long spawn_tick = 0;
    long lifetime_ticks = 0;
  };
  std::vector<LiveSurface> realistic_;
  unsigned rng_ = 2463534242u;
  long real_closes_ = 0;
  long age_sum_s_ = 0;   // sum of engine ages (s) at close, for the mean
  long age_max_s_ = 0;

  // Long-soak instrument: a TIME-keyed sample ring (independent of close cadence)
  // so commit growth can be judged as SLOPE over a moving window, and a plateau
  // declared explicitly — the test that distinguishes allocator/pool retention
  // (bounded, plateaus) from a real leak (monotonic over hours).
  struct SoakSample {
    long long t_ms = 0;      // uptime when sampled
    long long priv_kb = 0;
    long hwnds = 0, wgc = 0, dcomp = 0, handles = 0;
  };
  std::vector<SoakSample> soak_;
  long long start_ms_ = 0;
  long long last_soak_sample_ms_ = 0;
  long long last_soak_report_ms_ = 0;
  int plateau_streak_ = 0;
  bool plateau_announced_ = false;

  const flutter::DartProject* project_;
  policy::SurfaceEcology* ecology_;
  void* dxgi_adapter3_;  // IDXGIAdapter3* for VRAM (may be null)

  long tick_ = 0;
  long seq_ = 0;
  long cycle_ = 0;
  std::string churn_ws_;          // workspace the churned palettes parent into
  std::deque<std::string> live_;  // rolling live population (FIFO close)
  std::vector<res::Snapshot> history_;
  res::Snapshot baseline_{};
  bool baseline_set_ = false;
};

}  // namespace morphic

#endif  // RUNNER_CHURN_HARNESS_H_
