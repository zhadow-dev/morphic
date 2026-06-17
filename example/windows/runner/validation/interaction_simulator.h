#ifndef RUNNER_VALIDATION_INTERACTION_SIMULATOR_H_
#define RUNNER_VALIDATION_INTERACTION_SIMULATOR_H_

#include <windows.h>

#include <functional>
#include <string>
#include <vector>

#include "frame_clock.h"

class InteractionRouter;
class SurfaceGraph;
class SurfaceShell;

// PHASE 7D — InteractionSimulator
//
// Scripted driver for the runtime's interaction API. Calls
// router.BeginDrag/UpdatePointer/EndInteraction directly (NOT SendInput) so
// the test is deterministic and decoupled from the real OS input queue.
//
// A script is a sequence of Steps; the simulator subscribes to FrameClock and
// advances one step per tick (so live projection / pacing / audit subsystems
// observe the simulated interaction exactly as they would a real one).
//
// Limits in 7D:
//   - Only one script in flight at a time (rejects Run while busy).
//   - WM_NCLBUTTONDOWN is BYPASSED — the activation path that normally fires
//     before drag is NOT replayed; tests that need activation should script
//     RequestActivate explicitly via a future Activate step.
class InteractionSimulator {
 public:
  // PHASE 8B.5 — chaos op set extends the original drive set with hostile
  // composition primitives. Each chaos op MUTATES external state mid-script:
  //   DestroySurface  — DestroyWindow(target ? target : target_surface_);
  //                     used to test mid-session destruction resilience
  //   StealCapture    — ReleaseCapture() (simulates external capture loss);
  //                     tests router's WM_CAPTURECHANGED path under load
  //   GroupSurfaces   — graph.Group({target_surface_, target}); creates a
  //                     2-member group at the point the step runs
  //   UngroupSurface  — graph.Ungroup(target ? target : target_surface_);
  //                     tests topology mutation DURING active interaction
  enum class Op {
    Begin, BeginResize, Update, End, Cancel, Wait,
    DestroySurface, StealCapture, GroupSurfaces, UngroupSurface
  };

  struct Step {
    Op op;
    POINT pt{};            // screen coords; ignored for End/Cancel/Wait
    int resize_edge = 0;   // HT* for BeginResize
    int ticks = 0;         // Wait only
    SurfaceShell* target = nullptr;  // PHASE 8B.5 — for chaos ops that affect
                                      // a specific surface other than the
                                      // target_surface_ (e.g. follower).
  };

  using DoneCallback = std::function<void(const std::string& scenario_name)>;

  // PHASE 8B.5 — graph passed for the chaos ops (Group/Ungroup). Optional;
  // when null, those steps no-op with a forensic warning.
  InteractionSimulator(FrameClock* clock, InteractionRouter* router,
                       SurfaceGraph* graph);
  ~InteractionSimulator();

  InteractionSimulator(const InteractionSimulator&) = delete;
  InteractionSimulator& operator=(const InteractionSimulator&) = delete;

  bool busy() const { return !script_.empty(); }

  // Run `steps` against `surface`. Returns false if a script is already in
  // flight. `done` is invoked synchronously from the final tick (script
  // already drained), so callers can chain scenarios.
  bool Run(const std::string& scenario_name, SurfaceShell* surface,
           std::vector<Step> steps, DoneCallback done = {});

 private:
  void OnTick(double dt_ms);
  void ApplyStep(const Step& step);

  FrameClock* clock_;
  InteractionRouter* router_;
  SurfaceGraph* graph_;
  FrameClock::Token token_ = 0;

  std::vector<Step> script_;
  size_t cursor_ = 0;
  int wait_remaining_ = 0;
  SurfaceShell* target_surface_ = nullptr;
  std::string scenario_name_;
  DoneCallback done_;
};

#endif  // RUNNER_VALIDATION_INTERACTION_SIMULATOR_H_
